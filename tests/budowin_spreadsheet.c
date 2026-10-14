#include "../budo/BUDOWIN/appsrc/spreadsheet.c"
#include <assert.h>

static int client_w = 612, client_h = 424, closes, dialogs, confirmations;
static unsigned int modifiers;
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
