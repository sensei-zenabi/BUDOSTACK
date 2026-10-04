#define _XOPEN_SOURCE 700
/* Compile with SDL2/OpenGL and root lib objects except budo_gfx.o.
 * argv[1] is the test-only socket preload, argv[2] the repository root.
 * Offscreen GL validates actual uploads and runs both unmodified demo binaries.
 */
#define main transportTestMain
#include "budo_gfx.c"
#undef main
#define main terminalMain
#include "../apps/terminal.c"
#undef main
#include "../budo/lib/budo_screen.h"

static void test_key(struct budo_gfx_host *host, SDL_Keycode key,
                      SDL_Scancode scan, int down) {
    SDL_Event event = {0};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = scan;
    terminal_gfx_input(host, &event);
}

static void test_demo(const char *root, const char *preload, const char *name, int crash) {
    struct budo_gfx_host *host = test_host();
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(endpoints[0]);
        close(host->listener);
        char descriptor[32];
        snprintf(descriptor, sizeof(descriptor), "%d", endpoints[1]);
        assert(setenv("BUDO_TEST_FD", descriptor, 1) == 0);
        assert(setenv("LD_PRELOAD", preload, 1) == 0);
        char path[4096];
        snprintf(path, sizeof(path), "%s/budo/%s", root, name);
        /* Verify assets do not depend on the launching CWD. */
        assert(chdir("..") == 0);
        execl(path, path, (char *)NULL);
        _exit(127);
    }
    close(endpoints[0]);
    close(endpoints[1]);
    int status = 0;
    int frames = 0;
    int stage = 0;
    int64_t started = gfx_milliseconds();
    int64_t changed = started;
    for (;;) {
        assert(gfx_milliseconds() - started < 10000);
        budo_gfx_host_poll(host);
        if (budo_gfx_host_active(host)) {
            int width;
            int height;
            int dirty;
            const uint8_t *pixels = budo_gfx_host_pixels(host, &width, &height, &dirty);
            assert(width == 640 && height == 480);
            if (dirty) {
                frames++;
                assert(terminal_upload_framebuffer(pixels, width, height, 1) == 0);
                assert(terminal_texture_width == width && terminal_texture_height == height);
                uint8_t *readback = malloc((size_t)width * height * 4u);
                assert(readback);
                terminal_bind_texture(terminal_gl_texture);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback);
                assert(glGetError() == GL_NO_ERROR);
                assert(memcmp(readback, pixels, (size_t)width * height * 4u) == 0);
                free(readback);
                terminal_bind_texture(0);
                const char *snapshot_dir = getenv("BUDO_TEST_SNAPSHOT_DIR");
                if (frames == 10 && snapshot_dir) {
                    char path[4096];
                    snprintf(path, sizeof(path), "%s/%s.ppm", snapshot_dir, name);
                    FILE *image = fopen(path, "wb");
                    assert(image);
                    fprintf(image, "P6\n%d %d\n255\n", width, height);
                    for (size_t p = 0; p < (size_t)width * height; p++) {
                        assert(fwrite(pixels + p * 4u, 1, 3, image) == 3);
                    }
                    fclose(image);
                }
            }
            int64_t now = gfx_milliseconds();
            if (stage == 0 && frames >= 10) {
                if (crash) {
                    assert(kill(child, SIGKILL) == 0);
                    stage = 9;
                } else if (strcmp(name, "example") == 0) {
                    test_key(host, SDLK_UP, SDL_SCANCODE_UP, 1);
                    test_key(host, SDLK_UP, SDL_SCANCODE_UP, 0);
                    test_key(host, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, 1);
                    stage = 9;
                } else {
                    test_key(host, SDLK_RETURN, SDL_SCANCODE_RETURN, 1);
                    test_key(host, SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
                    test_key(host, SDLK_UP, SDL_SCANCODE_UP, 1);
                    test_key(host, SDLK_SPACE, SDL_SCANCODE_SPACE, 1);
                    changed = now;
                    stage = 1;
                }
            } else if (stage == 1 && now - changed > 500) {
                test_key(host, SDLK_UP, SDL_SCANCODE_UP, 0);
                test_key(host, SDLK_SPACE, SDL_SCANCODE_SPACE, 0);
                test_key(host, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, 1);
                test_key(host, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, 0);
                changed = now;
                stage = 2;
            } else if (stage == 2 && now - changed > 100) {
                for (int i = 0; i < 2; i++) {
                    test_key(host, SDLK_DOWN, SDL_SCANCODE_DOWN, 1);
                    test_key(host, SDLK_DOWN, SDL_SCANCODE_DOWN, 0);
                }
                test_key(host, SDLK_RETURN, SDL_SCANCODE_RETURN, 1);
                stage = 9;
            }
        }
        if (waitpid(child, &status, WNOHANG) == child) {
            break;
        }
        test_pause();
    }
    assert(frames >= 10 && stage == 9);
    if (crash) {
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    } else {
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    budo_gfx_host_poll(host);
    assert(!budo_gfx_host_active(host));
    budo_gfx_host_close(host);
}

static void test_input_state(void) {
    struct budo_gfx_host *host = test_host();
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(endpoints[0]);
        close(host->listener);
        struct budo_screen screen;
        assert(budo_screen_open(&screen, 640, 360) == 0);
        int received = 0;
        while (received < 3) {
            SDL_Event event;
            if (!budo_screen_poll(&screen, &event)) {
                test_pause();
                continue;
            }
            if (received == 0) {
                assert(event.type == SDL_KEYDOWN && screen.keys[SDL_SCANCODE_UP] == 1);
            } else if (received == 1) {
                assert(event.type == SDL_MOUSEMOTION && screen.keys[SDL_SCANCODE_UP] == 0);
                assert(event.motion.x == 320 && event.motion.y == 180);
            } else {
                assert(event.type == SDL_KEYUP && screen.keys[SDL_SCANCODE_UP] == 0);
            }
            received++;
        }
        budo_screen_close(&screen);
        close(endpoints[1]);
        _exit(0);
    }
    close(endpoints[0]);
    close(endpoints[1]);
    for (int i = 0; !budo_gfx_host_active(host); i++) {
        assert(i < 2000);
        budo_gfx_host_poll(host);
        test_pause();
    }
    test_key(host, SDLK_UP, SDL_SCANCODE_UP, 1);
    SDL_Event event = {0};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    terminal_gfx_input(host, &event);
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 480;
    event.motion.y = 270;
    terminal_gfx_input(host, &event);
    test_key(host, SDLK_UP, SDL_SCANCODE_UP, 0);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    budo_gfx_host_poll(host);
    assert(!budo_gfx_host_active(host));
    budo_gfx_host_close(host);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    assert(setenv("BUDO_GFX_TEST_SOCKETPAIR", "1", 1) == 0);
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window *window = SDL_CreateWindow("terminal graphics test", 0, 0, 960, 540,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context);
    terminal_window_handle = window;
    assert(terminal_resize_render_targets(944, 792) == 0);
    uint8_t *text_pixels = terminal_framebuffer_pixels;
    int x;
    int y;
    int w;
    int h;
    struct terminal_buffer percentage_buffer = {0};
    terminal_handle_osc_777(&percentage_buffer, "term_size=80.5x75.25;term_offset=-1.5,2.5");
    assert(terminal_display_width == 80.5 && terminal_display_height == 75.25);
    assert(terminal_offset_x == -1.5 && terminal_offset_y == 2.5);
    terminal_handle_osc_777(&percentage_buffer, "term_size=nanx50;term_offset=0,inf");
    assert(terminal_display_width == 80.5 && terminal_display_height == 75.25);
    assert(terminal_offset_x == -1.5 && terminal_offset_y == 2.5);
    terminal_overlay_width = 1920;
    terminal_overlay_height = 1080;
    terminal_handle_osc_777(&percentage_buffer, "overlay_zoom=125;overlay_offset=-2.5,5");
    assert(terminal_overlay_zoom == 125);
    assert(terminal_overlay_offset_x == -2.5 && terminal_overlay_offset_y == 5);
    terminal_overlay_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == -267 && y == -81 && w == 2400 && h == 1350);
    terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == -524 && y == -77 && w == 2276 && h == 1280);
    terminal_handle_osc_777(&percentage_buffer, "overlay_zoom=nan;overlay_offset=0,inf");
    assert(terminal_overlay_zoom == 125);
    assert(terminal_overlay_offset_x == -2.5 && terminal_overlay_offset_y == 5);
    terminal_handle_osc_777(&percentage_buffer, "overlay_zoom=80;overlay_offset=0,0");
    terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == -88 && y == 103 && w == 1456 && h == 819);
    terminal_handle_osc_777(&percentage_buffer, "overlay_zoom=100;overlay_offset=0,0");
    terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == -270 && y == 0 && w == 1820 && h == 1024);
    /* Overlay settings leave the content geometry unchanged. */
    assert(terminal_display_width == 80.5 && terminal_display_height == 75.25);
    assert(terminal_offset_x == -1.5 && terminal_offset_y == 2.5);
    /* Reference layout affects terminal only; native overlay stays independent. */
    terminal_handle_osc_777(&percentage_buffer, "layout_aspect=5:4");
    assert(terminal_layout_aspect == 1.25);
    terminal_display_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == 396 && y == 160 && w == 1087 && h == 813);
    terminal_display_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == 106 && y == 152 && w == 1030 && h == 771);
    terminal_handle_osc_777(&percentage_buffer, "overlay_zoom=125;overlay_offset=-2.5,5");
    terminal_overlay_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == -267 && y == -81 && w == 2400 && h == 1350);
    terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == -524 && y == -77 && w == 2276 && h == 1280);
    terminal_handle_osc_777(&percentage_buffer, "layout_aspect=nan:4");
    terminal_handle_osc_777(&percentage_buffer, "layout_aspect=5:0");
    terminal_handle_osc_777(&percentage_buffer, "layout_aspect=100:1");
    assert(terminal_layout_aspect == 1.25);
    terminal_overlay_enabled = 0;
    terminal_display_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == 158 && y == 160 && w == 1546 && h == 813);
    terminal_overlay_enabled = 1;
    terminal_handle_osc_777(&percentage_buffer, "layout_aspect=screen");
    assert(terminal_layout_aspect == 0);
    terminal_overlay_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == -267 && y == -81 && w == 2400 && h == 1350);
    /* Native square and portrait overlays retain their own proportions. */
    terminal_overlay_width = 1000;
    terminal_overlay_height = 1000;
    terminal_handle_osc_777(&percentage_buffer, "overlay_zoom=100;overlay_offset=0,0");
    terminal_overlay_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == 420 && y == 0 && w == 1080 && h == 1080);
    terminal_overlay_width = 600;
    terminal_overlay_height = 1200;
    terminal_overlay_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == 384 && y == 0 && w == 512 && h == 1024);
    /* Overlay disabled: ignore configured size/offsets and fill 5:4 displays. */
    terminal_overlay_enabled = 0;
    terminal_display_width = 62.5;
    terminal_display_height = 58.59375;
    terminal_offset_x = -0.234375;
    terminal_offset_y = 2.63671875;
    terminal_gfx_display_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == 0 && y == 0 && w == 1280 && h == 1024);
    terminal_gfx_display_rect(960, 540, &x, &y, &w, &h);
    assert(x == 0 && y == 0 && w == 960 && h == 540);
    /* Overlay enabled: use the exact configured viewport without letterboxes. */
    terminal_overlay_enabled = 1;
    terminal_gfx_display_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == 237 && y == 239 && w == 800 && h == 600);
    terminal_display_width = 81.5104167;
    terminal_display_height = 76.8518519;
    terminal_offset_x = -0.15625;
    terminal_offset_y = 2.5;
    terminal_gfx_display_rect(1920, 1080, &x, &y, &w, &h);
    assert(x == 174 && y == 152 && w == 1565 && h == 830);
    terminal_gfx_display_rect(1280, 1024, &x, &y, &w, &h);
    assert(x == 116 && y == 144 && w == 1043 && h == 787);
    terminal_overlay_enabled = 0;
    test_input_state();
    test_demo(argv[2], argv[1], "example", 0);
    test_demo(argv[2], argv[1], "rocket", 0);
    test_demo(argv[2], argv[1], "example", 1);
    assert(terminal_framebuffer_pixels == text_pixels);
    assert(terminal_framebuffer_width == 944 && terminal_framebuffer_height == 792);
    assert(terminal_upload_framebuffer(text_pixels, 944, 792, 0) == 0);
    assert(terminal_texture_width == 944 && terminal_texture_height == 792);
    terminal_bind_texture(terminal_gl_texture);
    GLint filtering = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filtering);
    assert(filtering == GL_LINEAR);
    terminal_release_gl_resources();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("Actual demos: frames/input/assets/exit/crash, OpenGL uploads, scaling and text texture restoration passed.");
    return 0;
}
