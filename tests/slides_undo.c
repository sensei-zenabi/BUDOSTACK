#define _XOPEN_SOURCE 600
/* Run: gcc -std=c11 -Wall -Wextra -Werror -Wpedantic
 * tests/slides_undo.c -o /tmp/slides_undo_test && /tmp/slides_undo_test */
#define main slidesMain
#define sleep testSleep
#include "../apps/slides.c"
#undef main
#undef sleep
#include <assert.h>

unsigned int testSleep(unsigned int seconds) {
    (void)seconds;
    return 0;
}

static void edit(const char *keys) {
    int fds[2];
    assert(pipe(fds) == 0);
    assert(write(fds[1], keys, strlen(keys)) == (ssize_t)strlen(keys));
    assert(write(fds[1], "\005", 1) == 1);
    close(fds[1]);
    assert(dup2(fds[0], STDIN_FILENO) >= 0);
    close(fds[0]);
    enterEditMode();
}

static void resetCursor(void) {
    g_last_edit_row = 0;
    g_last_edit_col = 0;
}

int main(void) {
    int output = open("/dev/null", O_WRONLY);
    assert(output >= 0);
    assert(dup2(output, STDOUT_FILENO) >= 0);
    close(output);
    g_term_rows = 6;
    g_term_cols = 10;
    g_content_height = 4;
    g_content_width = 8;
    g_slide_count = 2;
    g_slides = calloc(2, sizeof(*g_slides));
    assert(g_slides);
    g_slides[0] = newBlankSlide();
    g_slides[1] = newBlankSlide();
    Slide *slide = g_slides[0];

    edit("abc\032");
    assert(memcmp(slide->lines[0], "ab      ", 8) == 0);
    assert(g_last_edit_col == 2);
    edit("\032"); /* History survives leaving edit mode. */
    assert(memcmp(slide->lines[0], "a       ", 8) == 0);
    edit("\032\032");
    assert(slide->undo_count == 0);
    assert(slide->lines[0][0] == ' ');

    memcpy(slide->lines[0], "abcdefgh", 8);
    resetCursor();
    edit(" \032"); /* Restore shifted cells and discarded rightmost cell. */
    assert(memcmp(slide->lines[0], "abcdefgh", 8) == 0);
    edit("\033[C\033[C\177\032");
    assert(memcmp(slide->lines[0], "abcdefgh", 8) == 0);
    assert(g_last_edit_col == 2);
    resetCursor();
    edit("a"); /* Identical overwrite does not consume history. */
    assert(slide->undo_count == 0);

    memcpy(slide->lines[1], "ABCDEFGH", 8);
    resetCursor();
    edit("\024\033[C\033[C\033[B\030\026\032\032");
    assert(memcmp(slide->lines[0], "abcdefgh", 8) == 0);
    assert(memcmp(slide->lines[1], "ABCDEFGH", 8) == 0);
    assert(g_clipboard->rows == 2 && g_clipboard->cols == 3);
    assert(memcmp(g_clipboard->data[0], "abc", 3) == 0);
    /* Paste at bottom-right clips the rectangle; undo restores overwritten cell. */
    g_last_edit_row = 3;
    g_last_edit_col = 7;
    slide->lines[3][7] = 'Q';
    edit("\026\032");
    assert(slide->lines[3][7] == 'Q');

    resetCursor();
    edit("Z");
    g_current_slide = 1;
    resetCursor();
    edit("Y\032");
    assert(g_slides[1]->lines[0][0] == ' ');
    assert(slide->lines[0][0] == 'Z');
    g_current_slide = 0;
    edit("\032");
    assert(slide->lines[0][0] == 'a');

    /* Save does not clear history. */
    char filename[] = "slides-undo-XXXXXX";
    int saved = mkstemp(filename);
    assert(saved >= 0);
    close(saved);
    g_filename = filename;
    resetCursor();
    edit("Z\023\032");
    assert(slide->lines[0][0] == 'a');
    unlink(filename);

    for (size_t i = 0; i < UNDO_MAX_STEPS + 10; i++) {
        UndoEntry *entry = beginUndo(slide, 0, 0, 1, 1, 0, 0);
        assert(entry);
        slide->lines[0][0] = (i % 2) ? 'a' : 'b';
        finishUndo(slide, entry);
    }
    assert(slide->undo_count == UNDO_MAX_STEPS);
    assert(slide->undo_bytes <= UNDO_MAX_BYTES);
    int row = 0;
    int col = 0;
    while (slide->undo_count)
        undoEdit(slide, &row, &col);
    assert(!slide->undo_oldest && !slide->undo_newest && !slide->undo_bytes);

    for (int i = 0; i < g_slide_count; i++)
        freeSlide(g_slides[i]);
    free(g_slides);
    for (int i = 0; i < g_clipboard->rows; i++)
        free(g_clipboard->data[i]);
    free(g_clipboard->data);
    free(g_clipboard);

    /* Large rectangular edits evict old entries at the byte limit. */
    g_content_height = 1024;
    g_content_width = 1024;
    Slide *large = newBlankSlide();
    for (int step = 0; step < 2; step++) {
        UndoEntry *entry = beginUndo(large, 0, 0, 1024, 1024, 0, 0);
        assert(entry);
        for (int r = 0; r < 1024; r++)
            memset(large->lines[r], step ? 'b' : 'a', 1024);
        finishUndo(large, entry);
    }
    assert(large->undo_count == 1);
    assert(large->undo_bytes <= UNDO_MAX_BYTES);
    undoEdit(large, &row, &col);
    assert(large->lines[0][0] == 'a' && large->lines[1023][1023] == 'a');
    freeSlide(large);
    return 0;
}
