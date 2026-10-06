#ifndef BUDOWIN_TERMINAL_SESSION_H
#define BUDOWIN_TERMINAL_SESSION_H

/* A real PTY keeps BUDOSTACK's command parser, line editor and applications
 * intact. The child has its own graphics endpoint, hosted inside this window. */
#define SESSION_COLS 104
#define SESSION_ROWS 48

typedef struct TerminalCell {
    unsigned char ch, fg, bg;
} TerminalCell;

static TerminalCell session_cells[SESSION_ROWS][SESSION_COLS];
static TerminalCell session_alternate[SESSION_ROWS][SESSION_COLS];
static int session_fd = -1;
static pid_t session_pid;
static struct budo_gfx_host *session_gfx;
static int session_cols = 80, session_rows = 24;
static int session_x, session_y, session_saved_x, session_saved_y;
static int session_fg = 5, session_bg = 1, session_bold, session_inverse;
static int session_cursor_visible = 1, session_alternate_active;
static int session_state, session_params[16], session_param_count, session_private;
static unsigned int session_utf8, session_utf8_left;
static int session_scroll_top, session_scroll_bottom = 23;
static int session_wrap_pending;
static int session_gfx_buttons;
static void desktop_file_menu_draw(int app);

static TerminalCell session_blank(void)
{
    return (TerminalCell){' ', (unsigned char)session_fg, (unsigned char)session_bg};
}

static void session_clear(void)
{
    for (int y = 0; y < SESSION_ROWS; ++y)
        for (int x = 0; x < SESSION_COLS; ++x) session_cells[y][x] = session_blank();
    session_x = session_y = session_wrap_pending = 0;
}

static void session_write(const char *data, size_t size)
{
    while (size && session_fd >= 0) {
        ssize_t n = write(session_fd, data, size);
        if (n > 0) { data += n; size -= (size_t)n; }
        else if (n < 0 && errno == EINTR) continue;
        else { if (n < 0) perror("Terminal input"); break; }
    }
}

static void session_scroll_up(void)
{
    if (!session_alternate_active && session_scroll_top == 0) {
        char line[SESSION_COLS + 1];
        int length = session_cols;
        for (int x = 0; x < length; ++x) line[x] = (char)session_cells[0][x].ch;
        while (length > 0 && line[length - 1] == ' ') --length;
        line[length] = 0;
        terminal_add_line(line);
    }
    for (int y = session_scroll_top; y < session_scroll_bottom; ++y)
        memcpy(session_cells[y], session_cells[y + 1], sizeof(session_cells[y]));
    for (int x = 0; x < session_cols; ++x)
        session_cells[session_scroll_bottom][x] = session_blank();
}

static void session_linefeed(void)
{
    session_wrap_pending = 0;
    if (session_y == session_scroll_bottom) session_scroll_up();
    else if (session_y + 1 < session_rows) ++session_y;
}

static int session_value(int index, int default_value)
{
    return index < session_param_count && session_params[index] ? session_params[index] : default_value;
}

static unsigned char session_ansi_color(int color)
{
    static const unsigned char colors[16] = {1,9,10,13,6,12,11,5,2,9,10,8,7,12,11,4};
    return colors[color & 15];
}

#include "terminal_graphics.h"

static void session_csi(int command)
{
    int n = session_value(0, 1);
    session_wrap_pending = 0;
    switch (command) {
        case 'A': session_y -= n; break;
        case 'B': case 'e': session_y += n; break;
        case 'C': case 'a': session_x += n; break;
        case 'D': session_x -= n; break;
        case 'E': session_y += n; session_x = 0; break;
        case 'F': session_y -= n; session_x = 0; break;
        case 'G': case '`': session_x = n - 1; break;
        case 'd': session_y = n - 1; break;
        case 'H': case 'f': session_y = n - 1; session_x = session_value(1, 1) - 1; break;
        case 's': session_saved_x = session_x; session_saved_y = session_y; break;
        case 'u': session_x = session_saved_x; session_y = session_saved_y; break;
        case 'r':
            session_scroll_top = n - 1;
            session_scroll_bottom = session_value(1, session_rows) - 1;
            if (session_scroll_top < 0 || session_scroll_bottom >= session_rows || session_scroll_top >= session_scroll_bottom) {
                session_scroll_top = 0; session_scroll_bottom = session_rows - 1;
            }
            session_x = session_y = 0;
            break;
        case 'J': {
            int mode = session_params[0];
            for (int y = 0; y < session_rows; ++y)
                for (int x = 0; x < session_cols; ++x)
                    if (mode == 2 || mode == 3 ||
                        (mode == 0 && (y > session_y || (y == session_y && x >= session_x))) ||
                        (mode == 1 && (y < session_y || (y == session_y && x <= session_x))))
                        session_cells[y][x] = session_blank();
            if (mode == 3) terminal_line_count = 0;
            break;
        }
        case 'K':
            for (int x = 0; x < session_cols; ++x)
                if (session_params[0] == 2 || (session_params[0] == 0 && x >= session_x) ||
                    (session_params[0] == 1 && x <= session_x)) session_cells[session_y][x] = session_blank();
            break;
        case 'P':
            if (n > session_cols - session_x) n = session_cols - session_x;
            memmove(&session_cells[session_y][session_x], &session_cells[session_y][session_x + n],
                    (size_t)(session_cols - session_x - n) * sizeof(TerminalCell));
            for (int x = session_cols - n; x < session_cols; ++x) session_cells[session_y][x] = session_blank();
            break;
        case '@':
            if (n > session_cols - session_x) n = session_cols - session_x;
            memmove(&session_cells[session_y][session_x + n], &session_cells[session_y][session_x],
                    (size_t)(session_cols - session_x - n) * sizeof(TerminalCell));
            for (int x = session_x; x < session_x + n; ++x) session_cells[session_y][x] = session_blank();
            break;
        case 'X':
            for (int x = session_x; x < session_cols && x < session_x + n; ++x) session_cells[session_y][x] = session_blank();
            break;
        case 'S': while (n-- > 0) session_scroll_up(); break;
        case 'm':
            for (int i = 0; i < session_param_count; ++i) {
                int p = session_params[i];
                if (p == 0) { session_fg = 5; session_bg = 1; session_bold = session_inverse = 0; }
                else if (p == 1) session_bold = 1;
                else if (p == 22) session_bold = 0;
                else if (p == 7) session_inverse = 1;
                else if (p == 27) session_inverse = 0;
                else if (p == 39) session_fg = 5;
                else if (p == 49) session_bg = 1;
                else if (p >= 30 && p <= 37) session_fg = session_ansi_color(p - 30);
                else if (p >= 40 && p <= 47) session_bg = session_ansi_color(p - 40);
                else if (p >= 90 && p <= 97) session_fg = session_ansi_color(p - 90 + 8);
                else if (p >= 100 && p <= 107) session_bg = session_ansi_color(p - 100 + 8);
                else if ((p == 38 || p == 48) && i + 2 < session_param_count && session_params[i + 1] == 5) {
                    unsigned char color = session_ansi_color(session_params[i + 2]);
                    if (p == 38) session_fg = color; else session_bg = color;
                    i += 2;
                }
            }
            break;
        case 'h': case 'l':
            if (session_private) {
                for (int i = 0; i < session_param_count; ++i) {
                    if (session_params[i] == 25) session_cursor_visible = command == 'h';
                    if (session_params[i] == 1049 && (command == 'h') != session_alternate_active) {
                        if (command == 'h') {
                            memcpy(session_alternate, session_cells, sizeof(session_cells));
                            session_saved_x = session_x; session_saved_y = session_y;
                            session_clear();
                        } else {
                            memcpy(session_cells, session_alternate, sizeof(session_cells));
                            session_x = session_saved_x; session_y = session_saved_y;
                        }
                        session_alternate_active = command == 'h';
                    }
                }
            }
            break;
        case 'n':
            if (n == 6) {
                char answer[32];
                int length = snprintf(answer, sizeof(answer), "\033[%d;%dR", session_y + 1, session_x + 1);
                session_write(answer, (size_t)length);
            }
            break;
        default: break;
    }
    if (session_x < 0) session_x = 0;
    if (session_x >= session_cols) session_x = session_cols - 1;
    if (session_y < 0) session_y = 0;
    if (session_y >= session_rows) session_y = session_rows - 1;
}

static unsigned char session_glyph(unsigned int ch)
{
    for (int i = 128; i < 256; ++i)
        if (bw_text_unicode((unsigned char)i) == ch) return (unsigned char)i;
    switch (ch) {
        case 0x2500: return '-'; case 0x2502: return '|';
        case 0x250c: case 0x2510: case 0x2514: case 0x2518: return '+';
        default: return '?';
    }
}

static int session_caret(int *x, int *y)
{
    if (session_fd < 0) return -1;
    if (terminal_scroll || !session_cursor_visible || budo_gfx_host_active(session_gfx)) return 0;
    *x = terminal_window.x + 7 + session_x * 6;
    *y = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6 + session_y * 9;
    return 1;
}

static void session_character(unsigned int ch)
{
    if (session_wrap_pending) { session_x = 0; session_linefeed(); }
    unsigned char glyph = ch < 128 ? (unsigned char)ch : session_glyph(ch);
    if (!glyph) glyph = '?';
    int fg_color = session_bold && session_fg == 5 ? 4 : session_fg;
    session_cells[session_y][session_x] = (TerminalCell){glyph,
        (unsigned char)(session_inverse ? session_bg : fg_color),
        (unsigned char)(session_inverse ? fg_color : session_bg)};
    if (++session_x >= session_cols) { session_x = session_cols - 1; session_wrap_pending = 1; }
}

static void session_byte(unsigned char c)
{
    if (session_state == 3) {
        if (c == 7) {
            session_osc_finish();
            session_state = 0;
        } else if (c == 27) session_state = 4;
        else session_osc_append(c);
        return;
    }
    if (session_state == 4) {
        if (c == '\\') {
            session_osc_finish();
            session_state = 0;
        } else {
            session_osc_append(27);
            session_osc_append(c);
            session_state = 3;
        }
        return;
    }
    if (session_state == 5) { session_state = 0; return; } /* Charset designator. */
    if (session_state == 1) {
        session_state = 0;
        if (c == '[') { session_state = 2; memset(session_params,0,sizeof(session_params)); session_param_count = 1; session_private = 0; }
        else if (c == ']') {
            session_osc_size = 0;
            session_osc_failed = 0;
            session_state = 3;
        }
        else if (c == '(' || c == ')') session_state = 5;
        else if (c == '7') { session_saved_x = session_x; session_saved_y = session_y; }
        else if (c == '8') { session_x = session_saved_x; session_y = session_saved_y; }
        else if (c == 'D') session_linefeed();
        else if (c == 'E') { session_x = 0; session_linefeed(); }
        else if (c == 'c') session_clear();
        return;
    }
    if (session_state == 2) {
        if (c == '?') session_private = 1;
        else if (c >= '0' && c <= '9') {
            int *value = &session_params[session_param_count - 1];
            if (*value < 100000) *value = *value * 10 + c - '0';
        } else if (c == ';' && session_param_count < 16) ++session_param_count;
        else if (c >= 0x40 && c <= 0x7e) { session_csi(c); session_state = 0; }
        else if (c == 27) session_state = 1;
        return;
    }
    if (c == 27) { session_state = 1; session_utf8_left = 0; }
    else if (c == '\r') { session_x = 0; session_wrap_pending = 0; }
    else if (c == '\n') session_linefeed();
    else if (c == '\b') { if (session_x > 0) --session_x; session_wrap_pending = 0; }
    else if (c == '\t') { int stop = (session_x / 8 + 1) * 8; while (session_x < stop && !session_wrap_pending) session_character(' '); }
    else if (c >= 32 && c != 127) {
        if (session_utf8_left && (c & 0xc0) == 0x80) {
            session_utf8 = (session_utf8 << 6) | (c & 63);
            if (--session_utf8_left == 0) session_character(session_utf8);
        } else if (c >= 0xc2 && c <= 0xf4) {
            session_utf8_left = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
            session_utf8 = c & (session_utf8_left == 1 ? 31 : session_utf8_left == 2 ? 15 : 7);
        } else { session_utf8_left = 0; session_character(c); }
    }
}

static void session_resize(void)
{
    int cols = (terminal_window.w - 30) / 6;
    int rows = (terminal_window.h - WINDOW_TITLE_H - EDITOR_MENU_H - 12) / 9;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > SESSION_COLS) cols = SESSION_COLS;
    if (rows > SESSION_ROWS) rows = SESSION_ROWS;
    if (cols == session_cols && rows == session_rows) return;
    session_cols = cols; session_rows = rows;
    session_scroll_top = 0; session_scroll_bottom = rows - 1;
    if (session_x >= cols) session_x = cols - 1;
    if (session_y >= rows) session_y = rows - 1;
    struct winsize size = {.ws_row=(unsigned short)rows, .ws_col=(unsigned short)cols};
    if (session_fd >= 0 && ioctl(session_fd, TIOCSWINSZ, &size) != 0) perror("Terminal size");
}

static void session_stop(void)
{
    session_art_free();
    session_mouse_left = session_mouse_right = 0;
    session_overlay = 0;
    session_gfx_buttons = 0;
    if (session_fd >= 0) { close(session_fd); session_fd = -1; }
    if (session_pid > 0) {
        (void)kill(-session_pid, SIGHUP);
        (void)kill(-session_pid, SIGKILL);
        (void)kill(session_pid, SIGKILL);
        while (waitpid(session_pid, NULL, 0) < 0 && errno == EINTR) {}
        session_pid = 0;
    }
    if (session_gfx) { budo_gfx_host_close(session_gfx); session_gfx = NULL; }
}

static int session_start(void)
{
    if (session_fd >= 0) return 1;
    char executable[MAX_PATH], base[MAX_PATH], slave[MAX_PATH];
    char command_path[MAX_PATH * 6];
    const char *root = getenv("BUDOSTACK_BASE");
    if (root && *root) { if (!realpath(root,base)) return 0; }
    else {
        char relative[MAX_PATH];
        if (!join_path(relative,sizeof(relative),home_path,"../..") || !realpath(relative,base)) return 0;
    }
    if (!join_path(executable,sizeof(executable),base,"budostack") || access(executable,X_OK) != 0) return 0;
    int written = snprintf(command_path, sizeof(command_path), "%s/apps:%s/commands:%s/utilities:%s/games:%s/budo:%s",
        base, base, base, base, base, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
    if (written < 0 || (size_t)written >= sizeof(command_path)) return 0;
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0) { perror("Terminal PTY"); return 0; }
    if (grantpt(master) != 0 || unlockpt(master) != 0 || !ptsname(master) ||
        !copy_text(slave,sizeof(slave),ptsname(master))) {
        perror("Terminal PTY slave"); close(master); return 0;
    }
    if (budo_gfx_host_open(&session_gfx) != 0) { perror("Terminal graphics endpoint"); close(master); return 0; }
    session_pid = fork();
    if (session_pid < 0) { perror("Terminal fork"); close(master); session_stop(); return 0; }
    if (session_pid == 0) {
        if (setsid() < 0) _exit(126);
        int fd = open(slave,O_RDWR);
        if (fd < 0 || ioctl(fd,TIOCSCTTY,0) < 0 || dup2(fd,0) < 0 || dup2(fd,1) < 0 || dup2(fd,2) < 0) _exit(126);
        if (fd > 2) close(fd);
        close(master);
        if (chdir(base) != 0 || setenv("PATH",command_path,1) != 0 || setenv("TERM","xterm-256color",1) != 0 ||
            setenv("BUDOSTACK_BASE",base,1) != 0 || setenv("BUDOSTACK_TERM_ACTIVE","TRUE",1) != 0 || setenv("BUDOSTACK_EMBEDDED_TERMINAL","1",1) != 0 ||
            setenv("BUDOSTACK_GFX_SOCKET",budo_gfx_host_path(session_gfx),1) != 0) _exit(126);
        execl(executable,executable,(char *)NULL);
        perror("Start BUDOSTACK"); _exit(127);
    }
    session_fd = master;
    session_fg = 5; session_bg = 1; session_bold = session_inverse = session_state = 0;
    session_alternate_active = session_utf8_left = 0;
    session_cursor_visible = 1;
    terminal_line_count = terminal_scroll = 0;
    session_clear();
    session_cols = session_rows = 0;
    session_resize();
    return 1;
}

static int session_poll(void)
{
    if (session_fd < 0) return 0;
    session_resize();
    int changed = 0;
    unsigned char data[4096];
    for (int iteration = 0; iteration < 16; ++iteration) {
        ssize_t n = read(session_fd,data,sizeof(data));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i) session_byte(data[i]);
        changed = 1;
    }
    budo_gfx_host_poll(session_gfx);
    int w,h,dirty_frame;
    (void)budo_gfx_host_pixels(session_gfx,&w,&h,&dirty_frame);
    if (dirty_frame) changed = 1;
    if (waitpid(session_pid,NULL,WNOHANG) == session_pid) {
        session_pid = 0;
        session_stop();
        terminal_window.open = 0;
        if (active_window == APP_TERMINAL) active_window = APP_NONE;
        changed = 1;
    }
    return changed;
}

static int session_key(int key)
{
    if (session_fd < 0) return 0;
    if (key == 0) {
        int scan = getch();
        const char *sequence = NULL;
        switch (scan) {
            case 72: sequence = "\033[A"; break; case 80: sequence = "\033[B"; break;
            case 77: sequence = "\033[C"; break; case 75: sequence = "\033[D"; break;
            case 71: sequence = "\033[H"; break; case 79: sequence = "\033[F"; break;
            case 73: sequence = "\033[5~"; break; case 81: sequence = "\033[6~"; break;
            case 82: sequence = "\033[2~"; break; case 83: sequence = "\033[3~"; break;
            case 59: sequence = "\033OP"; break; case 60: sequence = "\033OQ"; break;
            case 61: sequence = "\033OR"; break; case 62: sequence = "\033OS"; break;
            case 63: sequence = "\033[15~"; break; case 64: sequence = "\033[17~"; break;
            case 65: sequence = "\033[18~"; break; case 66: sequence = "\033[19~"; break;
            case 67: sequence = "\033[20~"; break; case 68: sequence = "\033[21~"; break;
            case 201: case 202:
                terminal_scroll += scan == 201 ? 3 : -3;
                if (terminal_scroll < 0) terminal_scroll = 0;
                if (terminal_scroll > terminal_line_count) terminal_scroll = terminal_line_count;
                return 1;
            default: break;
        }
        if (sequence) session_write(sequence,strlen(sequence));
    } else if (key > 0 && key <= 255) {
        char raw[2] = {(char)(key == 8 ? 127 : key),0};
        char encoded[8];
        if (bw_text_encode(encoded,sizeof(encoded),raw)) session_write(encoded,strlen(encoded));
    }
    terminal_scroll = 0;
    return 1;
}

static void session_gfx_rect(int *x, int *y, int *w, int *h)
{
    *x = terminal_window.x + 7;
    *y = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6;
    *w = terminal_window.w - 30;
    *h = session_rows * 9;
    int source_w, source_h, dirty_frame;
    (void)budo_gfx_host_pixels(session_gfx,&source_w,&source_h,&dirty_frame);
    if (source_w <= 0 || source_h <= 0) return;
    if ((long long)*w * source_h > (long long)*h * source_w) {
        int width = *h * source_w / source_h;
        *x += (*w - width) / 2; *w = width;
    } else {
        int height = *w * source_h / source_w;
        *y += (*h - height) / 2; *h = height;
    }
}

static void session_reset_input(void)
{
    session_gfx_buttons = 0;
    if (session_gfx) {
        struct budo_gfx_event reset = {.type=BUDO_GFX_RESET};
        budo_gfx_host_event(session_gfx, &reset);
    }
}

static int session_forward_event(const struct budo_gfx_event *event)
{
    if (!session_gfx || !budo_gfx_host_active(session_gfx) || active_window != APP_TERMINAL ||
        terminal_window.minimized || confirm_kind || editor_file_dialog) return 0;
    struct budo_gfx_event input = *event;
    if (event->type == BUDO_GFX_KEY_DOWN || event->type == BUDO_GFX_KEY_UP) {
        if (event->scancode == 43 && (bw_modifiers() & 4u)) return 0;
        budo_gfx_host_event(session_gfx,&input);
        return 1;
    }
    if (event->type == BUDO_GFX_WHEEL) {
        int x,y,w,h;
        session_gfx_rect(&x,&y,&w,&h);
        if (point_in_rect(ui_pointer_x,ui_pointer_y,x,y,w,h)) {
            budo_gfx_host_event(session_gfx,&input);
            return 1;
        }
    }
    if (event->type == BUDO_GFX_RESET) budo_gfx_host_event(session_gfx,&input);
    return 0;
}

static int session_pointer(int x, int y, int buttons, int previous)
{
    if (session_fd >= 0 && active_window == APP_TERMINAL && !terminal_window.minimized &&
        !confirm_kind && !editor_file_dialog) session_art_mouse(x, y, buttons, previous);
    if (!session_gfx || !budo_gfx_host_active(session_gfx) || active_window != APP_TERMINAL ||
        terminal_window.minimized || confirm_kind || editor_file_dialog) return 0;
    int rx,ry,rw,rh,sw,sh,dirty_frame;
    session_gfx_rect(&rx,&ry,&rw,&rh);
    int inside = point_in_rect(x,y,rx,ry,rw,rh);
    if (!inside && !session_gfx_buttons) return 0;
    if (!session_gfx_buttons && buttons && buttons == previous) return 0;
    (void)budo_gfx_host_pixels(session_gfx,&sw,&sh,&dirty_frame);
    if (sw < 1 || sh < 1 || rw < 1 || rh < 1) return 0;
    if (x < rx) x = rx;
    if (x >= rx + rw) x = rx + rw - 1;
    if (y < ry) y = ry;
    if (y >= ry + rh) y = ry + rh - 1;
    struct budo_gfx_event input = {.type=BUDO_GFX_MOUSE_MOVE,.x=(x-rx)*sw/rw,.y=(y-ry)*sh/rh};
    budo_gfx_host_event(session_gfx,&input);
    for (int bit = 1; bit <= 2; bit <<= 1) {
        if ((buttons & bit) == (session_gfx_buttons & bit)) continue;
        input.type = buttons & bit ? BUDO_GFX_MOUSE_DOWN : BUDO_GFX_MOUSE_UP;
        input.button = bit == 1 ? 1 : 3;
        budo_gfx_host_event(session_gfx,&input);
    }
    session_gfx_buttons = buttons & 3;
    return 1;
}

static void session_draw(void)
{
    int x = terminal_window.x + 7;
    int y = terminal_window.y + WINDOW_TITLE_H + EDITOR_MENU_H + 6;
    if (budo_gfx_host_active(session_gfx)) {
        int rx,ry,rw,rh,sw,sh,dirty_frame;
        session_gfx_rect(&rx,&ry,&rw,&rh);
        const uint8_t *image = budo_gfx_host_pixels(session_gfx,&sw,&sh,&dirty_frame);
        if (image && sw > 0 && sh > 0)
            for (int py = 0; py < rh; ++py)
                for (int px = 0; px < rw; ++px) {
                    const uint8_t *rgb = image + ((py * sh / rh) * sw + px * sw / rw) * 4;
                    put_rgb_pixel(rx+px,ry+py,((uint32_t)rgb[0]<<16)|((uint32_t)rgb[1]<<8)|rgb[2]);
                }
    } else {
        for (int row = 0; row < session_rows; ++row) {
            int logical = row - terminal_scroll;
            if (logical < 0) {
                int index = terminal_line_count + logical;
                if (index >= 0) draw_text(x,y+row*9,terminal_lines[index],TERMINAL_TEXT_COLOR,session_cols);
                continue;
            }
            for (int col = 0; col < session_cols; ++col) {
                TerminalCell cell = session_cells[logical][col];
                fill_rect(x+col*6,y+row*9,6,9,cell.bg);
                char glyph[2] = {(char)cell.ch,0};
                draw_text(x+col*6,y+row*9,glyph,cell.fg,1);
            }
        }
        session_art_draw();
        desktop_scroll_draw(APP_TERMINAL, 0);
    }
    desktop_file_menu_draw(APP_TERMINAL);
}
#endif
