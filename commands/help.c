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
    printf("Programmed by:   Ville Suoranta\n");
    printf("Contact email:   budostack@gmail.com\n");
    printf("License:         GPLv2\n");
    printf("\n");
    printf("Note! AI has been used in the development of this software.\n");
    printf("\n");

    // Print available apps and commands
    printf("-----------------------------------------------------------------------------\n");
    printf("/* Generic Commands */\n");
    printf("\n");
    printf("  help     : Display this help message.\n");
    printf("  assist   : Interactive assistant that provides various utilities.\n");
    printf("  cls      : Clear terminal screen.\n");
    printf("  compile  : Universal C compile command. Type 'compile -help'.\n");
    printf("  copy     : Copy anything to anywhere, a file or complete folder.\n");
	printf("  crc32    : Calculate and/or verify CRC32 checksum of a file.\n");
    printf("  ctalk    : Simple UDP based LAN chat. Start with 'ctalk myname'.\n");
    printf("  display  : Display the contents of a file.\n");
    printf("  drives   : Lists all found drives.\n");
    printf("  edit     : Opens a basic file editor: edit <filename>.\n");
    printf("             Supported languages: C/C++, Markup\n");
    printf("  find     : Find anything.\n");
    printf("  git      : Git helper, type git -help.\n");
    printf("  inet     : Interactive internet connection manager.\n");
    printf("  list     : List contents of a directory (type 'list -help').\n");
    printf("  makedir  : Create a new directory.\n");
    printf("  move     : Moves anything. Can be used for renaming as well.\n");
    printf("  pack     : Pack anything, e.g. 'pack myfolder myfolder.zip'\n");
    printf("  remove   : Remove anything, whether it is a file or folder.\n");
    printf("  restart  : A command to re-compile and restart BUDOSTACK. Use with caution!\n");
    printf("             Use 'restart -f' to clean before building.\n");
    printf("  run      : Run any executable/shell script or execute linux terminal cmd.\n");
    printf("             e.g. 'run git status' or 'run ./myexecutable'\n");
    printf("  runtask  : Run a proprietary .task script until CTRL+c is pressed.\n");
    printf("             Type: runtask -help for more details.\n");
    printf("  stats    : Displays basic hardware stats.\n");
    printf("  time     : Display time now in different time-zones. 'time -s' for astro\n"); 
    printf("             events.\n");
    printf("  unpack   : Unpack what has been packed, e.g. 'unpack myfolder.zip'\n");
    printf("  update   : Create an empty file or update its modification time.\n");
    printf("  exit     : Exit BUDOSTACK.\n");
    printf("\n");    
    printf("/* Engineering and Science */\n");
    printf("\n");
    printf("  cmath    : Math interpreter that has interactive mode and macro execution.\n");
    printf("             To run existing macro, type 'cmath mymacro.m'.\n");
	printf("  csvclean : Basic .csv data cleaning method, type 'csvclean -help'.\n");
	printf("  csvstat  : Calculate statistics from .csv file columns.\n");
    printf("  skydial  : Simple sky-dial to identify and locate celestial objects.\n");
    printf("             Type 'skydial lat lon' to tell your position.\n");
    printf("  solar    : Visualizes the solar system and it's planets. 'solar X', where\n");
    printf("             X is the number of planets from 2 to 8.\n");
    printf("\n");
	printf("/* Electronics */\n");
	printf("\n");
    printf("  attenuator : attenuator -t [t|p] -d dB -z Z0\n");
    printf("  bwidth     : bw -l f1(Hz) -h f2(Hz)\n");
    printf("  capacitor  : capacitor -c capacitance(F) -f frequency(Hz)\n");
    printf("  cutoff     : cutoff [-f fc] [-r R] [-c C]\n");
    printf("  dbm        : dbm -p P_W | -d dBm -v value\n");
    printf("  decibel    : decibel -t [p|v] (-r ratio | -d decibels)\n");
    printf("  delay      : delay -l length_m -v VF(0 < VF ≤ 1)\n");
    printf("  filtd      : filtd -f fc -t [lp|hp] -s [E12|E24]\n");
    printf("  impedance  : impedance -r R(Ω) -l L(H) -c C(F) -f freq(Hz)\n");
    printf("  inductor   : inductor -l inductance(H) -f frequency(Hz)\n");                                              
    printf("  ohm        : ohm [-v voltage] [-i current] [-r resistance]\n");                                                                                                                     
    printf("  power      : power [-p power] [-v voltage] [-i current]\n");                                                                                                                   
    printf("  qfactor    : qfactor -r R(Ω) -l L(H) -c C(F)\n");                                                                                                                 
    printf("  resistor   : resistor -a R1 -b R2\n");                                                                                                                
    printf("  resonant   : resonant -l L(H) -c C(F)\n");                                                                                                                
    printf("  rms        : rms -t [p|P|r] -v value\n");
    printf("               -t p = Vpeak in, -t P = Vpp in, -t r = Vrms in\n");                                                                                                                     
    printf("  tau        : tau [-t tau] [-r R] [-c C]\n");                                                                                                                     
    printf("  tcnet      : tc_net -t [R|C] -n 'S:100,200;P:50,50;S:300'\n");                                                                                                                   
    printf("  voltdiv    : voltdiv -i Vin -o Vout -a R1 -b R2\n");
    printf("\n");
    printf("/* News and World */\n");
    printf("\n");
    printf("  exchange : Retrieves exchange rates of common currencies to euro.\n");
    printf("  rss      : Lightweight rss news app, tested only with yle rss feed.\n");
    printf("\n");
    printf("/* Office Tools */\n");
    printf("\n");
    printf("  csv_print: Pretty prints a .csv file.\n");
    printf("  mdread   : Pretty-prints .md files. Use: mdread readme.md\n");
    printf("  slides   : Terminal slideset editor, to start 'slides myslides.sld'.\n");
    printf("             For help CTRL+H when the app is running.\n");
    printf("  table    : Lightweight spreadsheet tool, open file 'table mytable.tbl'.\n");
       
    // Games list
    printf("\n");
    printf("-----------------------------------------------------------------------------\n"); 
    printf("/* List of Games */\n");
    printf("\n");
    printf("  invaders : A space invaders clone tailored to terminal.\n");
    printf("  snake    : A snake clone, reminence from the good old Nokia days.\n");
    printf("\n");
    printf("  > Go to games folder, and type \"run ./invaders\"\n");

    // Print node applications information
    printf("\n");
    printf("-----------------------------------------------------------------------------\n"); 
    printf("/* Running Node Applications */\n");
    printf("\n");
    printf("  All node apps are stored in the node/ folder and they can be ran only via\n");
    printf("  TASK scripting.\n");
    printf("\n");
    printf("  Different types of node applications:\n");
    printf("\n");
    printf("  server   : A switchboard server that uses route.rt to route client\n"); 
    printf("             application inputs and outputs.\n");
    printf("  <app>    : A client application that can have up to 5 inputs and 5 outputs.\n");
    printf("             Some apps might require that the server is running before\n");
    printf("             starting.\n");
    printf("  client.c : Client application template.\n");
    printf("\n");
    printf("  Tips:\n");
    printf("  Start BUDOSTACK faster: ./budostack -f | Start TASK: ./budostack mytask.task\n");
    printf("\n");
    printf("=============================================================================\n");

    // Check for the "-a" argument to display the reserved section
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        printf("This is reserved for something.\n");
        printf("\n");
    }

    return 0;
}
