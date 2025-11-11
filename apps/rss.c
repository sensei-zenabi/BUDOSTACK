#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#define CONFIG_FILE "rss.ini"
#define DEFAULT_REFRESH_INTERVAL 900
#define DEFAULT_FEED_URL "https://feeds.yle.fi/uutiset/v1/recent.rss?publisherIds=YLE_UUTISET"
#define DEFAULT_FEED_NAME "Top Stories"
#define STATUS_MESSAGE_LEN 256

typedef struct {
    char *title;
    char *published;
    char *link;
    char *summary;
    int is_read;
} RssItem;

typedef struct {
    char *name;
    char *url;
    RssItem *items;
    size_t count;
    size_t capacity;
    size_t selected;
    size_t scroll;
} RssFeed;

typedef struct {
    unsigned short rows;
    unsigned short cols;
} TerminalSize;

typedef enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_TOGGLE,
    KEY_REFRESH,
    KEY_QUIT,
    KEY_ESC
} KeyCode;

static RssFeed *feeds = NULL;
static size_t feed_count = 0;
static size_t feed_capacity = 0;
static int refresh_interval = DEFAULT_REFRESH_INTERVAL;
static char *startup_feed_name = NULL;

static struct termios original_termios;
static int terminal_configured = 0;

static char status_message[STATUS_MESSAGE_LEN] = "Loading feeds...";
static char last_refresh_str[64] = "Never";
static time_t last_refresh_time = 0;

static void free_item(RssItem *item);
static void free_feed(RssFeed *feed);
static void cleanup_feeds(void);
static void reset_terminal(void);

static void set_status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(status_message, STATUS_MESSAGE_LEN, fmt, args);
    va_end(args);
}

static void trim_whitespace_inplace(char *str) {
    if (!str) {
        return;
    }
    char *start = str;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
    if (start != str) {
        memmove(str, start, (size_t)(end - start) + 1);
    }
}

static void strip_cdata(char *str) {
    if (!str) {
        return;
    }
    const char *prefix = "<![CDATA[";
    size_t prefix_len = strlen(prefix);
    if (strncmp(str, prefix, prefix_len) == 0) {
        char *end = strstr(str + prefix_len, "]]>");
        if (end) {
            size_t len = (size_t)(end - (str + prefix_len));
            memmove(str, str + prefix_len, len);
            str[len] = '\0';
        }
    }
}

static void normalize_spaces(char *str) {
    if (!str) {
        return;
    }
    for (char *p = str; *p != '\0'; ++p) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            *p = ' ';
        }
    }
}

static void sanitize_summary(char *text) {
    if (!text) {
        return;
    }

    char *src = text;
    char *dst = text;
    while (*src) {
        if (*src == '<') {
            char *end = strchr(src, '>');
            if (!end) {
                break;
            }
            size_t tag_len = (size_t)(end - (src + 1));
            if (tag_len >= 31) {
                tag_len = 31;
            }
            char tag[32];
            memcpy(tag, src + 1, tag_len);
            tag[tag_len] = '\0';
            for (size_t i = 0; i < tag_len; ++i) {
                tag[i] = (char)tolower((unsigned char)tag[i]);
            }
            if (strncmp(tag, "br", 2) == 0 || strncmp(tag, "p", 1) == 0 || strncmp(tag, "/p", 2) == 0 || strncmp(tag, "li", 2) == 0 || strncmp(tag, "/li", 3) == 0) {
                if (dst > text && dst[-1] != '\n') {
                    *dst++ = '\n';
                }
            }
            src = end + 1;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';

    for (char *p = text; *p != '\0'; ++p) {
        if (*p == '\r') {
            *p = '\n';
        }
    }

    char *read = text;
    dst = text;
    int last_was_space = 1;
    while (*read) {
        if (*read == '\n') {
            while (dst > text && dst[-1] == ' ') {
                dst--;
            }
            *dst++ = '\n';
            last_was_space = 1;
        } else if (isspace((unsigned char)*read)) {
            if (!last_was_space) {
                *dst++ = ' ';
                last_was_space = 1;
            }
        } else {
            *dst++ = *read;
            last_was_space = 0;
        }
        read++;
    }
    while (dst > text && (dst[-1] == ' ' || dst[-1] == '\n')) {
        dst--;
    }
    *dst = '\0';
}

static char *duplicate_string(const char *src) {
    if (!src) {
        return NULL;
    }
    char *copy = strdup(src);
    if (!copy) {
        perror("strdup");
    }
    return copy;
}

static int ensure_feed_capacity(void) {
    if (feed_count < feed_capacity) {
        return 0;
    }
    size_t new_capacity = feed_capacity == 0 ? 4 : feed_capacity * 2;
    RssFeed *new_feeds = realloc(feeds, new_capacity * sizeof(*new_feeds));
    if (!new_feeds) {
        return -1;
    }
    for (size_t i = feed_capacity; i < new_capacity; ++i) {
        new_feeds[i].name = NULL;
        new_feeds[i].url = NULL;
        new_feeds[i].items = NULL;
        new_feeds[i].count = 0;
        new_feeds[i].capacity = 0;
        new_feeds[i].selected = 0;
        new_feeds[i].scroll = 0;
    }
    feeds = new_feeds;
    feed_capacity = new_capacity;
    return 0;
}

static RssFeed *add_feed(const char *name) {
    if (ensure_feed_capacity() != 0) {
        return NULL;
    }
    RssFeed *feed = &feeds[feed_count++];
    feed->name = duplicate_string(name);
    feed->url = NULL;
    feed->items = NULL;
    feed->count = 0;
    feed->capacity = 0;
    feed->selected = 0;
    feed->scroll = 0;
    if (!feed->name) {
        feed_count--;
        return NULL;
    }
    return feed;
}

static void assign_feed_url(RssFeed *feed, const char *url) {
    if (!feed) {
        return;
    }
    free(feed->url);
    feed->url = duplicate_string(url);
}

static void free_item(RssItem *item) {
    if (!item) {
        return;
    }
    free(item->title);
    free(item->published);
    free(item->link);
    free(item->summary);
}

static void free_feed(RssFeed *feed) {
    if (!feed) {
        return;
    }
    free(feed->name);
    free(feed->url);
    for (size_t i = 0; i < feed->count; ++i) {
        free_item(&feed->items[i]);
    }
    free(feed->items);
    feed->name = NULL;
    feed->url = NULL;
    feed->items = NULL;
    feed->count = 0;
    feed->capacity = 0;
    feed->selected = 0;
    feed->scroll = 0;
}

static void cleanup_feeds(void) {
    for (size_t i = 0; i < feed_count; ++i) {
        free_feed(&feeds[i]);
    }
    free(feeds);
    feeds = NULL;
    feed_count = 0;
    feed_capacity = 0;
    free(startup_feed_name);
    startup_feed_name = NULL;
}

static void reset_terminal(void) {
    if (terminal_configured) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        terminal_configured = 0;
    }
}

static void configure_terminal(void) {
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    struct termios raw = original_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    terminal_configured = 1;
    atexit(reset_terminal);
    atexit(cleanup_feeds);
}

static void get_terminal_size(TerminalSize *size) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        size->cols = 80;
        size->rows = 24;
    } else {
        size->cols = ws.ws_col;
        size->rows = ws.ws_row;
    }
}

static size_t count_unread_items(const RssFeed *feed) {
    size_t unread = 0;
    if (!feed) {
        return 0;
    }
    for (size_t i = 0; i < feed->count; ++i) {
        if (!feed->items[i].is_read) {
            unread++;
        }
    }
    return unread;
}

static void adjust_scroll(RssFeed *feed, size_t visible_items) {
    if (!feed) {
        return;
    }
    if (visible_items == 0 || feed->count == 0) {
        feed->scroll = 0;
        if (feed->count == 0) {
            feed->selected = 0;
        } else if (feed->selected >= feed->count) {
            feed->selected = feed->count - 1;
        }
        return;
    }
    if (feed->selected >= feed->count) {
        feed->selected = feed->count - 1;
    }
    if (feed->count <= visible_items) {
        feed->scroll = 0;
        return;
    }
    if (feed->selected < feed->scroll) {
        feed->scroll = feed->selected;
    } else if (feed->selected >= feed->scroll + visible_items) {
        feed->scroll = feed->selected - visible_items + 1;
    }
    size_t max_scroll = feed->count - visible_items;
    if (feed->scroll > max_scroll) {
        feed->scroll = max_scroll;
    }
}

static void truncate_text(const char *src, size_t width, char *dest, size_t dest_size) {
    if (dest_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    if (width + 1 > dest_size) {
        if (dest_size > 0) {
            width = dest_size - 1;
        } else {
            width = 0;
        }
    }
    size_t len = strlen(src);
    if (len <= width) {
        size_t copy_len = len < dest_size - 1 ? len : dest_size - 1;
        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
        return;
    }
    if (width <= 3) {
        size_t dots = width < dest_size - 1 ? width : dest_size - 1;
        for (size_t i = 0; i < dots; ++i) {
            dest[i] = '.';
        }
        dest[dots] = '\0';
        return;
    }
    size_t copy_len = width - 3;
    if (copy_len > dest_size - 4) {
        copy_len = dest_size - 4;
    }
    memcpy(dest, src, copy_len);
    memcpy(dest + copy_len, "...", 3);
    dest[copy_len + 3] = '\0';
}

static void update_last_refresh_string(void) {
    if (last_refresh_time == 0) {
        snprintf(last_refresh_str, sizeof(last_refresh_str), "Never");
        return;
    }
    struct tm *tm_info = localtime(&last_refresh_time);
    if (!tm_info) {
        snprintf(last_refresh_str, sizeof(last_refresh_str), "Unknown");
        return;
    }
    if (strftime(last_refresh_str, sizeof(last_refresh_str), "%Y-%m-%d %H:%M:%S", tm_info) == 0) {
        snprintf(last_refresh_str, sizeof(last_refresh_str), "Unknown");
    }
}

static char *extract_tag_content(const char *text, const char *tag) {
    char open_pattern[64];
    char close_pattern[64];
    size_t tag_len = strlen(tag);
    if (tag_len + 3 >= sizeof(open_pattern) || tag_len + 4 >= sizeof(close_pattern)) {
        return NULL;
    }
    snprintf(open_pattern, sizeof(open_pattern), "<%s", tag);
    snprintf(close_pattern, sizeof(close_pattern), "</%s>", tag);

    const char *open = text;
    while ((open = strstr(open, open_pattern)) != NULL) {
        char c = open[strlen(open_pattern)];
        if (c == '>' || isspace((unsigned char)c)) {
            break;
        }
        open++;
    }
    if (!open) {
        return NULL;
    }
    const char *start = strchr(open, '>');
    if (!start) {
        return NULL;
    }
    start++;
    const char *end = strstr(start, close_pattern);
    if (!end) {
        return NULL;
    }
    size_t len = (size_t)(end - start);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, start, len);
    result[len] = '\0';
    strip_cdata(result);
    normalize_spaces(result);
    trim_whitespace_inplace(result);
    return result;
}

static int parse_rss_items(const char *rss_data, RssItem **out_items, size_t *out_count) {
    if (!out_items || !out_count) {
        return -1;
    }
    *out_items = NULL;
    *out_count = 0;
    if (!rss_data) {
        return -1;
    }
    size_t capacity = 8;
    size_t count = 0;
    RssItem *items = calloc(capacity, sizeof(*items));
    if (!items) {
        return -1;
    }

    const char *cursor = rss_data;
    const char *close_tag = "</item>";
    size_t close_len = strlen(close_tag);

    while ((cursor = strstr(cursor, "<item")) != NULL) {
        const char *start = strchr(cursor, '>');
        if (!start) {
            break;
        }
        start++;
        const char *end = strstr(start, close_tag);
        if (!end) {
            break;
        }
        size_t segment_len = (size_t)(end - start);
        char *segment = malloc(segment_len + 1);
        if (!segment) {
            for (size_t i = 0; i < count; ++i) {
                free_item(&items[i]);
            }
            free(items);
            return -1;
        }
        memcpy(segment, start, segment_len);
        segment[segment_len] = '\0';

        if (count >= capacity) {
            size_t new_capacity = capacity * 2;
            RssItem *tmp = realloc(items, new_capacity * sizeof(*items));
            if (!tmp) {
                free(segment);
                for (size_t i = 0; i < count; ++i) {
                    free_item(&items[i]);
                }
                free(items);
                return -1;
            }
            items = tmp;
            memset(items + capacity, 0, (new_capacity - capacity) * sizeof(*items));
            capacity = new_capacity;
        }

        RssItem item = {0};
        item.title = extract_tag_content(segment, "title");
        item.published = extract_tag_content(segment, "pubDate");
        item.link = extract_tag_content(segment, "link");
        item.summary = extract_tag_content(segment, "content:encoded");
        if (!item.summary) {
            item.summary = extract_tag_content(segment, "description");
        }
        if (item.summary) {
            sanitize_summary(item.summary);
            if (item.summary[0] == '\0') {
                free(item.summary);
                item.summary = NULL;
            }
        }
        if (!item.title) {
            item.title = duplicate_string("Untitled");
        }
        if (!item.published) {
            item.published = duplicate_string("Unknown");
        }
        if (!item.summary && item.link) {
            item.summary = duplicate_string(item.link);
        }
        if (!item.title || !item.published || (!item.summary && !item.link)) {
            free_item(&item);
            free(segment);
            cursor = end + close_len;
            continue;
        }
        items[count++] = item;
        free(segment);
        cursor = end + close_len;
    }

    if (count == 0) {
        free(items);
        items = NULL;
    }
    *out_items = items;
    *out_count = count;
    return 0;
}

static char *fetch_rss(const char *url) {
    if (!url) {
        return NULL;
    }
    char command[1024];
    int written = snprintf(command, sizeof(command), "curl -s --fail --location \"%s\"", url);
    if (written <= 0 || (size_t)written >= sizeof(command)) {
        return NULL;
    }
    FILE *pipe = popen(command, "r");
    if (!pipe) {
        return NULL;
    }
    char *data = NULL;
    size_t size = 0;
    size_t capacity = 0;
    char buffer[1024];
    while (!feof(pipe)) {
        size_t n = fread(buffer, 1, sizeof(buffer), pipe);
        if (n > 0) {
            if (size + n + 1 > capacity) {
                size_t new_capacity = capacity == 0 ? 4096 : capacity * 2;
                while (new_capacity < size + n + 1) {
                    new_capacity *= 2;
                }
                char *tmp = realloc(data, new_capacity);
                if (!tmp) {
                    free(data);
                    pclose(pipe);
                    return NULL;
                }
                data = tmp;
                capacity = new_capacity;
            }
            memcpy(data + size, buffer, n);
            size += n;
        }
    }
    int exit_code = pclose(pipe);
    if (exit_code != 0 && size == 0) {
        free(data);
        return NULL;
    }
    if (data) {
        data[size] = '\0';
    }
    return data;
}

static int items_match(const RssItem *a, const RssItem *b) {
    if (!a || !b) {
        return 0;
    }
    if (a->link && b->link && strcmp(a->link, b->link) == 0) {
        return 1;
    }
    if (a->title && b->title && a->published && b->published) {
        if (strcmp(a->title, b->title) == 0 && strcmp(a->published, b->published) == 0) {
            return 1;
        }
    }
    return 0;
}

static void merge_feed_items(RssFeed *feed, RssItem *new_items, size_t new_count) {
    RssItem *old_items = feed->items;
    size_t old_count = feed->count;
    size_t old_selected = feed->selected;
    size_t selected_match = 0;

    for (size_t i = 0; i < new_count; ++i) {
        new_items[i].is_read = 0;
        for (size_t j = 0; j < old_count; ++j) {
            if (items_match(&new_items[i], &old_items[j])) {
                new_items[i].is_read = old_items[j].is_read;
                if (old_selected == j) {
                    selected_match = i;
                }
                break;
            }
        }
    }

    for (size_t i = 0; i < old_count; ++i) {
        free_item(&old_items[i]);
    }
    free(old_items);

    feed->items = new_items;
    feed->count = new_count;
    feed->capacity = new_count;
    if (new_count == 0) {
        feed->selected = 0;
        feed->scroll = 0;
    } else {
        if (selected_match >= new_count) {
            selected_match = new_count - 1;
        }
        feed->selected = selected_match;
        if (feed->scroll >= new_count) {
            feed->scroll = 0;
        }
    }
}

static void compute_layout(const TerminalSize *size, size_t *list_lines, size_t *detail_lines) {
    const size_t header_lines = 3;
    const size_t footer_lines = 2;
    size_t rows = size->rows;

    if (rows <= header_lines + footer_lines) {
        size_t body = rows > footer_lines ? rows - footer_lines : 0;
        *list_lines = body;
        *detail_lines = 0;
        return;
    }

    size_t body = rows - header_lines - footer_lines;
    if (body == 0) {
        *list_lines = 0;
        *detail_lines = 0;
        return;
    }

    size_t list = body / 2;
    if (list == 0) {
        list = 1;
    }
    if (list > body) {
        list = body;
    }

    size_t detail = body - list;

    if (detail == 0 && body > 1) {
        detail = 1;
        if (list > 1) {
            list--;
        }
    }

    if (detail < 4 && body >= 5) {
        size_t needed = 4 - detail;
        size_t transferable = (list > 1) ? list - 1 : 0;
        if (transferable > 0) {
            if (needed > transferable) {
                needed = transferable;
            }
            detail += needed;
            list -= needed;
        }
    }

    *list_lines = list;
    *detail_lines = detail;
}

static int refresh_feed(RssFeed *feed) {
    if (!feed || !feed->url) {
        return -1;
    }
    char *rss_data = fetch_rss(feed->url);
    if (!rss_data) {
        return -1;
    }
    RssItem *items = NULL;
    size_t count = 0;
    if (parse_rss_items(rss_data, &items, &count) != 0) {
        free(rss_data);
        return -1;
    }
    free(rss_data);
    merge_feed_items(feed, items, count);
    return 0;
}

static int refresh_all_feeds(int manual_trigger) {
    size_t updated = 0;
    size_t failed = 0;
    for (size_t i = 0; i < feed_count; ++i) {
        if (refresh_feed(&feeds[i]) == 0) {
            updated++;
        } else {
            failed++;
        }
    }
    if (updated > 0) {
        last_refresh_time = time(NULL);
        update_last_refresh_string();
        if (failed == 0) {
            set_status(manual_trigger ? "Refreshed %zu feed(s)." : "Feeds auto-refreshed successfully.", updated);
        } else {
            set_status(manual_trigger ? "Refreshed %zu feed(s). %zu failed." : "Auto refresh: %zu updated, %zu failed.", updated, failed);
        }
        return 0;
    }
    if (failed > 0) {
        set_status(manual_trigger ? "Failed to refresh feeds." : "Auto refresh failed.");
    } else {
        set_status("No feeds available.");
    }
    return -1;
}

static void print_wrapped_block(const char *label, const char *text, size_t cols, size_t max_lines) {
    if (max_lines == 0) {
        return;
    }
    const char *content = (text && *text) ? text : "(no details)";
    size_t label_len = strlen(label);
    size_t width = cols > label_len ? cols - label_len : 0;
    size_t lines = 0;
    size_t pos = 0;
    size_t len = strlen(content);

    while (lines < max_lines) {
        if (lines == 0) {
            printf("%s", label);
        } else {
            for (size_t i = 0; i < label_len; ++i) {
                putchar(' ');
            }
        }

        if (width == 0) {
            printf("\n");
            ++lines;
            continue;
        }

        size_t line_used = 0;
        int printed = 0;
        while (pos < len) {
            char c = content[pos];
            if (c == '\n' || c == '\r') {
                pos++;
                break;
            }
            if (isspace((unsigned char)c)) {
                pos++;
                if (!printed) {
                    continue;
                }
                size_t peek = pos;
                while (peek < len && isspace((unsigned char)content[peek]) && content[peek] != '\n' && content[peek] != '\r') {
                    peek++;
                }
                size_t next_word_len = 0;
                size_t tmp = peek;
                while (tmp < len && !isspace((unsigned char)content[tmp]) && content[tmp] != '\n' && content[tmp] != '\r') {
                    tmp++;
                }
                next_word_len = tmp - peek;
                if (next_word_len == 0) {
                    pos = peek;
                    continue;
                }
                if (line_used + 1 + next_word_len > width) {
                    pos = peek;
                    break;
                }
                putchar(' ');
                line_used++;
                printed = 1;
                pos = peek;
                continue;
            }

            size_t start = pos;
            while (pos < len && !isspace((unsigned char)content[pos]) && content[pos] != '\n' && content[pos] != '\r') {
                pos++;
            }
            size_t word_len = pos - start;
            if (!printed) {
                if (word_len > width) {
                    fwrite(content + start, 1, width, stdout);
                    line_used = width;
                    printed = 1;
                    if (word_len > width) {
                        pos = start + width;
                    }
                    break;
                }
                fwrite(content + start, 1, word_len, stdout);
                line_used = word_len;
                printed = 1;
            } else {
                if (line_used + 1 + word_len > width) {
                    pos = start;
                    break;
                }
                putchar(' ');
                fwrite(content + start, 1, word_len, stdout);
                line_used += 1 + word_len;
            }
        }

        printf("\n");
        ++lines;

        while (pos < len && (content[pos] == ' ' || content[pos] == '\t')) {
            pos++;
        }
        if (pos < len && (content[pos] == '\n' || content[pos] == '\r')) {
            pos++;
        }
        if (pos >= len) {
            break;
        }
    }

    while (lines < max_lines) {
        printf("\n");
        ++lines;
    }
}

static void draw_ui(size_t current_feed, size_t list_lines, size_t detail_lines, const TerminalSize *size) {
    printf("\033[2J\033[H");
    size_t cols = size->cols;

    printf("BUDOSTACK RSS Reader\n");
    if (refresh_interval > 0) {
        printf("Last refresh: %s (auto every %d s)\n", last_refresh_str, refresh_interval);
    } else {
        printf("Last refresh: %s (auto refresh disabled)\n", last_refresh_str);
    }

    printf("Feeds: ");
    for (size_t i = 0; i < feed_count; ++i) {
        if (i == current_feed) {
            printf("\033[7m %s \033[0m", feeds[i].name ? feeds[i].name : "(unnamed)");
        } else {
            printf(" %s ", feeds[i].name ? feeds[i].name : "(unnamed)");
        }
    }
    printf("\n");

    RssFeed *feed = (feed_count > 0) ? &feeds[current_feed] : NULL;
    size_t unread = feed ? count_unread_items(feed) : 0;
    size_t item_rows = (list_lines > 0) ? (list_lines - 1) : 0;

    if (list_lines > 0) {
        if (feed) {
            printf("Articles (%zu total, %zu unread)\n", feed->count, unread);
        } else {
            printf("Articles\n");
        }

        if (item_rows > 0) {
            if (!feed) {
                printf(" (no feeds configured)\n");
                for (size_t i = 1; i < item_rows; ++i) {
                    printf("\n");
                }
            } else if (feed->count == 0) {
                printf(" (no news items)\n");
                for (size_t i = 1; i < item_rows; ++i) {
                    printf("\n");
                }
            } else {
                size_t start = feed->scroll;
                size_t end = start + item_rows;
                if (end > feed->count) {
                    end = feed->count;
                }
                char linebuf[1024];
                size_t printed = 0;
                for (size_t i = start; i < end; ++i, ++printed) {
                    RssItem *item = &feed->items[i];
                    char indicator = item->is_read ? ' ' : '*';
                    size_t available_width = cols > 6 ? cols - 6 : 0;
                    if (available_width > 0) {
                        truncate_text(item->title, available_width, linebuf, sizeof(linebuf));
                        if (i == feed->selected) {
                            printf("\033[7m %c %s\033[0m\n", indicator, linebuf);
                        } else {
                            printf(" %c %s\n", indicator, linebuf);
                        }
                    } else {
                        if (i == feed->selected) {
                            printf("\033[7m %c\033[0m\n", indicator);
                        } else {
                            printf(" %c\n", indicator);
                        }
                    }
                }
                while (printed < item_rows) {
                    printf("\n");
                    ++printed;
                }
            }
        }
    }

    size_t detail_remaining = detail_lines;
    if (detail_remaining > 0) {
        size_t rule_width = cols > 0 ? cols : 80;
        for (size_t i = 0; i < rule_width; ++i) {
            putchar('-');
        }
        putchar('\n');
        --detail_remaining;
    }

    RssItem *selected = (feed && feed->count > 0) ? &feed->items[feed->selected] : NULL;

    if (detail_remaining > 0) {
        if (feed) {
            printf("Feed: %s (%zu items, %zu unread)\n", feed->name ? feed->name : "(unnamed)", feed->count, unread);
        } else {
            printf("Feed: -\n");
        }
        --detail_remaining;
    }

    if (detail_remaining > 0) {
        if (selected) {
            char line[1024];
            size_t title_width = cols > 7 ? cols - 7 : 0;
            if (title_width > 0) {
                truncate_text(selected->title, title_width, line, sizeof(line));
                printf("Title: %s\n", line);
            } else {
                printf("Title:\n");
            }
        } else {
            printf("Title: -\n");
        }
        --detail_remaining;
    }

    if (detail_remaining > 0) {
        if (selected) {
            char line[1024];
            size_t published_width = cols > 11 ? cols - 11 : 0;
            if (published_width > 0) {
                truncate_text(selected->published, published_width, line, sizeof(line));
                printf("Published: %s\n", line);
            } else {
                printf("Published:\n");
            }
        } else {
            printf("Published: -\n");
        }
        --detail_remaining;
    }

    if (detail_remaining > 0) {
        const char *detail_text = "(no details)";
        int show_link = 0;
        if (selected) {
            if (selected->summary && *selected->summary) {
                detail_text = selected->summary;
            } else if (selected->link && *selected->link) {
                detail_text = selected->link;
            }
            if (selected->link && *selected->link) {
                if (detail_text == selected->link || (detail_text && strcmp(detail_text, selected->link) == 0)) {
                    show_link = 0;
                } else {
                    show_link = 1;
                }
            }
        }

        size_t summary_lines = detail_remaining;
        size_t link_lines = 0;
        if (show_link) {
            if (summary_lines > 1) {
                summary_lines -= 1;
                link_lines = 1;
            } else {
                show_link = 0;
            }
        }

        if (summary_lines > 0) {
            print_wrapped_block("Summary: ", detail_text, cols, summary_lines);
        }

        detail_remaining -= summary_lines;

        if (show_link && link_lines == 1 && detail_remaining > 0) {
            if (cols > 6) {
                char line[1024];
                size_t link_width = cols - 6;
                truncate_text(selected->link, link_width, line, sizeof(line));
                printf("Link: %s\n", line);
            } else {
                printf("Link:\n");
            }
            --detail_remaining;
        }

        while (detail_remaining > 0) {
            printf("\n");
            --detail_remaining;
        }
    }

    printf("Controls: \342\206\220/\342\206\222 feeds | \342\206\221/\342\206\223 items | Enter toggle read | r refresh | q quit\n");
    printf("Status: %s\n", status_message);
    fflush(stdout);
}

static KeyCode read_key(void) {
    unsigned char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0) {
        return KEY_NONE;
    }
    if (ch == '\033') {
        unsigned char seq[2];
        struct timeval tv = {0, 100000};
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        if (select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) <= 0) {
            return KEY_ESC;
        }
        if (read(STDIN_FILENO, &seq[0], 1) <= 0) {
            return KEY_ESC;
        }
        if (seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) {
                return KEY_NONE;
            }
            switch (seq[1]) {
            case 'A':
                return KEY_UP;
            case 'B':
                return KEY_DOWN;
            case 'C':
                return KEY_RIGHT;
            case 'D':
                return KEY_LEFT;
            default:
                return KEY_NONE;
            }
        }
        return KEY_NONE;
    }
    switch (ch) {
    case 'q':
    case 'Q':
        return KEY_QUIT;
    case 'r':
    case 'R':
        return KEY_REFRESH;
    case '\n':
    case '\r':
    case ' ':
        return KEY_TOGGLE;
    default:
        break;
    }
    return KEY_NONE;
}

static FILE *open_config_file(void) {
    FILE *file = fopen(CONFIG_FILE, "r");
    if (file) {
        return file;
    }

    char path[PATH_MAX];
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0 && (size_t)len < sizeof(exe_path)) {
        exe_path[len] = '\0';
        char *slash = strrchr(exe_path, '/');
        if (slash) {
            slash[1] = '\0';
            int written = snprintf(path, sizeof(path), "%s%s", exe_path, CONFIG_FILE);
            if (written > 0 && (size_t)written < sizeof(path)) {
                file = fopen(path, "r");
                if (file) {
                    return file;
                }
            }
        }
    }

    int written = snprintf(path, sizeof(path), "apps/%s", CONFIG_FILE);
    if (written > 0 && (size_t)written < sizeof(path)) {
        file = fopen(path, "r");
        if (file) {
            return file;
        }
    }

    return NULL;
}

static void load_config(void) {
    FILE *file = open_config_file();
    char *legacy_url = NULL;
    RssFeed *current_feed = NULL;

    if (!file) {
        RssFeed *feed = add_feed(DEFAULT_FEED_NAME);
        if (!feed) {
            fprintf(stderr, "Failed to allocate default feed.\n");
            exit(EXIT_FAILURE);
        }
        assign_feed_url(feed, DEFAULT_FEED_URL);
        set_status("Using built-in feed configuration.");
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *trimmed = line;
        trim_whitespace_inplace(trimmed);
        if (trimmed[0] == '\0' || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }
        if (trimmed[0] == '[') {
            char *closing = strchr(trimmed, ']');
            if (!closing) {
                continue;
            }
            *closing = '\0';
            if (strcmp(trimmed, "[Settings]") == 0) {
                current_feed = NULL;
                continue;
            }
            if (strncmp(trimmed, "[Feed", 5) == 0) {
                char name_buf[256] = "";
                char *quote1 = strchr(trimmed, '"');
                char *quote2 = quote1 ? strchr(quote1 + 1, '"') : NULL;
                if (quote1 && quote2 && quote2 > quote1 + 1) {
                    size_t len = (size_t)(quote2 - quote1 - 1);
                    if (len >= sizeof(name_buf)) {
                        len = sizeof(name_buf) - 1;
                    }
                    memcpy(name_buf, quote1 + 1, len);
                    name_buf[len] = '\0';
                    trim_whitespace_inplace(name_buf);
                }
                if (name_buf[0] == '\0') {
                    snprintf(name_buf, sizeof(name_buf), "Feed %zu", feed_count + 1);
                }
                current_feed = add_feed(name_buf);
            }
            continue;
        }
        char *equals = strchr(trimmed, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';
        char *key = trimmed;
        char *value = equals + 1;
        trim_whitespace_inplace(key);
        trim_whitespace_inplace(value);
        size_t value_len = strlen(value);
        if (value_len >= 2 && value[0] == '"' && value[value_len - 1] == '"') {
            value[value_len - 1] = '\0';
            memmove(value, value + 1, value_len - 1);
        }
        if (!current_feed) {
            if (strcasecmp(key, "RSS_REFRESH_INTERVAL") == 0 || strcasecmp(key, "REFRESH_INTERVAL") == 0) {
                int val = atoi(value);
                if (val < 0) {
                    val = 0;
                }
                refresh_interval = val;
            } else if (strcasecmp(key, "RSS_URL") == 0) {
                free(legacy_url);
                legacy_url = duplicate_string(value);
            } else if (strcasecmp(key, "START_FEED") == 0) {
                free(startup_feed_name);
                startup_feed_name = duplicate_string(value);
            }
        } else {
            if (strcasecmp(key, "URL") == 0) {
                assign_feed_url(current_feed, value);
            }
        }
    }
    fclose(file);
    if (feed_count == 0) {
        if (legacy_url) {
            RssFeed *feed = add_feed(DEFAULT_FEED_NAME);
            if (!feed) {
                fprintf(stderr, "Failed to allocate feed for legacy URL.\n");
                free(legacy_url);
                exit(EXIT_FAILURE);
            }
            assign_feed_url(feed, legacy_url);
        } else {
            RssFeed *feed = add_feed(DEFAULT_FEED_NAME);
            if (!feed) {
                fprintf(stderr, "Failed to allocate default feed.\n");
                exit(EXIT_FAILURE);
            }
            assign_feed_url(feed, DEFAULT_FEED_URL);
        }
    }
    free(legacy_url);

    for (size_t i = 0; i < feed_count;) {
        if (!feeds[i].url) {
            fprintf(stderr, "Feed '%s' missing URL in configuration. Removing.\n", feeds[i].name ? feeds[i].name : "(unnamed)");
            free_feed(&feeds[i]);
            if (i + 1 < feed_count) {
                memmove(&feeds[i], &feeds[i + 1], (feed_count - i - 1) * sizeof(RssFeed));
            }
            feed_count--;
        } else {
            ++i;
        }
    }

    if (feed_count == 0) {
        RssFeed *feed = add_feed(DEFAULT_FEED_NAME);
        if (!feed) {
            fprintf(stderr, "Failed to allocate default feed.\n");
            exit(EXIT_FAILURE);
        }
        assign_feed_url(feed, DEFAULT_FEED_URL);
    }

    set_status("Loaded %zu feed(s).", feed_count);
}

int main(void) {
    load_config();
    if (feed_count == 0) {
        fprintf(stderr, "No RSS feeds configured.\n");
        cleanup_feeds();
        return EXIT_FAILURE;
    }

    configure_terminal();

    size_t current_feed = 0;
    if (startup_feed_name) {
        for (size_t i = 0; i < feed_count; ++i) {
            if (feeds[i].name && strcmp(feeds[i].name, startup_feed_name) == 0) {
                current_feed = i;
                break;
            }
        }
    }

    refresh_all_feeds(1);

    while (1) {
        TerminalSize size;
        get_terminal_size(&size);
        size_t list_lines = 0;
        size_t detail_lines = 0;
        compute_layout(&size, &list_lines, &detail_lines);
        size_t item_rows = (list_lines > 0) ? (list_lines - 1) : 0;
        adjust_scroll(&feeds[current_feed], item_rows);
        draw_ui(current_feed, list_lines, detail_lines, &size);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        } else if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            KeyCode key = read_key();
            RssFeed *feed = &feeds[current_feed];
            switch (key) {
            case KEY_UP:
                if (feed->count > 0 && feed->selected > 0) {
                    feed->selected--;
                }
                break;
            case KEY_DOWN:
                if (feed->count > 0 && feed->selected + 1 < feed->count) {
                    feed->selected++;
                }
                break;
            case KEY_LEFT:
                if (feed_count > 0) {
                    current_feed = (current_feed + feed_count - 1) % feed_count;
                }
                break;
            case KEY_RIGHT:
                if (feed_count > 0) {
                    current_feed = (current_feed + 1) % feed_count;
                }
                break;
            case KEY_TOGGLE:
                if (feed->count > 0) {
                    feed->items[feed->selected].is_read = !feed->items[feed->selected].is_read;
                    set_status(feed->items[feed->selected].is_read ? "Marked as read." : "Marked as unread.");
                }
                break;
            case KEY_REFRESH:
                refresh_all_feeds(1);
                break;
            case KEY_QUIT:
            case KEY_ESC:
                printf("\033[2J\033[H");
                return EXIT_SUCCESS;
            case KEY_NONE:
            default:
                break;
            }
        } else {
            if (refresh_interval > 0) {
                time_t now = time(NULL);
                if (last_refresh_time == 0 || difftime(now, last_refresh_time) >= refresh_interval) {
                    refresh_all_feeds(0);
                }
            }
        }
    }

    return EXIT_FAILURE;
}
