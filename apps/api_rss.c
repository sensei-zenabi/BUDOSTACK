/*
    RSS Reader Application
    ------------------------
    This application fetches an RSS feed from a defined URL and displays the news items in pages
    that exactly fit the terminal height. Each news item shows its publication timestamp and title.
    
    Design principles used:
      - Single-source definition: The RSS URL and timer values are defined as macros.
      - Minimal dependencies: Uses standard libraries and POSIX functions (popen, sleep, system).
      - Non-blocking timers: Uses sleep() calls to avoid busy-waiting.
      - Paging: Automatically scrolls through pages every PAGE_INTERVAL seconds, with a bottom bar showing the last update time and current page.
      - Periodic update: Every RSS_REFRESH_INTERVAL seconds the feed is refreshed.
    
    Notes:
      - The _POSIX_C_SOURCE macro is defined to expose popen/pclose.
      - Terminal height is determined via "tput lines" with a default of 24 if unavailable.
      - The bottom bar occupies the last terminal line.
      - Each news item is displayed as:
            [timestamp]
            News: <title>
            (empty line)
    
    To compile (using -std=c11):
        gcc -std=c11 -Wall -Wextra -pedantic -Wno-format-truncation -o rss_reader rss_reader.c
*/

// Default configuration macros
#define _POSIX_C_SOURCE 200809L
#define RSS_URL "https://feeds.yle.fi/uutiset/v1/recent.rss?publisherIds=YLE_UUTISET"
#define PAGE_INTERVAL 25          // Seconds to display each page
#define RSS_REFRESH_INTERVAL 1800 // Seconds between RSS feed updates (30 minutes)

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>  // For sleep()

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
    if (snprintf(command, sizeof(command), "curl -s \"%s\"", RSS_URL) < 0) {
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
// It clears the screen, prints news items (each using 2 lines plus a blank line) and pads to reach (term_lines - 1).
// Then prints a bottom bar with the last update time and page info.
void display_page(news_item *news, int news_count, int page, int items_per_page, int term_lines, const char *last_update_str, int total_pages) {
    system("clear");
    int start = page * items_per_page;
    int printed_lines = 0;

    // Print each news item in this page.
    for (int i = 0; i < items_per_page; i++) {
        int index = start + i;
        // Wrap-around if index exceeds available news items.
        if (index >= news_count) {
            index %= news_count;
        }
        // Each news item uses 3 lines: [timestamp], "News: <title>", and a blank line.
        printf("[%s]\n", news[index].timestamp);
        printf("News: %s\n", news[index].title);
        printf("\n");
        printed_lines += 3;
    }
    // Pad with blank lines if needed so that the bottom bar is always at the last line.
    int padding = (term_lines - 1) - printed_lines;
    for (int i = 0; i < padding; i++) {
        printf("\n");
    }
    // Print bottom bar.
    printf("Last update: %s | Page: %d/%d", last_update_str, page + 1, total_pages);
    fflush(stdout);
}

int main(void) {
    // Determine terminal height.
    int term_lines = get_terminal_lines();
    // Reserve last line for bottom bar; news items will use groups of 3 lines each.
    int items_per_page = (term_lines - 1) / 3;
    if (items_per_page < 1) {
        items_per_page = 1;
    }

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

    // Timers: page update every PAGE_INTERVAL seconds, RSS refresh every RSS_REFRESH_INTERVAL seconds.
    int elapsed_since_update = 0;
    int page = 0;

    // Main loop: display pages and update RSS every RSS_REFRESH_INTERVAL seconds.
    while (1) {
        display_page(news, news_count, page, items_per_page, term_lines, last_update_str, total_pages);
        sleep(PAGE_INTERVAL);  // Blocking sleep; does not consume CPU.
        elapsed_since_update += PAGE_INTERVAL;
        page = (page + 1) % total_pages;
        // If it's time to refresh the RSS feed.
        if (elapsed_since_update >= RSS_REFRESH_INTERVAL) {
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
                    // Reset page to start.
                    page = 0;
                    // Update last update timestamp.
                    format_current_time(last_update_str, sizeof(last_update_str));
                } else {
                    // If parsing fails, free new_news if allocated.
                    free_news(new_news, new_news_count);
                }
            }
            elapsed_since_update = 0;
        }
    }
    
    // Cleanup (unreachable in an infinite loop)
    free_news(news, news_count);
    return EXIT_SUCCESS;
}
