#define main original_graphics_test_main
#include "budowin_graphics.c"
#undef main
#include <sys/stat.h>

static void key_event(struct budo_gfx_host *host, int code, int key, unsigned int type)
{
    struct budo_gfx_event event = {.type = type, .scancode = code, .key = key};
    budo_gfx_host_event(host, &event);
}

static void key_press(struct budo_gfx_host *host, int code, int key, int control)
{
    if (control) key_event(host, 224, 0, BUDO_GFX_KEY_DOWN);
    key_event(host, code, key, BUDO_GFX_KEY_DOWN);
    key_event(host, code, key, BUDO_GFX_KEY_UP);
    if (control) key_event(host, 224, 0, BUDO_GFX_KEY_UP);
}

static int focus_at(const uint8_t *pixels, int w, int x, int y)
{
    const uint8_t *white = pixels + ((size_t)y * w + x) * 4;
    const uint8_t *black = pixels + ((size_t)(y + 1) * w + x + 1) * 4;
    const uint8_t *edge = pixels + ((size_t)y * w + x + 6) * 4;
    return white[0] == 255 && white[1] == 255 && white[2] == 255 &&
           black[0] == 0 && black[1] == 0 && black[2] == 0 &&
           edge[0] == 255 && edge[1] == 255 && edge[2] == 255;
}

static void save_frame(const char *path, const uint8_t *pixels, int w, int h)
{
    FILE *file = fopen(path, "wb");
    assert(file && fprintf(file, "P6\n%d %d\n255\n", w, h) > 0);
    for (int p = 0; p < w * h; ++p) assert(fwrite(pixels + p * 4, 1, 3, file) == 3);
    assert(fclose(file) == 0);
}

static void workflow(char **argv, const char *directory, const char *home, int scaled)
{
    socket_calls = accepted = 0;
    assert(setenv("BUDO_GFX_TEST_SOCKETPAIR", "1", 1) == 0);
    struct budo_gfx_host *host = test_host();
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        char fd[32];
        snprintf(fd, sizeof(fd), "%d", endpoints[1]);
        close(endpoints[0]);
        assert(chdir(directory) == 0 && setenv("HOME", home, 1) == 0);
        unsetenv("BUDOSTACK_BASE");
        assert(setenv("BUDO_TEST_FD", fd, 1) == 0 && setenv("LD_PRELOAD", argv[2], 1) == 0);
        execl(argv[1], argv[1], (char *)NULL);
        _exit(127);
    }
    close(endpoints[1]);
    int frames = 0, phase = 0, status = 0, done = 0, typed = 0, tabs = 0, arrows = 0;
    char output[4096], capture[4096];
    assert(snprintf(output, sizeof(output), "%s/keyboard-save.txt", directory) < (int)sizeof(output));
    assert(snprintf(capture, sizeof(capture), "%s/%s.ppm", directory, scaled ? "scaled-desktop" : "keyboard-settings") < (int)sizeof(capture));
    for (int iteration = 0; iteration < 20000; ++iteration) {
        budo_gfx_host_poll(host);
        if (budo_gfx_host_active(host)) {
            int w, h, dirty;
            const uint8_t *pixels = budo_gfx_host_pixels(host, &w, &h, &dirty);
            assert(w == (scaled ? 1280 : 640) && h == (scaled ? 960 : 480));
            if (dirty && ++frames % 5 == 0) {
                if (scaled) {
                    if (phase++ == 0) {
                        for (int y = 0; y < 20; y += 2)
                            for (int x = 0; x < 300; x += 2) {
                                const uint8_t *p = pixels + ((size_t)y * w + x) * 4;
                                assert(!memcmp(p, p + 4, 4) && !memcmp(p, p + w * 4, 4));
                            }
                        save_frame(capture, pixels, w, h);
                        key_press(host, 41, 27, 0);
                    }
                } else {
                    switch (phase) {
                        case 0: key_press(host, 74, 0, 0); ++phase; break; /* First launcher: Editor. */
                        case 1: key_press(host, 40, 13, 0); ++phase; break;
                        case 2: key_press(host, 18, 'o', 1); ++phase; break;
                        case 3: key_press(host, 4, 'a', 1); ++phase; break;
                        case 4: {
                            const char *text = "source.txt";
                            if (text[typed]) key_press(host, 0, text[typed++], 0);
                            else { typed = 0; ++phase; }
                            break;
                        }
                        case 5: key_press(host, 40, 13, 0); ++phase; break;
                        case 6: key_press(host, 4, 'a', 1); ++phase; break;
                        case 7: {
                            const char *text = "keyboard workflow";
                            if (text[typed]) key_press(host, 0, text[typed++], 0);
                            else { typed = 0; ++phase; }
                            break;
                        }
                        case 8: key_press(host, 43, 9, 1); ++phase; tabs = 0; break;
                        case 9:
                            if (focus_at(pixels, w, 97, 98)) { key_press(host, 40, 13, 0); ++phase; }
                            else { assert(++tabs < 25); key_press(host, 43, 9, 1); }
                            break;
                        case 10:
                            if (arrows++ < 4) key_press(host, 81, 0, 0);
                            else { arrows = 0; key_press(host, 40, 13, 0); ++phase; }
                            break;
                        case 11: key_press(host, 4, 'a', 1); ++phase; break;
                        case 12: {
                            const char *text = "keyboard-save.txt";
                            if (text[typed]) key_press(host, 0, text[typed++], 0);
                            else { typed = 0; ++phase; }
                            break;
                        }
                        case 13: key_press(host, 40, 13, 0); ++phase; tabs = 0; break;
                        case 14:
                            if (access(output, F_OK) == 0) { key_press(host, 43, 9, 1); ++phase; }
                            else assert(++tabs < 50);
                            break;
                        case 15:
                            if (focus_at(pixels, w, 554, 84)) { key_press(host, 40, 13, 0); ++phase; }
                            else { assert(++tabs < 75); key_press(host, 43, 9, 1); }
                            break;
                        case 16: key_press(host, 74, 0, 0); ++phase; break;
                        case 17:
                            if (arrows++ < 3) key_press(host, 79, 0, 0);
                            else { arrows = 0; key_press(host, 40, 13, 0); ++phase; tabs = 0; }
                            break;
                        case 18:
                            if (focus_at(pixels, w, 128, 184)) { key_press(host, 40, 13, 0); ++phase; }
                            else { assert(++tabs < 25); key_press(host, 43, 9, 0); }
                            break;
                        case 19:
                            save_frame(capture, pixels, w, h);
                            key_press(host, 43, 9, 0); ++phase; tabs = 0;
                            break;
                        case 20:
                            if (focus_at(pixels, w, 514, 76)) { key_press(host, 40, 13, 0); ++phase; }
                            else { assert(++tabs < 25); key_press(host, 43, 9, 0); }
                            break;
                        case 21: key_press(host, 41, 27, 0); ++phase; break;
                        case 22: key_press(host, 41, 27, 0); break;
                        default: break;
                    }
                }
            }
        }
        if (waitpid(child, &status, WNOHANG) == child) { done = 1; break; }
        test_pause();
    }
    if (!done) { fprintf(stderr, "Keyboard workflow stalled at phase %d\n", phase); kill(child, SIGKILL); waitpid(child, &status, 0); }
    assert(done && WIFEXITED(status) && !WEXITSTATUS(status));
    budo_gfx_host_close(host);
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    char directory[4096], home[4096], path[4096];
    assert(snprintf(directory, sizeof(directory), "%s/keyboard-workflow", argv[3]) < (int)sizeof(directory));
    assert(snprintf(home, sizeof(home), "%s/keyboard-home", argv[3]) < (int)sizeof(home));
    assert(mkdir(directory, 0700) == 0 && mkdir(home, 0700) == 0);
    assert(snprintf(path, sizeof(path), "%s/source.txt", directory) < (int)sizeof(path));
    FILE *file = fopen(path, "w");
    assert(file && fputs("original document", file) >= 0 && fclose(file) == 0);
    workflow(argv, directory, home, 0);
    assert(snprintf(path, sizeof(path), "%s/keyboard-save.txt", directory) < (int)sizeof(path));
    char text[64] = "";
    file = fopen(path, "r");
    assert(file && fread(text, 1, sizeof(text) - 1, file) == strlen("keyboard workflow") && fclose(file) == 0);
    assert(!strcmp(text, "keyboard workflow"));
    assert(snprintf(path, sizeof(path), "%s/source.txt", directory) < (int)sizeof(path));
    memset(text, 0, sizeof(text));
    file = fopen(path, "r");
    assert(file && fread(text, 1, sizeof(text) - 1, file) == strlen("original document") && fclose(file) == 0);
    assert(!strcmp(text, "original document"));
    assert(snprintf(path, sizeof(path), "%s/.budowin/keyboard.state", home) < (int)sizeof(path));
    file = fopen(path, "r");
    assert(file && fgetc(file) == '0' && fclose(file) == 0);
    assert(snprintf(path, sizeof(path), "%s/.budowin/display.state", home) < (int)sizeof(path));
    file = fopen(path, "w");
    assert(file && fputs("2 2\n", file) >= 0 && fclose(file) == 0);
    workflow(argv, directory, home, 1);
    puts("PASS: actual keyboard-only Open/Edit/Save As/Close and Settings, persistent 200% desktop over production graphics transport");
    return 0;
}
