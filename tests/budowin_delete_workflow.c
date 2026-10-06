#define main original_graphics_test_main
#include "budowin_graphics.c"
#undef main
#include <sys/stat.h>

static void pointer_event(struct budo_gfx_host *host, unsigned int type, int button, int x, int y)
{
    struct budo_gfx_event event = {.type = type, .button = (unsigned int)button, .x = x, .y = y};
    budo_gfx_host_event(host, &event);
}

static void delete_workflow(char **argv, int view)
{
    char directory[4096], target[4096], home[4096], state[4096];
    assert(snprintf(directory, sizeof(directory), "%s/delete-workflow-%d", argv[3], view) < (int)sizeof(directory));
    assert(snprintf(target, sizeof(target), "%s/Test", directory) < (int)sizeof(target));
    assert(snprintf(home, sizeof(home), "%s/delete-home-%d", argv[3], view) < (int)sizeof(home));
    assert(mkdir(directory, 0700) == 0 && mkdir(target, 0700) == 0 && mkdir(home, 0700) == 0);
    assert(snprintf(state, sizeof(state), "%s/.budowin", home) < (int)sizeof(state));
    assert(mkdir(state, 0700) == 0);
    assert(snprintf(state, sizeof(state), "%s/.budowin/session.state", home) < (int)sizeof(state));
    FILE *file = fopen(state, "w");
    assert(file);
    fprintf(file, "%s\n0 0 1 0 0 60 60 520 340 60 60 520 340\n", directory);
    assert(fclose(file) == 0);
    assert(snprintf(state, sizeof(state), "%s/.budowin/explorer-view.state", home) < (int)sizeof(state));
    file = fopen(state, "w");
    assert(file && fprintf(file, "%d\n", view) > 0 && fclose(file) == 0);
    socket_calls = accepted = 0;
    assert(setenv("BUDO_GFX_TEST_SOCKETPAIR", "1", 1) == 0);
    /* Both children inherit this process's UID; no sudo or privilege change. */
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
    int frames = 0, status = 0, done = 0;
    for (int iteration = 0; iteration < 10000; ++iteration) {
        budo_gfx_host_poll(host);
        if (budo_gfx_host_active(host)) {
            int w, h, dirty;
            (void)budo_gfx_host_pixels(host, &w, &h, &dirty);
            if (dirty) {
                ++frames;
                /* (Up) is row zero; Test is row one in both views. */
                int row_y = view ? 132 : 116;
                if (frames == 5) pointer_event(host, BUDO_GFX_MOUSE_DOWN, 3, 180, row_y);
                if (frames == 8) pointer_event(host, BUDO_GFX_MOUSE_UP, 3, 180, row_y);
                if (frames == 11) pointer_event(host, BUDO_GFX_MOUSE_DOWN, 1, 200, row_y + 58);
                if (frames == 14) pointer_event(host, BUDO_GFX_MOUSE_UP, 1, 200, row_y + 58);
                if (frames == 17) pointer_event(host, BUDO_GFX_MOUSE_DOWN, 1, 360, 262);
                if (frames == 20) pointer_event(host, BUDO_GFX_MOUSE_UP, 1, 360, 262);
                if (frames == 25) {
                    struct budo_gfx_event quit = {.type = BUDO_GFX_QUIT};
                    budo_gfx_host_event(host, &quit);
                }
            }
        }
        if (waitpid(child, &status, WNOHANG) == child) {
            done = 1;
            break;
        }
        test_pause();
    }
    if (!done) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    assert(done && WIFEXITED(status) && !WEXITSTATUS(status));
    budo_gfx_host_close(host);
    int explorer_deleted = access(target, F_OK) != 0;
    if (explorer_deleted) assert(mkdir(target, 0700) == 0);
    pid_t command = fork();
    assert(command >= 0);
    if (!command) {
        execl(argv[4], argv[4], "-del", target, "-f", (char *)NULL);
        _exit(127);
    }
    assert(waitpid(command, &status, 0) == command && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(access(target, F_OK) != 0);
    fprintf(stderr, "Explorer view=%d deleted=%d; do -del deleted=1\n", view, explorer_deleted);
    assert(explorer_deleted);
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    for (int view = 0; view < 2; ++view) delete_workflow(argv, view);
    puts("PASS: actual right-click Delete/confirmation in both views matches do -del for the same folder and UID");
    return 0;
}
