#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include "input.h"

#define INPUT_SIZE 1024

/* List of available commands */
static const char *commands[] = {
    "hello", "help", "list", "display", "copy",
    "move", "remove", "update", "makedir", "rmdir",
    "exit", "cd"
};
static const int num_commands = sizeof(commands) / sizeof(commands[0]);

/* Helper function prototypes */
static int autocomplete_command(const char *token, char *completion, size_t completion_size);
static int autocomplete_filename(const char *token, char *completion, size_t completion_size);
static void list_command_matches(const char *token);
static void list_filename_matches(const char *dir, const char *prefix);

char* read_input(void) {
    static char buffer[INPUT_SIZE];
    size_t pos = 0;
    struct termios oldt, newt;

    /* Get current terminal settings and disable canonical mode and echo */
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    memset(buffer, 0, sizeof(buffer));
    fflush(stdout);

    while (1) {
        int c = getchar();
        if (c == '\n') {
            putchar('\n');
            break;
        } else if (c == '\t') {  // TAB pressed: trigger autocomplete
            /* Find the beginning of the current token */
            size_t token_start = pos;
            while (token_start > 0 && buffer[token_start - 1] != ' ')
                token_start--;
            char token[INPUT_SIZE];
            size_t token_len = pos - token_start;
            strncpy(token, buffer + token_start, token_len);
            token[token_len] = '\0';

            if (token_len == 0)
                continue;  // nothing to complete

            /* If this is the first token, complete a command;
               otherwise, complete a filename */
            if (token_start == 0) {
                char completion[INPUT_SIZE] = {0};
                int count = autocomplete_command(token, completion, sizeof(completion));
                if (count == 1) {
                    size_t comp_len = strlen(completion);
                    if (comp_len > token_len) {
                        strcpy(buffer + pos, completion + token_len);
                        pos += comp_len - token_len;
                        printf("%s", completion + token_len);
                        fflush(stdout);
                    }
                } else if (count > 1) {
                    printf("\n");
                    list_command_matches(token);
                    printf("%s", buffer);
                    fflush(stdout);
                }
            } else {
                char completion[INPUT_SIZE] = {0};
                int count = autocomplete_filename(token, completion, sizeof(completion));
                if (count == 1) {
                    size_t comp_len = strlen(completion);
                    if (comp_len > token_len) {
                        strcpy(buffer + pos, completion + token_len);
                        pos += comp_len - token_len;
                        printf("%s", completion + token_len);
                        fflush(stdout);
                    }
                } else if (count > 1) {
                    printf("\n");
                    /* Split token into directory and prefix */
                    char dir[INPUT_SIZE];
                    char prefix[INPUT_SIZE];
                    const char *last_slash = strrchr(token, '/');
                    if (last_slash) {
                        size_t dir_len = last_slash - token + 1;
                        strncpy(dir, token, dir_len);
                        dir[dir_len] = '\0';
                        strcpy(prefix, last_slash + 1);
                    } else {
                        strcpy(dir, "./");
                        strcpy(prefix, token);
                    }
                    list_filename_matches(dir, prefix);
                    printf("%s", buffer);
                    fflush(stdout);
                }
            }
        } else if (c == 127 || c == 8) {  // handle backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else {
            if (pos < INPUT_SIZE - 1) {
                buffer[pos++] = (char)c;
                putchar(c);
                fflush(stdout);
            }
        }
    }
    buffer[pos] = '\0';
    /* Restore terminal settings */
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return strdup(buffer);  // caller is responsible for freeing
}

static int autocomplete_command(const char *token, char *completion, size_t completion_size) {
    int match_count = 0;
    const char *match = NULL;
    for (int i = 0; i < num_commands; i++) {
        if (strncmp(commands[i], token, strlen(token)) == 0) {
            match_count++;
            if (match_count == 1) {
                match = commands[i];
                strncpy(completion, match, completion_size - 1);
                completion[completion_size - 1] = '\0';
            }
        }
    }
    return match_count;
}

static void list_command_matches(const char *token) {
    for (int i = 0; i < num_commands; i++) {
        if (strncmp(commands[i], token, strlen(token)) == 0)
            printf("%s    ", commands[i]);
    }
    printf("\n");
}

static int autocomplete_filename(const char *token, char *completion, size_t completion_size) {
    char dir[INPUT_SIZE];
    char prefix[INPUT_SIZE];
    const char *last_slash = strrchr(token, '/');
    if (last_slash) {
        size_t dir_len = last_slash - token + 1;
        strncpy(dir, token, dir_len);
        dir[dir_len] = '\0';
        strcpy(prefix, last_slash + 1);
    } else {
        strcpy(dir, "./");
        strcpy(prefix, token);
    }
    int match_count = 0;
    char match[INPUT_SIZE] = {0};
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0) {
            match_count++;
            if (match_count == 1)
                strcpy(match, entry->d_name);
        }
    }
    closedir(d);
    if (match_count == 1) {
        char full_completion[INPUT_SIZE];
        snprintf(full_completion, sizeof(full_completion), "%s%s", dir, match);
        strncpy(completion, full_completion, completion_size - 1);
        completion[completion_size - 1] = '\0';
    }
    return match_count;
}

static void list_filename_matches(const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0)
            printf("%s    ", entry->d_name);
    }
    closedir(d);
    printf("\n");
}
