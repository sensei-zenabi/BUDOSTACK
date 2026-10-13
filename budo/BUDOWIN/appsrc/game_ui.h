#ifndef BUDOWIN_GAME_UI_H
#define BUDOWIN_GAME_UI_H
#include "../sdk/ui.h"
#include <stdio.h>
#include <string.h>

static const BwaHostApi *host_api;
static int paused, help_page, finished, confirming, previous_pause;
static int sound_enabled, sound_failed;
static unsigned long long next_tick;
static unsigned int random_state;
static char status[128];
typedef struct Layout { int x, y, w, h, bx, by, cell, status_h; } Layout;
static void game_reset(void);
static void game_tick(void);
static void game_draw(const Layout *l);
static int game_key(int key);
static void game_click(const Layout *l, int x, int y);
static void game_icon(int x, int y);
/* Simulation intervals and next_tick are in microseconds; host clock is milliseconds. */
static unsigned int game_interval(void);
#ifdef GAME_EXTRA_UI
static void game_toolbar_draw(const Layout *l);
static int game_toolbar_click(const Layout *l, int x, int y);
static void game_status_draw(const Layout *l, const char *line);
static void game_cancel_new(void);
#endif

static unsigned char color(int role) { return host_api->get_system_color(role); }
static inline unsigned int random_number(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static int sound_available(void) { return host_api->abi_minor >= 15 && host_api->play_tones; }
static void stop_sound(void)
{
    if (sound_available()) (void)host_api->play_tones(NULL, 0);
}
/* Short asynchronous effects: start, action, reward, loss, victory, error. */
static void effect(int kind)
{
    static const BwaTone notes[][4] = {
        {{523,45},{659,45},{784,80},{0,0}},
        {{880,25},{0,0},{0,0},{0,0}},
        {{660,35},{880,35},{1320,70},{0,0}},
        {{330,80},{220,80},{110,140},{0,0}},
        {{523,60},{659,60},{784,60},{1047,180}},
        {{147,70},{0,0},{0,0},{0,0}}
    };
    static const unsigned int counts[] = {3,1,3,3,4,1};
    if (!sound_enabled || sound_failed || !sound_available()) return;
    if (!host_api->play_tones(notes[kind], counts[kind])) {
        sound_failed = 1;
        fprintf(stderr, "%s: PC speaker effects unavailable in this terminal host.\n", GAME_NAME);
    }
}
static int layout(Layout *l)
{
    if (!host_api->window_get_client_rect(&l->x, &l->y, &l->w, &l->h)) return 0;
    l->status_h = host_api->get_system_metric(BUDO_SYS_METRIC_STATUS_HEIGHT);
    l->cell = (l->w - 20) / GRID_W;
    int vertical = (l->h - 68 - l->status_h) / GRID_H;
    if (l->cell > vertical) l->cell = vertical;
    if (l->cell < 6) return 0;
    l->bx = l->x + (l->w - l->cell * GRID_W) / 2;
    l->by = l->y + 58 + (l->h - 68 - l->status_h - l->cell * GRID_H) / 2;
    return 1;
}
static void text(int x, int y, const char *s, int chars)
{
    host_api->draw_text(x, y, s, color(BUDO_SYS_COLOR_TEXT), chars);
}
static void new_game(void)
{
    stop_sound();
    finished = help_page = confirming = 0;
    paused = START_PAUSED;
    game_reset();
    next_tick = host_api->get_time_ms() * 1000ULL + game_interval();
    effect(0);
}
static void request_new(void)
{
    if (confirming) return;
    if (finished) { new_game(); return; }
    previous_pause = paused;
    paused = 1;
    if (host_api->confirm_dialog(BUDO_CONFIRM_OVERWRITE, "New game?", "Replace the current game and start again?")) confirming = 1;
    else {
        paused = previous_pause;
        snprintf(status, sizeof(status), "Unable to open new-game confirmation.");
    }
}
static void confirm_result(int kind, int response)
{
    if (!confirming || kind != BUDO_CONFIRM_OVERWRITE) return;
    confirming = 0;
    if (response == BUDO_RESPONSE_SAVE) new_game();
    else {
#ifdef GAME_EXTRA_UI
        game_cancel_new();
#endif
        paused = previous_pause;
        next_tick = host_api->get_time_ms() * 1000ULL + game_interval();
    }
}
static void toggle_pause(void)
{
    paused = !paused;
    next_tick = host_api->get_time_ms() * 1000ULL + game_interval();
    if (paused) stop_sound();
}
static void toggle_sound(void)
{
    if (!sound_available()) return;
    sound_enabled = !sound_enabled;
    sound_failed = 0;
    if (sound_enabled) effect(1);
    else stop_sound();
}
#ifndef GAME_MAX_STEPS
#define GAME_MAX_STEPS 8
#endif
/* Deadlines, rather than redraws or key repeats, control simulation speed. */
static int tick(int visible)
{
    unsigned long long now = host_api->get_time_ms() * 1000ULL;
    if (!visible || paused || help_page || confirming || finished) {
        next_tick = now + game_interval();
        return 0;
    }
    int steps = 0;
    while (now >= next_tick && !finished && steps < GAME_MAX_STEPS) {
        unsigned long long deadline = next_tick;
        game_tick();
        if (next_tick == deadline) next_tick = deadline + game_interval();
        ++steps;
    }
    /* Limit catch-up after a stalled desktop; never process an unbounded burst. */
    if (steps == GAME_MAX_STEPS && now >= next_tick) next_tick = now + game_interval();
    return steps != 0;
}
static void draw(void)
{
    Layout l;
    if (!layout(&l)) return;
    /* Older hosts have no idle callback. Preserve their draw-driven fallback. */
    if (host_api->abi_minor < 16) (void)tick(1);
    const char *labels[] = {"New", paused ? "Resume" : "Pause", sound_enabled ? "Sound on" : "Sound off", "Help", "Close"};
    for (int i = 0; i < 5; ++i)
        budo_button_draw(host_api, l.x + 6 + i * 68, l.y + 5, 64, 22, labels[i],
                         i == 2 && !sound_available() ? BUDO_BUTTON_DISABLED : 0);
#ifdef GAME_EXTRA_UI
    game_toolbar_draw(&l);
#else
    text(l.x + 8, l.y + 36, GAME_HINT, (l.w - 16) / 6);
#endif
    game_draw(&l);
    host_api->fill_rect(l.x, l.y + l.h - l.status_h, l.w, l.status_h, color(BUDO_SYS_COLOR_STATUS_BG));
    char line[160];
    snprintf(line, sizeof(line), "%s%s", paused && !finished ? "Paused - " : "", status);
#ifdef GAME_EXTRA_UI
    game_status_draw(&l, line);
#else
    host_api->draw_text(l.x + 4, l.y + l.h - l.status_h + 3, line,
                        color(BUDO_SYS_COLOR_STATUS_TEXT), (l.w - 8) / 6);
#endif
    if (help_page) {
        int x = l.x + (l.w - 340) / 2, y = l.y + 65;
        host_api->draw_sunken_panel(x, y, 340, 144, color(BUDO_SYS_COLOR_FACE));
        const char *lines[] = {GAME_NAME, GAME_HELP, GAME_HINT,
            "P: pause. M: sound. R/Ctrl+N: new game.", "F1: help. Q: close. Esc/Enter: dismiss."};
        for (int i = 0; i < 5; ++i) text(x + 8, y + 10 + i * 20, lines[i], 54);
        budo_button_draw(host_api, x + 130, y + 112, 80, 22, "OK", BUDO_BUTTON_DEFAULT);
    }
}
static int key(int k)
{
    if (confirming) return 1;
    if (help_page) {
        if (k == 27 || k == 13 || k == ' ' || k == (0x100 | 59)) help_page = 0;
        next_tick = host_api->get_time_ms() * 1000ULL + game_interval();
        return 1;
    }
    if (k == 'q' || k == 'Q') return host_api->window_close();
    if (k == 'r' || k == 'R' || k == 14) { request_new(); return 1; }
    if (k == 'm' || k == 'M') { toggle_sound(); return 1; }
    if (k == 'p' || k == 'P') { toggle_pause(); return 1; }
    if (k == (0x100 | 59)) { help_page = 1; stop_sound(); return 1; }
    if (paused && k == ' ') { toggle_pause(); return 1; }
    return game_key(k);
}
static int mouse_down(int x, int y, int buttons)
{
    Layout l;
    if (!(buttons & 1) || !layout(&l)) return 0;
    if (confirming) return 1;
    if (help_page) {
        int ox = l.x + (l.w - 340) / 2, oy = l.y + 65;
        if (host_api->point_in_rect(x, y, ox + 130, oy + 112, 80, 22)) help_page = 0;
        return 1;
    }
    for (int i = 0; i < 5; ++i) {
        if (!host_api->point_in_rect(x, y, l.x + 6 + i * 68, l.y + 5, 64, 22)) continue;
        if (i == 0) request_new();
        else if (i == 1) toggle_pause();
        else if (i == 2) toggle_sound();
        else if (i == 3) { help_page = 1; stop_sound(); }
        else (void)host_api->window_close();
        return 1;
    }
#ifdef GAME_EXTRA_UI
    if (game_toolbar_click(&l, x, y)) return 1;
#endif
    if (!paused && !finished && host_api->point_in_rect(x, y, l.bx, l.by, GRID_W * l.cell, GRID_H * l.cell))
        game_click(&l, x, y);
    return 1;
}
static int open(void)
{
    random_state = (unsigned int)host_api->get_time_ms() | 1U;
    sound_enabled = 1;
    sound_failed = 0;
    new_game();
    if (!host_api->window_create(32, 32, WINDOW_W, WINDOW_H, GAME_NAME, BUDO_WINDOW_DEFAULT_BUTTONS)) return 0;
    return host_api->window_set_min_size(MIN_W, MIN_H);
}
int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->window_create || !host->window_get_client_rect || !host->window_set_min_size ||
        !host->window_close || !host->get_system_color || !host->get_system_metric ||
        !host->fill_rect || !host->fill_rect_rgb || !host->draw_rect || !host->draw_text ||
        !host->draw_standard_button || !host->draw_button_state || !host->draw_sunken_panel ||
        !host->point_in_rect || !host->confirm_dialog || !host->get_time_ms) return 0;
    host_api = host;
    budo_app_definition_clear(host, app);
    app->app_id = GAME_ID;
    app->name = GAME_NAME;
    app->callbacks.open = open;
    app->callbacks.draw = draw;
    app->callbacks.draw_icon = game_icon;
    app->callbacks.mouse_down = mouse_down;
    app->callbacks.key = key;
    app->callbacks.close = stop_sound;
    app->confirm_result = confirm_result;
    if (host->abi_minor >= 16) {
        app->flags |= BWA_FLAG_IDLE_TICK;
        app->tick = tick;
    }
    return 1;
}
/* Scale a 16x16 palette sprite to the desktop's 32x32 icon area. */
static void icon_sprite(int x, int y, const char *pixels, const unsigned int *palette)
{
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            int index = pixels[r * 16 + c] - '0';
            if (index) host_api->fill_rect_rgb(x + c * 2, y + r * 2, 2, 2, palette[index]);
        }
    }
}
#endif
