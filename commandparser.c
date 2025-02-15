#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

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

void parse_input(const char *input, CommandStruct *cmd) {
    char buffer[1024];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *token = strtok(buffer, " ");
    if (token == NULL) return;

    // Store command
    strncpy(cmd->command, token, sizeof(cmd->command) - 1);
    cmd->command[sizeof(cmd->command) - 1] = '\0';

    cmd->attr_count = 0;
    cmd->arg_count = 0;

    while ((token = strtok(NULL, " ")) != NULL) {
        if (token[0] == '-') {
            if (cmd->arg_count < MAX_ARGUMENTS) {
                cmd->arguments[cmd->arg_count++] = strdup(token);
            }
        } else {
            if (cmd->attr_count < MAX_ATTRIBUTES) {
                cmd->attributes[cmd->attr_count++] = strdup(token);
            }
        }
    }
}

void execute_command(const CommandStruct *cmd) {
    char command_path[128];
    snprintf(command_path, sizeof(command_path), "./commands/%s", cmd->command);

    // Prepare arguments array (execvp format)
    char *args[cmd->attr_count + cmd->arg_count + 2];
    args[0] = cmd->command;
    int index = 1;

    for (int i = 0; i < cmd->attr_count; i++) {
        args[index++] = cmd->attributes[i];
    }
    for (int i = 0; i < cmd->arg_count; i++) {
        args[index++] = cmd->arguments[i];
    }
    args[index] = NULL;

    // Execute command
    if (access(command_path, X_OK) == 0) {
        if (fork() == 0) { // Child process
            execvp(command_path, args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        } else { // Parent process
            wait(NULL);
        }
    } else {
        printf("Command not found: %s\n", cmd->command);
    }
}

void free_command_struct(CommandStruct *cmd) {
    for (int i = 0; i < cmd->attr_count; i++) {
        free(cmd->attributes[i]);
    }
    for (int i = 0; i < cmd->arg_count; i++) {
        free(cmd->arguments[i]);
    }
}
