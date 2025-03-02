#ifndef COMMANDPARSER_H
#define COMMANDPARSER_H

#define INPUT_SIZE 256
#define MAX_PARAMETERS 10
#define MAX_OPTIONS 10

/*
 * Relative path to the commands directory.
 * This assumes that the commands directory is located
 * relative to the current working directory or executable location.
 * Adjust the path as necessary to match your project's structure.
 */
#ifndef COMMANDS_DIR
#define COMMANDS_DIR "./commands"
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
