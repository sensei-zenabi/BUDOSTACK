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
    printf("================== BUDOSTACK - The Martial Arts of Software  ================\n");
    printf("\n");
    printf(" Programmed by:   Ville Suoranta\n");
    printf(" Contact email:   budostack@gmail.com\n");
    printf(" License:         GPLv2\n");
    printf("\n");
    printf("  You can use, modify, and sell GPLv2 software freely, but if you distribute\n");
    printf("  it, you must provide the source code and keep it under the same license.\n");
    printf("\n");
    printf(" Note! Setting up BUDOSTACK for your console:\n");
    printf("\n");
    printf("      1. Take into use the font: ModernDOS8x8.ttf installed by setup.sh\n");
    printf("      2. Adjust your console font size and view to display 118cols x 66rows\n");
    printf("         with 1980x1080 resolution, font size 24pt seems OK when fullscreen!\n");
    printf("      3. Run _TEST and verify the result\n");
    printf("\n");
    printf("-----------------------------------------------------------------------------\n");
    printf("                           - SYSTEM REQUIREMENTS -\n");
    printf("\n");
    printf(" Operating System: Debian Linux\n");
    printf("\n");
    printf(" Tested distributions:\n");
    printf("\n");
    printf("  - Ubuntu / Kubuntu\n");
    printf("  - Raspberry Pi OS\n");
    printf("\n");
    printf("-----------------------------------------------------------------------------\n");
    printf("                           - GENERAL INFORMATION -\n");
    printf("\n");
    printf("/* TOP Tips */\n");
    printf("\n");
    printf("  edit     : Opens a basic file editor: edit <filename>.\n");
    printf("             Supported languages: C/C++, Markup\n");
    printf("  restart  : A command to re-compile and restart BUDOSTACK. Use with caution!\n");
    printf("             Use 'restart -f' to clean before building.\n");
    printf("  update   : Update your BUDOSTACK version automatically.\n");     
    printf("\n");
    printf("  Start BUDOSTACK faster: ./budostack -f | Start TASK: ./budostack mytask.task\n");
    printf("\n");
    printf("/* Folder Structure */\n");
    printf("\n");
    printf("  ./apps/       - System applications that do not use paging.\n");
    printf("  ./commands/   - BUDOSTACK (BS) programming commands for developers.\n");
    printf("  ./fonts/      - Folder for built-in .ttf and .psf fonts\n");
    printf("  ./games/      - Built-in games.\n");
    printf("  ./lib/        - System libraries used by applications and utilities.\n");
    printf("  ./tasks/      - Reserved for tasks. Few built-in examples provided.\n");
    printf("  ./users/      - User folders for those who like them.\n");
    printf("  ./utilities/  - System utilities that use paging.\n");
    printf("\n");
    printf("-----------------------------------------------------------------------------\n");
    printf("                             - SYSTEM UTILITIES -\n");
    printf("\n");
    printf("  In general BUDOSTACK passes commands as they are unless they are found\n");
    printf("  from the list of built-in commands.\n");
    printf("\n");
    printf("/* System Commands */\n");
    printf("\n");
    printf("  cd       : Change Directory, remember to put 'if space' in folder name.\n");
    printf("  cls      : Clear terminal screen.\n");
    printf("  copy     : Copy anything to anywhere, a file or complete folder.\n");
    printf("  crc32    : Calculate and/or verify CRC32 checksum of a file.\n");
    printf("  diff     : See the difference of two files.\n");
    printf("  display  : Display the contents of a file or an image supported by paint.\n");    
    printf("  drives   : Lists all found drives.\n");    
    printf("  find     : Find anything.\n");    
    printf("  gitter   : Professional git helper for your daily development activities.\n");
    printf("  help     : Display this help message.\n");
    printf("  hw       : Learn your hardware specs just by typing 'hw'.\n");
    printf("  list     : List contents of a directory (type 'list -help').\n");    
    printf("  makedir  : Create a new directory.\n");    
    printf("  move     : Moves anything. Can be used for renaming as well.\n");
    printf("  mute     : Enable/Disable Voice Assistant.\n");
    printf("  pack     : Pack anything, e.g. 'pack myfolder myfolder.zip'\n");    
    printf("  remove   : Remove anything, whether it is a file or folder.\n");
    printf("  runtask  : Run a proprietary .task script until CTRL+c is pressed.\n");
    printf("             Type: runtask -help for more details.\n");
    printf("  stats    : Displays basic hardware stats.\n");
    printf("  unpack   : Unpack what has been packed, e.g. 'unpack myfolder.zip'\n");
    printf("  exit     : Exit BUDOSTACK.\n");
    printf("\n");
    printf("-----------------------------------------------------------------------------\n");
    printf("                            - SYSTEM APPLICATIONS -\n");
    printf("\n");
    printf("/* Office Tools */\n");
    printf("\n");
    printf("  Note! All csv related commands assume ; as delimiter!\n");
    printf("\n");
    printf("  cmath    : Math interpreter that has interactive mode and macro execution.\n");
    printf("             To run existing macro, type 'cmath mymacro.m'.\n");
    printf("  csvclean : Basic .csv data cleaning method, type 'csvclean -help'.\n");
    printf("  csvplot  : ASCII x-y plotter for .csv files.\n");
    printf("  csv_print: Pretty prints a .csv file.\n");
    printf("  csvstat  : Calculate statistics from .csv file columns.\n");    
    printf("  mdread   : Pretty-prints .md files. Use: mdread readme.md\n");
    printf("  slides   : Terminal slideset editor, to start 'slides myslides.sld'.\n");
    printf("             For help CTRL+H when the app is running.\n");
    printf("  table    : Lightweight spreadsheet tool, open file 'table mytable.csv'.\n");
    printf("\n");
    printf("/* Internet and Communications */\n");
    printf("\n");
    printf("  ctalk    : IRC like messaging tool. Type ctalk for instructions.\n");    
    printf("  exchange : Retrieves exchange rates of common currencies to euro.\n");
    printf("  inet     : Interactive internet connection manager.\n");    
    printf("  rss      : Lightweight rss news app, tested only with yle rss feed.\n");    
    printf("\n");
    printf("/* Engineering */\n");
    printf("\n");
    printf("  spectrum : Waterfall spectrum app that uses ALSA driver.\n");
    printf("\n");
    printf("/* Other */\n");
    printf("\n");
    printf("  dungeon  : Role-Playing tool, type 'dungeon map.bmp' or 'dungeon map.dng'.\n");
    printf("  paint    : ASCII paint application to edit bitmaps.\n");    
    printf("  psfedit  : Font editor for .psf fonts used in linux TTY.\n");
    printf("  signal   : Signal generator. Type 'signal' for help.\n");
    printf("  skydial  : Simple sky-dial to identify and locate celestial objects.\n");
    printf("             Type 'skydial lat lon' to tell your position.\n");
    printf("  solar    : Visualizes the solar system and it's planets. 'solar X', where\n");
    printf("             X is the number of planets from 2 to 8.\n");
    printf("  time     : Display time now in different time-zones. 'time -s' for\n");     
    printf("             astronomical events.\n");    
    printf("\n");
    printf("-----------------------------------------------------------------------------\n"); 
    printf("                              - BUILT-IN GAMES -\n");
    printf("\n");
    printf("/* Built-In Games */\n");
    printf("\n");
    printf("  invaders : Space invaders clone tailored to terminal.\n");
    printf("  snake    : Snake clone, reminence from the good old Nokia days.\n");
    printf("  tictactoe: Classic tictactoe, but with bigger game area.\n");
    printf("\n");
    printf("-----------------------------------------------------------------------------\n"); 
    printf("                              - FOR DEVELOPERS -\n");
    printf("\n");
    printf("/* BUDOSTACK Developer Tools */\n");
    printf("\n");
    printf("  compile              : Build a standalone binary from a TASK script.\n"); 
    printf("                         Type 'compile -help'.\n");
    printf("  floppycheck          : 'floppycheck ./file or ./folder/' estimates how many\n");
    printf("                         standard floppy disks the given content will take.\n");
    printf("\n");
    printf("/* BUDOSTACK Example TASK Programs */\n");
    printf("\n");
    printf("  autoexec.task        : Determines what happens before BUDOSTACK login screen\n");
    printf("  csvdemo.task         : Dynamic .csv file manipulation demo\n");
    printf("  demo.task            : TASK language demo\n");
    printf("  screen.task          : Screen calibration TASK\n");
    printf("  release.task         : Used to generate release notes\n");
    printf("  waves.task           : Demonstration how to plot basic math\n");
    printf("\n");
    printf("=============================================================================\n");

    // Check for the "-a" argument to display the reserved section
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        printf("This is reserved for something.\n");
        printf("\n");
    }

    return 0;
}
