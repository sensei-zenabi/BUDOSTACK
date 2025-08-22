#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include "input.h"

#define INPUT_SIZE 1024
#define MAX_HISTORY 100  /* Maximum number of commands to store in history */

/* List of available commands for autocomplete */
static const char *commands[] = {
	"help",
	"run",   // NEW: Added "run" command for executing arbitrary shell input.
    "exit"
};

static const int num_commands = sizeof(commands) / sizeof(commands[0]);

/* Helper function prototypes */
static int autocomplete_command(const char *token, char *completion, size_t completion_size);
static int autocomplete_filename(const char *token, char *completion, size_t completion_size);
static void list_command_matches(const char *token);
static void list_filename_matches(const char *dir, const char *prefix);

/*
 * read_input()
 *
 * Modified to include a fixed-size history buffer for command history.
 * Features:
 * - Raw mode input handling (non-canonical, no echo)
 * - Up/Down arrow keys to navigate through previously entered commands
 * - Left/Right arrow keys for in-line cursor movement
 * - TAB key for autocomplete (command or filename based on position)
 *
 * Design principles:
 * - Separation of Concerns: History management, input reading, and display updates are handled here.
 * - Memory Management: Uses a fixed-size history buffer and shifts entries when full.
 * - Usability: Provides immediate feedback by replacing the current input with history commands.
 */
char* read_input(void) {
    static char buffer[INPUT_SIZE];
    size_t pos = 0;        // End of current input
    size_t cursor = 0;     // Current cursor position within buffer
    struct termios oldt, newt;

    /* Static history storage */
    static char *history[MAX_HISTORY] = {0};
    static int history_count = 0;
    static int history_index = 0;

    /* Get current terminal settings and disable canonical mode and echo */
    if (tcgetattr(STDIN_FILENO, &oldt) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }

    memset(buffer, 0, sizeof(buffer));
    fflush(stdout);

    while (1) {
        int c = getchar();
        if (c == '\n') {
            putchar('\n');
            break;
        }
        /* Handle escape sequences for arrow keys (command history navigation) */
        else if (c == '\033') {
            int next1 = getchar();
            if (next1 == '[') {
                int next2 = getchar();
                if (next2 == 'A') { /* Up arrow */
                    if (history_count > 0 && history_index > 0) {
                        history_index--;
                        /* Move cursor to end and clear line */
                        while (cursor < pos) {
                            printf("\033[C");
                            cursor++;
                        }
                        for (size_t i = 0; i < pos; i++) {
                            printf("\b \b");
                        }
                        strcpy(buffer, history[history_index]);
                        pos = strlen(buffer);
                        cursor = pos;
                        printf("%s", buffer);
                        fflush(stdout);
                    }
                    continue;
                } else if (next2 == 'B') { /* Down arrow */
                    if (history_count > 0 && history_index < history_count - 1) {
                        history_index++;
                        while (cursor < pos) {
                            printf("\033[C");
                            cursor++;
                        }
                        for (size_t i = 0; i < pos; i++) {
                            printf("\b \b");
                        }
                        strcpy(buffer, history[history_index]);
                        pos = strlen(buffer);
                        cursor = pos;
                        printf("%s", buffer);
                        fflush(stdout);
                    } else if (history_count > 0 && history_index == history_count - 1) {
                        history_index = history_count;
                        while (cursor < pos) {
                            printf("\033[C");
                            cursor++;
                        }
                        for (size_t i = 0; i < pos; i++) {
                            printf("\b \b");
                        }
                        buffer[0] = '\0';
                        pos = 0;
                        cursor = 0;
                        fflush(stdout);
                    }
                    continue;
                } else if (next2 == 'C') { /* Right arrow */
                    if (cursor < pos) {
                        printf("\033[C");
                        cursor++;
                        fflush(stdout);
                    }
                    continue;
                } else if (next2 == 'D') { /* Left arrow */
                    if (cursor > 0) {
                        printf("\033[D");
                        cursor--;
                        fflush(stdout);
                    }
                    continue;
                }
                /* Ignore other escape sequences */
            }
        }
        /* TAB pressed: trigger autocomplete */
        else if (c == '\t') {
            /* Find beginning of current token */
            size_t token_start = pos;
            while (token_start > 0 && buffer[token_start - 1] != ' ')
                token_start--;
            char token[INPUT_SIZE];
            size_t token_len = pos - token_start;
            strncpy(token, buffer + token_start, token_len);
            token[token_len] = '\0';
            if (token_len == 0)
                continue; /* Nothing to complete */

            /* If first token, autocomplete a command; otherwise, a filename */
            if (token_start == 0) {
                char completion[INPUT_SIZE] = {0};
                int count = autocomplete_command(token, completion, sizeof(completion));
                if (count == 1) {
                    size_t comp_len = strlen(completion);
                    size_t num_backspaces = pos - token_start;
                    for (size_t i = 0; i < num_backspaces; i++) {
                        printf("\b");
                    }
                    printf("%s", completion);
                    if (comp_len < num_backspaces) {
                        for (size_t i = 0; i < (num_backspaces - comp_len); i++) {
                            printf(" ");
                        }
                        for (size_t i = 0; i < (num_backspaces - comp_len); i++) {
                            printf("\b");
                        }
                    }
                    fflush(stdout);
                    memmove(buffer + token_start, completion, comp_len + 1);
                    pos = token_start + comp_len;
                    cursor = pos;
                } else if (count > 1) {
                    printf("\n");
                    list_command_matches(token);
                    printf("%s", buffer);
                    fflush(stdout);
                    cursor = pos;
                }
            } else {
                char completion[INPUT_SIZE] = {0};
                int count = autocomplete_filename(token, completion, sizeof(completion));
                if (count == 1) {
                    size_t comp_len = strlen(completion);
                    size_t num_backspaces = pos - token_start;
                    for (size_t i = 0; i < num_backspaces; i++) {
                        printf("\b");
                    }
                    printf("%s", completion);
                    if (comp_len < num_backspaces) {
                        for (size_t i = 0; i < (num_backspaces - comp_len); i++) {
                            printf(" ");
                        }
                        for (size_t i = 0; i < (num_backspaces - comp_len); i++) {
                            printf("\b");
                        }
                    }
                    fflush(stdout);
                    memmove(buffer + token_start, completion, comp_len + 1);
                    pos = token_start + comp_len;
                    cursor = pos;
                } else if (count > 1) {
                    printf("\n");
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
                    cursor = pos;
                }
            }
        }
        /* Handle backspace */
        else if (c == 127 || c == 8) {
            if (cursor > 0) {
                memmove(buffer + cursor - 1, buffer + cursor, pos - cursor + 1);
                cursor--;
                pos--;
                printf("\b");
                printf("%s ", buffer + cursor);
                for (size_t i = cursor; i <= pos; i++) {
                    printf("\b");
                }
                fflush(stdout);
            }
        }
        /* Regular character input */
        else {
            if (pos < INPUT_SIZE - 1) {
                memmove(buffer + cursor + 1, buffer + cursor, pos - cursor + 1);
                buffer[cursor] = (char)c;
                pos++;
                cursor++;
                printf("%s", buffer + cursor - 1);
                for (size_t i = cursor; i < pos; i++) {
                    printf("\b");
                }
                fflush(stdout);
            }
        }
    }
    buffer[pos] = '\0';

    /* Restore terminal settings */
    if (tcsetattr(STDIN_FILENO, TCSANOW, &oldt) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }

    /* Add nonempty command to history */
    if (strlen(buffer) > 0) {
        if (history_count == MAX_HISTORY) {
            free(history[0]);
            for (int i = 1; i < MAX_HISTORY; i++) {
                history[i - 1] = history[i];
            }
            history_count--;
        }
        history[history_count] = strdup(buffer);
        if (!history[history_count]) {
            perror("strdup failed");
            exit(EXIT_FAILURE);
        }
        history_count++;
        history_index = history_count; // Reset history index for new input
    }

    /* Return a duplicate of the buffer (caller must free it) */
    return strdup(buffer);
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
            printf("%s ", commands[i]);
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
        /* Safely construct the completion without overflowing the buffer */
        strncpy(full_completion, dir, sizeof(full_completion));
        full_completion[sizeof(full_completion) - 1] = '\0';
        strncat(full_completion, match,
                sizeof(full_completion) - strlen(full_completion) - 1);
        snprintf(completion, completion_size, "%s", full_completion);
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
            printf("%s ", entry->d_name);
    }
    closedir(d);
    printf("\n");
}
