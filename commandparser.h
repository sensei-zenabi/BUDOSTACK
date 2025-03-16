/*
 * commandparser.h
 *
 * This header declares the functions and types used for parsing commands
 * and executing executables. The implementation now supports searching for
 * executables in multiple directories.
 *
 * Design Principle:
 * - Modularity & Separation of Concerns: The header remains focused on the
 *   command structure and related operations.
 */

#ifndef COMMANDPARSER_H
#define COMMANDPARSER_H

#define INPUT_SIZE 256
#define MAX_PARAMETERS 10
#define MAX_OPTIONS 10

/*
 * The command parser now supports multiple directories containing executables.
 * The list of directories is defined in the commandparser.c file.
 */

typedef struct {
    char command[INPUT_SIZE];
    char *parameters[MAX_PARAMETERS];
    char *options[MAX_OPTIONS];
    int param_count;
    int opt_count;
} CommandStruct;

void parse_input(const char *input, CommandStruct *cmd);
void execute_command(const CommandStruct *cmd);
void free_command_struct(CommandStruct *cmd);

#endif
