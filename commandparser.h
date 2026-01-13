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

#include <stddef.h>

#define INPUT_SIZE 1024

typedef struct {
    char command[INPUT_SIZE];
    char **parameters;
    char **options;
    char **args;
    int param_count;
    int opt_count;
    int arg_count;
    size_t param_capacity;
    size_t opt_capacity;
    size_t arg_capacity;
} CommandStruct;

void init_command_struct(CommandStruct *cmd);
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
