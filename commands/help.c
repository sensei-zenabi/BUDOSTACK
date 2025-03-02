#include <stdio.h>

int main() {
	printf("\n");
	printf("/* All-Around Linux Terminal Operator - AALTO */\n");
	printf("Programmed by: Ville Suoranta (and mr. AI)\n");
	printf("License: GPLv2\n");
	printf("\n");
    printf("Available Commands:\n");
    printf("\n");
    printf("  help     : Display this help message.\n");
    printf("  discover : Interactive discovery tool for network and hw peripherals.\n");
	printf("  display  : Display the contents of a file.\n");
    printf("  list     : List contents of a directory (default is current directory).\n");
    printf("  copy     : Copy a file from source to destination.\n");
    printf("  move     : Move (rename) a file from source to destination.\n");
    printf("  remove   : Remove a file.\n");
    printf("  update   : Create an empty file or update its modification time.\n");
    printf("  makedir  : Create a new directory.\n");
    printf("  rmdir    : Remove an empty directory.\n");
    printf("  runtask  : Run a proprietary .task script until CTRL+c is pressed.\n");
    printf("             Type: runtask -help for more details.\n");
	printf("  stats    : Displays basic hardware stats.\n");
    printf("  exit     : Exit AALTO.\n");
    printf("\n");
	printf("Running Applications:\n");
	printf("\n");
	printf("  All applications are stored in the apps/ folder and they can be ran only via TASK scripting.\n");
	printf("\n");
	printf("  Different types of applications:\n");
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
