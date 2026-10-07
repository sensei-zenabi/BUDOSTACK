static void document_io_reset_capture(void) { ui_captured = 0; }

static void document_io_draw(void)
{
    if (!document_io_active) return;
    int x = (SCREEN_WIDTH - 440) / 2, y = SCREEN_HEIGHT - 122;
    ui_region_count = 0;
    ui_draw_scope = ui_scope(); ui_draw_owner = UI_OVERLAY_OWNER;
    bwa_draw_standard_window(x, y, 440, 92, "Document operation", 0);
    draw_text_elided(x + 12, y + 30, document_io_name, TEXT_COLOR, 68);
    char progress[96];
    snprintf(progress, sizeof(progress), "%u KiB%s", document_io_bytes / 1024,
             atomic_load(&document_io_shared->cancel) ? " | Cancelling; original document kept" : " | Escape to cancel");
    draw_text(x + 12, y + 46, progress, TEXT_COLOR, 68);
    bwa_draw_button_state(x + 342, y + 58, 86, 26, "Cancel", atomic_load(&document_io_shared->cancel) ? BUDO_BUTTON_DISABLED : BUDO_BUTTON_DEFAULT);
}

/* Synchronous app callbacks retain their ABI, but all disk I/O runs in a
 * worker. This nested pump accepts Cancel and window dragging only, preventing
 * document re-entry while keeping presentation and terminal output alive. */
static void document_io_pump(void)
{
    static int previous_buttons;
    static AppWindow *drag;
    if (!bw_begin_frame()) atomic_store(&document_io_shared->cancel, 1);
    int x, y, buttons;
    bw_mouse_state(&x, &y, &buttons);
    int down = (buttons & 1) && !(previous_buttons & 1);
    int up = !(buttons & 1) && (previous_buttons & 1);
    ui_pointer_x = x; ui_pointer_y = y; ui_pointer_buttons = buttons;
    int panel_x = (SCREEN_WIDTH - 440) / 2, panel_y = SCREEN_HEIGHT - 122;
    int activate = ui_button_event(x, y, down, up);
    if (activate && point_in_rect(x, y, panel_x + 342, panel_y + 58, 86, 26))
        atomic_store(&document_io_shared->cancel, 1);
    if (kbhit()) {
        int key = getch();
        if (key == 0 && kbhit()) (void)getch();
        if (key == 27 || key == 13) atomic_store(&document_io_shared->cancel, 1);
    }
    if (down && !point_in_rect(x, y, panel_x, panel_y, 440, 92)) {
        int owner = desktop_point_owner(x, y);
        AppWindow *window = owner == APP_EXPLORER ? &explorer_window : owner == APP_EDITOR ? &editor_window : owner == APP_TERMINAL ? &terminal_window : NULL;
        BwaLoadedApp *app = bwa_find_external_app(owner);
        if (app && app->managed_window) window = &app->window;
        if (window && !window->maximized && point_in_rect(x, y, window->x + 2, window->y + 2, window->w - 54, WINDOW_TITLE_H - 2)) {
            drag = window; drag->dragging = 1; drag->drag_dx = x - window->x; drag->drag_dy = y - window->y;
        }
    }
    if (drag && (buttons & 1)) (void)window_update_pointer(drag, x, y, WINDOW_MIN_W, WINDOW_MIN_H);
    if (drag && (up || !(buttons & 1))) { window_end_pointer(drag); drag = NULL; }
    previous_buttons = buttons;
    BwaLoadedApp *callback = bwa_callback_app;
    (void)session_poll();
    draw_desktop(explorer_page_pointer ? *explorer_page_pointer : 0);
    bwa_callback_app = callback;
    (void)present_framebuffer();
    draw_cursor_vga(x, y);
    if (!bw_end_frame()) atomic_store(&document_io_shared->cancel, 1);
}
