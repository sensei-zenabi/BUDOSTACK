/*
 * find.c - A command-line tool to search files for tokens matching a given pattern.
 *
 * Design principles:
 * - Uses recursion to traverse directories using opendir/readdir/closedir.
 * - For each regular file, reads line-by-line.
 * - Tokenizes each line by scanning for alphanumeric characters and underscores.
 * - Supports search patterns with wildcards:
 *      Plain text: token contains the pattern as a substring.
 *      Pattern ending with '*' (e.g., "ass*"): token must start with the given prefix.
 *      Pattern starting with '*' (e.g., "*ass"): token must end with the given suffix.
 *      Pattern with '*' at both ends (e.g., "*ass*"): token must contain the given infix.
 * - When matches are found, prints the file’s relative path, and for each matching line prints
 *   the line number (with an indent) and the matching line.
 * - If no argument is provided, prints "please provide an argument".
 * - If two arguments are provided, the first argument is treated as a file or directory to search,
 *   and the second argument is the search pattern.
 *
 * Compile with: gcc -std=c11 -Wall -Wextra -o find find.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_LINE 1024
#define MAX_TOKEN 256
#define INDENT "    "

/*
 * match_token() checks if a given token matches the search pattern.
 * Wildcard rules:
 *   - If pattern starts with '*' and ends with '*' (and length > 2): match if token contains the infix.
 *   - If pattern starts with '*' (only): token must end with the remainder (suffix).
 *   - If pattern ends with '*' (only): token must start with the prefix.
 *   - Otherwise: match if token contains pattern as a substring.
 */
int match_token(const char *token, const char *pattern) {
    size_t tlen = strlen(token);
    size_t plen = strlen(pattern);
    
    if (plen == 0)
        return 0; // empty pattern doesn't match

    int starts_wild = (pattern[0] == '*');
    int ends_wild = (pattern[plen - 1] == '*');

    if (starts_wild && ends_wild && plen > 2) {
        // Pattern of form "*infix*": token must contain infix.
        size_t infix_len = plen - 2;
        char infix[MAX_TOKEN];
        if (infix_len >= MAX_TOKEN)
            return 0;
        strncpy(infix, pattern + 1, infix_len);
        infix[infix_len] = '\0';
        return (strstr(token, infix) != NULL);
    } else if (starts_wild && !ends_wild) {
        // Pattern of form "*suffix": token must end with suffix.
        size_t suffix_len = plen - 1;
        const char *suffix = pattern + 1;
        if (suffix_len > tlen)
            return 0;
        return (strcmp(token + tlen - suffix_len, suffix) == 0);
    } else if (!starts_wild && ends_wild) {
        // Pattern of form "prefix*": token must start with prefix.
        size_t prefix_len = plen - 1;
        if (prefix_len > tlen)
            return 0;
        return (strncmp(token, pattern, prefix_len) == 0);
    } else {
        // No wildcards: token must contain pattern as a substring.
        return (strstr(token, pattern) != NULL);
    }
}

/*
 * check_line() tokenizes a line and returns 1 if any token matches the pattern.
 */
int check_line(const char *line, const char *pattern) {
    char token[MAX_TOKEN];
    int i = 0, j;
    int matched = 0;
    int in_token = 0;
    
    for (j = 0; line[j] != '\0'; j++) {
        if (isalnum((unsigned char)line[j]) || line[j] == '_') {
            if (!in_token) {
                in_token = 1;
                i = 0;
            }
            if (i < MAX_TOKEN - 1)
                token[i++] = line[j];
        } else {
            if (in_token) {
                token[i] = '\0';
                if (match_token(token, pattern)) {
                    matched = 1;
                    break;
                }
                in_token = 0;
            }
        }
    }
    if (!matched && in_token) {
        token[i] = '\0';
        if (match_token(token, pattern))
            matched = 1;
    }
    return matched;
}

/*
 * process_file() opens a file and prints matching lines (with line numbers)
 * if any token in the line matches the search pattern.
 */
void process_file(const char *filepath, const char *pattern) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "Could not open file %s: %s\n", filepath, strerror(errno));
        return;
    }
    
    char line[MAX_LINE];
    int lineno = 0;
    int file_printed = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        if (check_line(line, pattern)) {
            if (!file_printed) {
                printf("%s\n", filepath);
                file_printed = 1;
            }
            // Remove newline from the line.
            line[strcspn(line, "\n")] = '\0';
            printf(INDENT "%d: %s\n", lineno, line);
        }
    }
    fclose(fp);
}

/*
 * search_directory() recursively traverses directories starting from dir.
 * For each regular file found, it calls process_file().
 */
void search_directory(const char *dir, const char *pattern) {
    DIR *dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "Cannot open directory %s: %s\n", dir, strerror(errno));
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        
        struct stat path_stat;
        if (stat(path, &path_stat) < 0) {
            fprintf(stderr, "stat error on %s: %s\n", path, strerror(errno));
            continue;
        }
        
        if (S_ISDIR(path_stat.st_mode)) {
            // Recursively search subdirectories.
            search_directory(path, pattern);
        } else if (S_ISREG(path_stat.st_mode)) {
            // Process regular file.
            process_file(path, pattern);
        }
    }
    closedir(dp);
}

/*
 * main() parses command-line arguments.
 *
 * Usage:
 *   1. To search the entire directory tree:
 *          find "pattern"
 *   2. To search a specific file or directory:
 *          find path "pattern"
 *
 * If no argument is provided, prints "please provide an argument".
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("please provide an argument\n");
        return EXIT_FAILURE;
    }

    if (argc == 2) {
        // Only pattern provided; search starting from the current directory.
        const char *pattern = argv[1];
        search_directory(".", pattern);
    } else if (argc == 3) {
        // A specific file or directory is provided.
        const char *path = argv[1];
        const char *pattern = argv[2];
        
        struct stat path_stat;
        if (stat(path, &path_stat) < 0) {
            fprintf(stderr, "stat error on %s: %s\n", path, strerror(errno));
            return EXIT_FAILURE;
        }
        
        if (S_ISDIR(path_stat.st_mode)) {
            // If a directory, search within it.
            search_directory(path, pattern);
        } else if (S_ISREG(path_stat.st_mode)) {
            // If a file, process only that file.
            process_file(path, pattern);
        } else {
            fprintf(stderr, "%s is not a regular file or directory.\n", path);
            return EXIT_FAILURE;
        }
    } else {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s \"pattern\"\n", argv[0]);
        fprintf(stderr, "  %s path \"pattern\"\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
