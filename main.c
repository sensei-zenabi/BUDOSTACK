#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>

#define INPUT_SIZE 1024
#define MAX_ATTRIBUTES 10
#define MAX_ARGUMENTS 10
#define MAX_COMMAND_LENGTH 100

typedef struct {
    char command[MAX_COMMAND_LENGTH];
    char *attributes[MAX_ATTRIBUTES];
    char *arguments[MAX_ARGUMENTS];
    int attr_count;
    int arg_count;
} CommandStruct;

// Function declarations
void parse_input(const char *input, CommandStruct *cmd);
void execute_command(const CommandStruct *cmd);
void free_command_struct(CommandStruct *cmd);

// List of available commands (all single words)
const char *available_commands[] = {
    "hello",
    "help",
    "list",
    "display",
    "copy",
    "move",
    "remove",
    "update",
    "makedir",
    "rmdir",
    "exit"
};
const int num_commands = sizeof(available_commands) / sizeof(available_commands[0]);

// Auto-completion helper: completes the first token in the buffer.
void auto_complete(char *buffer, int *pos, size_t size) {
    char token[100];
    int i = 0;
    while (buffer[i] != '\0' && buffer[i] != ' ' && i < 99) {
        token[i] = buffer[i];
        i++;
    }
    token[i] = '\0';
    if (i == 0) return; // nothing to complete

    int match_count = 0;
    const char *match = NULL;
    for (int j = 0; j < num_commands; j++) {
        if (strncmp(token, available_commands[j], strlen(token)) == 0) {
            match_count++;
            if (match_count == 1) {
                match = available_commands[j];
            }
        }
    }
    if (match_count == 1 && match) {
        int token_len = strlen(token);
        int match_len = strlen(match);
        int rest = match_len - token_len;
        if (*pos + rest < size) {
            strcpy(buffer + *pos, match + token_len);
            *pos += rest;
            printf("%s", match + token_len);
            fflush(stdout);
        }
    } else if (match_count > 1) {
        printf("\n");
        for (int j = 0; j < num_commands; j++) {
            if (strncmp(token, available_commands[j], strlen(token)) == 0) {
                printf("%s\t", available_commands[j]);
            }
        }
        printf("\n");
        printf("linux-shell> %s", buffer);
        fflush(stdout);
    }
}

// Reads input from the user with auto-completion support.
void read_input(char *buffer, size_t size) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    // Disable canonical mode and echo
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int pos = 0;
    int c;
    printf("linux-shell> ");
    fflush(stdout);
    while ((c = getchar()) != '\n' && pos < size - 1) {
        if (c == '\t') { // Tab pressed: trigger auto-completion
            auto_complete(buffer, &pos, size);
        } else if (c == 127 || c == 8) { // Handle backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else {
            buffer[pos++] = c;
            putchar(c);
            fflush(stdout);
        }
    }
    buffer[pos] = '\0';
    printf("\n");
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

int main() {
    char input[INPUT_SIZE];
    CommandStruct cmd;
    printf("Welcome to the Linux-like Terminal!\nType 'exit' to quit.\n");
    while (1) {
        read_input(input, INPUT_SIZE);
        if (strcmp(input, "exit") == 0) {
            break;
        }
        parse_input(input, &cmd);
        execute_command(&cmd);
        free_command_struct(&cmd);
    }
    printf("Exiting terminal...\n");
    return 0;
}
