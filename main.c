#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

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

void display_prompt() {
    printf("linux-shell> ");
}

int main() {
    char input[INPUT_SIZE];
    CommandStruct cmd;

    printf("Welcome to the Linux-like Terminal!\nType 'exit' to quit.\n");

    while (1) {
        display_prompt();
        if (fgets(input, INPUT_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }

        // Remove newline character
        input[strcspn(input, "\n")] = 0;

        // Exit command
        if (strcmp(input, "exit") == 0) {
            break;
        }

        // Parse and execute the command
        parse_input(input, &cmd);
        execute_command(&cmd);
        free_command_struct(&cmd);
    }

    printf("Exiting terminal...\n");
    return 0;
}
