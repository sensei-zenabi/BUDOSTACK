#define _XOPEN_SOURCE 700
#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include "../budo/BUDOWIN/src/platform.c"
#include <assert.h>
#include <sys/socket.h>

/* Substitute endpoint establishment only; each window still owns a real PTY,
 * child process, graphics host, callback dispatcher and module data segment. */
static int peers[128], peer_count;
static int instance_socket(int domain, int type, int protocol)
{
    int pair[2];
    assert(socketpair(domain, type, protocol, pair) == 0);
    assert(peer_count < 128);
    peers[peer_count++] = pair[1];
    return pair[0];
}
static int instance_bind(int fd, const struct sockaddr *address, socklen_t length)
{
    (void)fd; (void)address; (void)length;
    return 0;
}
static int instance_listen(int fd, int backlog)
{
    (void)fd; (void)backlog;
    return 0;
}
static int instance_accept(int fd, struct sockaddr *address, socklen_t *length)
{
    (void)fd; (void)address; (void)length;
    errno = EAGAIN;
    return -1;
}
#define socket instance_socket
#define bind instance_bind
#define listen instance_listen
#define accept instance_accept
#include "../lib/budo_gfx.c"
#undef socket
#undef bind
#undef listen
#undef accept

static void editors(void)
{
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    int first = active_window;
    editor_insert_char('A');
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    int second = active_window;
    assert(first != second && !editor_lines[0][0] && !editor_undo_count);
    editor_insert_char('B');
    builtin_select(first);
    assert(!strcmp(editor_lines[0], "A"));
    editor_history_restore(0);
    assert(!editor_lines[0][0]);
    builtin_select(second);
    assert(!strcmp(editor_lines[0], "B"));
    active_window = second;
    close_editor();
    assert(confirm_owner == second && confirm_kind == BUDO_CONFIRM_SAVE);
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    assert(editor_window.open && !strcmp(editor_lines[0], "B"));
    close_editor();
    desktop_confirm_result(BUDO_RESPONSE_DISCARD);
    assert(!builtin_window(second)->open && builtin_window(first)->open);
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    assert(active_window == second && !editor_lines[0][0] && !editor_undo_count);
    /* Exit checks every document, including a modified non-active instance. */
    builtin_select(first);
    editor_insert_char('X');
    builtin_select(second);
    active_window = second;
    assert(!desktop_request_exit() && confirm_owner == first);
    desktop_confirm_result(BUDO_RESPONSE_DISCARD);
    builtin_select(second);
    /* Hitting a window uses the same stacking order as rendering. */
    editor_window.x = 140;
    editor_window.y = 100;
    active_window = second;
    draw_desktop(0);
    assert(desktop_point_owner(160, 130) == second);
    window_minimize_state(&editor_window);
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    assert(active_window != second && builtin_window(second)->minimized);
    int third = active_window;
    desktop_switch_app(1);
    assert(active_window != third);
    builtin_select(second);
    active_window = second;
    window_open_state(&editor_window);
    assert(!editor_lines[0][0]);
    for (int slot = 0; slot < BUILTIN_INSTANCE_MAX; ++slot) {
        if (!editor_instances[slot]) continue;
        builtin_select(2 + slot * 3);
        editor_close_now();
    }
}

static void explorers(const char *directory)
{
    assert(builtin_new_instance(BWA_HOST_APP_EXPLORER));
    int first = active_window;
    assert(load_directory(directory));
    char original[MAX_PATH];
    assert(copy_text(original, sizeof(original), current_path));
    explorer_state->v_page = 4;
    explorer_select_item(0, 0);
    assert(builtin_new_instance(BWA_HOST_APP_EXPLORER));
    int second = active_window;
    assert(second != first && explorer_state->v_page == 0 && explorer_selected_item == -1);
    assert(load_directory("/"));
    builtin_select(first);
    assert(!strcmp(current_path, original) && explorer_state->v_page == 4 && explorer_selected_item == 0);
    builtin_select(second);
    assert(!strcmp(current_path, "/") && explorer_select.selected != explorer_instances[0]->v_explorer_select.selected);
    close_explorer();
    assert(builtin_window(first)->open);
}

static BwaLoadedApp *native_open(const char *id)
{
    BwaLoadedApp *program = bwa_find_external_app_id(id);
    assert(program);
    BwaLoadedApp *app = bwa_new_instance(program);
    assert(app);
    bwa_callback_app = app;
    assert(app->definition.callbacks.open());
    app->open = 1;
    active_window = app->definition.runtime_id;
    bwa_callback_app = NULL;
    return app;
}

static void native_programs(void)
{
    BwaLoadedApp *first = native_open("paint");
    BwaLoadedApp *second = native_open("paint");
    assert(first != second && first->handle != second->handle);
    assert(first->definition.callbacks.draw != second->definition.callbacks.draw);
    assert(first->definition.runtime_id != second->definition.runtime_id);
    assert(bwa_external_app_count == 8 && desktop_item_visible(2));
    int x = first->window.x + 180, y = first->window.y + 130;
    bwa_callback_app = first;
    assert(first->definition.callbacks.mouse_down(x, y, 1));
    assert(first->definition.callbacks.mouse_up(x, y, 0));
    assert(!bwa_window_close() && confirm_owner == first->definition.runtime_id);
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    bwa_callback_app = second;
    assert(bwa_window_close()); /* The untouched second canvas remains clean. */
    assert(first->open && !second->open);
    BwaLoadedApp *replacement = native_open("paint");
    bwa_callback_app = replacement;
    assert(bwa_window_close());
    bwa_callback_app = first;
    assert(!bwa_window_close());
    desktop_confirm_result(BUDO_RESPONSE_DISCARD);
    assert(!first->open);
    BwaLoadedApp *settings1 = native_open("settings");
    BwaLoadedApp *settings2 = native_open("settings");
    assert(settings1->handle != settings2->handle);
    bwa_callback_app = settings1;
    assert(bwa_window_close() && settings2->open);
    bwa_callback_app = settings2;
    assert(bwa_window_close());
    bwa_callback_app = NULL;
}

static int screen_contains(const char *needle)
{
    for (int row = 0; row < session_rows; ++row) {
        char line[SESSION_COLS + 1];
        for (int col = 0; col < session_cols; ++col) line[col] = session_cells[row][col].ch;
        line[session_cols] = 0;
        if (strstr(line, needle)) return 1;
    }
    return 0;
}

static void terminals(const char *directory)
{
    assert(builtin_new_instance(BWA_HOST_APP_TERMINAL));
    int first = active_window, first_fd = session_fd;
    pid_t first_pid = session_pid;
    assert(first_fd >= 0 && first_pid > 0);
    session_write("echo INSTANCE-FIRST\r", 20);
    assert(builtin_new_instance(BWA_HOST_APP_TERMINAL));
    int second = active_window;
    assert(first != second && session_fd >= 0 && session_fd != first_fd && session_pid != first_pid);
    session_write("echo INSTANCE-SECOND\r", 21);
    for (int i = 0; i < 5000; ++i) {
        builtin_poll_terminals();
        builtin_select(first);
        int one = screen_contains("INSTANCE-FIRST");
        builtin_select(second);
        int two = screen_contains("INSTANCE-SECOND");
        if (one && two) break;
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    builtin_select(first);
    assert(screen_contains("INSTANCE-FIRST") && !screen_contains("INSTANCE-SECOND"));
    session_byte('\033'); /* A partial ANSI decoder is private too. */
    assert(session_state != 0);
    builtin_select(second);
    assert(screen_contains("INSTANCE-SECOND") && !screen_contains("INSTANCE-FIRST") && !session_state);
    active_window = second;
    close_terminal();
    builtin_select(first);
    assert(session_pid == first_pid && fcntl(session_fd, F_GETFD) >= 0);
    active_window = first;
    close_terminal();
    /* An executable gets a PTY/window without replacing the desktop. */
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), directory, "concurrent-program"));
    FILE *file = fopen(path, "w");
    assert(file && fputs("#!/bin/sh\nprintf 'DIRECT-PROGRAM\\n'\nsleep 30\n", file) >= 0 && fclose(file) == 0);
    assert(chmod(path, 0700) == 0);
    assert(desktop_launch_program(path, directory));
    int program1 = active_window;
    assert(!strcmp(terminal_cwd, directory) && !strcmp(terminal_title, "concurrent-program"));
    assert(desktop_launch_program(path, directory));
    int program2 = active_window;
    assert(program1 != program2 && !launch_command[0]);
    builtin_select(program1);
    assert(session_fd >= 0);
    active_window = program1;
    close_terminal();
    builtin_select(program2);
    assert(session_fd >= 0 && terminal_window.open);
    active_window = program2;
    close_terminal();
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    assert(join_path(home_path, sizeof(home_path), argv[1], "budo/BUDOWIN"));
    char *initialize_args[] = {argv[0], home_path, NULL};
    assert(bw_initialize(2, initialize_args));
    assert(setenv("BUDOSTACK_BASE", argv[1], 1) == 0);
    set_classic_gui_palette();
    bwa_load_external_apps();
    assert(bwa_external_app_count == 8);
    editors();
    explorers(argv[2]);
    native_programs();
    terminals(argv[2]);
    for (int slot = 0; slot < BUILTIN_INSTANCE_MAX; ++slot) {
        if (editor_instances[slot]) {
            builtin_select(2 + slot * 3);
            editor_history_reset();
            if (slot) free(editor_instances[slot]);
        }
        if (slot) { free(explorer_instances[slot]); free(terminal_instances[slot]); }
    }
    for (int i = 0; i < bwa_instance_count; ++i) if (bwa_instances[i].handle) dlclose(bwa_instances[i].handle);
    for (int i = 0; i < bwa_external_app_count; ++i) dlclose(bwa_external_apps[i].handle);
    for (int i = 0; i < peer_count; ++i) close(peers[i]);
    free(editor_clipboard);
    puts("PASS: independent Editor/Explorer/Paint/Settings/PTY and executable instances, dialogs, focus, reuse and cleanup");
    return 0;
}
