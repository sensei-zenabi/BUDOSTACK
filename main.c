#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commandparser.h"

/* Displays the terminal prompt */
void display_prompt(void) {
    printf("linux-shell> ");
}

int main(void) {
    char input[INPUT_SIZE];
    CommandStruct cmd;

    printf("Welcome to the Linux-like Terminal!\nType 'exit' to quit.\n");

    while (1) {
        display_prompt();
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }
        /* Remove the trailing newline character */
        input[strcspn(input, "\n")] = '\0';

        /* Exit command */
        if (strcmp(input, "exit") == 0) {
            break;
        }

        /* Parse the input and execute the command */
        parse_input(input, &cmd);
        execute_command(&cmd);
        free_command_struct(&cmd);
    }

    printf("Exiting terminal...\n");
    return 0;
}
