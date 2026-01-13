/*
 * find.c - A command-line tool to search files for patterns with optional flags.
 *
 * Usage: find <string> [-fw] [-hf] [-cs] [-git]
 *
 * Flags:
 *   -fw  = full word matching (match must align to word boundaries).
 *   -hf  = include hidden folders and files in search (except .git unless -git).
 *   -cs  = case-sensitive matching (default is case-insensitive).
 *   -git = include .git folders in search.
 *
 * The search string supports '*' wildcards to match any sequence of characters.
 * Examples: *.*, *.txt, note.*, *note.*, note*.*, *note*.*, *note.txt, note*.txt,
 *           *note*.txt, note.ex*, note.*xe.
 *
 * Output prints the file path, then matching lines with line numbers and
 * highlighted matches. A blank line separates results between files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_LINE 1024
#define INDENT "    "
#define HIGHLIGHT_START "\x1b[43m"
#define HIGHLIGHT_END "\x1b[0m"

struct search_options {
    int full_word;
    int include_hidden;
    int case_sensitive;
    int include_git;
};

struct match_span {
    size_t start;
    size_t length;
};

static int is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int chars_equal(char a, char b, int case_sensitive) {
    if (case_sensitive) {
        return a == b;
    }
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int pattern_all_wildcards(const char *pattern) {
    while (*pattern != '\0') {
        if (*pattern != '*') {
            return 0;
        }
        pattern++;
    }
    return 1;
}

static int match_pattern_recursive(const char *text, const char *pattern,
                                   int case_sensitive, size_t *match_len) {
    if (*pattern == '\0') {
        *match_len = 0;
        return 1;
    }

    if (*pattern == '*') {
        size_t i = 0;
        while (1) {
            size_t sub_len = 0;
            if (match_pattern_recursive(text + i, pattern + 1, case_sensitive,
                                        &sub_len)) {
                *match_len = i + sub_len;
                return 1;
            }
            if (text[i] == '\0') {
                break;
            }
            i++;
        }
        return 0;
    }

    if (*text == '\0') {
        return 0;
    }

    if (!chars_equal(*text, *pattern, case_sensitive)) {
        return 0;
    }

    size_t sub_len = 0;
    if (match_pattern_recursive(text + 1, pattern + 1, case_sensitive,
                                &sub_len)) {
        *match_len = 1 + sub_len;
        return 1;
    }

    return 0;
}

static int match_pattern_at(const char *text, const char *pattern,
                            const struct search_options *options,
                            size_t *match_len) {
    return match_pattern_recursive(text, pattern, options->case_sensitive,
                                   match_len);
}

static int match_full_word(const char *line, size_t line_len, size_t start,
                           size_t length) {
    size_t end = start + length;
    if (start > 0 && is_word_char(line[start - 1])) {
        return 0;
    }
    if (end < line_len && is_word_char(line[end])) {
        return 0;
    }
    return 1;
}

static size_t collect_matches(const char *line, const char *pattern,
                              const struct search_options *options,
                              struct match_span *matches, size_t max_matches) {
    size_t line_len = strlen(line);
    size_t count = 0;

    if (pattern_all_wildcards(pattern)) {
        if (line_len > 0 && count < max_matches) {
            matches[count].start = 0;
            matches[count].length = line_len;
            count++;
        }
        return count;
    }

    for (size_t i = 0; i < line_len; ) {
        size_t match_len = 0;
        if (match_pattern_at(line + i, pattern, options, &match_len)) {
            if (match_len == 0) {
                i++;
                continue;
            }

            if (!options->full_word ||
                match_full_word(line, line_len, i, match_len)) {
                if (count < max_matches) {
                    matches[count].start = i;
                    matches[count].length = match_len;
                    count++;
                }
                i += match_len;
                continue;
            }
        }
        i++;
    }

    return count;
}

static void print_line_with_highlight(int lineno, const char *line,
                                      const struct match_span *matches,
                                      size_t match_count) {
    printf(INDENT "%d: ", lineno);

    size_t cursor = 0;
    size_t line_len = strlen(line);
    for (size_t i = 0; i < match_count; i++) {
        size_t start = matches[i].start;
        size_t length = matches[i].length;
        if (start > cursor) {
            fwrite(line + cursor, 1, start - cursor, stdout);
        }
        printf(HIGHLIGHT_START);
        fwrite(line + start, 1, length, stdout);
        printf(HIGHLIGHT_END);
        cursor = start + length;
    }
    if (cursor < line_len) {
        fwrite(line + cursor, 1, line_len - cursor, stdout);
    }
    printf("\n");
}

static int process_file(const char *filepath, const char *pattern,
                        const struct search_options *options) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "Could not open file %s: %s\n", filepath,
                strerror(errno));
        return 0;
    }

    char line[MAX_LINE];
    int lineno = 0;
    int file_printed = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        line[strcspn(line, "\n")] = '\0';

        struct match_span matches[MAX_LINE];
        size_t match_count = collect_matches(line, pattern, options, matches,
                                             MAX_LINE);
        if (match_count > 0) {
            if (!file_printed) {
                printf("%s\n", filepath);
                file_printed = 1;
            }
            print_line_with_highlight(lineno, line, matches, match_count);
        }
    }

    fclose(fp);
    return file_printed;
}

static int should_skip_entry(const char *name,
                             const struct search_options *options) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1;
    }

    if (name[0] == '.') {
        if (strcmp(name, ".git") == 0) {
            return !options->include_git;
        }
        return !options->include_hidden;
    }

    return 0;
}

static void search_directory(const char *dir, const char *pattern,
                             const struct search_options *options) {
    DIR *dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "Cannot open directory %s: %s\n", dir,
                strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (should_skip_entry(entry->d_name, options)) {
            continue;
        }

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        struct stat path_stat;
        if (stat(path, &path_stat) < 0) {
            fprintf(stderr, "stat error on %s: %s\n", path, strerror(errno));
            continue;
        }

        if (S_ISDIR(path_stat.st_mode)) {
            search_directory(path, pattern, options);
        } else if (S_ISREG(path_stat.st_mode)) {
            if (process_file(path, pattern, options)) {
                printf("\n");
            }
        }
    }

    closedir(dp);
}

static void print_usage(const char *program) {
    (void)program;
    printf("Usage: find <string> [-fw] [-hf] [-cs] [-git]\n");
    printf("\n");
    printf("Search files for lines matching <string> (supports '*'\n");
    printf("wildcards).\n");
    printf("\n");
    printf("Options:\n");
    printf("  -fw    Match full words only.\n");
    printf("  -hf    Include hidden folders and files (except .git\n");
    printf("         unless -git).\n");
    printf("  -cs    Case-sensitive matching (default is case-insensitive).\n");
    printf("  -git   Include .git folders in search.\n");
    printf("  -h     Show this help message.\n");
    printf("  -help  Show this help message.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  find note\n");
    printf("  find \"*note*\" -fw\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "-help") == 0) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    const char *pattern = argv[1];
    struct search_options options = {0, 0, 0, 0};

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-fw") == 0) {
            options.full_word = 1;
        } else if (strcmp(argv[i], "-hf") == 0) {
            options.include_hidden = 1;
        } else if (strcmp(argv[i], "-cs") == 0) {
            options.case_sensitive = 1;
        } else if (strcmp(argv[i], "-git") == 0) {
            options.include_git = 1;
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    search_directory(".", pattern, &options);
    return EXIT_SUCCESS;
}
