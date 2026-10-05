#ifndef DHERO_RUNTIME_H
#define DHERO_RUNTIME_H
#include <stddef.h>
#include <stdio.h>
void dhero_mode(int mode);
void dhero_text(int row, int col, const char *text, unsigned char color);
void dhero_pixels_write(const void *source, size_t size, unsigned long address);
void dhero_pixels_read(unsigned long address, size_t size, void *dest);
void dhero_dac(int port, int value);
int dhero_getch(void);
void dhero_delay(unsigned int ms);
void dhero_tone(unsigned int frequency, unsigned int ms);
int dhero_printf(const char *format, ...);
int dhero_putchar(int value);
int dhero_fputs(const char *text, FILE *stream);
int dhero_run(int argc, char **argv);
int dhero_game_run(void);
int dhero_editor_run(void);
#endif
