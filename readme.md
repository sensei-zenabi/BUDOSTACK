# Linux-like Terminal

This project implements a simple Linux-like terminal in C using the C11 standard. It allows you to execute commands from a custom `commands` directory.

## Features

- **Basic Command Parsing:** Supports splitting input into a command, parameters (non-option tokens), and options (tokens starting with `-`).
- **Modular Design:** Shared structures and functions are declared in `commandparser.h` for better maintainability.
- **Error Handling:** Robust error checking for dynamic memory allocation and system calls (fork, execvp, waitpid).
- **Build Process:** A provided Makefile compiles the main program and commands with enhanced warning flags.

## Files

- **main.c:** Contains the main loop and prompt logic.
- **commandparser.c / commandparser.h:** Contains command parsing and execution functions.
- **makefile:** Build instructions.
- **readme.md:** This file.

## Build Instructions

Ensure you have GCC installed, then run:
```bash
make
