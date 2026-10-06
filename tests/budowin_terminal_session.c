#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>
#include <sys/socket.h>

/* Only endpoint setup is substituted: PTYs, process execution, graphics
 * packets, shared memory, acknowledgements and input use production code. */
static int endpoints[2], socket_calls, accept_enabled, accepted;
static int test_socket(int domain, int type, int protocol)
{
    assert(domain == AF_UNIX && type == SOCK_SEQPACKET && protocol == 0);
    return dup(endpoints[socket_calls++ ? 1 : 0]);
}
static int test_bind(int fd, const struct sockaddr *address, socklen_t length)
{
    (void)fd; (void)address; (void)length;
    return 0;
}
static int test_listen(int fd, int backlog)
{
    (void)fd; (void)backlog;
    return 0;
}
static int test_connect(int fd, const struct sockaddr *address, socklen_t length)
{
    (void)fd; (void)address; (void)length;
    return 0;
}
static int test_accept(int fd, struct sockaddr *address, socklen_t *length)
{
    (void)fd; (void)address; (void)length;
    if (!accept_enabled || accepted++) { errno = EAGAIN; return -1; }
    return dup(endpoints[0]);
}
#define socket test_socket
#define bind test_bind
#define listen test_listen
#define connect test_connect
#define accept test_accept
#include "../lib/budo_gfx.c"
#undef socket
#undef bind
#undef listen
#undef connect
#undef accept

static void pause_ms(void)
{
    struct timespec delay = {0,1000000};
    nanosleep(&delay,NULL);
}

static int contains_screen(const char *needle)
{
    for (int row = 0; row < session_rows; ++row) {
        char text[SESSION_COLS + 1];
        for (int col = 0; col < session_cols; ++col) text[col] = (char)session_cells[row][col].ch;
        text[session_cols] = 0;
        if (strstr(text,needle)) return 1;
    }
    return 0;
}

static int contains(const char *needle)
{
    if (contains_screen(needle)) return 1;
    for (int i = 0; i < terminal_line_count; ++i)
        if (strstr(terminal_lines[i],needle)) return 1;
    return 0;
}

static void await_text(const char *needle)
{
    for (int i = 0; i < 10000; ++i) {
        session_poll();
        if (contains(needle)) return;
        pause_ms();
    }
    fprintf(stderr,"Missing embedded terminal output: %s\n",needle);
    for (int row = 0; row < session_rows; ++row) {
        for (int col = 0; col < session_cols; ++col) fputc(session_cells[row][col].ch,stderr);
        fputc('\n',stderr);
    }
    assert(0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char assets[MAX_PATH];
    assert(join_path(assets,sizeof(assets),argv[1],"budo/BUDOWIN"));
    assert(copy_text(home_path,sizeof(home_path),assets));
    assert(setenv("BUDOSTACK_BASE",argv[1],1) == 0);
    assert(socketpair(AF_UNIX,SOCK_SEQPACKET,0,endpoints) == 0);
    terminal_window.open = 1;
    active_window = APP_TERMINAL;
    assert(session_start());
    pid_t launcher = session_pid;
    await_text("BUDOSTACK OPERATING SYSTEM");
    assert(!contains("Start BUDOWIN?"));
    session_write("cmath\r",6);
    await_text("math>");
    session_write("123456+7\r",9);
    await_text("123463");
    assert(session_pid == launcher && terminal_window.open);
    session_write("exit\r",5);
    await_text("Goodbye.");
    for (int i = 0; i < 50; ++i) { session_poll(); pause_ms(); }
    session_write("echo PTY-RETURN-OK\r",19);
    await_text("PTY-RETURN-OK");
    /* Run the reported application and check its actual highlighted keyword. */
    for (int i = 0; i < 100; ++i) { session_poll(); pause_ms(); }
    char syntax_path[MAX_PATH], edit_command[MAX_PATH + 16];
    assert(join_path(syntax_path,sizeof(syntax_path),getenv("BUDOWIN_TEST_DIR"),"syntax.c"));
    FILE *syntax = fopen(syntax_path,"w");
    assert(syntax && fputs("int answer = 42; /* syntax */\n",syntax) >= 0 && fclose(syntax) == 0);
    int edit_length = snprintf(edit_command,sizeof(edit_command),"edit %s\r",syntax_path);
    assert(edit_length > 0 && (size_t)edit_length < sizeof(edit_command));
    session_write(edit_command,(size_t)edit_length);
    await_text("answer");
    int keyword_colored = 0;
    for (int row = 0; row < session_rows; ++row)
        for (int col = 0; col + 2 < session_cols; ++col) {
            TerminalCell cell = session_cells[row][col];
            if (cell.ch == 'i' && session_cells[row][col+1].ch == 'n' &&
                session_cells[row][col+2].ch == 't' && cell.fg == (SESSION_RGB | 0x00cccc)) {
                assert(cell.bg == 1);
                keyword_colored = 1;
            }
        }
    assert(keyword_colored);
    session_write("\021",1);
    for (int i = 0; i < 10000 && !contains_screen("$ "); ++i) { session_poll(); pause_ms(); }
    assert(contains_screen("$ ") && session_pid == launcher);
    struct winsize size;
    assert(ioctl(session_fd,TIOCGWINSZ,&size) == 0);
    assert(size.ws_col == session_cols && size.ws_row == session_rows);
    terminal_window.w -= 60;
    session_resize();
    assert(ioctl(session_fd,TIOCGWINSZ,&size) == 0 && size.ws_col == session_cols);
    session_stop();
    assert(waitpid(launcher,NULL,WNOHANG) == -1 && errno == ECHILD);
    close(endpoints[0]);
    close(endpoints[1]);

    /* Fragmented escape and UTF-8 sequences, cursor edits and alternate screen. */
    session_fg = 5;
    session_bg = 1;
    session_clear();
    const char *sample = "abc\033[2DXY\r\n\303\244\303\266\303\245";
    for (const unsigned char *p = (const unsigned char *)sample; *p; ++p) session_byte(*p);
    assert(session_cells[0][0].ch == 'a' && session_cells[0][1].ch == 'X' && session_cells[0][2].ch == 'Y');
    assert(session_cells[1][0].ch == 132 && session_cells[1][1].ch == 148 && session_cells[1][2].ch == 134);
    const char *alternate = "\033[?1049hother\033[?1049l";
    for (const unsigned char *p = (const unsigned char *)alternate; *p; ++p) session_byte(*p);
    assert(session_cells[0][1].ch == 'X' && session_cells[1][0].ch == 132);

    /* libedit emits RGB foreground sequences; their components are not SGR attributes. */
    session_clear();
    const char *colors = "\033[38;2;204;102;255mA\033[0mB\033[48;2;7;41;90mC\033[0mD\033[38;5;196mE";
    for (const unsigned char *p = (const unsigned char *)colors; *p; ++p) session_byte(*p);
    assert(session_cells[0][0].fg == (SESSION_RGB | 0xcc66ff));
    assert(session_cells[0][0].bg == 1 && !session_inverse && !session_bold);
    assert(session_cells[0][1].fg == 5 && session_cells[0][1].bg == 1);
    assert(session_cells[0][2].bg == (SESSION_RGB | 0x07295a));
    assert(session_cells[0][2].fg == 5);
    assert(session_cells[0][3].bg == 1);
    assert(session_cells[0][4].fg == (SESSION_RGB | 0xff0000));
    session_draw();
    int tx = terminal_window.x + 7;
    int ty = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6;
    assert(!rgb_mask[(ty + 8) * SCREEN_WIDTH + tx]); /* Foreground leaves background indexed. */
    const unsigned char *letter = glyph_for('A');
    for (int gy = 0; gy < 7; ++gy)
        for (int gx = 0; gx < 5; ++gx)
            if (letter[gy] & (0x10 >> gx))
                assert(rgb_framebuffer[(ty + gy) * SCREEN_WIDTH + tx + gx] == 0xffcc66ff);

    /* OSC graphics survive fragmented reads and both sequence terminators. */
    const char *art = "\033]777;pixel=rect;pixel_x=2;pixel_y=3;pixel_w=4;pixel_h=2;pixel_r=18;pixel_g=52;pixel_b=86;pixel_layer=2\a"
        "\033]777;pixel=render;pixel_layer=2\033\\";
    for (const unsigned char *p = (const unsigned char *)art; *p; ++p) session_byte(*p);
    assert(session_art_front[1][3 * SESSION_ART_W + 2] == 0xff123456);
    assert(session_art_front[1][3 * SESSION_ART_W + 6] == 0);
    const char *sprite = "\033]777;sprite=draw;sprite_x=1;sprite_y=1;sprite_w=1;sprite_h=1;sprite_data=/wAA/w==\a\033]777;pixel=render\a";
    for (const unsigned char *p = (const unsigned char *)sprite; *p; ++p) session_byte(*p);
    assert(session_art_front[0][SESSION_ART_W + 1] == 0xffff0000);
    session_art_free();

    /* A graphics application presents and receives input within the terminal. */
    socket_calls = accepted = 0;
    accept_enabled = 1;
    assert(socketpair(AF_UNIX,SOCK_SEQPACKET,0,endpoints) == 0);
    assert(budo_gfx_host_open(&session_gfx) == 0);
    assert(setenv("BUDOSTACK_GFX_SOCKET",budo_gfx_host_path(session_gfx),1) == 0);
    int signal_pipe[2];
    assert(pipe(signal_pipe) == 0);
    pid_t graphic = fork();
    assert(graphic >= 0);
    if (!graphic) {
        close(signal_pipe[0]);
        struct budo_gfx *gfx = NULL;
        uint32_t image[16 * 16];
        for (int i = 0; i < 256; ++i) image[i] = 0xff123456;
        assert(budo_gfx_open(&gfx,16,16,BUDO_GFX_ARGB8888) == 0);
        assert(budo_gfx_present(gfx,image,NULL) == 0);
        struct budo_gfx_event event;
        int received = 0;
        for (int i = 0; i < 10000; ++i) {
            if (budo_gfx_poll_event(gfx,&event) == 1 && event.type == BUDO_GFX_KEY_DOWN && event.key == 'x') { received = 1; break; }
            pause_ms();
        }
        assert(received && write(signal_pipe[1],"x",1) == 1);
        budo_gfx_close(gfx);
        _exit(0);
    }
    close(signal_pipe[1]);
    int ready = 0;
    for (int i = 0; i < 10000; ++i) {
        budo_gfx_host_poll(session_gfx);
        int w,h,dirty_frame;
        const uint8_t *rgb = budo_gfx_host_pixels(session_gfx,&w,&h,&dirty_frame);
        if (rgb && w == 16 && rgb[0] == 0x12 && rgb[1] == 0x34) { ready = 1; break; }
        pause_ms();
    }
    assert(ready);
    int rx,ry,rw,rh;
    session_gfx_rect(&rx,&ry,&rw,&rh);
    session_draw();
    assert(rgb_framebuffer[ry * SCREEN_WIDTH + rx] == 0xff123456);
    struct budo_gfx_event event = {.type=BUDO_GFX_KEY_DOWN,.key='x',.scancode=27};
    assert(session_forward_event(&event));
    char received;
    assert(read(signal_pipe[0],&received,1) == 1 && received == 'x');
    int status;
    assert(waitpid(graphic,&status,0) == graphic && WIFEXITED(status) && !WEXITSTATUS(status));
    close(signal_pipe[0]);
    session_stop();
    close(endpoints[0]);
    close(endpoints[1]);
    puts("PASS: real BUDOSTACK PTY, interactive cmath/edit with RGB syntax/return, resize, ANSI/Nordic output and embedded graphics/input");
    return 0;
}
