#ifndef COMMANDPARSER_H
#define COMMANDPARSER_H

#define INPUT_SIZE 256
#define MAX_PARAMETERS 10
#define MAX_OPTIONS 10

/* 
 * Absolute path to the commands directory.
 * Override this at build time if needed:
 *   gcc -std=c11 -Wall -DCOMMANDS_DIR="\"/your/absolute/path/to/commands\"" ...
 */
#ifndef COMMANDS_DIR
#define COMMANDS_DIR "/home/suoravi/git/C/commands"
#endif

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
