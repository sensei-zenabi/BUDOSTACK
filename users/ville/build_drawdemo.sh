gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 -c libdraw.c -o libdraw.o
gcc -O3 -march=native -ffast-math -fno-strict-aliasing -std=c11 drawdemo.c libdraw.o -lm -o drawdemo
sudo ./drawdemo

