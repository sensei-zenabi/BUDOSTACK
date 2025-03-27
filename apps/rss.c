/*
    RSS Reader Application
    ------------------------
    This application fetches an RSS feed from a defined URL and displays the news items in pages
    that exactly fit the terminal height. Each news item shows its publication timestamp and title.

    Design principles used:
      - Single-source configuration: All configurable values are defined in the "rss.ini" file.
      - Fallback defaults: If "rss.ini" is not found or a key is missing, the built-in default values are used.
      - Minimal dependencies: Uses only standard libraries and POSIX functions.
      - Non-blocking timers and input: Uses a per-second loop with select() for checking user input without busy-waiting.
      - Paging: Automatically scrolls through pages every PAGE_INTERVAL seconds, with a bottom bar showing the last update time, current page, and available controls.
      - Periodic update: Every RSS_REFRESH_INTERVAL seconds the feed is refreshed.
      - Layout safety: Ensures that there is always sufficient margin at the top and bottom so that news items are not clipped vertically.

    To compile (using -std=c11):
        gcc -std=c11 -Wall -Wextra -pedantic -Wno-format-truncation -o rss_reader rss.c
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>    // For sleep(), STDIN_FILENO
#include <termios.h>   // For terminal I/O settings
#include <sys/select.h> // For select()

// Default layout defines
#define TOP_MARGIN 1              // Number of blank lines at the top
#define BOTTOM_MARGIN 1           // Number of blank lines at the bottom
#define NEWS_PER_PAGE 6           // Number of news displayed per page
#define BUFFER_SIZE 1024

// Global configuration variables with default values.
// These values can be overridden by the contents of "rss.ini".
static char config_rss_url[1024] = "https://feeds.yle.fi/uutiset/v1/recent.rss?publisherIds=YLE_UUTISET";
static int config_page_interval = 25;          // Seconds to display each page
static int config_rss_refresh_interval = 1800;   // Seconds between RSS feed updates

// Function to load configuration from "rss.ini".
// The configuration file should have a [Settings] section with keys:
// RSS_URL, PAGE_INTERVAL, and RSS_REFRESH_INTERVAL.
void load_config(void) {
    FILE *file = fopen("rss.ini", "r");
    if (!file) {
        // Configuration file not found, using default values.
        return;
    }
    char line[1024];
    int in_settings_section = 0;
    while (fgets(line, sizeof(line), file)) {
        // Remove trailing newline characters.
        line[strcspn(line, "\r\n")] = '\0';
        // Skip empty lines.
        if (line[0] == '\0')
            continue;
        // Skip comment lines.
        if (line[0] == ';' || line[0] == '#')
            continue;
        // Check for section headers.
        if (line[0] == '[') {
            if (strcmp(line, "[Settings]") == 0) {
                in_settings_section = 1;
            } else {
                in_settings_section = 0;
            }
            continue;
        }
        if (!in_settings_section)
            continue;
        // Process key=value pairs.
        char key[256], value[768];
        if (sscanf(line, "%255[^=]=%767[^\n]", key, value) != 2)
            continue;
        // Remove surrounding quotes from the value if present.
        size_t len = strlen(value);
        if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
            memmove(value, value + 1, len - 2);
            value[len - 2] = '\0';
        }
        // Update configuration variables based on key.
        if (strcmp(key, "RSS_URL") == 0) {
            strncpy(config_rss_url, value, sizeof(config_rss_url) - 1);
            config_rss_url[sizeof(config_rss_url) - 1] = '\0';
        } else if (strcmp(key, "PAGE_INTERVAL") == 0) {
            config_page_interval = atoi(value);
        } else if (strcmp(key, "RSS_REFRESH_INTERVAL") == 0) {
            config_rss_refresh_interval = atoi(value);
        }
    }
    fclose(file);
}

// External function for pretty printing with a delay (assumed to be defined elsewhere).
extern void prettyprint(const char *message, unsigned int delay_ms);

// Structure to hold a news item.
typedef struct {
    char *timestamp; // extracted from <pubDate>
    char *title;     // extracted from <title>
} news_item;

// Function to get the number of terminal lines using "tput lines".
// Returns a default value (24) if retrieval fails.
int get_terminal_lines(void) {
    FILE *fp = popen("tput lines", "r");
    if (!fp) {
        return 24;
    }
    int lines = 24;
    if (fscanf(fp, "%d", &lines) != 1) {
        lines = 24;
    }
    pclose(fp);
    return lines;
}

// Function to fetch the RSS feed data using curl.
char *fetch_rss(void) {
    char command[256];
    if (snprintf(command, sizeof(command), "curl -s \"%s\"", config_rss_url) < 0) {
        return NULL;
    }
    FILE *fp = popen(command, "r");
    if (!fp) {
        return NULL;
    }
    char *data = NULL;
    size_t size = 0;
    const size_t chunk_size = 1024;
    char chunk[1024];
    while (!feof(fp)) {
        size_t n = fread(chunk, 1, chunk_size, fp);
        if (n > 0) {
            char *temp = realloc(data, size + n + 1);
            if (!temp) {
                free(data);
                pclose(fp);
                return NULL;
            }
            data = temp;
            memcpy(data + size, chunk, n);
            size += n;
            data[size] = '\0';
        }
    }
    pclose(fp);
    return data;
}

// Helper function to extract text between start_tag and end_tag in the given text.
// Returns a dynamically allocated string (or NULL if not found).
char *extract_tag(const char *text, const char *start_tag, const char *end_tag) {
    char *start = strstr(text, start_tag);
    if (!start) return NULL;
    start += strlen(start_tag);
    char *end = strstr(start, end_tag);
    if (!end) return NULL;
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

// Function to parse the RSS feed and extract news items into an array.
// Sets *count to the number of news items found.
news_item *parse_rss(const char *rss_data, int *count) {
    int capacity = 10;
    int num_items = 0;
    news_item *items = malloc(capacity * sizeof(news_item));
    if (!items) return NULL;

    const char *cursor = rss_data;
    while ((cursor = strstr(cursor, "<item>")) != NULL) {
        const char *item_end = strstr(cursor, "</item>");
        if (!item_end) break;
        size_t item_len = item_end - cursor;
        char *item_text = malloc(item_len + 1);
        if (!item_text) break;
        memcpy(item_text, cursor, item_len);
        item_text[item_len] = '\0';

        // Extract <pubDate> and <title> from the item.
        char *pubDate = extract_tag(item_text, "<pubDate>", "</pubDate>");
        char *title   = extract_tag(item_text, "<title>", "</title>");
        free(item_text);

        // If no pubDate or title found, set to "N/A"
        if (!pubDate) {
            pubDate = strdup("N/A");
        }
        if (!title) {
            title = strdup("N/A");
        }

        // Save the news item.
        if (num_items >= capacity) {
            capacity *= 2;
            news_item *temp = realloc(items, capacity * sizeof(news_item));
            if (!temp) {
                free(pubDate);
                free(title);
                break;
            }
            items = temp;
        }
        items[num_items].timestamp = pubDate;
        items[num_items].title = title;
        num_items++;

        cursor = item_end + strlen("</item>");
    }
    *count = num_items;
    return items;
}

// Function to free an array of news items.
void free_news(news_item *news, int count) {
    for (int i = 0; i < count; i++) {
        free(news[i].timestamp);
        free(news[i].title);
    }
    free(news);
}

// Function to format current time as a string.
void format_current_time(char *buffer, size_t buf_size) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    strftime(buffer, buf_size, "%Y-%m-%d %H:%M:%S", local);
}

// Function to display one page of news.
// The layout now includes TOP_MARGIN blank lines at the top and BOTTOM_MARGIN blank lines before the bottom bar.
// Also prints user instructions for navigation at the bottom.
void display_page(news_item *news, int news_count, int page, int items_per_page, const char *last_update_str, int total_pages) {
    system("clear");
    int printed_lines = 0;

    // Print top margin
    for (int i = 0; i < TOP_MARGIN; i++) {
        printf("\n");
        printed_lines++;
    }

    int start = page * items_per_page;
    // Print each news item in this page.
    for (int i = 0; i < items_per_page; i++) {
        int index = start + i;
        // Stop printing if there are no more news items.
        if (index >= news_count) {
            break;
        }
        // Each news item uses 3 lines: [timestamp], news title, and a blank line.
        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "[%s]\n", news[index].timestamp);
        prettyprint(buffer, 10);
        snprintf(buffer, BUFFER_SIZE, "%s\n", news[index].title);
        prettyprint(buffer, 10);
        printed_lines += 3;
    }
    
    // Print bottom margin blank lines.
    for (int i = 0; i < BOTTOM_MARGIN; i++) {
        printf("\n");
        printed_lines++;
    }
    // Print bottom bar with update info and user instructions.
    printf("Last update: %s | Page: %d/%d\n", last_update_str, page + 1, total_pages);
    printf("Controls: [n] Next, [p] Previous, [q] Quit");
    fflush(stdout);
}

// Global variable to hold original terminal settings for restoration.
static struct termios orig_termios;

// Function to reset terminal input mode to original settings.
void reset_input_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

// Function to set terminal to non-canonical mode with no echo.
void set_input_mode(void) {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(reset_input_mode);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    // Minimum number of characters for non-canonical read.
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

int main(void) {
    // Load configuration from rss.ini
    load_config();

    // Set terminal to non-canonical mode for immediate key processing.
    set_input_mode();

    // Determine terminal height.
    int term_lines = get_terminal_lines();
    // Adjust items_per_page to account for TOP_MARGIN and BOTTOM_MARGIN.
    int items_per_page = (term_lines - TOP_MARGIN - BOTTOM_MARGIN - 1) / 3;
    if (items_per_page < 1) {
        items_per_page = 1;
    }
    // Override with constant for consistent paging.
    items_per_page = NEWS_PER_PAGE;

    // Fetch and parse the RSS feed initially.
    char *rss_data = fetch_rss();
    if (!rss_data) {
        fprintf(stderr, "Failed to fetch RSS data.\n");
        return EXIT_FAILURE;
    }
    int news_count = 0;
    news_item *news = parse_rss(rss_data, &news_count);
    free(rss_data);
    if (!news || news_count == 0) {
        fprintf(stderr, "No news items found.\n");
        free_news(news, news_count);
        return EXIT_FAILURE;
    }
    int total_pages = (news_count + items_per_page - 1) / items_per_page;

    // Set last update time.
    char last_update_str[64];
    format_current_time(last_update_str, sizeof(last_update_str));

    // Timers: elapsed time for page update and RSS refresh.
    int elapsed_since_update = 0;
    int page = 0;

    // Main loop: display pages and update RSS periodically.
    while (1) {
        display_page(news, news_count, page, items_per_page, last_update_str, total_pages);
        int seconds_waited = 0;
        int manual_change = 0;  // flag to indicate manual page change
        // Wait in one-second increments for either a key press or timeout.
        while (seconds_waited < config_page_interval) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
            if (ret > 0) {
                char ch;
                if (read(STDIN_FILENO, &ch, 1) > 0) {
                    if (ch == 'q') {
                        // Quit the application.
                        reset_input_mode();
                        free_news(news, news_count);
                        exit(EXIT_SUCCESS);
                    } else if (ch == 'n') {
                        page = (page + 1) % total_pages;
                        manual_change = 1;
                        break;
                    } else if (ch == 'p') {
                        page = (page - 1 + total_pages) % total_pages;
                        manual_change = 1;
                        break;
                    }
                }
            }
            seconds_waited++;
            elapsed_since_update++;
            // Check if it's time to refresh the RSS feed.
            if (elapsed_since_update >= config_rss_refresh_interval) {
                char *new_rss_data = fetch_rss();
                if (new_rss_data) {
                    int new_news_count = 0;
                    news_item *new_news = parse_rss(new_rss_data, &new_news_count);
                    free(new_rss_data);
                    if (new_news && new_news_count > 0) {
                        free_news(news, news_count);
                        news = new_news;
                        news_count = new_news_count;
                        total_pages = (news_count + items_per_page - 1) / items_per_page;
                        page = 0;
                        format_current_time(last_update_str, sizeof(last_update_str));
                    } else {
                        free_news(new_news, new_news_count);
                    }
                }
                elapsed_since_update = 0;
            }
        }
        // If no manual change occurred, automatically move to the next page.
        if (!manual_change) {
            page = (page + 1) % total_pages;
        }
    }

    // Cleanup (unreachable in an infinite loop)
    free_news(news, news_count);
    return EXIT_SUCCESS;
}
