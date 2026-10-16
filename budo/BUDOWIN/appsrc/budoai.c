#define _POSIX_C_SOURCE 200809L
#include "../sdk/ui.h"
#include "budoai_engine.h"
#include <stdarg.h>

static const BwaHostApi *host_api;
static AiEngine engine;
static char root_input[AI_PATH], resolved_root[AI_PATH], question[256];
static char transcript[65536];
static size_t caret;
static int focus, selected_all, pending_answer, scroll_bottom, expanded, primary_cut;
static BudoScrollbar scrollbar;

typedef struct AiLayout { int x, y, w, h, cw, lh, rows, columns; } AiLayout;
static unsigned char ai_color(int role) { return host_api->get_system_color(role); }
static int ai_layout(AiLayout *l)
{
    if (!host_api->window_get_client_rect(&l->x, &l->y, &l->w, &l->h)) return 0;
    l->cw = host_api->get_system_metric(BUDO_SYS_METRIC_CHAR_WIDTH);
    l->lh = host_api->get_system_metric(BUDO_SYS_METRIC_LINE_HEIGHT);
    if (l->cw < 1) l->cw = 6;
    if (l->lh < 8) l->lh = 10;
    l->columns = (l->w - 34) / l->cw;
    l->rows = (l->h - 110) / l->lh;
    return l->columns > 0 && l->rows > 0;
}
static void ai_append(const char *format, ...)
{
    char buffer[8192];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    size_t used = strlen(transcript), add = strlen(buffer);
    if (used + add >= sizeof(transcript)) {
        size_t remove = 8192;
        while (transcript[remove] && transcript[remove] != '\n') ++remove;
        memmove(transcript, transcript + remove, used - remove + 1);
        used -= remove;
    }
    memcpy(transcript + used, buffer, add + 1);
    scroll_bottom = 1;
}
static void ai_excerpt(int i, int full)
{
    AiPassage *p = &engine.passages[engine.result[i]];
    const char *path = engine.docs[p->doc].path + strlen(engine.root);
    if (*path == '/') ++path;
    const char *text = engine.excerpts[i];
    size_t length = strlen(text), shown = length;
    while (length && isspace((unsigned char)text[length - 1])) --length;
    shown = length;
    if (!full && shown > 400) {
        shown = 400;
        while (shown && !isspace((unsigned char)text[shown])) --shown;
        if (!shown) shown = 400;
        primary_cut = 1;
    }
    ai_append("[%d] %.*s%s\nSource [%d]: %s, lines %u-%u\n\n", i + 1,
              (int)shown, text, shown < length ? " ..." : "", i + 1, path, p->first_line, p->last_line);
}
/* Application guidance describes the program; factual answers still use books. */
static int ai_guidance(void)
{
    char text[256];
    size_t n = strlen(engine.question);
    if (n >= sizeof(text)) n = sizeof(text) - 1;
    for (size_t i = 0; i < n; ++i) text[i] = (char)tolower((unsigned char)engine.question[i]);
    while (n && (isspace((unsigned char)text[n - 1]) || text[n - 1] == '!' || text[n - 1] == '?' || text[n - 1] == '.')) --n;
    text[n] = '\0';
    if (!strcmp(text, "hello") || !strcmp(text, "hi") || !strcmp(text, "hey")) {
        ai_append("BUDOAI: Hello! Ask me a question about your local books. I will show where I found the answer.\n\n");
        return 1;
    }
    if (!strncmp(text, "are you ", 8) || !strcmp(text, "what are you") || !strcmp(text, "who are you")) {
        ai_append("BUDOAI: I am a computer program, not a person. I look for answers in your local books. I can only explain what those books support.\n\n");
        return 1;
    }
    return 0;
}
static void ai_answer(void)
{
    expanded = primary_cut = 0;
    if (ai_guidance()) { engine.result_count = 0; return; }
    if (!engine.result_count) {
        ai_append(engine.intent == AI_SIMPLER ?
            "BUDOAI: I don't have a simpler explanation of that in these books yet.\n\n" :
            "BUDOAI: I couldn't find an answer in these books. Try different words, or add a TXT file and press Refresh Knowledge.\n\n");
        return;
    }
    if (engine.corrections[0]) ai_append("Spelling used: %s\n", engine.corrections);
    ai_append("BUDOAI (from your books):\n");
    ai_excerpt(0, 0);
}
static int ai_more_available(void)
{
    return engine.result_count > 0 && !pending_answer && engine.phase == AI_READY && !expanded &&
        (engine.result_count > 1 || primary_cut);
}
static void ai_more(void)
{
    if (!ai_more_available()) return;
    ai_append("BUDOAI: More from these books:\n");
    for (int i = primary_cut ? 0 : 1; i < engine.result_count; ++i) {
        char checked[AI_CHUNK + 1];
        if (ai_read_passage(&engine, engine.result[i], checked)) ai_excerpt(i, 1);
        else ai_append("BUDOAI: That source changed or could not be read. Press Refresh Knowledge before asking again.\n\n");
    }
    expanded = 1;
}
static void ai_submit(void)
{
    if (engine.phase != AI_READY || pending_answer || !question[0]) return;
    ai_append("USER: %s\n", question);
    ai_ask(&engine, question);
    question[0] = '\0';
    caret = 0;
    selected_all = 0;
    focus = 0;
    pending_answer = 1;
}
static void ai_reload(void)
{
    if (root_input[0] == '/') snprintf(resolved_root, sizeof(resolved_root), "%s", root_input);
    else if (!ai_path(resolved_root, host_api->get_working_directory(), root_input)) {
        ai_append("BUDOAI: Knowledge root path is too long.\n");
        return;
    }
    pending_answer = 0;
    expanded = primary_cut = 0;
    ai_refresh(&engine, resolved_root);
    ai_append("BUDOAI: Reading your books. Conversation topic cleared.\n");
}
static void ai_clear(void)
{
    transcript[0] = '\0';
    question[0] = '\0';
    engine.category[0] = '\0';
    engine.subject_count = 0;
    engine.question[0] = '\0';
    expanded = primary_cut = 0;
    engine.result_count = engine.hit_count = 0;
    if (engine.phase == AI_DF || engine.phase == AI_SCORE) engine.phase = AI_READY;
    pending_answer = 0;
    caret = 0;
    focus = selected_all = 0;
    memset(&scrollbar, 0, sizeof(scrollbar));
    ai_append("BUDOAI: Ask a question about your local books. I will show where I found each answer. Use More for extra detail.\n\n");
}
/* Word wrapping is recomputed on resize; transcript stores bounded session history. */
static void ai_lines(const AiLayout *l, int paint)
{
    const char *s = transcript;
    int row = 0;
    while (*s) {
        size_t n = 0;
        while (s[n] && s[n] != '\n' && n < (size_t)l->columns) ++n;
        if (n == (size_t)l->columns && s[n] && s[n] != '\n') {
            size_t split = n;
            while (split && s[split] != ' ' && s[split] != '\t') --split;
            if (split) n = split;
        }
        if (paint && row >= scrollbar.position && row < scrollbar.position + l->rows) {
            char line[1024];
            size_t copy = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
            memcpy(line, s, copy);
            line[copy] = '\0';
            for (size_t i = 0; i < copy; ++i) if (line[i] == '\r' || line[i] == '\t') line[i] = ' ';
            host_api->draw_text(l->x + 10, l->y + 76 + (row - scrollbar.position) * l->lh,
                                line, ai_color(BUDO_SYS_COLOR_TEXT), l->columns);
        }
        s += n;
        if (*s == '\n' || *s == ' ' || *s == '\t') ++s;
        ++row;
    }
    if (!paint) scrollbar.total = row;
}
static void ai_field(const AiLayout *l, int y, int width, const char *text, int id)
{
    unsigned char surface = ai_color(selected_all && focus == id ? BUDO_SYS_COLOR_SELECTION_BG : BUDO_SYS_COLOR_SURFACE);
    host_api->draw_sunken_panel(l->x + 6, y, width, 22, surface);
    size_t length = strlen(text), pos = focus == id ? caret : length;
    int chars = (width - 12) / l->cw;
    size_t start = pos >= (size_t)chars ? pos - (size_t)chars + 1 : 0;
    host_api->draw_text(l->x + 10, y + 7, text + start,
                        ai_color(selected_all && focus == id ? BUDO_SYS_COLOR_SELECTION_TEXT : BUDO_SYS_COLOR_TEXT), chars);
    if (focus == id) budo_text_caret(host_api, l->x + 10 + (int)(pos - start) * l->cw,
                                    y + 6, 10, ai_color(BUDO_SYS_COLOR_TEXT));
    if (host_api->pointer_region) host_api->pointer_region(l->x + 6, y, width, 22,
        BUDO_CURSOR_TEXT, id == 0 ? "Question (Enter to submit)" : "Books folder (Enter or F5 to refresh)");
}
static void ai_draw(void)
{
    AiLayout l;
    if (!ai_layout(&l)) return;
    host_api->fill_rect(l.x, l.y, l.w, l.h, ai_color(BUDO_SYS_COLOR_FACE));
    ai_field(&l, l.y + 6, l.w - 88, question, 0);
    budo_button_draw(host_api, l.x + l.w - 76, l.y + 6, 70, 22, "Submit",
        (engine.phase != AI_READY || pending_answer ? BUDO_BUTTON_DISABLED : BUDO_BUTTON_DEFAULT) |
        (focus == 1 ? BUDO_BUTTON_FOCUSED : 0));
    ai_field(&l, l.y + 34, l.w - 214, root_input, 2);
    budo_button_draw(host_api, l.x + l.w - 202, l.y + 34, 120, 22, "Refresh Knowledge", focus == 3 ? BUDO_BUTTON_FOCUSED : 0);
    budo_button_draw(host_api, l.x + l.w - 76, l.y + 34, 70, 22, "Clear", focus == 4 ? BUDO_BUTTON_FOCUSED : 0);
    host_api->draw_text(l.x + 8, l.y + 62, "Answers and session history", ai_color(BUDO_SYS_COLOR_TEXT), (l.w - 100) / l.cw);
    budo_button_draw(host_api, l.x + l.w - 76, l.y + 58, 70, 14, "More",
        (ai_more_available() ? 0U : BUDO_BUTTON_DISABLED) | (focus == 6 ? BUDO_BUTTON_FOCUSED : 0));
    host_api->draw_sunken_panel(l.x + 6, l.y + 72, l.w - 12, l.h - 100, ai_color(BUDO_SYS_COLOR_SURFACE));
    scrollbar.x = l.x + l.w - 20;
    scrollbar.y = l.y + 75;
    scrollbar.length = l.h - 106;
    scrollbar.page = l.rows;
    ai_lines(&l, 0);
    if (scroll_bottom) { scrollbar.position = budo_scroll_limit(&scrollbar); scroll_bottom = 0; }
    budo_scroll_clamp(&scrollbar);
    ai_lines(&l, 1);
    budo_scroll_draw(host_api, &scrollbar);
    char status[160];
    snprintf(status, sizeof(status), "%s | %d books | %d reading problems%s | F5: refresh Ctrl+D: more",
        engine.phase == AI_INDEX ? "Reading books" : engine.phase != AI_READY || pending_answer ? "Searching" : "Ready",
        engine.documents, engine.errors, engine.limited ? " | Some text skipped" : "");
    host_api->draw_text(l.x + 6, l.y + l.h - 18, status, ai_color(BUDO_SYS_COLOR_STATUS_TEXT), (l.w - 12) / l.cw);
}
static int ai_tick(int visible)
{
    (void)visible;
    int changed = 0, previous = engine.phase;
    for (int i = 0; i < 8 && engine.phase != AI_READY; ++i) changed |= ai_step(&engine);
    if (previous == AI_INDEX && engine.phase == AI_READY) {
        ai_append("BUDOAI: Ready. I read %d TXT files.%s%s\n\n", engine.documents,
                  engine.errors ? " Some files could not be read; check the folder or file format." : "",
                  engine.limited ? " Some text reached the size limit; try smaller files." : "");
    }
    if (pending_answer && engine.phase == AI_READY) {
        ai_answer();
        pending_answer = 0;
        changed = 1;
    }
    return changed;
}
static int ai_key(int key)
{
    if (key == 4) { ai_more(); return 1; }
    if (key == 12) { ai_clear(); return 1; }
    if (key == (0x100 | 63)) { ai_reload(); return 1; }
    if (key == 9) {
        int backwards = host_api->get_keyboard_modifiers && (host_api->get_keyboard_modifiers() & 3);
        focus = (focus + (backwards ? 6 : 1)) % 7;
        caret = strlen(focus == 2 ? root_input : question);
        selected_all = 0;
        return 1;
    }
    if (key == 13) {
        if (focus == 2 || focus == 3) ai_reload();
        else if (focus == 4) ai_clear();
        else if (focus == 6) ai_more();
        else ai_submit();
        return 1;
    }
    if (key == (0x100 | 201) || key == (0x100 | 73)) scrollbar.position -= key == (0x100 | 73) ? scrollbar.page : 3;
    else if (key == (0x100 | 202) || key == (0x100 | 81)) scrollbar.position += key == (0x100 | 81) ? scrollbar.page : 3;
    else if (focus == 5 && key == (0x100 | 72)) --scrollbar.position;
    else if (focus == 5 && key == (0x100 | 80)) ++scrollbar.position;
    else if (focus == 0 || focus == 2) {
        char *text = focus == 0 ? question : root_input;
        size_t capacity = focus == 0 ? sizeof(question) : sizeof(root_input);
        size_t length = strlen(text);
        if (key == 1) selected_all = 1;
        else if (key == (0x100 | 75)) { if (caret) --caret; selected_all = 0; }
        else if (key == (0x100 | 77)) { if (caret < length) ++caret; selected_all = 0; }
        else if (key == (0x100 | 71)) { caret = 0; selected_all = 0; }
        else if (key == (0x100 | 79)) { caret = length; selected_all = 0; }
        else if (key == 8 || key == (0x100 | 83) || (key >= 32 && key < 127)) {
            if (selected_all) { text[0] = '\0'; caret = length = 0; selected_all = 0; }
            if (key == 8) {
                if (caret) { memmove(text + caret - 1, text + caret, length - caret + 1); --caret; }
            } else if (key == (0x100 | 83)) {
                if (caret < length) memmove(text + caret, text + caret + 1, length - caret);
            } else if (length + 1 < capacity) {
                memmove(text + caret + 1, text + caret, length - caret + 1);
                text[caret++] = (char)key;
            }
        }
    }
    budo_scroll_clamp(&scrollbar);
    return 1;
}
static int ai_mouse_down(int x, int y, int buttons)
{
    AiLayout l;
    if (!(buttons & 1) || !ai_layout(&l)) return 0;
    if (budo_scroll_pointer_host(host_api, &scrollbar, x, y, BUDO_POINTER_DOWN)) { focus = 5; return 1; }
    if (host_api->point_in_rect(x, y, l.x + l.w - 76, l.y + 6, 70, 22)) ai_submit();
    else if (host_api->point_in_rect(x, y, l.x + l.w - 202, l.y + 34, 120, 22)) ai_reload();
    else if (host_api->point_in_rect(x, y, l.x + l.w - 76, l.y + 34, 70, 22)) ai_clear();
    else if (host_api->point_in_rect(x, y, l.x + l.w - 76, l.y + 58, 70, 14)) ai_more();
    else {
        focus = y < l.y + 30 ? 0 : y < l.y + 60 ? 2 : 5;
        caret = strlen(focus == 2 ? root_input : question);
        selected_all = 0;
    }
    return 1;
}
static int ai_mouse_move(int x, int y, int buttons)
{
    (void)buttons;
    return budo_scroll_pointer_host(host_api, &scrollbar, x, y, BUDO_POINTER_MOVE);
}
static int ai_mouse_up(int x, int y, int buttons)
{
    (void)buttons;
    return budo_scroll_pointer_host(host_api, &scrollbar, x, y, BUDO_POINTER_UP);
}
static void ai_icon(int x, int y)
{
    host_api->draw_sunken_panel(x + 2, y + 2, 28, 28, ai_color(BUDO_SYS_COLOR_SURFACE));
    host_api->draw_text(x + 7, y + 12, "AI", ai_color(BUDO_SYS_COLOR_TEXT), 2);
}
static void ai_close(void) { ai_destroy(&engine); }
static int ai_open(void)
{
    if (!host_api->window_create(24, 24, 580, 420, "BUDOAI", BUDO_WINDOW_DEFAULT_BUTTONS)) return 0;
    if (!host_api->window_set_min_size(420, 260)) { (void)host_api->window_close(); return 0; }
    const char *root = getenv("BUDOAI_KNOWLEDGE_ROOT");
    snprintf(root_input, sizeof(root_input), "%s", root && *root ? root : "./knowledge");
    ai_clear();
    ai_reload();
    return 1;
}
int bwa_entry(const BwaHostApi *host, BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 19 ||
        !host->get_working_directory || !host->get_working_directory() || !host->window_create ||
        !host->window_set_min_size || !host->window_close || !host->window_get_client_rect ||
        !host->get_system_color || !host->get_system_metric || !host->fill_rect || !host->draw_text ||
        !host->draw_standard_button || !host->draw_sunken_panel || !host->point_in_rect) return 0;
    host_api = host;
    budo_app_definition_clear(host, app);
    app->app_id = "budoai";
    app->name = "BUDOAI";
    app->flags = BWA_FLAG_IDLE_TICK;
    app->callbacks.open = ai_open;
    app->callbacks.draw = ai_draw;
    app->callbacks.draw_icon = ai_icon;
    app->callbacks.key = ai_key;
    app->callbacks.mouse_down = ai_mouse_down;
    app->callbacks.mouse_move = ai_mouse_move;
    app->callbacks.mouse_up = ai_mouse_up;
    app->callbacks.close = ai_close;
    app->tick = ai_tick;
    return 1;
}
