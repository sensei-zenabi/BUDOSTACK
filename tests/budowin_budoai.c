#define _POSIX_C_SOURCE 200809L
#include "../budo/BUDOWIN/appsrc/budoai.c"
#include <assert.h>

static const char *workspace;
static int draw_count, create_count;
static const char *working(void) { return workspace; }
static int rectangle(int *x, int *y, int *w, int *h)
{
    *x = 0; *y = 0; *w = 572; *h = 394;
    return 1;
}
static void fill(int x, int y, int w, int h, unsigned char c)
{
    (void)c;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 572 && y + h <= 394);
    ++draw_count;
}
static void label(int x, int y, const char *s, unsigned char c, int n)
{
    (void)c;
    assert(x >= 0 && y >= 0 && y + 8 <= 394 && s && n > 0);
    ++draw_count;
}
static int create(int x, int y, int w, int h, const char *s, unsigned int flags)
{
    (void)x; (void)y; (void)flags;
    assert(w == 580 && h == 420 && !strcmp(s, "BUDOAI"));
    ++create_count;
    return 1;
}
static int minimum(int w, int h) { assert(w == 420 && h == 260); return 1; }
static int close_window(void) { return 1; }
static unsigned char color(int role) { return (unsigned char)role; }
static int metric(int role) { return role == BUDO_SYS_METRIC_CHAR_WIDTH ? 6 : 10; }
static int inside(int x, int y, int bx, int by, int w, int h)
{
    return x >= bx && y >= by && x < bx + w && y < by + h;
}
static void button(int x, int y, int w, int h, const char *s, int pressed)
{
    (void)s; (void)pressed;
    fill(x, y, w, h, 0);
}
static unsigned long long now(void) { return 1000; }
static void run(AiEngine *e)
{
    size_t steps = 0;
    while (e->phase != AI_READY) { assert(ai_step(e)); assert(++steps < 100000); }
}
static int contains(AiEngine *e, const char *word)
{
    for (int i = 0; i < e->result_count; ++i) if (strstr(e->excerpts[i], word)) return 1;
    return 0;
}
static void check_sources(AiEngine *e)
{
    for (int i = 0; i < e->result_count; ++i) {
        AiPassage *p = &e->passages[e->result[i]];
        char source[AI_CHUNK + 1];
        assert(ai_read_passage(e, e->result[i], source));
        assert(!strcmp(source, e->excerpts[i]));
        assert(p->first_line <= p->last_line);
        FILE *f = fopen(e->docs[p->doc].path, "rb");
        assert(f);
        unsigned int line = 1;
        for (long offset = 0; offset < p->offset; ++offset) if (fgetc(f) == '\n') ++line;
        assert(line == p->first_line);
        for (int b = 0; b < p->bytes; ++b) {
            assert(fgetc(f) == (unsigned char)source[b]);
            if (b == p->bytes - 1) assert(line == p->last_line);
            if (source[b] == '\n') ++line;
        }
        fclose(f);
    }
}
static void ask(AiEngine *e, const char *question_text, const char *expected)
{
    ai_ask(e, question_text);
    run(e);
    if (expected && !contains(e, expected)) {
        fprintf(stderr, "Question failed: %s (%d results)\n", question_text, e->result_count);
        for (int i = 0; i < e->result_count; ++i) fprintf(stderr, "%s\n", e->excerpts[i]);
        abort();
    }
    if (!expected) assert(e->result_count == 0);
    check_sources(e);
}
static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    AiEngine e = {0};
    ai_refresh(&e, argv[1]);
    run(&e);
    assert(e.documents == 7 && e.count >= 18 && !e.errors);
    ask(&e, "How do I fish?", "Step 1");
    assert(contains(&e, "Step 2") && contains(&e, "Step 3"));
    int docs = 0;
    for (int i = 0; i < e.result_count; ++i) docs |= 1 << e.passages[e.result[i]].doc;
    assert((docs & (docs - 1)) != 0);
    ask(&e, "What equipment do I need?", "rod");
    assert(!strcmp(e.category, "fishing"));
    ask(&e, "What about bait?", "worms");
    assert(e.use_context && !strcmp(e.category, "fishing"));
    ask(&e, "What equipment is needed for fishing?", "equipment includes");
    ask(&e, "Explain Newton's first law", "remains at rest");
    assert(e.result_count == 1 && !strcmp(e.category, "physics"));
    ask(&e, "What is computer memory?", "Random-access memory");
    ask(&e, "What bait should I use?", "worms");
    ask(&e, "How do quantum unicorns teleport?", NULL);
    ask(&e, "What is quantum fishing?", NULL);
    ask(&e, "the and how", NULL);
    ask(&e, "WHAT IS COMPUTER MEMORY?", "Random-access memory");
    assert(ai_question_type("What is voltage?") == 1);
    assert(ai_question_type("Who wrote this?") == 2);
    assert(ai_question_type("Where is it?") == 3);
    assert(ai_question_type("When does it start?") == 4);
    assert(ai_question_type("Why does it move?") == 5);
    assert(ai_question_type("How does it work?") == 6);
    AiBag a, b;
    ai_tokenize("algorithms variables fishing memories", &a);
    ai_tokenize("algorithm variable fish memory", &b);
    assert(a.count == b.count);
    for (int i = 0; i < a.count; ++i) assert(ai_frequency(&b, a.term[i]));
    ai_destroy(&e);

    char path[AI_PATH], nested[AI_PATH];
    ai_refresh(&e, argv[2]); run(&e);
    assert(!e.count && !e.errors);
    ask(&e, "How do I fish?", NULL);
    assert(ai_path(nested, argv[2], "robotics") && mkdir(nested, 0700) == 0);
    assert(ai_path(path, nested, "gantry.TXT"));
    write_text(path, "A gantry uses orthogonal rails to position a payload.\n");
    ask(&e, "What is a gantry?", NULL); /* Explicit refresh is required. */
    ai_refresh(&e, argv[2]); run(&e);
    ask(&e, "What is a gantry?", "orthogonal rails");
    assert(!strcmp(e.category, "robotics"));
    assert(unlink(path) == 0);
    ask(&e, "What is a gantry?", NULL); /* Removed indexed file. */
    assert(e.errors);
    write_text(path, "A gantry uses orthogonal rails to position a payload.\n");
    ai_refresh(&e, argv[2]); run(&e);
    write_text(path, "A changed unrelated passage replaces the old content.\n");
    ask(&e, "What is a gantry?", NULL); /* Checksum rejects edited content. */
    assert(e.errors);
    write_text(path, "A gantry uses orthogonal rails to position a payload.\n");
    ai_refresh(&e, argv[2]); run(&e);
    /* A document timestamp change invalidates even an unchanged excerpt. */
    e.docs[0].modified.tv_nsec = (e.docs[0].modified.tv_nsec + 1) % 1000000000L;
    ask(&e, "gantry", NULL);
    ai_refresh(&e, argv[2]); run(&e);
    char copy[AI_PATH];
    assert(ai_path(copy, nested, "copy.txt"));
    write_text(copy, "A gantry uses orthogonal rails to position a payload.\n");
    ai_refresh(&e, argv[2]); run(&e);
    ask(&e, "gantry", "orthogonal");
    assert(e.result_count == 1); /* Cross-file duplicate removal. */
    assert(unlink(copy) == 0);
    assert(ai_path(copy, nested, "windows.txt"));
    write_text(copy, "A capacitor stores electric charge.\r\n\r\nAn inductor stores energy in a magnetic field.\r\n");
    ai_refresh(&e, argv[2]); run(&e);
    assert(e.count == 3 && !e.errors);
    ask(&e, "What is a capacitor?", "stores electric charge");
    assert(unlink(copy) == 0);
    assert(ai_path(copy, nested, "bad.txt"));
    FILE *bad = fopen(copy, "wb");
    assert(bad && fputc(0, bad) != EOF && fclose(bad) == 0);
    assert(ai_path(copy, nested, "pipe.txt") && mkfifo(copy, 0600) == 0);
    assert(ai_path(copy, nested, "loop") && symlink(argv[2], copy) == 0);
    ai_refresh(&e, argv[2]); run(&e);
    assert(e.errors == 1 && e.documents == 2);
    ask(&e, "gantry", "orthogonal");
    assert(ai_path(copy, nested, "long.txt"));
    FILE *large = fopen(copy, "wb");
    assert(large);
    for (int i = 0; i < AI_FILE_BYTES + AI_CHUNK; ++i) assert(fputc('x', large) != EOF);
    assert(fclose(large) == 0);
    ai_refresh(&e, argv[2]); run(&e);
    assert(e.limited && e.count <= AI_PASSAGES);
    ai_destroy(&e);
    ai_refresh(&e, "/no/such/budoai/knowledge");
    assert(e.phase == AI_READY && !e.count && e.errors);
    ai_destroy(&e);

    /* Exercise production UI callbacks and native ABI, including offline root resolution. */
    workspace = argv[1];
    BwaHostApi host = {.abi_major = 1, .abi_minor = 19, .get_working_directory = working,
        .window_create = create, .window_set_min_size = minimum, .window_close = close_window,
        .window_get_client_rect = rectangle, .get_system_color = color, .get_system_metric = metric,
        .fill_rect = fill, .draw_rect = fill, .draw_text = label, .draw_standard_button = button,
        .draw_sunken_panel = fill, .point_in_rect = inside, .get_time_ms = now};
    BwaAppDefinition app;
    host.abi_minor = 18;
    assert(!bwa_entry(&host, &app));
    host.abi_minor = 19;
    assert(bwa_entry(&host, &app) && !strcmp(app.app_id, "budoai"));
    assert(setenv("BUDOAI_KNOWLEDGE_ROOT", ".", 1) == 0);
    assert(app.callbacks.open() && create_count == 1);
    while (engine.phase != AI_READY) assert(app.tick(1));
    app.callbacks.draw();
    const char *q = "How do I fish?";
    for (; *q; ++q) assert(app.callbacks.key(*q));
    assert(app.callbacks.key(13));
    while (engine.phase != AI_READY || pending_answer) app.tick(1);
    assert(strstr(transcript, "Step 1") && strstr(transcript, "Source [1]"));
    assert(app.callbacks.key(9) && focus == 1);
    assert(app.callbacks.key(9) && focus == 2);
    assert(app.callbacks.key(1));
    assert(app.callbacks.key('.') && !strcmp(root_input, "."));
    assert(app.callbacks.mouse_down(505, 15, 1)); /* Submit uses release callback. */
    assert(app.callbacks.key(0x100 | 202));
    assert(app.callbacks.mouse_move(553, 100, 1) >= 0);
    assert(app.callbacks.mouse_up(553, 100, 0) >= 0);
    app.callbacks.draw();
    assert(draw_count);
    assert(app.callbacks.key(12) && !engine.category[0] && !strstr(transcript, "USER:"));
    assert(app.callbacks.key(0x100 | 63));
    while (engine.phase != AI_READY) app.tick(0);
    app.callbacks.close();
    assert(!engine.docs && !engine.passages && !engine.depth && !engine.file);
    puts("BUDOAI acceptance, source verification, resource/error and UI tests passed.");
    return 0;
}
