#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>   // For chdir() and getcwd()
#include "commandparser.h"

void display_prompt(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s$ ", cwd);
    else
        printf("shell$ ");
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
        if (strcmp(input, "exit") == 0) {
            break;
        }
        parse_input(input, &cmd);
        /* Handle built-in "cd" command */
        if (strcmp(cmd.command, "cd") == 0) {
            if (cmd.param_count > 0) {
                if (chdir(cmd.parameters[0]) != 0) {
                    perror("cd");
                }
            } else {
                fprintf(stderr, "cd: missing operand\n");
            }
            free_command_struct(&cmd);
            continue;
        }
        /* For all other commands, execute normally using the fixed COMMANDS_DIR */
        execute_command(&cmd);
        free_command_struct(&cmd);
    }
    printf("Exiting terminal...\n");
    return 0;
}
