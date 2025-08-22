#!/bin/sh
# Build and run drawdemo (includes libdraw.c directly)
gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 drawdemo.c -lm -o drawdemo
sudo ./drawdemo
