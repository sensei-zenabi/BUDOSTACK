#include <stdio.h>
#include <string.h>

/*
 * Design Principles:
 * - Written in plain C using -std=c11 and only standard libraries.
 * - No header files are created; everything is contained in a single file.
 * - The code includes comments to clarify design decisions.
 * - If the program is started with the "-a" argument (i.e. "help -a"),
 *   a reserved section is printed for future hidden features or advanced help.
 */
int main(int argc, char *argv[]) {
    // Print header information
    printf("\n");
    printf("/* All-Around Linux Terminal Operator - AALTO */\n");
    printf("Programmed by: Ville Suoranta (and mr. AI)\n");
    printf("License: GPLv2\n");
    printf("\n");
    
    // Print available apps and commands
    printf("Available Apps & Commands:\n");
    printf("\n");
    printf("  help     : Display this help message.\n");
    printf("  assist   : Interactive assistant that provides various utilities.\n");
	printf("  cmath    : Opens a math editor.\n");
    printf("  copy     : Copy a file from source to destination.\n");
    printf("  display  : Display the contents of a file.\n");
    printf("  edit     : Opens a basic file editor: edit <filename>.\n");
    printf("  inet     : Interactive internet connection manager.\n");
    printf("  list     : List contents of a directory (e.g. 'list tasks' or 'list apps').\n");
	printf("  makedir  : Create a new directory.\n");
    printf("  move     : Move (rename) a file from source to destination.\n");
    printf("  remove   : Remove a file.\n");
    printf("  update   : Create an empty file or update its modification time.\n");
    printf("  rmdir    : Remove an empty directory.\n");
    printf("  runtask  : Run a proprietary .task script until CTRL+c is pressed.\n");
    printf("             Type: runtask -help for more details.\n");
    printf("  stats    : Displays basic hardware stats.\n");
    printf("  exit     : Exit AALTO.\n");
    printf("\n");

    // Check for the "-a" argument to display the reserved section
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        printf("All commands:\n");
        printf("  cmath    : Opens a math editor.\n");
        printf("\n");
    }

    // Print node applications information
    printf("Running Node Applications:\n");
    printf("\n");
    printf("  All node apps are stored in the node/ folder and they can be ran only via TASK scripting.\n");
    printf("\n");
    printf("  Different types of node applications:\n");
    printf("\n");
    printf("  server   : A switchboard server that uses route.rt to route client application inputs and outputs.\n");
    printf("  <app>    : A client application that can have up to 5 inputs and 5 outputs.\n");
    printf("             Some apps might require that the server is running before starting.\n");
    printf("  client.c : Client application template.\n");
    printf("\n");
    printf("  Tips:\n");
    printf("  Start AALTO faster: ./aalto -f | Start TASK from cmd line: ./aalto mytask.task\n");
    printf("\n");
    
    return 0;
}
