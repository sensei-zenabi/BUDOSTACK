#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "platform.h"
#include "../../../lib/budo_gfx.h"
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static struct budo_gfx *screen;
static uint32_t pixels[640 * 480];
static uint32_t palette[256];
static int mouse_x = 320, mouse_y = 240, mouse_buttons;
static int raw_mouse_x, raw_mouse_y, raw_mouse_valid;
static unsigned char keys[512];
static int queue[256];
static unsigned int queue_modifiers[256], key_modifiers;
static unsigned int queue_read, queue_write;
static int disconnected;
static int (*event_filter)(const struct budo_gfx_event *);
void bw_set_event_filter(int (*filter)(const struct budo_gfx_event *)) { event_filter = filter; }
static int caps_lock, text_key_handled;
static int nordic_keyboard = 1;
static char user_directory[4096], state_directory[4096];
static char own_executable[4096];
static char launch_command[4096], launch_directory[4096];
static int launch_shell;

int findnext(struct ffblk *entry) {
    DIR *directory = opendir(entry->directory);
    struct dirent *item;
    char chosen[256] = "";
    unsigned int attrib = 0;
    if (!directory) return -1;
    while ((item = readdir(directory)) != NULL) {
        char path[4096];
        struct stat st;
        int written;
        if (strlen(item->d_name) >= sizeof(chosen) ||
            strcmp(item->d_name, entry->previous) <= 0 ||
            (chosen[0] && strcmp(item->d_name, chosen) >= 0)) continue;
        if (strcmp(entry->pattern, "*.*") != 0 &&
            fnmatch(entry->pattern, item->d_name, 0) != 0) continue;
        written = snprintf(path, sizeof(path), "%s/%s", entry->directory, item->d_name);
        if (written < 0 || (size_t)written >= sizeof(path) || (stat(path, &st) != 0 && lstat(path, &st) != 0)) continue;
        if (S_ISDIR(st.st_mode) && !(entry->attributes & FA_DIREC)) continue;
        strcpy(chosen, item->d_name);
        attrib = S_ISDIR(st.st_mode) ? FA_DIREC : FA_ARCH;
    }
    closedir(directory);
    if (!chosen[0]) return -1;
    strcpy(entry->ff_name, chosen);
    strcpy(entry->previous, chosen);
    entry->ff_attrib = attrib;
    return 0;
}
int findfirst(const char *pattern, struct ffblk *entry, int attributes) {
    const char *slash = strrchr(pattern, '/');
    size_t length = slash ? (size_t)(slash - pattern) : 1;
    memset(entry, 0, sizeof(*entry));
    if (length >= sizeof(entry->directory) ||
        strlen(slash ? slash + 1 : pattern) >= sizeof(entry->pattern)) return -1;
    if (slash) {
        memcpy(entry->directory, pattern, length);
        if (!length) strcpy(entry->directory, "/");
    } else strcpy(entry->directory, ".");
    strcpy(entry->pattern, slash ? slash + 1 : pattern);
    entry->attributes = (unsigned int)attributes;
    return findnext(entry);
}
static int copy_path(char *dest, size_t size, const char *source) {
    size_t length = strlen(source);
    if (length >= size) return 0;
    memcpy(dest, source, length + 1);
    return 1;
}
int bw_initialize(int argc, char **argv) {
    char executable[4096], assets[4096], path[8192];
    const char *base = getenv("BUDOSTACK_BASE");
    const char *home = getenv("HOME");
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0) {
        if (argc < 1 || !realpath(argv[0], executable)) {
            perror("BUDOWIN: executable path"); return 0;
        }
        length = (ssize_t)strlen(executable);
    }
    executable[length] = '\0';
    (void)copy_path(own_executable, sizeof(own_executable), executable);
    char *slash = strrchr(executable, '/');
    if (!slash) return 0;
    *slash = '\0';
    int written = snprintf(assets, sizeof(assets), "%s/BUDOWIN", executable);
    if (written < 0 || (size_t)written >= sizeof(assets)) return 0;
    if (!getcwd(user_directory, sizeof(user_directory))) return 0;
    if (base) {
        written = snprintf(path, sizeof(path), "%s/users/default", base);
        if (written > 0 && (size_t)written < sizeof(path) && access(path, R_OK | X_OK) == 0)
            (void)copy_path(user_directory, sizeof(user_directory), path);
        written = snprintf(path, sizeof(path), "%s/apps:%s/commands:%s/utilities:%s/budo:%s", base, base, base, base, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
        if (written < 0 || (size_t)written >= sizeof(path) || setenv("PATH", path, 1) != 0) return 0;
    }
    /* All writable state is user-owned, never stored beside installed sources. */
    written = snprintf(state_directory, sizeof(state_directory), "%s/.budowin", home ? home : user_directory);
    if (written < 0 || (size_t)written >= sizeof(state_directory)) return 0;
    if (mkdir(state_directory, 0700) != 0 && errno != EEXIST) { perror("BUDOWIN: state directory"); return 0; }
    if (chdir(assets) != 0) { perror("BUDOWIN: asset directory"); return 0; }
    return 1;
}
const char *bw_user_directory(void) { return user_directory; }
int bw_user_path(char *out, size_t size, const char *path) {
    if (path[0] == '/') return copy_path(out, size, path);
    int written = snprintf(out, size, "%s/%s", user_directory, path);
    return written >= 0 && (size_t)written < size;
}
const char *bw_state_file(const char *name) {
    static char paths[16][4096];
    static unsigned int next;
    char *path = paths[next++ % 16];
    int written = snprintf(path, 4096, "%s/%s", state_directory, name);
    if (written < 0 || written >= 4096) { fprintf(stderr, "BUDOWIN: state path too long\n"); return "/dev/null"; }
    return path;
}
int bw_screen_open(void) {
    disconnected = 0;
    if (budo_gfx_open(&screen, 640, 480, BUDO_GFX_ARGB8888) != 0) return 0;
    return budo_gfx_set_keyboard_grab(screen, 1) == 0;
}
int bw_play_tones(const BwaTone *tones, unsigned int count)
{
    if (!screen || count > BUDO_GFX_TONE_LIMIT || (count && !tones)) return 0;
    struct budo_gfx_tone notes[BUDO_GFX_TONE_LIMIT];
    for (unsigned int i = 0; i < count; ++i) {
        notes[i].frequency_hz = tones[i].frequency_hz;
        notes[i].duration_ms = tones[i].duration_ms;
    }
    return budo_gfx_play_tones(screen, notes, count) == 0;
}

int bw_clipboard_set(void *context, const char *text) {
    (void)context;
    if (!screen) { errno = ENOTCONN; return -1; }
    return budo_gfx_set_clipboard(screen, text);
}
char *bw_clipboard_get(void *context) {
    (void)context;
    if (!screen) { errno = ENOTCONN; return NULL; }
    return budo_gfx_get_clipboard(screen);
}
void bw_screen_close(void) { budo_gfx_close(screen); screen = NULL; }
void bw_palette_rgb(unsigned int index, unsigned int rgb) {
    if (index < 256) palette[index] = 0xff000000u | (rgb & 0xffffffu);
}
void bw_palette_entry(unsigned int index, unsigned int r, unsigned int g, unsigned int b) {
    if (index < 256) palette[index] = 0xff000000u | ((r * 255 / 63) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 63);
}
/* Composite exact PCX RGB with the independent indexed GUI palette. */
int bw_screen_copy(unsigned long offset, const unsigned char *data, size_t length,
                   const uint32_t *background, const unsigned char *mask) {
    size_t count = sizeof(pixels) / sizeof(pixels[0]);
    if (offset > count || length > count - offset) return 0;
    for (size_t i = 0; i < length; ++i) {
        pixels[offset + i] = mask && mask[i] ? background[i] : palette[data[i]];
    }
    return 1;
}
static void enqueue(int key) {
    if (queue_write - queue_read < 256) {
        queue[queue_write % 256] = key;
        queue_modifiers[queue_write % 256] = bw_modifiers();
        ++queue_write;
    }
}
unsigned int bw_modifiers(void) {
    return ((keys[225] || keys[229]) ? 3u : 0u) | ((keys[224] || keys[228]) ? 4u : 0u) |
        (keys[226] ? 8u : 0u);
}
unsigned int bw_key_modifiers(void) { return key_modifiers; }
int bw_get_keyboard_layout(void) { return nordic_keyboard; }
void bw_load_keyboard_layout(void) {
    FILE *file = fopen(bw_state_file("keyboard.state"), "r");
    int value;
    nordic_keyboard = 1;
    if (file) {
        if (fscanf(file, "%d", &value) == 1 && (value == 0 || value == 1)) nordic_keyboard = value;
        fclose(file);
    }
}
int bw_set_keyboard_layout(int nordic) {
    char path[4096], temporary[4096];
    if (nordic != 0 && nordic != 1) return 0;
    if (!copy_path(path, sizeof(path), bw_state_file("keyboard.state")) ||
        !copy_path(temporary, sizeof(temporary), bw_state_file("keyboard.tmp"))) return 0;
    FILE *file = fopen(temporary, "w");
    if (!file) { perror("Save keyboard layout"); return 0; }
    int ok = fprintf(file, "%d\n", nordic) > 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok || rename(temporary, path) != 0) {
        perror("Save keyboard layout");
        (void)remove(temporary);
        return 0;
    }
    nordic_keyboard = nordic;
    return 1;
}
static int unicode_key(unsigned int codepoint)
{
    if (codepoint >= 32 && codepoint < 127) return (int)codepoint;
    switch (codepoint) {
        case 0xe4: return 132;
        case 0xc4: return 142;
        case 0xf6: return 148;
        case 0xd6: return 153;
        case 0xe5: return 134;
        case 0xc5: return 143;
        case 0xe6: return 145;
        case 0xc6: return 146;
        case 0xf8: return 155;
        case 0xd8: return 157;
        default: return 0;
    }
}

static void translate_key(const struct budo_gfx_event *event) {
    int key = event->key;
    int code = event->scancode;
    int extended = 0;
    switch (code) {
        case 73: extended = 82; break; case 74: extended = 71; break;
        case 75: extended = 73; break; case 76: extended = 83; break;
        case 77: extended = 79; break; case 78: extended = 81; break;
        case 79: extended = 77; break; case 80: extended = 75; break;
        case 81: extended = 80; break; case 82: extended = 72; break;
        default: if (code >= 58 && code <= 67) extended = code - 58 + 59; break;
    }
    if (extended) {
        if (queue_write - queue_read <= 254) { enqueue(0); enqueue(extended); }
        return;
    }
    if (code >= 224 && code <= 231) return;
    if (code == 57) { if (!event->repeat) caps_lock = !caps_lock; return; }
    /* Physical SDL scancodes make the selected layout independent of the
     * host desktop layout. NORD uses the Finnish/Swedish key positions. */
    int shift_down = (bw_modifiers() & 3u) != 0;
    int altgr = keys[230] != 0;
    if (code >= 4 && code <= 29) key = 'a' + code - 4;
    if (code >= 30 && code <= 39) key = "1234567890"[code - 30];
    if (nordic_keyboard && (altgr || !(bw_modifiers() & 4u))) {
        if (altgr) {
            switch (code) {
                case 31: key = '@'; break;
                case 32: key = 156; break;
                case 33: key = '$'; break;
                case 36: key = '{'; break;
                case 37: key = '['; break;
                case 38: key = ']'; break;
                case 39: key = '}'; break;
                case 45: key = '\\'; break;
                case 100: key = '|'; break;
                default: return;
            }
            enqueue(key);
            return;
        }
        int upper = shift_down != (caps_lock != 0);
        if (code == 47 || code == 51 || code == 52) {
            key = code == 47 ? (upper ? 143 : 134) :
                  code == 51 ? (upper ? 153 : 148) : (upper ? 142 : 132);
            enqueue(key);
            return;
        }
        if (shift_down && code >= 30 && code <= 39) {
            enqueue((unsigned char)"!\"#\244%&/()="[code - 30]);
            return;
        }
        switch (code) {
            case 45: key = shift_down ? '?' : '+'; break;
            case 46: key = shift_down ? '`' : '\''; break;
            case 48: key = shift_down ? '^' : '~'; break;
            case 49: case 50: key = shift_down ? '*' : '\''; break;
            case 53: key = shift_down ? 171 : 245; break;
            case 54: key = shift_down ? ';' : ','; break;
            case 55: key = shift_down ? ':' : '.'; break;
            case 56: key = shift_down ? '_' : '-'; break;
            case 100: key = shift_down ? '>' : '<'; break;
            default: break;
        }
        if (code >= 45 && code <= 56 && code != 57) { enqueue(key); return; }
        if (code == 100) { enqueue(key); return; }
    } else if (code >= 45 && code <= 56) {
        key = "-=[]\\\\;'`,./"[code - 45];
    }
    /* Some hosts provide a Unicode key without a physical scancode. */
    if (!(bw_modifiers() & 4u) && (code == 0 || nordic_keyboard)) {
        int cell = unicode_key((unsigned int)event->key);
        if (cell < 128) cell = 0;
        if (cell) {
            enqueue(cell);
            return;
        }
    }
    if (code == 43) key = 9;
    if (code == 88) key = 13;
    if (code >= 89 && code <= 97) key = '1' + code - 89;
    if (code == 98) key = '0';
    if (key == 10) key = 13;
    if (key == 127) key = 8;
    if ((bw_modifiers() & 4u) && key >= 'a' && key <= 'z') key -= 'a' - 1;
    else if (((bw_modifiers() & 3u) != 0) != (caps_lock != 0) && key >= 'a' && key <= 'z') key -= 'a' - 'A';
    else if (bw_modifiers() & 3u) {
        const char *plain = "1234567890-=[]\\;',./`";
        const char *shift = "!@#$%^&*()_+{}|:\"<>?~";
        const char *found = key > 0 && key < 128 ? strchr(plain, key) : NULL;
        if (found) key = shift[found - plain];
    }
    if (key > 0 && key < 128) enqueue(key);
}
static int process_event(const struct budo_gfx_event *input) {
    struct budo_gfx_event event = *input;
    if (event.type == BUDO_GFX_QUIT) disconnected = 1;
    else if (event.type == BUDO_GFX_RESET) {
        if (event_filter) (void)event_filter(&event);
        memset(keys, 0, sizeof(keys)); mouse_buttons = 0;
        raw_mouse_valid = 0;
        text_key_handled = 0;
        queue_read = queue_write = 0;
    } else if (event.type == BUDO_GFX_KEY_DOWN || event.type == BUDO_GFX_KEY_UP) {
        if (event.scancode >= 0 && event.scancode < 512) keys[event.scancode] = event.type == BUDO_GFX_KEY_DOWN;
        int consumed = event_filter && event_filter(&event);
        if (event.type == BUDO_GFX_KEY_DOWN && !(event.scancode >= 224 && event.scancode <= 231)) {
            unsigned int before = queue_write;
            if (!consumed) translate_key(&event);
            text_key_handled = consumed || (queue_write != before && queue[before % 256] >= 32);
        }
    } else if (event.type == BUDO_GFX_TEXT_INPUT) {
        if (!event_filter || !event_filter(&event)) {
            int cell = unicode_key((unsigned int)event.key);
            if (!text_key_handled && cell && !(bw_modifiers() & 4u)) enqueue(cell);
        }
        text_key_handled = 0;
    } else if (event.type == BUDO_GFX_MOUSE_MOVE || event.type == BUDO_GFX_MOUSE_DOWN || event.type == BUDO_GFX_MOUSE_UP) {
        /* Apply movement to the already-clamped cursor, not the physical
         * pointer position. Outward movement at an edge is discarded, so
         * reversing direction moves the cursor immediately. */
        int64_t x = raw_mouse_valid ? (int64_t)mouse_x + event.x - raw_mouse_x : event.x;
        int64_t y = raw_mouse_valid ? (int64_t)mouse_y + event.y - raw_mouse_y : event.y;
        mouse_x = x < 0 ? 0 : x > 639 ? 639 : (int)x;
        mouse_y = y < 0 ? 0 : y > 479 ? 479 : (int)y;
        raw_mouse_x = event.x; raw_mouse_y = event.y;
        raw_mouse_valid = 1;
        int mask = event.button == 1 ? 1 : event.button == 3 ? 2 : 4;
        if (event.type == BUDO_GFX_MOUSE_DOWN) mouse_buttons |= mask;
        if (event.type == BUDO_GFX_MOUSE_UP) mouse_buttons &= ~mask;
        if (event.type != BUDO_GFX_MOUSE_MOVE) return 2;
    } else if (event.type == BUDO_GFX_WHEEL) {
        if (!event_filter || !event_filter(&event)) {
            enqueue(0);
            enqueue(event.y > 0 ? 201 : 202);
        }
    }
    return 1;
}
static int pump_one(void) {
    struct budo_gfx_event event;
    int result = budo_gfx_poll_event(screen, &event);
    if (result < 0) { disconnected = 1; return 0; }
    if (!result) return 0;
    return process_event(&event);
}
int bw_begin_frame(void) {
    /* Collapse stale motion promptly, but render every button transition.
     * A press and release must remain separate frames for the original UI. */
    for (unsigned int i = 0; i < 256; ++i) {
        int result = pump_one();
        if (!result || result == 2 || disconnected) break;
    }
    return !disconnected;
}
void bw_screen_flush(void) {
    if (screen && budo_gfx_present(screen, pixels, NULL) != 0) disconnected = 1;
}
int bw_end_frame(void) {
    struct timespec delay = {0, 16000000};
    if (budo_gfx_present(screen, pixels, NULL) != 0) return 0;
    nanosleep(&delay, NULL);
    return !disconnected;
}
void bw_mouse_state(int *x, int *y, int *buttons) { *x = mouse_x; *y = mouse_y; *buttons = mouse_buttons; }
int kbhit(void) { return queue_read != queue_write; }
int getch(void) {
    if (!kbhit()) return -1;
    key_modifiers = queue_modifiers[queue_read % 256];
    return queue[queue_read++ % 256];
}
clock_t bw_clock(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (clock_t)((double)now.tv_sec * CLOCKS_PER_SEC + (double)now.tv_nsec * CLOCKS_PER_SEC / 1000000000.0);
}
int bw_request_launch(const char *command, const char *directory, int shell) {
    if (!copy_path(launch_command, sizeof(launch_command), command) ||
        !copy_path(launch_directory, sizeof(launch_directory), directory)) return 0;
    launch_shell = shell;
    return 1;
}
int bw_command_needs_handoff(const char *command) {
    char name[256];
    size_t length = strcspn(command, " \t");
    const char *base = getenv("BUDOSTACK_BASE");
    if (!length || length >= sizeof(name)) return 0;
    memcpy(name, command, length); name[length] = '\0';
    if (strstr(name, "/budo/") || strncmp(name, "budo/", 5) == 0 ||
        strcmp(name, "budowin") == 0 || strcmp(name, "rocket") == 0 ||
        strcmp(name, "example") == 0) return 1;
    if (base) {
        const char *groups[] = {"apps", "games", "budo"};
        for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
            char path[4096];
            int written = snprintf(path, sizeof(path), "%s/%s/%s", base, groups[i], name);
            if (written > 0 && (size_t)written < sizeof(path) && access(path, X_OK) == 0) return 1;
        }
    }
    return 0;
}
void bw_finish(void) {
    if (!launch_command[0]) return;
    pid_t pid = fork();
    if (pid < 0) { perror("BUDOWIN: launch"); return; }
    if (pid == 0) {
        if (chdir(launch_directory) != 0) { perror("BUDOWIN: launch directory"); _exit(1); }
        if (launch_shell) execl("/bin/sh", "sh", "-c", launch_command, (char *)NULL);
        else execl(launch_command, launch_command, (char *)NULL);
        perror("BUDOWIN: executable"); _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    bw_restore_directory(user_directory);
    execl(own_executable, own_executable, (char *)NULL);
    perror("BUDOWIN: resume");
}

void bw_restore_directory(const char *path) {
    if (chdir(path) != 0) perror("BUDOWIN: restore directory");
}
