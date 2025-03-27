#!/bin/sh
# Change directory to the parent of the script directory
cd "$(dirname "$0")/.." || exit
# Execute the aalto executable located in the parent folder
./aalto -f
