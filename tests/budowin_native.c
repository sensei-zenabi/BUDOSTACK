#define main budowin_application_main
#include "../budo/BUDOWIN/src/main.c"
#undef main
#include <assert.h>

static void screenshot(const char *directory, const char *name)
{
    char path[MAX_PATH];
    static const unsigned char colors[16][3] = {
        {0,128,128}, {0,0,0}, {125,125,125}, {170,170,170},
        {255,255,255}, {194,194,194}, {0,0,162}, {0,0,255},
        {255,255,0}, {162,0,0}, {0,162,0}, {0,162,162},
        {162,0,162}, {162,81,0}, {227,227,227}, {255,255,255}
    };
    assert(join_path(path, sizeof(path), directory, name));
    draw_desktop(0);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n640 480\n255\n");
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        unsigned int color = framebuffer[i];
        unsigned int rgb = rgb_framebuffer[i];
        if (!rgb_mask[i]) {
            if (color < 16) rgb = (unsigned int)colors[color][0] << 16 |
                                  (unsigned int)colors[color][1] << 8 | colors[color][2];
            else rgb = 0;
        }
        fputc((rgb >> 16) & 255, file);
        fputc((rgb >> 8) & 255, file);
        fputc(rgb & 255, file);
    }
    assert(fclose(file) == 0);
}

static void click_native(BwaLoadedApp *app, int x, int y)
{
    active_window = app->definition.runtime_id;
    ui_pointer_x = x;
    ui_pointer_y = y;
    draw_desktop(0);
    assert(ui_hit(x, y, 1) >= 0);
    assert(!ui_button_event(x, y, 1, 0));
    assert(ui_button_event(x, y, 0, 1));
    /* Use the production dispatch with the physical button already released. */
    assert(bwa_dispatch_left_press(app, x, y, 0));
}
static void press_native(BwaLoadedApp *app, int x, int y)
{
    active_window = app->definition.runtime_id;
    draw_desktop(0);
    if (ui_button_event(x, y, 1, 0)) assert(bwa_dispatch_left_press(app, x, y, 1));
    if (ui_button_event(x, y, 0, 1)) assert(bwa_dispatch_left_press(app, x, y, 0));
    if (app->open && app->definition.callbacks.mouse_up) {
        bwa_callback_app = app;
        app->definition.callbacks.mouse_up(x, y, 0);
        bwa_callback_app = NULL;
    }
}
static int native_label(BwaLoadedApp *app, const char *label)
{
    draw_desktop(0);
    for (int i = 0; i < ui_region_count; ++i) {
        if (ui_regions[i].owner == app->definition.runtime_id && !strcmp(ui_regions[i].tip, label)) return 1;
    }
    return 0;
}
static int snake_head_x(void)
{
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            int p = y * SCREEN_WIDTH + x;
            if (rgb_mask[p] && (rgb_framebuffer[p] & 0xffffffU) == 0xb0e878) return x;
        }
    }
    return -1;
}
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    assert(chdir(getenv("BUDOWIN_TEST_DIR")) == 0);
    assert(bw_initialize(argc, argv));
    assert(chdir(argv[1]) == 0);
    assert(getcwd(home_path, sizeof(home_path)));
    bwa_load_external_apps();
    assert(bwa_external_app_count == 15);
    load_file_associations();
    char path[MAX_PATH];
    assert(join_path(path, sizeof(path), "/", "tmp") && strcmp(path, "/tmp") == 0);
    strcpy(current_path, "/tmp");
    assert(parent_path(path, sizeof(path)) && strcmp(path, "/") == 0);
    strcpy(editor_file_path, "/tmp");
    assert(editor_file_parent(path, sizeof(path)) && strcmp(path, "/") == 0);
    assert(load_directory(getenv("BUDOWIN_TEST_DIR")));
    assert(item_count >= 2);
    assert(explorer_path_is_same_or_child("/tmp/a/x", "/tmp/a"));
    assert(!explorer_path_is_same_or_child("/tmp/A/x", "/tmp/a"));
    assert(!explorer_path_is_same_or_child("/tmp/ab", "/tmp/a"));
    for (int i = 0; i < bwa_external_app_count; ++i) {
        BwaLoadedApp *app = &bwa_external_apps[i];
        if (!(app->definition.flags & BWA_FLAG_LAUNCHER)) app = bwa_new_instance(app);
        assert(app);
        bwa_callback_app = app;
        assert(app->definition.callbacks.open());
        app->open = 1;
        if (app->definition.callbacks.draw) app->definition.callbacks.draw();
        bwa_callback_app = NULL;
    }
    const char *games[] = {"snake", "invaders", "tictactoe"};
    for (int g = 0; g < 3; ++g) {
        BwaLoadedApp *game = NULL;
        for (int i = 0; i < bwa_instance_count; ++i)
            if (!strcmp(bwa_instances[i].definition.app_id, games[g])) game = &bwa_instances[i];
        assert(game && game->managed_window);
        bwa_callback_app = game;
        active_window = game->definition.runtime_id;
        int gx, gy, gw, gh;
        assert(bwa_window_get_client_rect(&gx, &gy, &gw, &gh));
        assert((game->definition.flags & BWA_FLAG_IDLE_TICK) && game->definition.tick);
        click_native(game, gx + 85, gy + 12); /* Resume via actual capture/release. */
        assert(native_label(game, "Pause"));
        if (!strcmp(games[g], "snake")) {
            int before = snake_head_x();
            assert(before >= 0);
            struct timespec delay = {0, 240000000};
            assert(nanosleep(&delay, NULL) == 0);
            assert(bwa_tick_external_apps()); /* No key/mouse/redraw triggers. */
            draw_desktop(0);
            assert(snake_head_x() > before);
        }
        click_native(game, gx + 155, gy + 12);
        assert(native_label(game, "Sound off"));
        click_native(game, gx + 225, gy + 12);
        assert(native_label(game, "OK"));
        click_native(game, gx + (gw - 340) / 2 + 150, gy + 65 + 122);
        assert(!native_label(game, "OK"));
        click_native(game, gx + 85, gy + 12);
        assert(native_label(game, "Resume"));
        click_native(game, gx + 25, gy + 12);
        assert(confirm_kind == BUDO_CONFIRM_OVERWRITE);
        desktop_confirm_result(BUDO_RESPONSE_CANCEL);
        bwa_callback_app = game;
        game->definition.callbacks.draw_icon(0, 0);
        if (argc == 3) {
            char name[64];
            snprintf(name, sizeof(name), "%s.ppm", games[g]);
            screenshot(argv[2], name);
        }
        bwa_callback_app = game;
        assert(game->definition.callbacks.key(0x100 | 59));
        game->definition.callbacks.draw();
        assert(game->definition.callbacks.key(27));
        click_native(game, gx + 295, gy + 12); /* Close also activates on release. */
        assert(!game->open);
        bwa_callback_app = NULL;
    }
    {
        BwaLoadedApp *cad = NULL;
        for (int i = 0; i < bwa_instance_count; ++i)
            if (!strcmp(bwa_instances[i].definition.app_id, "cad")) cad = &bwa_instances[i];
        assert(cad && cad->managed_window);
        int cad_assoc = find_file_association(".BCAD");
        assert(cad_assoc >= 0 && !strcmp(file_associations[cad_assoc].app_id, "cad"));
        bwa_callback_app = cad;
        active_window = cad->definition.runtime_id;
        int cx, cy, cw, ch;
        assert(bwa_window_get_client_rect(&cx, &cy, &cw, &ch));
        press_native(cad, cx + 20, cy + 112); /* Rectangle through native button capture. */
        press_native(cad, cx + 170, cy + 140);
        press_native(cad, cx + 350, cy + 300);
        bwa_callback_app = cad;
        assert(cad->definition.callbacks.key('c'));
        press_native(cad, cx + 260, cy + 200);
        press_native(cad, cx + 305, cy + 200);
        press_native(cad, cx + 20, cy + 156); /* Horizontal dimension, three clicks. */
        press_native(cad, cx + 170, cy + 300);
        press_native(cad, cx + 350, cy + 300);
        press_native(cad, cx + 350, cy + 335);
        bwa_callback_app = cad;
        assert(cad->definition.callbacks.key(27));
        if (argc == 3) screenshot(argv[2], "cad.ppm");
        bwa_callback_app = cad;
        assert(cad->definition.callbacks.key(13));
        if (argc == 3) screenshot(argv[2], "cad-properties.ppm");
        bwa_callback_app = cad;
        assert(cad->definition.callbacks.key(27));
        assert(cad->definition.callbacks.key(0x100 | 59));
        if (argc == 3) screenshot(argv[2], "cad-help.ppm");
        bwa_callback_app = cad;
        assert(cad->definition.callbacks.key(27));
        assert(!cad->definition.request_close());
        assert(confirm_kind == BUDO_CONFIRM_SAVE);
        desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    }
    BwaLoadedApp *flow = NULL;
    for (int i = 0; i < bwa_instance_count; ++i)
        if (!strcmp(bwa_instances[i].definition.app_id, "flow")) flow = &bwa_instances[i];
    assert(flow && flow->managed_window);
    int assoc = find_file_association(".FLOW");
    assert(assoc >= 0 && !strcmp(file_associations[assoc].app_id, "flow"));
    bwa_callback_app = flow;
    active_window = flow->definition.runtime_id;
    int fx, fy, fw, fh;
    assert(bwa_window_get_client_rect(&fx, &fy, &fw, &fh));
    click_native(flow, fx + 20, fy + 277); /* Templates through capture/release. */
    int overlay_x = fx + (fw - 380) / 2, overlay_y = fy + (fh - 264) / 2;
    click_native(flow, overlay_x + 30, overlay_y + 46);
    if (argc == 3) screenshot(argv[2], "flow.ppm");
    press_native(flow, fx + 110, fy + 10); /* Diagram menu. */
    press_native(flow, fx + 110, fy + 62); /* Properties. */
    draw_desktop(0);
    assert(input_caret_active);
    input_caret_epoch = bwa_get_time_ms();
    if (argc == 3) screenshot(argv[2], "flow-properties.ppm");
    bwa_callback_app = flow;
    assert(flow->definition.callbacks.key(27));
    draw_desktop(0);
    assert(!input_caret_active);
    bwa_callback_app = flow;
    assert(flow->definition.callbacks.key(0x100 | 59));
    if (argc == 3) screenshot(argv[2], "flow-help.ppm");
    bwa_callback_app = flow;
    assert(flow->definition.callbacks.key(27));
    assert(!flow->definition.request_close()); /* Dirty project cannot silently close. */
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    bwa_callback_app = NULL;
    BwaLoadedApp *spreadsheet = NULL;
    for (int i = 0; i < bwa_instance_count; ++i)
        if (!strcmp(bwa_instances[i].definition.app_id, "spreadsheet")) spreadsheet = &bwa_instances[i];
    assert(spreadsheet && spreadsheet->managed_window);
    int sheet_assoc = find_file_association(".BSHEET");
    assert(sheet_assoc >= 0 && !strcmp(file_associations[sheet_assoc].app_id, "spreadsheet"));
    bwa_callback_app = spreadsheet;
    active_window = spreadsheet->definition.runtime_id;
    const char *sheet_values[] = {"0", "1", "2", "3", "4"};
    for (int i = 0; i < 5; ++i) {
        assert(spreadsheet->definition.callbacks.key(sheet_values[i][0]));
        assert(spreadsheet->definition.callbacks.key(13));
    }
    assert(spreadsheet->definition.callbacks.key(0x100 | 71));
    for (int i = 0; i < 5; ++i) assert(spreadsheet->definition.callbacks.key(0x100 | 72));
    assert(spreadsheet->definition.callbacks.key(0x100 | 77));
    const char *equation = "=A1^2";
    for (const char *p = equation; *p; ++p) assert(spreadsheet->definition.callbacks.key(*p));
    assert(spreadsheet->definition.callbacks.key(13));
    int bx, by, bw, bh;
    assert(bwa_window_get_client_rect(&bx, &by, &bw, &bh));
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.mouse_down(bx + 130, by + 93, 1));
    assert(spreadsheet->definition.callbacks.mouse_move(bx + 130, by + 176, 1));
    assert(spreadsheet->definition.callbacks.mouse_up(bx + 130, by + 176, 0));
    assert(spreadsheet->definition.callbacks.key(4));
    assert(spreadsheet->definition.callbacks.mouse_down(bx + 40, by + 93, 1));
    assert(spreadsheet->definition.callbacks.mouse_move(bx + 38 + 88 + 4, by + 92 + 4 * 20 + 4, 1));
    assert(spreadsheet->definition.callbacks.mouse_up(bx + 38 + 88 + 4, by + 92 + 4 * 20 + 4, 0));
    if (argc == 3) screenshot(argv[2], "spreadsheet.ppm");
    press_native(spreadsheet, bx + 212, by + 10); /* Figure menu. */
    press_native(spreadsheet, bx + 220, by + 42); /* Numeric scatter chart. */
    if (argc == 3) screenshot(argv[2], "spreadsheet-scatter.ppm");
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.key(27));
    assert(!spreadsheet->definition.request_close());
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    bwa_callback_app = NULL;
    active_window = spreadsheet->definition.runtime_id;
    draw_desktop(0);
    int divider_x = bx+38+88, divider_y = by+84;
    assert(ui_cursor_kind(divider_x,divider_y) == BUDO_CURSOR_RESIZE_EW);
    assert(ui_hit(divider_x,divider_y,1) < 0);
    assert(ui_button_event(divider_x,divider_y,1,0));
    assert(bwa_dispatch_left_press(spreadsheet,divider_x,divider_y,1));
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.mouse_move(divider_x+42,divider_y,1));
    assert(spreadsheet->definition.callbacks.mouse_up(divider_x+42,divider_y,0));
    draw_desktop(0);
    assert(ui_cursor_kind(divider_x+42,divider_y) == BUDO_CURSOR_RESIZE_EW);
    divider_x = bx+14;
    divider_y = by+92+20;
    assert(ui_cursor_kind(divider_x,divider_y) == BUDO_CURSOR_RESIZE_NS);
    assert(ui_button_event(divider_x,divider_y,1,0));
    assert(bwa_dispatch_left_press(spreadsheet,divider_x,divider_y,1));
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.mouse_move(divider_x,divider_y+18,1));
    assert(spreadsheet->definition.callbacks.mouse_up(divider_x,divider_y+18,0));
    /* Alignment and renaming through the production menus after formulas exist. */
    press_native(spreadsheet,bx+110,by+10);
    press_native(spreadsheet,bx+110,by+22+2+8*16+8);
    press_native(spreadsheet,bx+160,by+10);
    press_native(spreadsheet,bx+160,by+48);
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.key(1));
    for (const char *p = "Results"; *p; ++p) assert(spreadsheet->definition.callbacks.key(*p));
    assert(spreadsheet->definition.callbacks.key(13));
    assert(native_label(spreadsheet,"Results"));
    if (argc == 3) screenshot(argv[2],"spreadsheet-resized.ppm");
    /* Every Cartesian grid direction has its own checked menu item. */
    for (int grid = 10; grid <= 12; ++grid) {
        press_native(spreadsheet,bx+212,by+10);
        press_native(spreadsheet,bx+220,by+22+2+grid*16+8);
    }
    for (int axis = 7; axis <= 8; ++axis) {
        press_native(spreadsheet,bx+212,by+10);
        press_native(spreadsheet,bx+220,by+22+2+axis*16+8);
        bwa_callback_app = spreadsheet;
        assert(spreadsheet->definition.callbacks.key(1));
        const char *label = axis == 7 ? "Time [s]" : "Voltage [V]";
        for (const char *p = label; *p; ++p) assert(spreadsheet->definition.callbacks.key(*p));
        assert(spreadsheet->definition.callbacks.key(13));
    }
    click_native(spreadsheet,bx+342,by+36); /* Toggle to saved figure. */
    if (argc == 3) screenshot(argv[2],"spreadsheet-axis-grids.ppm");
    bwa_callback_app = NULL;
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.key(14));
    assert(confirm_kind == BUDO_CONFIRM_SAVE);
    desktop_confirm_result(BUDO_RESPONSE_DISCARD);
    static const char *const training_table[8][3] = {
        {"Date","Weight","Bench"},
        {"2020-01-01","110.20","70.00"}, {"2020-02-01","110.00","82.00"},
        {"2020-03-01","109.00","88.00"}, {"2020-04-01","108.70","92.00"},
        {"2020-05-01","105.00","98.00"}, {"2020-06-01","106.20","102.00"},
        {"2020-07-01","106.00","108.00"}
    };
    bwa_callback_app = spreadsheet;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 3; ++c) {
            int x = bx+38+c*88+10, y = by+92+r*20+6;
            assert(spreadsheet->definition.callbacks.mouse_down(x,y,1));
            assert(spreadsheet->definition.callbacks.mouse_up(x,y,0));
            for (const char *p = training_table[r][c]; *p; ++p) assert(spreadsheet->definition.callbacks.key(*p));
            assert(spreadsheet->definition.callbacks.key(13));
        }
    }
    assert(spreadsheet->definition.callbacks.mouse_down(bx+42,by+96,1));
    assert(spreadsheet->definition.callbacks.mouse_move(bx+38+2*88+5,by+92+7*20+4,1));
    assert(spreadsheet->definition.callbacks.mouse_up(bx+38+2*88+5,by+92+7*20+4,0));
    if (argc == 3) screenshot(argv[2],"spreadsheet-training-table.ppm");
    press_native(spreadsheet,bx+212,by+10);
    press_native(spreadsheet,bx+220,by+42);
    if (argc == 3) screenshot(argv[2],"spreadsheet-date-both.ppm");
    press_native(spreadsheet,bx+212,by+10);
    press_native(spreadsheet,bx+220,by+256);
    assert(native_label(spreadsheet,"Reset auto"));
    if (argc == 3) screenshot(argv[2],"spreadsheet-axes-dialog.ppm");
    static const char *const axis_values[] = {"2020-01-01","2020-07-01","1mo","1w","60","120","20","5"};
    bwa_callback_app = spreadsheet;
    for (int i = 0; i < 8; ++i) {
        for (const char *p = axis_values[i]; *p; ++p) assert(spreadsheet->definition.callbacks.key(*p));
        if (i < 7) assert(spreadsheet->definition.callbacks.key(9));
    }
    assert(spreadsheet->definition.callbacks.key(13));
    if (argc == 3) screenshot(argv[2],"spreadsheet-manual-date-axes.ppm");
    press_native(spreadsheet,bx+212,by+10);
    press_native(spreadsheet,bx+220,by+240);
    assert(native_label(spreadsheet,"Apply"));
    if (argc == 3) screenshot(argv[2],"spreadsheet-data-series.ppm");
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.key('2'));
    assert(spreadsheet->definition.callbacks.key(13));
    if (argc == 3) screenshot(argv[2],"spreadsheet-date-bench.ppm");
    press_native(spreadsheet,bx+212,by+10);
    press_native(spreadsheet,bx+220,by+240);
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.key(0x100|77));
    assert(spreadsheet->definition.callbacks.key(13));
    if (argc == 3) screenshot(argv[2],"spreadsheet-weight-bench.ppm");
    /* Add/delete and undo/redo a tab through the production Sheet menu. */
    press_native(spreadsheet,bx+160,by+10);
    press_native(spreadsheet,bx+160,by+32);
    assert(native_label(spreadsheet,"Sheet2"));
    press_native(spreadsheet,bx+160,by+10);
    press_native(spreadsheet,bx+160,by+160);
    assert(!native_label(spreadsheet,"Sheet2") && native_label(spreadsheet,"Sheet1"));
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.key(26));
    assert(native_label(spreadsheet,"Sheet2"));
    bwa_callback_app = spreadsheet;
    assert(spreadsheet->definition.callbacks.key(25));
    assert(!native_label(spreadsheet,"Sheet2"));
    bwa_callback_app = NULL;
    BwaLoadedApp *settings = NULL;
    for (int i = 0; i < bwa_instance_count; ++i)
        if (!strcmp(bwa_instances[i].definition.app_id, "settings")) settings = &bwa_instances[i];
    assert(settings && settings->open);
    bwa_callback_app = settings;
    active_window = settings->definition.runtime_id;
    int sx, sy, sw, sh;
    assert(bwa_window_get_rect(&sx, &sy, &sw, &sh));
    press_native(settings, sx + 140, sy + 55);
    press_native(settings, sx + 30, sy + sh - 36);
    draw_desktop(0);
    assert(input_caret_active);
    bwa_callback_app = settings;
    assert(settings->definition.callbacks.key(27));
    draw_desktop(0);
    assert(!input_caret_active);
    bwa_callback_app = NULL;
    BwaLoadedApp *chess = NULL;
    for (int i = 0; i < bwa_instance_count; ++i)
        if (!strcmp(bwa_instances[i].definition.app_id, "chess")) chess = &bwa_instances[i];
    assert(chess && chess->managed_window);
    bwa_callback_app = chess;
    active_window = chess->definition.runtime_id;
    assert(chess->definition.callbacks.key(13)); /* Select e2 with the cursor. */
    assert(chess->definition.callbacks.key(0x100 | 72));
    assert(chess->definition.callbacks.key(0x100 | 72));
    assert(chess->definition.callbacks.key(13)); /* e2-e4. */
    if (argc == 3) screenshot(argv[2], "chess.ppm");
    bwa_callback_app = chess;
    assert(chess->definition.callbacks.key(0x100 | 59));
    if (argc == 3) screenshot(argv[2], "chess-help.ppm");
    bwa_callback_app = chess;
    assert(chess->definition.callbacks.key(27));
    assert(chess->definition.callbacks.key(26));
    assert(bwa_window_close());
    bwa_callback_app = NULL;
    BwaLoadedApp *paint = NULL;
    for (int i = 0; i < bwa_instance_count; ++i)
        if (!strcmp(bwa_instances[i].definition.app_id, "paint")) paint = &bwa_instances[i];
    assert(paint && paint->managed_window);
    active_window = paint->definition.runtime_id;
    bwa_callback_app = paint;
    int px, py, pw, ph;
    assert(bwa_window_get_client_rect(&px, &py, &pw, &ph));
    press_native(paint, px + 20, py + 174); /* Stroke-width text input. */
    draw_desktop(0);
    assert(input_caret_active);
    bwa_callback_app = paint;
    assert(paint->definition.callbacks.key(27));
    press_native(paint, px + 140, py + 10); /* Image menu. */
    press_native(paint, px + 140, py + 30); /* Image-size text fields. */
    draw_desktop(0);
    assert(input_caret_active);
    bwa_callback_app = paint;
    assert(paint->definition.callbacks.key(27));
    bwa_callback_app = paint;
    active_window = paint->definition.runtime_id;
    assert(paint->definition.callbacks.key(19));
    assert(editor_file_dialog == EDITOR_FILE_DIALOG_SAVE_AS);
    assert(editor_file_accept());
    bwa_callback_app = paint; /* Ctrl+S writes PCX. */
    unsigned char data[200000];
    unsigned int size;
    assert(bwa_file_read_all("PAINT.PCX", data, sizeof(data), &size));
    assert(size > 128 && data[0] == 10 && data[3] == 8);
    unsigned char before[200000];
    memcpy(before, data, size);
    unsigned int before_size = size;
    assert(paint->definition.callbacks.mouse_down(200, 150, 1));
    assert(paint->definition.callbacks.mouse_move(205, 150, 1));
    assert(paint->definition.callbacks.mouse_up(205, 150, 0));
    assert(!bwa_window_close());
    assert(confirm_kind == BUDO_CONFIRM_SAVE && paint->open);
    desktop_confirm_result(BUDO_RESPONSE_CANCEL);
    assert(!confirm_kind && paint->open);
    bwa_callback_app = paint;
    assert(paint->definition.callbacks.key(19));
    assert(bwa_file_read_all("PAINT.PCX", data, sizeof(data), &size));
    assert(size != before_size || memcmp(data, before, size) != 0);
    assert(paint->definition.callbacks.key(26)); /* Undo the stroke. */
    assert(paint->definition.callbacks.key(19));
    assert(bwa_file_read_all("PAINT.PCX", data, sizeof(data), &size));
    assert(size == before_size && memcmp(data, before, size) == 0);
    assert(paint->definition.callbacks.open_file("PAINT.PCX"));
    /* Render the native Paint View menu: grid check mark follows the toggle. */
    int cx,cy,cw,ch;
    assert(bwa_window_get_client_rect(&cx,&cy,&cw,&ch));
    for (int pass = 0; pass < 3; ++pass) {
        assert(paint->definition.callbacks.mouse_down(cx+90,cy+8,1));
        bwa_callback_app = NULL;
        draw_desktop(0);
        int marks = 0;
        for (int py = 0; py < 7; ++py)
            for (int px = 0; px < 5; ++px)
                if (framebuffer[(cy+22+6*16+4+py)*SCREEN_WIDTH+cx+84+5+px] == TEXT_COLOR) ++marks;
        assert(pass == 1 ? marks == 0 : marks > 0);
        bwa_callback_app = paint;
        assert(paint->definition.callbacks.mouse_down(cx+108,cy+22+6*16+8,1));
    }
    assert(paint->definition.callbacks.key(27));
    bwa_callback_app = NULL;
    assert(bwa_file_write_all("exact.bin", (const unsigned char *)"abc", 3));
    assert(bwa_file_read_all("exact.bin", data, 3, &size) && size == 3);
    assert(builtin_new_instance(BWA_HOST_APP_EDITOR));
    editor_reset_document();
    editor_insert_char('B'); editor_insert_char('U'); editor_insert_char('D');
    char document[MAX_PATH];
    assert(join_path(document, sizeof(document), getenv("BUDOWIN_TEST_DIR"), "document.txt"));
    assert(editor_save_file(document));
    editor_reset_document();
    assert(editor_load_file(document) && strcmp(editor_lines[0], "BUD") == 0);
    strcpy(terminal_cwd, getenv("BUDOWIN_TEST_DIR"));
    terminal_run_external("printf native-terminal-ok");
    int found = 0;
    for (int i = 0; i < terminal_line_count; ++i)
        if (strstr(terminal_lines[i], "native-terminal-ok")) found = 1;
    assert(found);
    char source[MAX_PATH], copy[MAX_PATH], link[MAX_PATH];
    assert(join_path(source, sizeof(source), getenv("BUDOWIN_TEST_DIR"), "folder"));
    assert(join_path(copy, sizeof(copy), getenv("BUDOWIN_TEST_DIR"), "folder-copy"));
    assert(explorer_copy_path(source, copy, TYPE_FOLDER));
    assert(explorer_delete_path(copy, TYPE_FOLDER));
    assert(join_path(link, sizeof(link), getenv("BUDOWIN_TEST_DIR"), "link"));
    assert(symlink(source, link) == 0);
    assert(!explorer_copy_path(link, copy, TYPE_FOLDER));
    assert(explorer_delete_path(link, TYPE_FOLDER));
    assert(access(source, F_OK) == 0);
    if (argc == 3) {
        int cx, cy, cw, ch;
        paint->window.w = 500;
        paint->window.h = 330;
        editor_window.open = terminal_window.open = explorer_window.open = 0;
        active_window = paint->definition.runtime_id;
        bwa_callback_app = paint;
        assert(bwa_window_get_client_rect(&cx, &cy, &cw, &ch));
        assert(paint->definition.callbacks.mouse_down(cx + 8, cy + 6, 1));
        bwa_callback_app = NULL;
        screenshot(argv[2], "paint-menu.ppm");
        bwa_callback_app = paint;
        assert(paint->definition.callbacks.key(27));
        bwa_callback_app = NULL;
        screenshot(argv[2], "paint-scrollbars.ppm");
        BwaLoadedApp *settings = NULL;
        for (int i = 0; i < bwa_instance_count; ++i)
            if (!strcmp(bwa_instances[i].definition.app_id, "settings")) settings = &bwa_instances[i];
        assert(settings);
        active_window = settings->definition.runtime_id;
        screenshot(argv[2], "settings-scrollbar.ppm");
        assert(load_directory(bw_user_directory()));
        open_explorer();
        active_window = APP_EXPLORER;
        explorer_file_menu = 1;
        screenshot(argv[2], "explorer-file-menu.ppm");
        explorer_file_menu = 0;
        open_terminal();
        terminal_file_menu = 1;
        screenshot(argv[2], "terminal-file-menu.ppm");
        terminal_file_menu = 0;
    }
    puts("BUDOWIN native app, filesystem, editor, paint and terminal checks passed.");
    return 0;
}
