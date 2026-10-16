/* Simulated language/usability regression, not a study with children. */
#define main budoai_core_acceptance_main
#include "budowin_budoai.c"
#undef main

typedef struct ChildCase {
    const char *question, *meaning;
    int keep_context;
} ChildCase;
static const ChildCase child_cases[] = {
    {"How do I fish?", "Step 3:", 0},
    {"What equipment do I need?", "rod, a reel", 1},
    {"What about bait?", "Bait is food", 1},
    {"What stuff do I need to go fishing?", "rod, a reel", 0},
    {"How can I start fishing?", "Step 3:", 0},
    {"How do I catch a fish?", "Step 3:", 0},
    {"What do I put on the hook?", "worms", 0},
    {"Can I use worms?", "worms", 0},
    {"What is bait?", "Bait is food", 0},
    {"How do I go fishng?", "Step 3:", 0},
    {"What is computer memory?", "Random-access memory", 0},
    {"What is RAM?", "power turns off", 0},
    {"How does a computer remember things?", "locations with addresses", 0},
    {"What is a variable?", "labelled box", 0},
    {"What is an algorithm?", "set of steps", 0},
    {"How do I find a number in a list?", "linear search", 0},
    {"Explain Newton's first law", "remains at rest", 0},
    {"Can you explain that more simply?", "rolling ball", 1},
    {"Why do things keep moving?", "because", 0},
    {"What is electricity?", "flow of charge", 0},
    {"What is voltage?", "electrical push", 0},
    {"What is resistance?", "opposes an electric current", 0},
    {"What is Newton's fourth law?", NULL, 0},
    {"Can a computer catch a fish?", NULL, 0},
    {"What bait is best for sharks?", NULL, 0},
    {"Is a fishing rod a fish?", NULL, 0},
    {"Why does my fishing rod float?", NULL, 0},
    {"Tell me a joke", NULL, 0},
    {"Hello", NULL, 0},
    {"Are you a person?", NULL, 0},
    {"What does orthogonal mean?", NULL, 0},
    {"What is the capital of Mars?", NULL, 0}
};
static void child_question(const char *q)
{
    transcript[0] = '\0'; /* Retain engine subject while isolating this response. */
    snprintf(question, sizeof(question), "%s", q);
    caret = strlen(question);
    focus = 0;
    assert(ai_key(13));
    size_t steps = 0;
    while (engine.phase != AI_READY || pending_answer) { ai_tick(1); assert(++steps < 100000); }
}
static void child_sources(void)
{
    check_sources(&engine);
    assert(strstr(transcript, "Source [1]:"));
    assert(!strstr(transcript, "bytes "));
    assert(strlen(transcript) < 700); /* Includes question, source and correction notice. */
    assert(!strstr(transcript, "Source [2]:")); /* More must be an explicit action. */
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    workspace = argv[1];
    BwaHostApi host = {.abi_major = 1, .abi_minor = 19, .get_working_directory = working,
        .window_create = create, .window_set_min_size = minimum, .window_close = close_window,
        .window_get_client_rect = rectangle, .get_system_color = color, .get_system_metric = metric,
        .fill_rect = fill, .draw_rect = fill, .draw_text = label, .draw_standard_button = button,
        .draw_sunken_panel = fill, .point_in_rect = inside, .get_time_ms = now};
    BwaAppDefinition app;
    assert(bwa_entry(&host, &app));
    assert(setenv("BUDOAI_KNOWLEDGE_ROOT", ".", 1) == 0 && app.callbacks.open());
    while (engine.phase != AI_READY) ai_tick(1);
    for (size_t i = 0; i < sizeof(child_cases) / sizeof(child_cases[0]); ++i) {
        const ChildCase *c = &child_cases[i];
        if (!c->keep_context) ai_clear();
        child_question(c->question);
        if (c->meaning) {
            if (!engine.result_count || !strstr(engine.excerpts[0], c->meaning)) {
                fprintf(stderr, "FAIL child case %zu: %s\n%s", i + 1, c->question, transcript);
                return 1;
            }
            child_sources();
            if (c->keep_context) assert(engine.use_context);
            if (strstr(c->question, "fishng")) assert(strstr(transcript, "fishng -> fishing"));
            if (strstr(c->meaning, "Step 3:")) {
                assert(strstr(engine.excerpts[0], "Step 1:") && strstr(engine.excerpts[0], "Step 2:"));
            }
        } else {
            assert(!engine.result_count);
            if (!strcmp(c->question, "Hello")) assert(strstr(transcript, "Hello!"));
            else if (!strcmp(c->question, "Are you a person?")) assert(strstr(transcript, "computer program, not a person"));
            else assert(strstr(transcript, "couldn't find an answer"));
            assert(!strstr(transcript, "Source ["));
        }
        app.callbacks.draw();
    }
    ai_clear();
    child_question("How do I fish?");
    assert(ai_more_available());
    size_t short_length = strlen(transcript);
    assert(ai_key(4));
    assert(strlen(transcript) > short_length && strstr(transcript, "Source [2]:"));
    size_t expanded_length = strlen(transcript);
    assert(ai_key(4) && strlen(transcript) == expanded_length);
    ai_clear();
    child_question("Can you explain that more simply?");
    assert(!engine.result_count && !engine.subject_count);

    /* Unseen TXT content, no application-specific topic or question rules. */
    char path[AI_PATH];
    assert(ai_path(path, argv[2], "space.txt"));
    write_text(path,
        "A nebula is a cloud of gas and dust in space.\n\n"
        "Simple explanation: A nebula is a huge cloud in space. It contains gas and dust.\n\n"
        "A comet is an icy body that orbits a star.\n\n"
        "A covet is a fictional label in this spelling ambiguity test.\n\n"
        "To align a lens, first loosen the mount. Then adjust the lens until the image is sharp. Finally tighten the mount.\n\n"
        "Procedure: To calibrate a sensor, Step 1: measure a known reference. Step 2: adjust the offset. Step 3: measure the reference again.\n");
    ai_refresh(&engine, argv[2]);
    while (engine.phase != AI_READY) ai_tick(1);
    child_question("What is a nebulla?");
    assert(engine.result_count && strstr(engine.excerpts[0], "gas and dust"));
    assert(strstr(transcript, "nebulla -> nebula"));
    child_sources();
    child_question("Can you explain that more simply?");
    assert(engine.result_count && strstr(engine.excerpts[0], "huge cloud"));
    child_sources();
    ai_clear();
    child_question("How can I start calibrating a sensor?");
    assert(engine.result_count && strstr(engine.excerpts[0], "adjust the offset"));
    child_sources();
    ai_clear();
    child_question("Why does a nebula explode?");
    assert(!engine.result_count);
    child_question("What is a comets?"); /* Existing morphology should win over spelling. */
    assert(engine.result_count && strstr(engine.excerpts[0], "icy body"));
    child_question("What is a cojet?"); /* One edit from both comet and covet. */
    assert(!engine.result_count && !engine.corrections[0]);
    child_question("Is a nebula a cloud?");
    assert(engine.result_count && strstr(engine.excerpts[0], "cloud"));
    child_sources();
    child_question("Is a cloud a nebula?");
    assert(!engine.result_count); /* Do not reverse a one-way definition. */
    child_question("How do I align a lens?");
    assert(engine.result_count && strstr(engine.excerpts[0], "tighten the mount"));
    child_sources();
    child_question("What is an unknown zorbulator?");
    assert(!engine.result_count);
    child_question("Can you explain that more simply?");
    assert(!engine.result_count); /* Never explain an older topic after a failed question. */
    child_question("What is a nebula?");
    assert(ai_more_available());
    write_text(path, "The book has changed since the answer was retrieved.\n");
    ai_more();
    assert(strstr(transcript, "That source changed") && !strstr(transcript, "Source [2]:"));
    assert(unlink(path) == 0);
    app.callbacks.close();
    puts("PASS: 32 child-style cases, short answers/More, grounding, spelling, follow-ups and unseen-document transfer.");
    return 0;
}
