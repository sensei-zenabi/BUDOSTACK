#!/bin/sh
# Change directory to the parent of the script directory
cd "$(dirname "$0")/.." || exit
# Execute the budostack executable located in the parent folder
./budostack -f
