#ifndef BUDOWIN_PLATFORM_H
#define BUDOWIN_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#define FA_RDONLY 1
#define FA_HIDDEN 2
#define FA_SYSTEM 4
#define FA_LABEL 8
#define FA_DIREC 16
#define FA_ARCH 32
/* Stateless iteration avoids leaking a directory handle on early exits. */
struct ffblk {
    char ff_name[256];
    unsigned int ff_attrib;
    char directory[4096];
    char pattern[256];
    char previous[256];
    unsigned int attributes;
};
int findfirst(const char *pattern, struct ffblk *entry, int attributes);
int findnext(struct ffblk *entry);
int kbhit(void);
int getch(void);
int bw_initialize(int argc, char **argv);
int bw_screen_open(void);
void bw_screen_close(void);
void bw_palette_entry(unsigned int index, unsigned int r, unsigned int g, unsigned int b);
int bw_screen_copy(unsigned long offset, const unsigned char *data, size_t length,
                   const uint32_t *background, const unsigned char *mask);
void bw_mouse_state(int *x, int *y, int *buttons);
unsigned int bw_modifiers(void);
clock_t bw_clock(void);
int bw_begin_frame(void);
int bw_end_frame(void);
void bw_screen_flush(void);
const char *bw_state_file(const char *name);
const char *bw_user_directory(void);
int bw_user_path(char *out, size_t size, const char *path);
int bw_request_launch(const char *command, const char *directory, int shell);
int bw_command_needs_handoff(const char *command);
void bw_finish(void);
void bw_restore_directory(const char *path);
#endif
