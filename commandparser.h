#ifndef COMMANDPARSER_H
#define COMMANDPARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define INPUT_SIZE 1024
#define MAX_PARAMETERS 10   // For tokens that are not options
#define MAX_OPTIONS 10      // For tokens starting with '-'
#define MAX_COMMAND_LENGTH 100

typedef struct {
    char command[MAX_COMMAND_LENGTH];
    char *parameters[MAX_PARAMETERS];
    char *options[MAX_OPTIONS];
    int param_count;
    int opt_count;
} CommandStruct;

/* 
 * Parses the input line into a CommandStruct.
 * Tokens starting with '-' are treated as options.
 */
void parse_input(const char *input, CommandStruct *cmd);

/*
 * Executes the command by searching for an executable in the "./commands" directory.
 */
void execute_command(const CommandStruct *cmd);

/*
 * Frees all dynamically allocated memory in CommandStruct.
 */
void free_command_struct(CommandStruct *cmd);

#endif // COMMANDPARSER_H
