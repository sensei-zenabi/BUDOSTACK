#!/bin/bash

# Set the source file and output executable
SOURCE="main.c"
OUTPUT="terminal"

# Compile with gcc
echo "Compiling $SOURCE..."
gcc "$SOURCE" -o "$OUTPUT" -Wall -Wextra -pedantic -std=c11

# Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "Build successful! Run ./$OUTPUT to start."
else
    echo "Build failed!"
fi
