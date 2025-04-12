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
    printf("==== All-Around Linux Terminal Operator - AALTO ====\n");
    printf("Programmed by: Ville Suoranta (and mr. AI)\n");
    printf("License: GPLv2\n");
    printf("\n");

    // Print available apps and commands
    printf("/* Available Apps & Commands */\n");
    printf("\n");
    printf("  help     : Display this help message.\n");
    printf("  assist   : Interactive assistant that provides various utilities.\n");
    printf("  cls      : Clear terminal screen.\n");
    printf("  cmath    : Opens a math editor.\n");
    printf("  compile  : Universal C compile command. Type 'compile -help'.\n");
    printf("  copy     : Copy anything to anywhere, a file or complete folder.\n");
    printf("  csv_print: Pretty prints a .csv file.\n");
    printf("  ctalk    : Simple UDP based LAN chat. Start with 'ctalk myname'.\n");
    printf("  display  : Display the contents of a file.\n");
    printf("  drives   : Lists all found drives.\n");
    printf("  edit     : Opens a basic file editor: edit <filename>.\n");
    printf("             Supported languages: C/C++, Markup\n");
    printf("  exchange : Retrieves exchange rates of common currencies to euro.\n");
    printf("  find     : Find anything.\n");
    printf("  git      : Git helper, type git -help.\n");
    printf("  inet     : Interactive internet connection manager.\n");
    printf("  list     : List contents of a directory (type 'list -help').\n");
    printf("  makedir  : Create a new directory.\n");
    printf("  mdread   : Pretty-prints .md files. Use: mdread readme.md\n");
    printf("  move     : Moves anything. Can be used for renaming as well.\n");
    printf("  pack     : Pack anything, e.g. 'pack myfolder myfolder.zip'\n");
    printf("  remove   : Remove anything, whether it is a file or folder.\n");
    printf("  restart  : A command to re-compile and restart AALTO. Use with caution!\n");
    printf("             Use 'restart -f' to clean before building.\n");
    printf("  rss      : Lightweight rss news app, tested only with yle rss feed.\n");
    printf("  run      : Run any executable/shell script or execute linux terminal cmd.\n");
    printf("             e.g. 'run git status' or 'run ./myexecutable'\n");
    printf("  runtask  : Run a proprietary .task script until CTRL+c is pressed.\n");
    printf("             Type: runtask -help for more details.\n");
	printf("  skydial  : Simple sky-dial to identify and locate celestial objects.\n");
	printf("             Type 'skydial lat lon' to tell your position.\n");
	printf("  slides   : Terminal slideset editor, to start 'slides myslides.sld'.\n");
	printf("             For help CTRL+H when the app is running.\n");
	printf("  solar    : Visualizes the solar system and it's planets. 'solar X', where\n");
	printf("             X is the number of planets from 2 to 8.\n");
    printf("  stats    : Displays basic hardware stats.\n");
    printf("  table    : Lightweight spreadsheet tool, open file 'table mytable.tbl'.\n");
	printf("  time     : Display time now in different time-zones. 'time -s' for astro events.\n");
    printf("  unpack   : Unpack what has been packed, e.g. 'unpack myfolder.zip'\n");
    printf("  update   : Create an empty file or update its modification time.\n");
    printf("  exit     : Exit AALTO.\n");
    printf("\n");

    // Check for the "-a" argument to display the reserved section
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        printf("All commands:\n");
        printf("  cmath    : Opens a math editor.\n");
        printf("\n");
    }

    // Games list
    printf("\n/* List of Games */\n");
    printf("\n");
    printf("  invaders : A space invaders clone tailored to terminal.\n");
    printf("  snake    : A snake clone, reminence from the good old Nokia days.\n");
    printf("\n");
    printf("  > Go to games folder, and type \"run ./invaders\"\n");
    printf("\n");

    // Print node applications information
    printf("\n/* Running Node Applications */\n");
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
