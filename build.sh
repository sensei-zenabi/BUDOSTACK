#!/bin/bash

# Set the source files and output executable
SOURCES="main.c teach.c"
OUTPUT="terminal"

# Compile with gcc
echo "Compiling $SOURCES..."
gcc $SOURCES -o $OUTPUT -Wall -Wextra -pedantic -std=c11

# Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "Build successful! Run ./$OUTPUT to start."
else
    echo "Build failed!"
fi
