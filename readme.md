All around Linux Terminal
==========================

Welcome to the All around Linux Terminal project – a simple Linux-like terminal emulator written in plain C (compiled with -std=c11). This project demonstrates modular design, real-time command execution with paging, and built-in support for features like TAB autocompletion.

Table of Contents
-----------------
- Introduction
- Features
- Installation & Build
- Usage Instructions
    - Starting the Terminal
    - Available Commands
    - Auto-Completion
- Task Scripting
- Design & Architecture
- Troubleshooting
- License

Introduction
------------
This project implements a Linux terminal emulator with a modular design:
- Command Parsing & Execution: Commands are parsed and dispatched using a dedicated parser (commandparser.c) and executed either with paging or in realtime.
- Autocompletion: The terminal supports TAB autocompletion for both commands and filenames (implemented in input.c).
- Extensible Commands: Executable commands are stored in the "commands/" directory. These include commands such as:
    - hello
    - help
    - list
    - display
    - copy
    - move
    - remove
    - update
    - makedir
    - rmdir
    - runtask  (executed in realtime mode)
- Built-In Commands: In addition to the commands from the folder, built-in commands like "cd" and "exit" are supported.
- Task Automation: A simple task script format is provided in mytask.task which demonstrates looping, condition checking, and basic I/O.

Features
--------
- Paging for Output: If command output exceeds one screen, the app paginates the results.
- Realtime Mode: Certain commands (e.g., runtask) are executed in realtime without paging.
- TAB Autocompletion: Automatically complete command names (for the first token) and file names (for subsequent tokens).
- Modular Design: Separation of concerns between input handling, command parsing, and execution.
- Standard C Implementation: Built with portability in mind using only C11 and standard libraries.

Installation & Build
----------------------
Prerequisites:
- GCC (with support for C11)
- POSIX-compliant environment (e.g., Linux or macOS)

Build Steps:
1. Clone the Repository:
   git clone https://github.com/sensei-zenabi/C.git
   cd C

2. Build the Project:
   Use the provided makefile to compile both the main terminal executable and the individual command executables:
   make all
   This will produce:
     - The main terminal executable named "terminal"
     - Command executables built from source files in the "commands/" directory

3. Cleaning Up:
   To remove compiled objects and executables, run:
   make clean

Usage Instructions
------------------
Starting the Terminal:
Run the main executable from the command line:
   ./terminal
On startup, you will see several initialization messages (simulated delays and system checks) and then a prompt showing your current working directory (e.g., /home/user$). Type "exit" to quit the terminal.

Available Commands:
Within the terminal, the following commands are available:

Commands from the "commands/" folder:
- hello: Print a greeting message.
- help: Display this help message.
- list: List contents of a directory (default is the current directory).
- display: Show the contents of a file.
- copy: Copy a file from a source to a destination.
- move: Rename or move a file.
- remove: Delete a file.
- update: Create an empty file or update its modification time.
- makedir: Create a new directory.
- rmdir: Remove an empty directory.
- runtask: Execute a task script in realtime mode.

Built-in commands (handled directly in the main program):
- cd: Change the current working directory.
- exit: Exit the terminal.

Auto-Completion:
- Command Name Completion: When typing the first word of a command, press TAB to auto-complete or list available commands.
- Filename Completion: When typing file or directory names (after the command), press TAB to auto-complete file paths based on the current directory.
The autocompletion functionality is implemented in input.c, which scans the available commands for the first token and filesystem entries for subsequent tokens.

Task Scripting
--------------
The file "mytask.task" provides an example of a simple task script:

VAR X
X = 0
10
X++
PRINT "IN THE LOOP..."
WAIT 1000
IF (X > 5) THEN
 BREAK
END_IF
GOTO 10
EXIT

This script demonstrates:
- Variable declaration and incrementation.
- Looping with a conditional break.
- A waiting/delay mechanism.
- Basic output using the PRINT command.

While the current terminal does not automatically interpret this script, it serves as an example for extending the terminal’s functionality to support task automation.

Design & Architecture
---------------------
- Modularity: The project separates command parsing (commandparser.c/.h), input handling (input.c/.h), and the main terminal loop (main.c).
- Separation of Concerns: Each component is responsible for a single aspect of the terminal’s operation.
- Extensibility: New commands can be added to the "commands/" directory and will be compiled into separate executables using the makefile.
- Minimal Dependencies: The implementation uses only standard libraries and POSIX APIs, ensuring cross-platform compatibility.

Troubleshooting
---------------
- Compilation Issues: Ensure you are using GCC with C11 support. Verify with "gcc --version".
- Terminal Display Problems: If paging or autocompletion do not work as expected, check that your terminal supports ANSI escape codes.
- Command Not Found: Make sure the commands in the "commands/" folder are properly compiled. Rebuild with "make all" if necessary.

License
-------
This project is open source and available under the MIT License. See the LICENSE file for more information.

For additional help or to report issues, please open an issue in the repository.

Happy coding!
