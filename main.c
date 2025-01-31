#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Function declarations
void echo(char *input);
void get_date();
void help();
void process_command(char *command);

int main() {
    char command[256];

    printf("Welcome to the Simple C Terminal!\n");
    printf("Type 'help' for a list of commands.\n");

    while (1) {
        printf("> ");  // Terminal prompt
        fgets(command, sizeof(command), stdin);

        // Remove trailing newline
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "exit") == 0) {
            printf("Exiting terminal...\n");
            break;
        }

        process_command(command);
    }

    return 0;
}

// Function to process the command
void process_command(char *command) {
    if (strncmp(command, "echo ", 5) == 0) {
        echo(command + 5);
    } else if (strcmp(command, "date") == 0) {
        get_date();
    } else if (strcmp(command, "help") == 0) {
        help();
    } else {
        printf("Unknown command: %s\n", command);
    }
}

// Echo command implementation
void echo(char *input) {
    printf("%s\n", input);
}

// Date command implementation
void get_date() {
    time_t t;
    time(&t);
    printf("Current date and time: %s", ctime(&t));
}

// Help command implementation
void help() {
    printf("Available commands:\n");
    printf("  echo [text] - Prints the text to the terminal\n");
    printf("  date - Displays the current date and time\n");
    printf("  help - Lists available commands\n");
    printf("  exit - Exits the terminal\n");
}
