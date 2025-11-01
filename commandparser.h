/*
 * commandparser.h
 *
 * This header declares the functions and types used for parsing commands
 * and executing executables. Wildcard expansion for parameters is now performed
 * in the parse_input function, ensuring that commands receive pre-expanded arguments.
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

typedef struct {
    char command[INPUT_SIZE];
    char *parameters[MAX_PARAMETERS];
    char *options[MAX_OPTIONS];
    int param_count;
    int opt_count;
    char *redirect_path;
    int redirect_append;
} CommandStruct;

void parse_input(const char *input, CommandStruct *cmd);
int execute_command(const CommandStruct *cmd);
void free_command_struct(CommandStruct *cmd);

/* 
 * Sets the base directory for command lookup.
 * The base directory is typically the directory where the executable is located.
 * This allows commands to be found regardless of the current working directory.
 */
void set_base_path(const char *path);

#endif
