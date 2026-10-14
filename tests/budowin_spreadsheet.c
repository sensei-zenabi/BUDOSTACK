#include "../budo/BUDOWIN/appsrc/spreadsheet.c"
#include <assert.h>

static int client_w = 612, client_h = 424, closes, dialogs, confirmations;
static unsigned int modifiers;
static const char *capture_text;
static int captured_x, captured_y;
static int grid_h_major, grid_v_major, grid_h_minor, grid_v_minor;
static void count_grid_line(void *context,int x,int y,int xx,int yy,unsigned int color_value)
{
    (void)context;
    if (color_value == 0xc8c8c8) { if (y == yy) ++grid_h_major; if (x == xx) ++grid_v_major; }
    if (color_value == 0xe8e8e8) { if (y == yy) ++grid_h_minor; if (x == xx) ++grid_v_minor; }
}
static int client(int *x,int *y,int *w,int *h)
{
    *x=0;
    *y=0;
    *w=client_w;
    *h=client_h;
    return 1;
}
static int create(int x,int y,int w,int h,const char *s,unsigned int flags)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)s;
    (void)flags;
    return 1;
}
static int minimum(int w,int h)
{
    assert(w == 460 && h == 320);
    return 1;
}
static int title(const char *s)
{
    assert(s);
    return 1;
}
static int window_close(void)
{
    ++closes;
    return 1;
}
static void bounds(int x,int y,int w,int h)
{
    if (!(x>=0 && y>=0 && w>0 && h>0 && x+w<=client_w && y+h<=client_h))
        fprintf(stderr,"Out of bounds: %d %d %d %d in %d %d\n",x,y,w,h,client_w,client_h);
    assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=client_w && y+h<=client_h);
}
static void fill(int x,int y,int w,int h,unsigned char color)
{
    (void)color;
    bounds(x,y,w,h);
}
static void rgb(int x,int y,int w,int h,unsigned int color)
{
    (void)color;
    bounds(x,y,w,h);
}
static void text(int x,int y,const char *s,unsigned char color,int chars)
{
    (void)color;
    if (capture_text && !strcmp(s,capture_text)) { captured_x = x; captured_y = y; }
    assert(s && chars>0);
    bounds(x,y,1,8);
}
static void button(int x,int y,int w,int h,const char *s,int state)
{
    (void)s;
    (void)state;
    bounds(x,y,w,h);
}
static void button_state(int x,int y,int w,int h,const char *s,unsigned int state)
{
    button(x,y,w,h,s,(int)state);
}
static unsigned char color(int role)
{
    return (unsigned char)role;
}
static int metric(int role)
{
    return role == BUDO_SYS_METRIC_STATUS_HEIGHT ? 16 : 18;
}
static int inside(int x,int y,int bx,int by,int w,int h)
{
    return x>=bx && y>=by && x<bx+w && y<by+h;
}
static unsigned int mods(void)
{
    return modifiers;
}
static unsigned long long clock_now(void)
{
    return 1000;
}
static void region(int x,int y,int w,int h,int cursor,const char *tip)
{
    (void)cursor;
    (void)tip;
    bounds(x,y,w,h);
}
static int dialog(int mode,const char *path)
{
    assert(mode && path);
    ++dialogs;
    return 1;
}
static int confirm(int kind,const char *title_text,const char *message)
{
    assert(kind && title_text && message);
    ++confirmations;
    return 1;
}
static void caret(int x,int y,int h,unsigned char c)
{
    fill(x,y,1,h,c);
}
static void put(int s,int r,int c,const char *raw)
{
    assert(sheet_set(book,s,r,c,raw,0));
    sheet_calc_reset(calculation,book);
}
static void formula(const char *raw,double expected)
{
    put(0,20,20,raw);
    SheetValue v = sheet_eval(calculation,0,20,20);
    if (v.type != SV_NUMBER || fabs(v.number-expected)>1e-9*fmax(1,fabs(expected)))
        fprintf(stderr,"%s -> type=%d error=%d number=%.17g expected=%.17g\n",raw,v.type,v.error,v.number,expected);
    assert(v.type == SV_NUMBER && fabs(v.number-expected)<=1e-9*fmax(1,fabs(expected)));
}
static void error_formula(const char *raw,int error)
{
    put(0,20,20,raw);
    SheetValue v = sheet_eval(calculation,0,20,20);
    assert(v.type == SV_ERROR && v.error == error);
}
static void path_name(char *out,const char *root,const char *name)
{
    assert(snprintf(out,4096,"%s/%s",root,name)<4096);
}
static void test_layout_changes(const char *root)
{
    sheet_new(book);
    active_sheet = row = col = anchor_row = anchor_col = 0;
    sheet_hscroll.position = sheet_vscroll.position = 0;
    menu = -1;
    chart_view = editing = help_view = 0;
    sheet_history_reset();
    SheetLayout l;
    assert(sheet_layout(&l));
    int edge = l.gx+l.xs[1];
    assert(sheet_mouse_down(edge,l.gy-8,1) && resize_axis == 1);
    assert(sheet_mouse_move(edge+52,l.gy-8,1));
    assert(sheet_width(book,0,0) == 140);
    assert(sheet_mouse_up(edge+52,l.gy-8,0));
    assert(!resize_axis && history_at == 1);
    sheet_undo(-1);
    assert(sheet_width(book,0,0) == 88);
    sheet_undo(1);
    assert(sheet_width(book,0,0) == 140);
    assert(sheet_layout(&l));
    edge = l.gy+l.ys[1];
    assert(sheet_mouse_down(l.x+12,edge,1) && resize_axis == 2);
    assert(sheet_mouse_move(l.x+12,edge+40,1));
    assert(sheet_mouse_up(l.x+12,edge+40,0));
    assert(sheet_height(book,0,0) == 60);
    assert(sheet_layout(&l));
    edge = l.gx+l.xs[1];
    assert(sheet_mouse_down(edge,l.gy-8,1));
    assert(sheet_mouse_move(edge+100,l.gy-8,1));
    assert(sheet_key_input(27));
    assert(sheet_width(book,0,0) == 140 && !resize_axis);
    sheet_begin_edit(5,0);
    sheet_string(edit_buffer,sizeof(edit_buffer),"160");
    sheet_edit_commit();
    assert(!editing && sheet_width(book,0,0) == 160);
    sheet_begin_edit(6,0);
    sheet_string(edit_buffer,sizeof(edit_buffer),"10");
    sheet_edit_commit();
    assert(editing && sheet_height(book,0,0) == 60);
    assert(sheet_key_input(27));
    put(0,0,0,"Align");
    capture_text = "Align";
    for (int horizontal = 0; horizontal <= 3; ++horizontal) {
        for (int vertical = 0; vertical <= 2; ++vertical) {
            sheet_mutate(20+horizontal);
            sheet_mutate(24+vertical);
            assert(sheet_layout(&l));
            sheet_draw();
            int x = horizontal == 3 ? l.gx+160-4-30 : horizontal == 2 ? l.gx+(160-30)/2 : l.gx+4;
            int y = vertical == 1 ? l.gy+3 : vertical == 2 ? l.gy+60-11 : l.gy+(60-8)/2;
            if (captured_x != x || captured_y != y) fprintf(stderr,"Alignment %d,%d: got %d,%d expected %d,%d size=%d,%d row=%d col=%d\n",horizontal,vertical,captured_x,captured_y,x,y,sheet_width(book,0,0),sheet_height(book,0,0),row,col);
            assert(captured_x == x && captured_y == y);
        }
    }
    capture_text = NULL;
    row = anchor_row = 1;
    col = anchor_col = 1;
    sheet_mutate(22);
    sheet_mutate(26);
    const SheetCell *blank = sheet_cell(book,0,1,1);
    assert(blank && blank->align_h == 2 && blank->align_v == 2 && !*blank->raw);
    row = anchor_row = col = anchor_col = 0;
    *staging = *book;
    assert(sheet_copy_range(staging,book,selection(),0,2,2));
    const SheetCell *copied = sheet_cell(staging,0,2,2);
    assert(copied && copied->align_h == 3 && copied->align_v == 2);
    assert(sheet_structure(staging,book,0,0,0,1));
    assert(staging->heights[0][1] == 60 && staging->heights[0][0] == 0);
    assert(sheet_cell(staging,0,1,0)->align_h == 3);
    assert(sheet_structure(staging,book,0,1,0,-1));
    assert(staging->widths[0][0] == 0);
    /* Variable geometry must be used by hit testing, autofill and navigation. */
    assert(sheet_layout(&l));
    int rr,cc;
    assert(sheet_hit_cell(&l,l.gx+159,l.gy+59,&rr,&cc) && rr == 0 && cc == 0);
    assert(sheet_hit_cell(&l,l.gx+161,l.gy+61,&rr,&cc) && rr == 1 && cc == 1);
    book->heights[0][1] = 512;
    row = 10;
    col = 100;
    sheet_reveal();
    assert(sheet_layout(&l) && row >= first_row && row < first_row+l.rows && col >= first_col && col < first_col+l.cols);
    book->heights[0][1] = 0;
    sheet_hscroll.position = sheet_vscroll.position = 0;
    row = anchor_row = col = anchor_col = 0;
    put(0,0,0,"1");
    put(0,1,0,"2");
    anchor_row = 1;
    assert(sheet_layout(&l));
    int hx = l.gx+l.xs[1]-2, hy = l.gy+l.ys[2]-2;
    assert(sheet_mouse_down(hx,hy,1) && fill_drag);
    assert(sheet_mouse_move(l.gx+30,l.gy+l.ys[4]+5,1));
    assert(sheet_mouse_up(l.gx+30,l.gy+l.ys[4]+5,0));
    assert(!strcmp(sheet_cell(book,0,4,0)->raw,"5"));
    /* Formula-safe rename across tabs, quoted names, ranges and text literals. */
    book->tabs = 2;
    sheet_string(book->names[1],32,"Data");
    put(0,0,1,"=A1*2");
    put(1,0,0,"=Sheet1!$A$1+SUM('Sheet1'!A1:A2)");
    put(1,0,1,"=\"Sheet1!A1\"&'Data'!A1");
    sheet_history_reset();
    active_sheet = 0;
    sheet_begin_edit(1,0);
    sheet_string(edit_buffer,sizeof(edit_buffer),"Test's data");
    sheet_edit_commit();
    assert(!editing && !strcmp(book->names[0],"Test's data"));
    assert(!strcmp(sheet_cell(book,0,0,1)->raw,"=A1*2"));
    assert(!strcmp(sheet_cell(book,1,0,0)->raw,"='Test''s data'!$A$1+SUM('Test''s data'!A1:A2)"));
    assert(!strcmp(sheet_cell(book,1,0,1)->raw,"=\"Sheet1!A1\"&'Data'!A1"));
    SheetValue value = sheet_eval(calculation,1,0,0);
    assert(value.type == SV_NUMBER && value.number == 4);
    sheet_undo(-1);
    assert(!strcmp(book->names[0],"Sheet1") && !strcmp(sheet_cell(book,1,0,0)->raw,"=Sheet1!$A$1+SUM('Sheet1'!A1:A2)"));
    sheet_undo(1);
    assert(!strcmp(book->names[0],"Test's data"));
    *clipboard = *book;
    assert(!sheet_rename(staging,book,0,"dAtA") && !memcmp(clipboard,book,sizeof(*book)));
    char long_formula[SHEET_TEXT];
    sheet_string(long_formula,sizeof(long_formula),"='Test''s data'!A1&\"");
    size_t start = strlen(long_formula);
    memset(long_formula+start,'x',SHEET_TEXT-start-3);
    long_formula[SHEET_TEXT-3] = '"';
    long_formula[SHEET_TEXT-2] = 0;
    put(1,2,0,long_formula);
    *clipboard = *book;
    assert(!sheet_rename(staging,book,0,"An extremely long renamed sheet") && !memcmp(clipboard,book,sizeof(*book)));
    put(1,2,0,"");
    /* Grid switches are independent, undoable and persistent. */
    row = col = anchor_col = 0;
    anchor_row = 4;
    active_sheet = 0;
    sheet_chart_create(1);
    SheetChart *chart = &book->charts[0];
    sheet_string(chart->title,sizeof(chart->title),"Grid and axis test");
    sheet_string(chart->xlabel,sizeof(chart->xlabel),"Time [s]");
    sheet_string(chart->ylabel,sizeof(chart->ylabel),"Voltage [V]");
    unsigned int previous = chart->grids;
    for (int i = 0; i < 4; ++i) {
        sheet_menu_action(4,9+i);
        assert(chart->grids == (previous^(1U<<i)));
        previous = chart->grids;
    }
    sheet_undo(-1);
    assert(chart->grids == (previous^SHEET_GRID_V_MINOR));
    sheet_undo(1);
    for (unsigned int flags = 0; flags < 16; ++flags) {
        chart->grids = flags;
        grid_h_major = grid_v_major = grid_h_minor = grid_v_minor = 0;
        SheetCanvas canvas = {.line=count_grid_line};
        chart_grids(&canvas,chart,0,0,400,200);
        assert(grid_h_major == ((flags&SHEET_GRID_H_MAJOR) ? 3 : 0));
        assert(grid_v_major == ((flags&SHEET_GRID_V_MAJOR) ? 3 : 0));
        assert(grid_h_minor == ((flags&SHEET_GRID_H_MINOR) ? 16 : 0));
        assert(grid_v_minor == ((flags&SHEET_GRID_V_MINOR) ? 16 : 0));
    }
    char path[4096];
    path_name(path,root,"layout-v2.bsheet");
    assert(sheet_write(book,path));
    assert(sheet_read(staging,path) && !memcmp(staging,book,sizeof(*book)));
    path_name(path,root,"legacy-v1.bsheet");
    FILE *file = fopen(path,"wb");
    assert(file);
    fputs("BUDOSHEET 1\n1 2\n536865657431\n0 0 0 0 0 0 0\n\n\n\n0 0\n37\n1 0\n3d41312a32\n",file);
    assert(!fclose(file));
    assert(sheet_read(staging,path) && sheet_width(staging,0,0) == 88 && sheet_height(staging,0,0) == 20);
    assert(staging->charts[0].grids == SHEET_GRID_H_MAJOR && staging->cells[0].align_h == 0);
    SheetCalc *legacy_calc = calloc(1,sizeof(*legacy_calc));
    assert(legacy_calc);
    sheet_calc_reset(legacy_calc,staging);
    value = sheet_eval(legacy_calc,0,0,1);
    assert(value.type == SV_NUMBER && value.number == 14);
    free(legacy_calc);
    /* Export all types for an independent Python PCX and SVG check. */
    for (int type = 0; type < SHEET_CHART_TYPES; ++type) {
        chart->type = type;
        snprintf(path,sizeof(path),"%s/figure-%d.pcx",root,type);
        assert(sheet_chart_export_pcx(calculation,chart,path));
    }
    chart->type = 1;
    sheet_menu_action(0,7);
    assert(dialog_kind == SD_PCX);
    path_name(path,root,"figure-ui.pcx");
    assert(sheet_file_selected(BUDO_FILE_SAVE_AS,path) && !dialog_kind);
    for (unsigned int flags = 0; flags < 16; ++flags) {
        chart->grids = flags;
        snprintf(path,sizeof(path),"%s/grid-%u.svg",root,flags);
        assert(sheet_chart_export(calculation,chart,path));
    }
    chart_view = 0;
    for (int width = 452; width <= 900; width += 112) {
        client_w = width;
        for (int size = 16; size <= 512; size += 31) {
            book->widths[0][0] = (unsigned short)size;
            book->heights[0][0] = (unsigned short)size;
            sheet_draw();
        }
        chart_view = 1;
        sheet_draw();
        chart_view = 0;
    }
    client_w = 612;
    chart_view = 0;
    path_name(path,root,"/missing/figure.pcx");
    assert(!sheet_chart_export_pcx(calculation,chart,path));
}

static void test_date_figures(const char *root)
{
    /* Exact table from the user's attachment, including the leap-year month. */
    static const char *const dates[] = {"2020-01-01","2020-02-01","2020-03-01","2020-04-01","2020-05-01","2020-06-01","2020-07-01"};
    static const char *const weights[] = {"110.20","110.00","109.00","108.70","105.00","106.20","106.00"};
    static const char *const benches[] = {"70.00","82.00","88.00","92.00","98.00","102.00","108.00"};
    char legacy_path[4096];
    path_name(legacy_path,root,"legacy-v2.bsheet");
    FILE *legacy = fopen(legacy_path,"wb");
    assert(legacy);
    fputs("BUDOSHEET 2\n1 2\n4f6c64\n1 0 0 0 0 0 1\n\n\n\n1\n",legacy);
    unsigned short old_sizes[SHEET_ROWS] = {0};
    old_sizes[0] = 120;
    sheet_sizes_write(legacy,old_sizes,SHEET_COLS);
    old_sizes[0] = 32;
    sheet_sizes_write(legacy,old_sizes,SHEET_ROWS);
    fputs("0 0 0 0\n32\n1 0 0 0\n34\n",legacy);
    assert(!fclose(legacy));
    assert(sheet_read(staging,legacy_path));
    assert(!staging->charts[0].configured && sheet_width(staging,0,0) == 120 && sheet_height(staging,0,0) == 32);
    SheetCalc *old_calc = calloc(1,sizeof(*old_calc));
    assert(old_calc);
    sheet_calc_reset(old_calc,staging);
    double old_x,old_y;
    assert(chart_point(old_calc,&staging->charts[0],0,0,&old_x,&old_y) && old_x == 0 && old_y == 4);
    free(old_calc);
    sheet_new(book);
    filename[0] = 0;
    active_sheet = row = col = anchor_row = anchor_col = 0;
    sheet_hscroll.position = sheet_vscroll.position = 0;
    menu = -1;
    editing = data_choosing = chart_view = help_view = 0;
    put(0,0,0,"Date");
    put(0,0,1,"Weight");
    put(0,0,2,"Bench");
    for (int i = 0; i < 7; ++i) {
        put(0,i+1,0,dates[i]);
        put(0,i+1,1,weights[i]);
        put(0,i+1,2,benches[i]);
    }
    sheet_history_reset();
    double serial;
    assert(sheet_iso_date("2020-01-01",&serial) && serial == 43831);
    assert(sheet_iso_date("2020-02-29",&serial) && serial == 43890);
    assert(!sheet_iso_date("2020-02-30",&serial));
    assert(!sheet_iso_date("1900-02-29",&serial));
    assert(!sheet_iso_date("2020-1-01",&serial));
    SheetValue date = sheet_eval(calculation,0,1,0);
    char displayed[32];
    sheet_display(date,0,displayed,sizeof(displayed));
    assert(date.type == SV_NUMBER && date.date && !strcmp(displayed,"2020-01-01"));
    formula("=YEAR(A2)",2020);
    formula("=MONTH(A3)",2);
    formula("=DAY(A4)",1);
    put(0,20,20,"");
    anchor_row = 7;
    anchor_col = 2;
    /* Both series vs date: line and scatter use the actual elapsed days. */
    sheet_chart_create(0);
    SheetChart *chart = &book->charts[0];
    assert(chart->configured && chart->header && chart->x_col == 0 && chart->y_count == 2);
    assert(chart_date_axis(calculation,chart));
    assert(!strcmp(chart->xlabel,"Date") && !strcmp(chart->ylabel,"Value"));
    double x,y;
    assert(!chart_point(calculation,chart,0,0,&x,&y));
    assert(chart_point(calculation,chart,1,0,&x,&y) && x == 43831 && fabs(y-110.2) < 1e-9);
    assert(chart_point(calculation,chart,1,1,&x,&y) && x == 43831 && y == 70);
    assert(chart_point(calculation,chart,2,0,&x,&y) && x == 43862 && y == 110);
    assert(chart_point(calculation,chart,3,0,&x,&y) && x == 43891 && y == 109);
    assert(chart_point(calculation,chart,7,1,&x,&y) && x == 44013 && y == 108);
    char path[4096];
    path_name(path,root,"dates-both-line.svg");
    assert(sheet_chart_export(calculation,chart,path));
    put(0,2,0,"N/A");
    assert(!chart_point(calculation,chart,2,0,&x,&y));
    path_name(path,root,"dates-missing-line.svg");
    assert(sheet_chart_export(calculation,chart,path));
    put(0,2,0,"2020-02-01");
    sheet_chart_create(1);
    path_name(path,root,"dates-both-scatter.svg");
    assert(sheet_chart_export(calculation,chart,path));
    /* Either series vs date without moving or duplicating spreadsheet columns. */
    sheet_data_open();
    assert(data_choosing);
    sheet_data_toggle(2);
    sheet_data_apply();
    assert(!data_choosing && chart->y_count == 1 && chart->y_cols[0] == 1 && !strcmp(chart->ylabel,"Weight"));
    path_name(path,root,"dates-weight.svg");
    assert(sheet_chart_export(calculation,chart,path));
    sheet_data_open();
    assert(sheet_data_key('2'));
    assert(sheet_data_key('3'));
    assert(sheet_data_key(13));
    assert(chart->x_col == 0 && chart->y_count == 1 && chart->y_cols[0] == 2 && !strcmp(chart->ylabel,"Bench"));
    path_name(path,root,"dates-bench.svg");
    assert(sheet_chart_export(calculation,chart,path));
    path_name(path,root,"dates-bench.pcx");
    assert(sheet_chart_export_pcx(calculation,chart,path));
    *clipboard = *book;
    sheet_data_open();
    sheet_data_toggle(2);
    sheet_data_apply();
    assert(data_choosing && !memcmp(book,clipboard,sizeof(*book)));
    assert(sheet_data_key(27) && !data_choosing && !memcmp(book,clipboard,sizeof(*book)));
    sheet_data_open();
    sheet_data_x(1);
    sheet_data_apply();
    assert(chart->x_col == 1 && chart->y_count == 1 && chart->y_cols[0] == 2);
    assert(!chart_date_axis(calculation,chart) && !strcmp(chart->xlabel,"Weight") && !strcmp(chart->ylabel,"Bench"));
    assert(chart_point(calculation,chart,1,0,&x,&y) && fabs(x-110.2) < 1e-9 && y == 70);
    path_name(path,root,"weight-bench.svg");
    assert(sheet_chart_export(calculation,chart,path));
    sheet_undo(-1);
    assert(chart->x_col == 0 && chart_date_axis(calculation,chart));
    sheet_undo(1);
    assert(chart->x_col == 1);
    /* Numeric serials with date formatting and dates produced by formulas. */
    put(0,1,0,"=DATE(2020,1,1)");
    date = sheet_eval(calculation,0,1,0);
    assert(date.date && date.number == 43831);
    put(0,2,0,"43862");
    assert(sheet_set(book,0,2,0,"43862",5));
    sheet_calc_reset(calculation,book);
    chart->x_col = 0;
    chart->x_mode = SHEET_X_DATE;
    assert(chart_date_axis(calculation,chart));
    assert(chart_point(calculation,chart,2,0,&x,&y) && x == 43862);
    put(0,2,0,"'2020-02-01");
    assert(chart_point(calculation,chart,2,0,&x,&y) && x == 43862);
    put(0,2,0,"not a date");
    assert(!chart_point(calculation,chart,2,0,&x,&y));
    put(0,2,0,"2020-02-01");
    path_name(path,root,"data-series-v3.bsheet");
    assert(sheet_write(book,path) && sheet_read(staging,path) && !memcmp(book,staging,sizeof(*book)));
    assert(staging->charts[0].x_col == 0 && staging->charts[0].y_cols[0] == 2);
    assert(sheet_structure(staging,book,0,1,1,1));
    assert(staging->charts[0].x_col == 0 && staging->charts[0].y_cols[0] == 3);
    assert(sheet_structure(staging,book,0,1,1,-1));
    assert(staging->charts[0].enabled && staging->charts[0].y_cols[0] == 1);
    assert(sheet_structure(staging,book,0,1,0,-1));
    assert(!staging->charts[0].enabled && !staging->charts[0].configured);
    path_name(path,root,"deleted-x-v3.bsheet");
    assert(sheet_write(staging,path));
    /* All chooser controls, clipping, cancellation and wide-table pagination. */
    for (int width = 452; width <= 900; width += 112) {
        client_w = width;
        client_h = 294;
        sheet_data_open();
        sheet_draw();
        SheetLayout l;
        assert(sheet_layout(&l));
        SheetDataLayout d = sheet_data_layout(&l);
        assert(sheet_data_mouse(&l,d.x+d.w-40,d.y+36));
        assert(sheet_data_mouse(&l,d.x+40,d.y+60));
        assert(sheet_data_mouse(&l,d.x+40,d.y+88));
        sheet_draw();
        assert(sheet_data_mouse(&l,d.x+d.w-64,d.y+d.h-20));
        assert(!data_choosing);
    }
    client_w = 612;
    client_h = 424;
    chart->source.c1 = SHEET_COLS-1;
    sheet_data_open();
    for (int i = 0; i < 40; ++i) assert(sheet_data_key(0x100|80));
    sheet_draw();
    assert(data_offset > 0 && data_draft.source.c0+data_offset <= data_draft.source.c1);
    assert(sheet_data_key(27));
    chart->source.c1 = 2;
    chart_view = 0;
    /* Selecting B:C directly is sufficient for Bench vs Weight. */
    row = anchor_row = 0;
    col = 1;
    anchor_col = 2;
    anchor_row = 7;
    sheet_chart_create(1);
    assert(chart->x_col == 1 && chart->y_count == 1 && chart->y_cols[0] == 2);
    assert(!strcmp(chart->xlabel,"Weight") && !strcmp(chart->ylabel,"Bench"));
    put(0,0,4,"Values");
    put(0,1,4,"3");
    row = 0;
    anchor_row = 1;
    col = anchor_col = 4;
    sheet_chart_create(3);
    assert(!chart->configured);
    path_name(path,root,"histogram-after-scatter.bsheet");
    assert(sheet_write(book,path) && sheet_read(staging,path) && !memcmp(book,staging,sizeof(*book)));
    chart_view = 0;
}

static void test_axis_settings(const char *root)
{
    char fields[4][32] = {"2020-01-31","2020-06-30","1mo","1w"};
    SheetAxis axis;
    assert(sheet_axis_parse(fields,1,&axis));
    assert(sheet_axis_resolve(&axis,1,2));
    double date;
    assert(sheet_iso_date("2020-02-29",&date) && sheet_axis_tick(&axis,2,1) == date);
    assert(sheet_iso_date("2020-03-31",&date) && sheet_axis_tick(&axis,2,2) == date);
    strcpy(fields[0],"2020-02-29");
    strcpy(fields[1],"2024-03-01");
    strcpy(fields[2],"1y");
    assert(sheet_axis_parse(fields,1,&axis) && sheet_axis_resolve(&axis,1,2));
    assert(sheet_iso_date("2021-02-28",&date) && sheet_axis_tick(&axis,2,1) == date);
    assert(sheet_iso_date("2024-02-29",&date) && sheet_axis_tick(&axis,2,4) == date);
    strcpy(fields[2],"0");
    assert(!sheet_axis_parse(fields,1,&axis));
    strcpy(fields[2],"1.5mo");
    assert(!sheet_axis_parse(fields,1,&axis));
    strcpy(fields[2],"nan");
    assert(!sheet_axis_parse(fields,1,&axis));
    strcpy(fields[0],"2020-02-30");
    assert(!sheet_axis_parse(fields,1,&axis));
    memset(fields,0,sizeof(fields));
    strcpy(fields[2],"0.00001");
    assert(sheet_axis_parse(fields,0,&axis) && !sheet_axis_resolve(&axis,0,1));
    strcpy(fields[2],"1e-30");
    assert(sheet_axis_parse(fields,0,&axis) && !sheet_axis_resolve(&axis,100,101));
    sheet_new(book);
    active_sheet = 0;
    menu = -1;
    chart_view = help_view = editing = data_choosing = axes_choosing = 0;
    put(0,0,0,"Date");
    put(0,0,1,"Bench");
    for (int i = 0; i < 7; ++i) {
        char raw[32];
        snprintf(raw,sizeof(raw),"2020-%02d-01",i+1);
        put(0,i+1,0,raw);
        snprintf(raw,sizeof(raw),"%d",70+i*10);
        put(0,i+1,1,raw);
    }
    row = col = anchor_col = 0;
    anchor_col = 1;
    anchor_row = 7;
    sheet_history_reset();
    sheet_chart_create(1);
    SheetChart *chart = &book->charts[0];
    SheetChart before = *chart;
    sheet_menu_action(4,14);
    assert(axes_choosing);
    static const char *const values[] = {"2020-01-01","2020-07-01","1mo","1w","60","140","20","5"};
    for (int i = 0; i < 8; ++i) {
        for (const char *p = values[i]; *p; ++p) assert(sheet_key_input(*p));
        if (i < 7) assert(sheet_key_input(9));
    }
    for (int width = 452; width <= 900; width += 112) {
        client_w = width;
        client_h = 294;
        sheet_draw();
    }
    client_w = 612;
    client_h = 424;
    sheet_key_input(13);
    assert(!axes_choosing && !strcmp(chart->axes[0][2],"1mo"));
    sheet_undo(-1);
    assert(!memcmp(chart,&before,sizeof(before)));
    sheet_undo(1);
    assert(!strcmp(chart->axes[1][3],"5"));
    sheet_axes_open();
    strcpy(axes_draft.axes[1][0],"200");
    sheet_axes_apply();
    assert(axes_choosing && *axes_status && !strcmp(chart->axes[1][0],"60"));
    sheet_axes_key(27);
    sheet_axes_open();
    SheetLayout l;
    assert(sheet_layout(&l));
    SheetDataLayout d = sheet_data_layout(&l);
    assert(sheet_axes_mouse(&l,d.x+16,d.y+d.h-20));
    assert(!*axes_draft.axes[0][0]);
    sheet_axes_key(27);
    assert(!strcmp(chart->axes[0][0],"2020-01-01"));
    chart->grids = 15;
    char path[4096];
    path_name(path,root,"axes-monthly.svg");
    assert(sheet_chart_export(calculation,chart,path));
    path_name(path,root,"axes-monthly.pcx");
    assert(sheet_chart_export_pcx(calculation,chart,path));
    path_name(path,root,"axes-v4.bsheet");
    assert(sheet_write(book,path) && sheet_read(staging,path) && !memcmp(book,staging,sizeof(*book)));
    /* A v3 workbook has mappings but no axis fields; migration remains automatic. */
    path_name(path,root,"axes-legacy-v3.bsheet");
    FILE *f = fopen(path,"wb");
    assert(f);
    fputs("BUDOSHEET 3\n1 0\n4f6c64\n0 0 0 0 0 0 0\n\n\n\n1\n",f);
    unsigned short sizes[SHEET_ROWS] = {0};
    sheet_sizes_write(f,sizes,SHEET_COLS);
    sheet_sizes_write(f,sizes,SHEET_ROWS);
    fputs("0 0 0 0 0 0 0 0 0 0 0 0 0\n",f);
    assert(!fclose(f) && sheet_read(staging,path) && !*staging->charts[0].axes[0][0]);
    /* Numeric negative bounds survive persistence, and excluded markers disappear. */
    chart->x_mode = SHEET_X_NUMBER;
    chart->source.r1 = 5;
    memset(chart->axes,0,sizeof(chart->axes));
    for (int i = 0; i < 5; ++i) {
        char raw[32];
        snprintf(raw,sizeof(raw),"%d",i-2);
        put(0,i+1,0,raw);
        snprintf(raw,sizeof(raw),"%d",(i-2)*10);
        put(0,i+1,1,raw);
    }
    strcpy(chart->axes[0][0],"-1");
    strcpy(chart->axes[0][1],"1");
    strcpy(chart->axes[0][2],"1");
    strcpy(chart->axes[0][3],"0.5");
    strcpy(chart->axes[1][0],"-10");
    strcpy(chart->axes[1][1],"10");
    strcpy(chart->axes[1][2],"10");
    strcpy(chart->axes[1][3],"5");
    path_name(path,root,"axes-negative-v4.bsheet");
    assert(sheet_write(book,path) && sheet_read(staging,path) && !memcmp(book,staging,sizeof(*book)));
    chart->type = 0;
    path_name(path,root,"axes-clipped.svg");
    assert(sheet_chart_export(calculation,chart,path));
    chart->type = 2;
    path_name(path,root,"axes-clipped-bar.svg");
    assert(sheet_chart_export(calculation,chart,path));
    strcpy(chart->axes[0][2],"nan");
    path_name(path,root,"axes-invalid-v4.bsheet");
    assert(sheet_write(book,path) && !sheet_read(staging,path));
    /* Y-date axes use the same calendar settings and labels as X-date axes. */
    memset(chart->axes,0,sizeof(chart->axes));
    chart->type = 1;
    chart->source.r1 = 2;
    put(0,1,0,"1");
    put(0,2,0,"2");
    put(0,1,1,"2020-01-01");
    put(0,2,1,"2020-02-01");
    assert(chart_y_date_axis(calculation,chart));
    strcpy(chart->axes[1][0],"2020-01-01");
    strcpy(chart->axes[1][1],"2020-03-01");
    strcpy(chart->axes[1][2],"1mo");
    path_name(path,root,"axes-y-date.svg");
    assert(sheet_chart_export(calculation,chart,path));
    chart_view = 0;
}

static void test_delete_sheet(const char *root)
{
    sheet_new(book);
    book->tabs = 3;
    sheet_string(book->names[0],32,"Left");
    sheet_string(book->names[1],32,"O'Brien");
    sheet_string(book->names[2],32,"Last");
    put(0,0,0,"5");
    put(1,0,0,"11");
    put(1,0,1,"2");
    put(2,0,0,"17");
    put(2,0,1,"=Left!A1+A1");
    put(2,1,0,"='O''Brien'!$A$1");
    put(2,2,0,"=SUM('o''brien' ! A1 : B2)");
    put(2,3,0,"=\"O'Brien!A1\"");
    put(2,4,0,"=IFERROR('O''Brien'!A1,99)");
    put(2,5,0,"='Left'!A1");
    assert(sheet_set_full(book,2,9,9,"",4,3,2));
    book->widths[2][3] = 123;
    book->heights[2][4] = 42;
    book->charts[0].enabled = 1;
    book->charts[0].source.sheet = 1;
    book->charts[2].enabled = 1;
    book->charts[2].type = 1;
    book->charts[2].source = sheet_range(2,0,0,0,1);
    strcpy(book->charts[2].axes[0][0],"0");
    SheetBook *original = malloc(sizeof(*original));
    assert(original);
    *original = *book;
    active_sheet = 1;
    row = col = anchor_row = anchor_col = 4;
    chart_view = 1;
    clipboard_valid = 1;
    sheet_history_reset();
    saved_hash = sheet_hash();
    sheet_menu_action(3,8);
    assert(book->tabs == 2 && active_sheet == 1 && !strcmp(book->names[1],"Last"));
    assert(!row && !col && !anchor_row && !anchor_col && !chart_view && !clipboard_valid && dirty);
    assert(!book->charts[0].enabled && book->charts[1].enabled && book->charts[1].source.sheet == 1);
    assert(!strcmp(book->charts[1].axes[0][0],"0"));
    assert(book->widths[1][3] == 123 && book->heights[1][4] == 42);
    const SheetCell *cell = sheet_cell(book,1,9,9);
    assert(cell && cell->format == 4 && cell->align_h == 3 && cell->align_v == 2);
    assert(!strcmp(sheet_cell(book,1,1,0)->raw,"=#REF!"));
    assert(!strcmp(sheet_cell(book,1,2,0)->raw,"=SUM(#REF!)"));
    assert(!strcmp(sheet_cell(book,1,0,1)->raw,"=Left!A1+A1"));
    SheetValue value = sheet_eval(calculation,1,0,1);
    assert(value.type == SV_NUMBER && value.number == 22);
    value = sheet_eval(calculation,1,1,0);
    assert(value.type == SV_ERROR && value.error == SE_REF);
    value = sheet_eval(calculation,1,4,0);
    assert(value.type == SV_NUMBER && value.number == 99);
    value = sheet_eval(calculation,1,3,0);
    assert(value.type == SV_TEXT && !strcmp(value.text,"O'Brien!A1"));
    sheet_undo(-1);
    assert(!memcmp(book,original,sizeof(*book)) && !dirty);
    sheet_undo(1);
    assert(book->tabs == 2 && dirty);
    char path[4096];
    path_name(path,root,"deleted-sheet.bsheet");
    assert(sheet_write(book,path) && sheet_read(staging,path) && !memcmp(book,staging,sizeof(*book)));
    /* Reusing a deleted name cannot resurrect broken references. */
    sheet_menu_action(3,0);
    sheet_string(book->names[2],32,"O'Brien");
    put(2,0,0,"999");
    value = sheet_eval(calculation,1,1,0);
    assert(value.type == SV_ERROR && value.error == SE_REF);
    /* Delete last, then first; surviving unqualified formulas stay local. */
    sheet_menu_action(3,8);
    assert(book->tabs == 2 && active_sheet == 1);
    active_sheet = 0;
    sheet_menu_action(3,8);
    assert(book->tabs == 1 && active_sheet == 0 && !strcmp(book->names[0],"Last"));
    value = sheet_eval(calculation,0,0,1);
    assert(value.type == SV_ERROR && value.error == SE_REF);
    value = sheet_eval(calculation,0,0,0);
    assert(value.type == SV_NUMBER && value.number == 17);
    *original = *book;
    sheet_menu_action(3,8);
    assert(!memcmp(book,original,sizeof(*book)));
    assert(!sheet_delete_tab(staging,book,-1) && !sheet_delete_tab(staging,book,1));
    /* If rewriting would exceed a cell's limit, deletion is atomic. */
    sheet_new(book);
    book->tabs = 2;
    sheet_string(book->names[0],32,"D");
    sheet_string(book->names[1],32,"Keep");
    char raw[SHEET_TEXT] = "=";
    for (int i = 0; i < 38; ++i) strcat(raw,i ? "+D!A1" : "D!A1");
    put(1,0,0,raw);
    active_sheet = 0;
    sheet_history_reset();
    *original = *book;
    sheet_menu_action(3,8);
    assert(!memcmp(book,original,sizeof(*book)) && history_count == 1);
    /* Unquoted qualifiers, whitespace and text literals use the same lexer. */
    char rewritten[SHEET_TEXT];
    assert(sheet_rename_formula(book,"=D ! $A$1 + SUM(d! A1:B2)+\"D!A1\"",0,NULL,rewritten));
    assert(!strcmp(rewritten,"=#REF! + SUM(#REF!)+\"D!A1\""));
    free(original);
    sheet_new(book);
    active_sheet = 0;
    sheet_history_reset();
}

int main(int argc,char **argv)
{
    assert(argc == 2);
    BwaHostApi host = {.abi_major=BWA_ABI_MAJOR,.abi_minor=BWA_ABI_MINOR,.window_create=create,
        .window_get_client_rect=client,.window_set_min_size=minimum,.window_set_title=title,.window_close=window_close,
        .fill_rect=fill,.fill_rect_rgb=rgb,.draw_rect=fill,.draw_text=text,.get_system_color=color,.get_system_metric=metric,
        .draw_standard_button=button,.draw_button_state=button_state,.draw_sunken_panel=fill,.point_in_rect=inside,
        .get_keyboard_modifiers=mods,.get_time_ms=clock_now,.pointer_region=region,.file_dialog=dialog,.confirm_dialog=confirm,
        .draw_input_caret=caret};
    BwaAppDefinition app;
    assert(bwa_entry(&host,&app));
    assert(!strcmp(app.app_id,"spreadsheet") && app.callbacks.open());
    app.callbacks.draw();
    assert(app.callbacks.key('4'));
    assert(app.callbacks.key('2'));
    assert(app.callbacks.key(13));
    assert(sheet_cell(book,0,0,0) && !strcmp(sheet_cell(book,0,0,0)->raw,"42"));
    assert(dirty);
    assert(app.request_close() == 0 && confirmations == 1);
    app.confirm_result(BUDO_CONFIRM_SAVE,BUDO_RESPONSE_CANCEL);
    assert(!pending_action && !confirming && !closes);
    sheet_new(book);
    sheet_history_reset();
    put(0,0,0,"2");
    put(0,1,0,"4");
    put(0,2,0,"6");
    formula("=SUM(A1:A3)",12);
    formula("=AVERAGE(A1:A3)",4);
    formula("=STDEV(A1:A3)",2);
    formula("=VAR(A1:A3)",4);
    formula("=COUNT(A1:A4)",3);
    formula("=SUM($A$1,A2*2)",10);
    formula("=2^3^2",512);
    formula("=-2^2",-4);
    formula("=2^-2",0.25);
    formula("=50%*10",5);
    formula("=ROUND(1.235,2)",1.24);
    formula("=SIN(PI()/2)",1);
    formula("=IF(FALSE,1/0,7)",7);
    formula("=IF(TRUE,7,1/0)",7);
    formula("=IFERROR(1/0,9)",9);
    formula("=IFERROR(7,1/0)",7);
    formula("=IF(FALSE,SUM(A1:A1024),7)",7);
    formula("=AND(TRUE,2>1)",1);
    formula("=1=\"\"",0);
    formula("=\"a\"=\"A\"",1);
    formula("=LEN(UPPER(\"abc\"))",3);
    formula("=LEN(\"a\"\"b\")",3);
    error_formula("=\"unclosed",SE_VALUE);
    error_formula("=1/0",SE_DIV);
    error_formula("=SQRT(-1)",SE_NUM);
    error_formula("=BOGUS(2)",SE_NAME);
    error_formula("=SUM()",SE_VALUE);
    error_formula("=A1025",SE_NAME);
    error_formula("=INDEX(1)",SE_VALUE);
    formula("=MEDIAN(A1:A3)",4);
    formula("=SUMSQ(A1:A3)",56);
    formula("=VAR.P(A1:A3)",8.0/3);
    formula("=COUNTIF(A1:A3,\">3\")",2);
    formula("=SUMIF(A1:A3,\">3\")",10);
    formula("=INDEX(A1:A3,2)",4);
    formula("=MATCH(4,A1:A3,0)",2);
    formula("=LOG(8,2)",3);
    formula("=DATE(1900,1,1)",1);
    formula("=DATE(1900,3,1)",61);
    formula("=YEAR(DATE(2024,2,29))",2024);
    formula("=MONTH(DATE(2024,13,1))",1);
    formula("=DAY(DATE(2024,2,30))",1);
    formula("=TODAY()",(int)(time(NULL)/86400)+25569);
    error_formula("=YEAR(-1)",SE_NUM);
    error_formula("=Sheet1!#REF!",SE_REF);
    formula("=PMT(0,12,1200)",-100);
    formula("=PV(0,12,-100)",1200);
    formula("=FV(0,12,-100)",1200);
    formula("=NPV(0.1,110,121)",200);
    put(0,0,1,"5");
    put(0,1,1,"9");
    put(0,2,1,"13");
    formula("=VLOOKUP(4,A1:B3,2,FALSE)",9);
    formula("=SUMIF(A1:A3,\">3\",B1:B3)",22);
    formula("=SLOPE(B1:B3,A1:A3)",2);
    formula("=INTERCEPT(B1:B3,A1:A3)",1);
    formula("=CORREL(B1:B3,A1:A3)",1);
    book->tabs = 2;
    strcpy(book->names[1],"Test data");
    put(1,0,0,"11");
    formula("='Test data'!$A$1+Sheet1!A1",13);
    error_formula("=Missing!A1",SE_REF);
    put(0,8,0,"=A10");
    put(0,9,0,"=A9");
    SheetValue v = sheet_eval(calculation,0,8,0);
    assert(v.type == SV_ERROR && v.error == SE_CYCLE);
    formula("=IF(FALSE,A9,3)",3);
    put(0,8,0,"=A1");
    v = sheet_eval(calculation,0,9,0);
    assert(v.type == SV_NUMBER && v.number == 2);
    char raw[SHEET_TEXT];
    assert(sheet_translate("=SUM(A1,$B2,C$3,$D$4,'Test data'!A1,\"A1\")",2,1,raw,sizeof(raw)));
    assert(!strcmp(raw,"=SUM(B3,$B4,D$3,$D$4,'Test data'!B3,\"A1\")"));
    assert(sheet_translate("=A1",-1,0,raw,sizeof(raw)) && !strcmp(raw,"=#REF!"));
    assert(sheet_translate("=LOG10(A1)",1,1,raw,sizeof(raw)) && !strcmp(raw,"=LOG10(B2)"));
    *staging = *book;
    assert(sheet_fill(staging,book,sheet_range(0,0,0,5,0),0));
    assert(!strcmp(sheet_cell(staging,0,5,0)->raw,"12"));
    *staging = *book;
    assert(sheet_extend_fill(staging,book,sheet_range(0,0,0,1,0),sheet_range(0,0,0,5,0),0));
    assert(!strcmp(sheet_cell(staging,0,5,0)->raw,"12"));
    put(0,0,2,"=$A$1+A1");
    *staging = *book;
    assert(sheet_fill(staging,book,sheet_range(0,0,2,3,2),0));
    assert(!strcmp(sheet_cell(staging,0,3,2)->raw,"=$A$1+A4"));
    *staging = *book;
    assert(sheet_copy_range(staging,book,sheet_range(0,0,2,3,2),1,1,3));
    assert(!strcmp(sheet_cell(staging,1,1,3)->raw,"=$A$1+B2"));
    *staging = *book;
    assert(!sheet_copy_range(staging,book,sheet_range(0,0,0,4,0),0,SHEET_ROWS-2,0));
    row=0;
    col=0;
    anchor_row=2;
    anchor_col=1;
    sheet_history_reset();
    sheet_mutate(6);
    assert(!strcmp(sheet_cell(book,0,0,0)->raw,"6"));
    sheet_undo(-1);
    assert(!strcmp(sheet_cell(book,0,0,0)->raw,"2"));
    sheet_undo(1);
    assert(!strcmp(sheet_cell(book,0,0,0)->raw,"6"));
    sheet_undo(-1);
    *staging = *book;
    assert(sheet_structure(staging,book,0,0,1,1));
    assert(!strcmp(sheet_cell(staging,0,2,0)->raw,"4"));
    assert(sheet_structural_formula(book,0,"=SUM($A$1:A3)+\"A3\"",0,0,1,1,raw));
    assert(!strcmp(raw,"=SUM($A$1:A4)+\"A3\""));
    assert(sheet_structural_formula(book,1,"='Sheet1'!A2+'Test data'!A2",0,0,1,-1,raw));
    assert(!strcmp(raw,"='Sheet1'!#REF!+'Test data'!A2"));
    assert(sheet_structural_formula(book,0,"=SUM(A1:A3)",0,0,0,-1,raw));
    assert(!strcmp(raw,"=SUM(A1:A2)"));
    assert(sheet_structural_formula(book,0,"=$A$1+B2+LOG10(C1)",0,1,0,1,raw));
    assert(!strcmp(raw,"=$B$1+C2+LOG10(D1)"));
    assert(sheet_structural_formula(book,0,"=1e10+2.5E-3+A1",0,0,0,1,raw));
    assert(!strcmp(raw,"=1e10+2.5E-3+A2"));
    char path[4096];
    for (int type=0; type<5; ++type) {
        sheet_chart_create(type);
        sheet_draw();
        path_name(path,argv[1],sheet_chart_types[type].name);
        assert(sheet_chart_export(calculation,&book->charts[0],path));
    }
    sheet_string(book->charts[0].title,64,"<& Figure");
    path_name(path,argv[1],"book.bsheet");
    assert(sheet_write(book,path));
    assert(sheet_read(staging,path) && !memcmp(book,staging,sizeof(*book)));
    saved_hash=sheet_hash();
    sheet_string(filename,sizeof(filename),path);
    sheet_title();
    assert(!dirty);
    put(0,5,0,"123");
    sheet_changed();
    assert(dirty && !app.request_close());
    app.confirm_result(BUDO_CONFIRM_SAVE,BUDO_RESPONSE_SAVE);
    assert(closes == 1 && !dirty);
    allow_close=0;
    path_name(path,argv[1],"bad.bsheet");
    FILE *f=fopen(path,"wb");
    assert(f);
    fputs("BUDOSHEET 99\n",f);
    fclose(f);
    *clipboard=*book;
    assert(!sheet_load(path) && !memcmp(clipboard,book,sizeof(*book)));
    path_name(path,argv[1],"literal.csv");
    f=fopen(path,"wb");
    assert(f);
    fputs("\"name\",\"value\"\r\n\"a,\"\"b\",\"=1+2\"\r\n\"multiline\ntext\",\"2.5\"\r\n",f);
    fclose(f);
    assert(sheet_csv_read(staging,path,','));
    assert(!strcmp(sheet_cell(staging,0,1,0)->raw,"a,\"b"));
    assert(!strcmp(sheet_cell(staging,0,1,1)->raw,"'=1+2"));
    assert(sheet_load(path) && dirty && !*filename);
    formula("=LEN(B2)",4);
    path_name(path,argv[1],"export.csv");
    assert(sheet_csv_write(calculation,0,path,','));
    assert(sheet_csv_read(staging,path,','));
    assert(!strcmp(sheet_cell(staging,0,1,0)->raw,"a,\"b"));
    path_name(path,argv[1],"truncated.csv");
    f=fopen(path,"wb");
    assert(f);
    fputs("\"unclosed",f);
    fclose(f);
    assert(!sheet_csv_read(staging,path,','));
    chart_view=0;
    for (int width=452; width<=900; width+=112) {
        client_w=width;
        client_h=294;
        menu=-1;
        help_view=0;
        editing=0;
        sheet_draw();
        sheet_begin_edit(0,0);
        sheet_draw();
        editing=0;
        for (int i=0;i<6;++i) {
            menu=i;
            sheet_draw();
        }
        menu=-1;
        help_view=1;
        sheet_draw();
        help_view=2;
        sheet_draw();
        help_view=0;
    }
    client_w=612;
    client_h=424;
    for (int type=0;type<5;++type) {
        row=1;
        col=0;
        anchor_row=2;
        anchor_col=1;
        sheet_chart_create(type);
        sheet_draw();
    }
    chart_view=0;
    menu=-1;
    sheet_begin_edit(0,0);
    strcpy(edit_buffer,"456");
    edit_cursor=3;
    edit_select=0;
    assert(!app.request_close() && !editing && dirty);
    app.confirm_result(BUDO_CONFIRM_SAVE,BUDO_RESPONSE_CANCEL);
    assert(!sheet_write(book,"/nonexistent-directory/book.bsheet"));
    test_layout_changes(argv[1]);
    test_date_figures(argv[1]);
    test_axis_settings(argv[1]);
    test_delete_sheet(argv[1]);
    /* Exercise malformed token streams under sanitizers as well as normal builds. */
    unsigned int random_state = 12345;
    const char alphabet[] = "A1$!+-*/^%<>=(,)\"' ._&0123456789";
    for (int trial = 0; trial < 2000; ++trial) {
        raw[0] = '=';
        for (int i = 1; i < 80; ++i) {
            random_state = random_state*1664525U+1013904223U;
            raw[i] = alphabet[random_state%(sizeof(alphabet)-1)];
        }
        raw[80] = 0;
        put(0,20,20,raw);
        (void)sheet_eval(calculation,0,20,20);
        char translated[SHEET_TEXT];
        (void)sheet_translate(raw,1,-1,translated,sizeof(translated));
    }
    sheet_new(staging);
    for (int i = 0; i < SHEET_CELLS; ++i) assert(sheet_set(staging,0,i/SHEET_COLS,i%SHEET_COLS,"1",0));
    assert(!sheet_set(staging,0,SHEET_CELLS/SHEET_COLS,0,"overflow",0));
    assert(staging->count == SHEET_CELLS);
    assert(sheet_set(staging,0,0,0,"",0) && staging->count == SHEET_CELLS-1);
    app.callbacks.close();
    puts("Spreadsheet calculation, editing, persistence, figures and UI checks passed.");
    return 0;
}
