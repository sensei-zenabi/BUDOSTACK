#define _POSIX_C_SOURCE 200809L
#include "../sdk/ui.h"
#include "sheet_chart.h"
#include "sheet_edit.h"
#include <sys/stat.h>

static const BwaHostApi *sheet_host;
static SheetBook *book, *staging, *snapshots, *clipboard;
static SheetCalc *calculation;
static int history_count, history_at, active_sheet, row, col, anchor_row, anchor_col;
static int menu = -1, dragging, editing, edit_cursor, edit_select, chart_view, help_view;
static int dirty, dialog_kind, pending_action, confirming, allow_close, edit_kind;
static uint64_t saved_hash;
static SheetRange copied_range;
static int clipboard_valid;
static char filename[4096], pending_path[4096], edit_buffer[SHEET_TEXT], status[160];
static BudoScrollbar sheet_hscroll, sheet_vscroll;
static int first_row, first_col, fill_drag, fill_horizontal;
static SheetRange fill_seed;
static int help_offset;
static unsigned long long last_cell_click;
static int last_cell_key = -1;
static int resize_axis, resize_index, resize_start, resize_original;
static int data_choosing, data_offset;
static int axes_choosing, axes_focus, axes_select;
static SheetChart axes_draft;
static char axes_status[96];
static SheetChart data_draft;
static char data_status[96];
enum { SD_OPEN = 1, SD_SAVE, SD_CSV, SD_TSV, SD_SVG, SD_PCX };
enum { SA_NEW = 1, SA_OPEN, SA_CLOSE, SA_PATH };
typedef struct SheetLayout {
    int x,y,w,h,gx,gy,gw,gh,rows,cols,tabs,status;
    int xs[SHEET_COLS+1], ys[SHEET_ROWS+1];
} SheetLayout;
static void sheet_continue(void);
static void sheet_action(int action);
static int sheet_save(void);
static void sheet_edit_commit(void);
static void ui_text(int x,int y,const char *s,int chars);
static void sheet_chart_labels(SheetChart *chart,const SheetChart *previous);
static void sheet_data_open(void);
static unsigned char ui_color(int role)
{
    return sheet_host->get_system_color(role);
}
static void sheet_status(const char *s)
{
    sheet_string(status, sizeof(status), s);
}
static uint64_t sheet_hash(void)
{
    const unsigned char *p = (const unsigned char *)book;
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < sizeof(*book); ++i) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}
static void sheet_title(void)
{
    dirty = sheet_hash() != saved_hash;
    const char *base = strrchr(filename, '/');
    char title[160];
    snprintf(title, sizeof(title), "Spreadsheet%s - %.100s", dirty ? " *" : "", filename[0] ? (base ? base + 1 : filename) : "Untitled");
    (void)sheet_host->window_set_title(title);
}
static void sheet_history_reset(void)
{
    snapshots[0] = *book;
    history_count = 1;
    history_at = 0;
    sheet_calc_reset(calculation, book);
}
static void sheet_changed(void)
{
    if (!memcmp(book, &snapshots[history_at], sizeof(*book))) return;
    history_count = history_at + 1;
    if (history_count == SHEET_HISTORY) {
        memmove(snapshots, snapshots + 1, (SHEET_HISTORY - 1) * sizeof(*book));
        --history_count;
    }
    history_at = history_count++;
    snapshots[history_at] = *book;
    sheet_calc_reset(calculation, book);
    sheet_title();
}
static void sheet_undo(int direction)
{
    int next = history_at + direction;
    if (next < 0 || next >= history_count) return;
    history_at = next;
    *book = snapshots[next];
    active_sheet = sheet_min(active_sheet, book->tabs - 1);
    sheet_calc_reset(calculation, book);
    sheet_title();
}
static SheetRange selection(void)
{
    return sheet_range(active_sheet, row, col, anchor_row, anchor_col);
}
static int sheet_layout(SheetLayout *l)
{
    if (!sheet_host->window_get_client_rect(&l->x,&l->y,&l->w,&l->h)) return 0;
    if (l->w < 440 || l->h < 260) return 0;
    l->gx = l->x + 38;
    l->gy = l->y + 92;
    l->status = l->y + l->h - sheet_host->get_system_metric(BUDO_SYS_METRIC_STATUS_HEIGHT);
    l->tabs = l->status - 24;
    l->gw = l->w - 38 - BUDO_SCROLL_WIDTH;
    l->gh = l->tabs - l->gy - BUDO_SCROLL_WIDTH;
    sheet_hscroll.position = sheet_clamp(sheet_hscroll.position,0,SHEET_COLS-1);
    sheet_vscroll.position = sheet_clamp(sheet_vscroll.position,0,SHEET_ROWS-1);
    first_col = sheet_hscroll.position;
    first_row = sheet_vscroll.position;
    l->cols = l->rows = 0;
    l->xs[0] = l->ys[0] = 0;
    while (first_col+l->cols < SHEET_COLS && l->xs[l->cols] < l->gw) {
        l->xs[l->cols+1] = sheet_min(l->gw,l->xs[l->cols]+sheet_width(book,active_sheet,first_col+l->cols));
        ++l->cols;
    }
    while (first_row+l->rows < SHEET_ROWS && l->ys[l->rows] < l->gh) {
        l->ys[l->rows+1] = sheet_min(l->gh,l->ys[l->rows]+sheet_height(book,active_sheet,first_row+l->rows));
        ++l->rows;
    }
    sheet_hscroll = (BudoScrollbar){.x=l->gx,.y=l->gy+l->gh,.length=l->gw,.horizontal=1,
        .total=SHEET_COLS+l->cols-1,.page=l->cols,.position=sheet_hscroll.position,.dragging=sheet_hscroll.dragging,.grab=sheet_hscroll.grab,
        .held=sheet_hscroll.held,.armed=sheet_hscroll.armed,.repeat_at=sheet_hscroll.repeat_at};
    sheet_vscroll = (BudoScrollbar){.x=l->gx+l->gw,.y=l->gy,.length=l->gh,
        .total=SHEET_ROWS+l->rows-1,.page=l->rows,.position=sheet_vscroll.position,.dragging=sheet_vscroll.dragging,.grab=sheet_vscroll.grab,
        .held=sheet_vscroll.held,.armed=sheet_vscroll.armed,.repeat_at=sheet_vscroll.repeat_at};
    budo_scroll_clamp(&sheet_hscroll);
    budo_scroll_clamp(&sheet_vscroll);
    first_col = sheet_hscroll.position;
    first_row = sheet_vscroll.position;
    return l->rows > 0 && l->cols > 0;
}
static void sheet_reveal(void)
{
    SheetLayout l;
    if (!sheet_layout(&l)) return;
    int row_visible = row >= first_row && row < first_row+l.rows &&
        l.ys[row-first_row+1]-l.ys[row-first_row] == sheet_height(book,active_sheet,row);
    int col_visible = col >= first_col && col < first_col+l.cols &&
        l.xs[col-first_col+1]-l.xs[col-first_col] == sheet_width(book,active_sheet,col);
    if (!row_visible) {
        int start = row, extent = sheet_height(book,active_sheet,row);
        while (start > 0 && extent+sheet_height(book,active_sheet,start-1) <= l.gh) {
            extent += sheet_height(book,active_sheet,--start);
        }
        sheet_vscroll.position = start;
    }
    if (!col_visible) {
        int start = col, extent = sheet_width(book,active_sheet,col);
        while (start > 0 && extent+sheet_width(book,active_sheet,start-1) <= l.gw) {
            extent += sheet_width(book,active_sheet,--start);
        }
        sheet_hscroll.position = start;
    }
}
static void sheet_begin_edit(int kind, int replace)
{
    edit_kind = kind;
    edit_buffer[0] = 0;
    const SheetCell *cell = sheet_cell(book, active_sheet, row, col);
    if (!replace) {
        if (kind == 1) sheet_string(edit_buffer,sizeof(edit_buffer),book->names[active_sheet]);
        else if (kind == 2) sheet_string(edit_buffer,sizeof(edit_buffer),book->charts[active_sheet].title);
        else if (kind == 3) sheet_string(edit_buffer,sizeof(edit_buffer),book->charts[active_sheet].xlabel);
        else if (kind == 4) sheet_string(edit_buffer,sizeof(edit_buffer),book->charts[active_sheet].ylabel);
        else if (kind == 5) snprintf(edit_buffer,sizeof(edit_buffer),"%d",sheet_width(book,active_sheet,col));
        else if (kind == 6) snprintf(edit_buffer,sizeof(edit_buffer),"%d",sheet_height(book,active_sheet,row));
        else if (cell) sheet_string(edit_buffer,sizeof(edit_buffer),cell->raw);
    }
    editing = 1;
    edit_cursor = (int)strlen(edit_buffer);
    edit_select = !replace;
}
static void sheet_edit_commit(void)
{
    if (!editing) return;
    if (edit_kind == 1) {
        if (!*edit_buffer || strlen(edit_buffer) >= 32) {
            sheet_status("Sheet names require 1 to 31 characters.");
            return;
        }
        if (!sheet_rename(staging,book,active_sheet,edit_buffer)) {
            sheet_status("Rename failed: duplicate name or a rewritten formula exceeds the cell limit.");
            return;
        }
        *book = *staging;
    } else if (edit_kind >= 5) {
        char *end;
        long size = strtol(edit_buffer,&end,10);
        if (end == edit_buffer || *end || size < SHEET_SIZE_MIN || size > SHEET_SIZE_MAX) {
            sheet_status("Row/column sizes must be 16 to 512 pixels.");
            return;
        }
        SheetRange a = selection();
        if (edit_kind == 5) {
            for (int c = a.c0; c <= a.c1; ++c) book->widths[active_sheet][c] = (unsigned short)size;
        } else {
            for (int r = a.r0; r <= a.r1; ++r) book->heights[active_sheet][r] = (unsigned short)size;
        }
    } else if (edit_kind >= 2) {
        SheetChart *c = &book->charts[active_sheet];
        size_t size = edit_kind == 2 ? sizeof(c->title) : sizeof(c->xlabel);
        if (strlen(edit_buffer) >= size) {
            sheet_status("Chart label too long.");
            return;
        }
        sheet_string(edit_kind == 2 ? c->title : edit_kind == 3 ? c->xlabel : c->ylabel,size,edit_buffer);
    } else {
        const SheetCell *cell = sheet_cell(book,active_sheet,row,col);
        if (!sheet_set(book,active_sheet,row,col,edit_buffer,cell ? cell->format : 0)) {
            sheet_status("Cell limit reached; edit preserved.");
            return;
        }
    }
    editing = 0;
    sheet_changed();
}
static void sheet_mutate(int action)
{
    *staging = *book;
    SheetRange a = selection();
    int ok = 1;
    if (action == 1) ok = sheet_fill(staging,book,a,0);
    else if (action == 2) ok = sheet_fill(staging,book,a,1);
    else if (action == 3) {
        if (!clipboard_valid) return;
        ok = sheet_copy_range(staging,clipboard,copied_range,active_sheet,a.r0,a.c0);
    } else if (action >= 20) {
        for (int r = a.r0; ok && r <= a.r1; ++r) {
            for (int c = a.c0; ok && c <= a.c1; ++c) {
                const SheetCell *cell = sheet_cell(book,active_sheet,r,c);
                int horizontal = action <= 23 ? action-20 : cell ? cell->align_h : 0;
                int vertical = action >= 24 ? action-24 : cell ? cell->align_v : 0;
                ok = sheet_set_full(staging,active_sheet,r,c,cell ? cell->raw : "",cell ? cell->format : 0,horizontal,vertical);
            }
        }
    } else if (action == 5 || action == 6) ok = sheet_sort(staging,calculation,a,action == 6);
    else {
        for (int r = a.r0; ok && r <= a.r1; ++r) {
            for (int c = a.c0; ok && c <= a.c1; ++c) {
                const SheetCell *cell = sheet_cell(book,active_sheet,r,c);
                ok = sheet_set(staging,active_sheet,r,c,action == 4 ? "" : cell ? cell->raw : "",action == 4 ? 0 : action - 10);
            }
        }
    }
    if (!ok) {
        sheet_status("Operation exceeds limits or requires a larger selection; workbook preserved.");
        return;
    }
    *book = *staging;
    sheet_changed();
    sheet_status("Selection updated.");
}
static void sheet_copy(void)
{
    *clipboard = *book;
    copied_range = selection();
    clipboard_valid = 1;
    sheet_status("Copied selection. Ctrl+V pastes with relative references.");
}
static void sheet_chart_create(int type)
{
    SheetRange a = selection();
    int choose_data = type < 3 && a.c1-a.c0 > SHEET_SERIES;
    SheetChart *chart = &book->charts[active_sheet];
    SheetChart previous = *chart;
    chart->enabled = 1;
    chart->type = type;
    chart->source = a;
    if (memcmp(&a,&previous.source,sizeof(a)) || type >= 3 || previous.type >= 3)
        memset(chart->axes,0,sizeof(chart->axes));
    if (!*chart->title) sheet_string(chart->title,sizeof(chart->title),"Figure");
    if (!*chart->xlabel) sheet_string(chart->xlabel,sizeof(chart->xlabel),type == 1 ? "X" : "Sample");
    if (!*chart->ylabel) sheet_string(chart->ylabel,sizeof(chart->ylabel),"Value");
    if (type < 3) {
        chart_configure(calculation,chart);
        sheet_chart_labels(chart,&previous);
    } else {
        chart->configured = chart->x_col = chart->x_mode = chart->header = chart->y_count = 0;
        memset(chart->y_cols,0,sizeof(chart->y_cols));
    }
    sheet_changed();
    chart_view = 1;
    sheet_status("Figure updates from the source cells. Esc returns to the grid.");
    if (choose_data) sheet_data_open();
}
static void sheet_dialog(int kind)
{
    dialog_kind = kind;
    const char *initial = kind == SD_OPEN ? filename : kind == SD_CSV ? "SHEET.csv" : kind == SD_TSV ? "SHEET.tsv" :
        kind == SD_SVG ? "FIGURE.svg" : kind == SD_PCX ? "FIGURE.pcx" : *filename ? filename : "WORKBOOK.bsheet";
    if (!sheet_host->file_dialog(kind == SD_OPEN ? BUDO_FILE_OPEN : BUDO_FILE_SAVE_AS,initial)) {
        dialog_kind = pending_action = 0;
        sheet_status("Unable to open file dialog.");
    }
}
static int sheet_save(void)
{
    if (!*filename) {
        sheet_dialog(SD_SAVE);
        return 0;
    }
    if (!sheet_write(book,filename)) {
        perror("Spreadsheet: save");
        sheet_status("Save failed; original file preserved.");
        return 0;
    }
    saved_hash = sheet_hash();
    sheet_title();
    sheet_status("Workbook saved.");
    return 1;
}
static int sheet_load(const char *path)
{
    const char *ext = strrchr(path,'.');
    int csv = ext && (!strcmp(ext,".csv") || !strcmp(ext,".CSV"));
    int tsv = ext && (!strcmp(ext,".tsv") || !strcmp(ext,".TSV"));
    int native = sheet_read(staging,path);
    int ok = native || ((csv || tsv) && sheet_csv_read(staging,path,tsv ? '\t' : ','));
    if (native) csv = tsv = 0;
    if (!ok) {
        sheet_status("Invalid, oversized or unreadable file; workbook preserved.");
        return 0;
    }
    *book = *staging;
    filename[0] = 0;
    if (!csv && !tsv) sheet_string(filename,sizeof(filename),path);
    active_sheet = row = col = anchor_row = anchor_col = 0;
    sheet_hscroll.position = sheet_vscroll.position = 0;
    chart_view = editing = data_choosing = axes_choosing = 0;
    saved_hash = sheet_hash();
    sheet_history_reset();
    if (csv || tsv) saved_hash = 0;
    sheet_title();
    sheet_status(csv || tsv ? "Imported literal data. Save as .bsheet to keep formulas, formats and figures." : "Workbook loaded.");
    return 1;
}
static void sheet_continue(void)
{
    int action = pending_action;
    pending_action = 0;
    if (action == SA_NEW) {
        sheet_new(book);
        filename[0] = 0;
        active_sheet = row = col = anchor_row = anchor_col = 0;
        sheet_hscroll.position = sheet_vscroll.position = 0;
        chart_view = 0;
        saved_hash = sheet_hash();
        sheet_history_reset();
        sheet_title();
    } else if (action == SA_OPEN) sheet_dialog(SD_OPEN);
    else if (action == SA_PATH) (void)sheet_load(pending_path);
    else if (action == SA_CLOSE) {
        allow_close = 1;
        (void)sheet_host->window_close();
    }
}
static void sheet_action(int action)
{
    if (confirming || dialog_kind) return;
    data_choosing = 0;
    axes_choosing = 0;
    if (editing) sheet_edit_commit();
    if (editing) return;
    menu = -1;
    pending_action = action;
    sheet_title();
    if (!dirty) sheet_continue();
    else if (sheet_host->confirm_dialog(BUDO_CONFIRM_SAVE,"Unsaved workbook","Save the workbook before continuing?")) confirming = 1;
    else {
        pending_action = 0;
        sheet_status("Unable to open save confirmation.");
    }
}
static void sheet_confirm(int kind, int response)
{
    if (kind != BUDO_CONFIRM_SAVE) return;
    if (dialog_kind && response == BUDO_RESPONSE_CANCEL) {
        dialog_kind = pending_action = 0;
        return;
    }
    if (!confirming) return;
    confirming = 0;
    if (response == BUDO_RESPONSE_CANCEL) pending_action = 0;
    else if (response == BUDO_RESPONSE_DISCARD) sheet_continue();
    else if (response == BUDO_RESPONSE_SAVE && sheet_save()) sheet_continue();
}
static int sheet_file_selected(int mode, const char *path)
{
    (void)mode;
    int kind = dialog_kind;
    if (!path || !*path) {
        dialog_kind = pending_action = 0;
        return 0;
    }
    struct stat destination, original;
    int same_file = *filename && (!strcmp(path,filename) ||
        (stat(path,&destination) == 0 && stat(filename,&original) == 0 &&
         destination.st_dev == original.st_dev && destination.st_ino == original.st_ino));
    if (kind >= SD_CSV && same_file) {
        sheet_status("Export requires a different filename from the workbook.");
        return 0;
    }
    int ok = 0;
    if (kind == SD_OPEN) ok = sheet_load(path);
    else if (kind == SD_SAVE) {
        ok = sheet_write(book,path);
        if (ok) {
            sheet_string(filename,sizeof(filename),path);
            saved_hash = sheet_hash();
            sheet_title();
        }
    } else if (kind == SD_CSV || kind == SD_TSV) ok = sheet_csv_write(calculation,active_sheet,path,kind == SD_TSV ? '\t' : ',');
    else if (kind == SD_SVG) ok = sheet_chart_export(calculation,&book->charts[active_sheet],path);
    else if (kind == SD_PCX) ok = sheet_chart_export_pcx(calculation,&book->charts[active_sheet],path);
    if (ok) {
        dialog_kind = 0;
        sheet_status(kind == SD_OPEN ? "File loaded." : "File saved.");
        if (pending_action) sheet_continue();
    } else sheet_status("File operation failed; current workbook and original destination preserved.");
    return ok;
}
static int sheet_request_close(void)
{
    if (allow_close) return 1;
    data_choosing = 0;
    axes_choosing = 0;
    if (dialog_kind || confirming) return 0;
    if (editing) sheet_edit_commit();
    if (editing) return 0;
    sheet_title();
    if (!dirty) return 1;
    sheet_action(SA_CLOSE);
    return 0;
}
static void sheet_chart_labels(SheetChart *chart, const SheetChart *previous)
{
    SheetChart old_defaults = *previous, new_defaults = *chart;
    chart_default_labels(calculation,&old_defaults);
    chart_default_labels(calculation,&new_defaults);
    if (!*previous->xlabel || !strcmp(previous->xlabel,"X") || !strcmp(previous->xlabel,"Sample") ||
        !strcmp(previous->xlabel,old_defaults.xlabel))
        sheet_string(chart->xlabel,sizeof(chart->xlabel),new_defaults.xlabel);
    if (!*previous->ylabel || !strcmp(previous->ylabel,"Value") || !strcmp(previous->ylabel,old_defaults.ylabel))
        sheet_string(chart->ylabel,sizeof(chart->ylabel),new_defaults.ylabel);
}
typedef struct SheetDataLayout {
    int x, y, w, h, rows;
} SheetDataLayout;
static SheetDataLayout sheet_data_layout(const SheetLayout *l)
{
    SheetDataLayout d = {l->x+10,l->y+8,l->w-20,l->h-16,0};
    d.rows = sheet_clamp((d.h-198)/20,1,SHEET_SERIES);
    return d;
}
static void sheet_data_open(void)
{
    const SheetChart *chart = &book->charts[active_sheet];
    if (!chart->enabled || chart->type >= 3) {
        sheet_status("Create a line, scatter or bar figure from the table first.");
        return;
    }
    data_draft = *chart;
    if (!data_draft.configured) chart_configure(calculation,&data_draft);
    data_offset = 0;
    data_choosing = 1;
    menu = -1;
    sheet_string(data_status,sizeof(data_status),"Left/right: X; 1-8: Y; M: axis mode; H: headers; Enter: apply.");
}
static void sheet_data_x(int direction)
{
    int count = data_draft.source.c1-data_draft.source.c0+1;
    if (count == 1) return;
    data_draft.x_col = data_draft.source.c0+(data_draft.x_col-data_draft.source.c0+count+direction)%count;
    for (int i = 0; i < data_draft.y_count; ++i) {
        if (data_draft.y_cols[i] != data_draft.x_col) continue;
        memmove(data_draft.y_cols+i,data_draft.y_cols+i+1,(size_t)(data_draft.y_count-i-1)*sizeof(int));
        data_draft.y_cols[--data_draft.y_count] = 0;
        break;
    }
}
static void sheet_data_toggle(int column)
{
    for (int i = 0; i < data_draft.y_count; ++i) {
        if (data_draft.y_cols[i] != column) continue;
        memmove(data_draft.y_cols+i,data_draft.y_cols+i+1,(size_t)(data_draft.y_count-i-1)*sizeof(int));
        data_draft.y_cols[--data_draft.y_count] = 0;
        return;
    }
    if (data_draft.y_count == SHEET_SERIES) {
        sheet_string(data_status,sizeof(data_status),"At most eight Y series may be plotted together.");
        return;
    }
    int i = data_draft.y_count++;
    while (i > 0 && data_draft.y_cols[i-1] > column) {
        data_draft.y_cols[i] = data_draft.y_cols[i-1];
        --i;
    }
    data_draft.y_cols[i] = column;
}
static void sheet_data_apply(void)
{
    if (!data_draft.y_count || chart_first_row(&data_draft) > data_draft.source.r1) {
        sheet_string(data_status,sizeof(data_status),"Select at least one Y column and one data row.");
        return;
    }
    SheetChart previous = book->charts[active_sheet];
    sheet_chart_labels(&data_draft,&previous);
    if (data_draft.x_col != previous.x_col || chart_x_mode(calculation,&data_draft) != chart_x_mode(calculation,&previous))
        memset(data_draft.axes[0],0,sizeof(data_draft.axes[0]));
    if (chart_y_date_axis(calculation,&data_draft) != chart_y_date_axis(calculation,&previous))
        memset(data_draft.axes[1],0,sizeof(data_draft.axes[1]));
    book->charts[active_sheet] = data_draft;
    data_choosing = 0;
    axes_choosing = 0;
    chart_view = 1;
    sheet_changed();
    sheet_status("Figure data updated. Date axes use calendar dates and elapsed time.");
}
static void sheet_data_page(int direction, int rows)
{
    int count = data_draft.source.c1-data_draft.source.c0+1;
    data_offset = sheet_clamp(data_offset+direction*rows,0,((count-1)/rows)*rows);
}
static void sheet_data_draw(const SheetLayout *l)
{
    SheetDataLayout d = sheet_data_layout(l);
    sheet_host->draw_standard_button(d.x,d.y,d.w,d.h,"",0);
    sheet_host->pointer_region(d.x,d.y,d.w,d.h,BUDO_CURSOR_ARROW|BUDO_CURSOR_OVERLAY,NULL);
    ui_text(d.x+12,d.y+8,"Figure data - choose X and one or more Y columns",(d.w-24)/6);
    char label[128], name[32], address[24];
    chart_column_label(calculation,&data_draft,data_draft.x_col,name,sizeof(name));
    sheet_address(0,data_draft.x_col,address,sizeof(address));
    char *digit = address;
    while (isalpha((unsigned char)*digit)) ++digit;
    *digit = 0;
    snprintf(label,sizeof(label),"X: %s - %.31s",address,name);
    ui_text(d.x+12,d.y+36,label,(d.w-100)/6);
    budo_button_draw(sheet_host,d.x+d.w-72,d.y+28,28,22,"<",0);
    budo_button_draw(sheet_host,d.x+d.w-40,d.y+28,28,22,">",0);
    static const char *const modes[] = {"Auto (numbers / dates)","Numbers","Dates","Sample positions"};
    snprintf(label,sizeof(label),"X axis: %s",modes[data_draft.x_mode]);
    budo_button_draw(sheet_host,d.x+12,d.y+54,d.w-24,22,label,0);
    snprintf(label,sizeof(label),"[%c] First row contains headers",data_draft.header ? 'x' : ' ');
    budo_button_draw(sheet_host,d.x+12,d.y+80,d.w-24,22,label,0);
    for (int i = 0; i < d.rows; ++i) {
        int column = data_draft.source.c0+data_offset+i;
        if (column > data_draft.source.c1) break;
        int selected = 0;
        for (int j = 0; j < data_draft.y_count; ++j) if (data_draft.y_cols[j] == column) selected = 1;
        chart_column_label(calculation,&data_draft,column,name,sizeof(name));
        sheet_address(0,column,address,sizeof(address));
        digit = address;
        while (isalpha((unsigned char)*digit)) ++digit;
        *digit = 0;
        snprintf(label,sizeof(label),"%d [%c] Y: %s - %.31s",i+1,selected ? 'x' : ' ',address,name);
        budo_button_draw(sheet_host,d.x+12,d.y+108+i*20,d.w-24,18,label,selected ? BUDO_BUTTON_PRESSED : 0);
    }
    ui_text(d.x+12,d.y+d.h-76,data_status,(d.w-24)/6);
    budo_button_draw(sheet_host,d.x+12,d.y+d.h-56,28,20,"<",0);
    budo_button_draw(sheet_host,d.x+d.w-40,d.y+d.h-56,28,20,">",0);
    int count = data_draft.source.c1-data_draft.source.c0+1;
    snprintf(label,sizeof(label),"Y columns %d-%d / %d",data_offset+1,sheet_min(count,data_offset+d.rows),count);
    ui_text(d.x+48,d.y+d.h-50,label,(d.w-96)/6);
    budo_button_draw(sheet_host,d.x+d.w-184,d.y+d.h-30,80,22,"Apply",BUDO_BUTTON_DEFAULT);
    budo_button_draw(sheet_host,d.x+d.w-96,d.y+d.h-30,80,22,"Cancel",0);
}
static int sheet_data_mouse(const SheetLayout *l, int x, int y)
{
    SheetDataLayout d = sheet_data_layout(l);
    if (sheet_host->point_in_rect(x,y,d.x+d.w-72,d.y+28,28,22)) sheet_data_x(-1);
    else if (sheet_host->point_in_rect(x,y,d.x+d.w-40,d.y+28,28,22)) sheet_data_x(1);
    else if (sheet_host->point_in_rect(x,y,d.x+12,d.y+54,d.w-24,22)) data_draft.x_mode = (data_draft.x_mode+1)%4;
    else if (sheet_host->point_in_rect(x,y,d.x+12,d.y+80,d.w-24,22)) data_draft.header = !data_draft.header;
    else if (sheet_host->point_in_rect(x,y,d.x+12,d.y+d.h-56,28,20)) sheet_data_page(-1,d.rows);
    else if (sheet_host->point_in_rect(x,y,d.x+d.w-40,d.y+d.h-56,28,20)) sheet_data_page(1,d.rows);
    else if (sheet_host->point_in_rect(x,y,d.x+d.w-184,d.y+d.h-30,80,22)) sheet_data_apply();
    else if (sheet_host->point_in_rect(x,y,d.x+d.w-96,d.y+d.h-30,80,22)) data_choosing = 0;
    else for (int i = 0; i < d.rows; ++i) {
        int column = data_draft.source.c0+data_offset+i;
        if (column <= data_draft.source.c1 && sheet_host->point_in_rect(x,y,d.x+12,d.y+108+i*20,d.w-24,18)) {
            sheet_data_toggle(column);
            break;
        }
    }
    return 1;
}
static int sheet_data_key(int key)
{
    SheetLayout l;
    if (!sheet_layout(&l)) return 1;
    SheetDataLayout d = sheet_data_layout(&l);
    if (key == 27) data_choosing = 0;
    else if (key == 13) sheet_data_apply();
    else if (key == (0x100|75)) sheet_data_x(-1);
    else if (key == (0x100|77)) sheet_data_x(1);
    else if (key == (0x100|72)) sheet_data_page(-1,d.rows);
    else if (key == (0x100|80)) sheet_data_page(1,d.rows);
    else if (key == 'm' || key == 'M') data_draft.x_mode = (data_draft.x_mode+1)%4;
    else if (key == 'h' || key == 'H') data_draft.header = !data_draft.header;
    else if (key >= '1' && key < '1'+d.rows) {
        int column = data_draft.source.c0+data_offset+key-'1';
        if (column <= data_draft.source.c1) sheet_data_toggle(column);
    }
    return 1;
}

static void sheet_axes_open(void)
{
    if (!book->charts[active_sheet].enabled || book->charts[active_sheet].type >= 3) {
        sheet_status("Create a line, scatter or bar figure first.");
        return;
    }
    axes_draft = book->charts[active_sheet];
    axes_choosing = axes_select = 1;
    axes_focus = 0;
    *axes_status = 0;
    menu = -1;
}
static void sheet_axes_apply(void)
{
    SheetAxis axes[2];
    int dates = chart_date_axis(calculation,&axes_draft);
    int ydates = chart_y_date_axis(calculation,&axes_draft);
    if (!sheet_axis_parse(axes_draft.axes[0],dates,&axes[0]) || !sheet_axis_parse(axes_draft.axes[1],ydates,&axes[1])) {
        sheet_string(axes_status,sizeof(axes_status),"Check bounds and positive intervals; date units: d/w/mo/y.");
        return;
    }
    double lo[2] = {0,0}, hi[2] = {0,0};
    int count = 0;
    SheetChart coordinates = axes_draft;
    if (coordinates.configured) coordinates.x_mode = chart_x_mode(calculation,&coordinates);
    for (int series = 0; series < chart_series_count(&coordinates); ++series) {
        for (int r = chart_first_row(&coordinates); r <= coordinates.source.r1; ++r) {
            double point[2];
            if (!chart_point(calculation,&coordinates,r,series,&point[0],&point[1])) continue;
            for (int a = 0; a < 2; ++a) {
                if (!count || point[a] < lo[a]) lo[a] = point[a];
                if (!count || point[a] > hi[a]) hi[a] = point[a];
            }
            ++count;
        }
    }
    if (coordinates.type == 2 && !ydates) { lo[1] = fmin(0,lo[1]); hi[1] = fmax(0,hi[1]); }
    for (int a = 0; a < 2; ++a) {
        if (lo[a] == hi[a]) {
            double pad = a ? ydates ? 1 : fmax(1,fabs(lo[a])*0.1) : dates ? 1 : 0.5;
            lo[a] -= pad;
            hi[a] += pad;
            if ((!a && dates) || (a && ydates)) { lo[a] = fmax(1,lo[a]); hi[a] = fmin(2958465,hi[a]); }
        }
        if (count && !sheet_axis_resolve(&axes[a],lo[a],hi[a])) {
            sheet_string(axes_status,sizeof(axes_status),"Bounds must increase; use fewer than 2048 ticks per interval.");
            return;
        }
    }
    book->charts[active_sheet] = axes_draft;
    sheet_changed();
    axes_choosing = 0;
    chart_view = 1;
    sheet_status("Axis intervals start at the minimum. Blank fields use automatic settings.");
}
static void sheet_axes_draw(const SheetLayout *l)
{
    SheetDataLayout d = sheet_data_layout(l);
    sheet_host->draw_standard_button(d.x,d.y,d.w,d.h,"",0);
    sheet_host->pointer_region(d.x,d.y,d.w,d.h,BUDO_CURSOR_ARROW|BUDO_CURSOR_OVERLAY,NULL);
    ui_text(d.x+12,d.y+8,"Figure axes - blank fields = automatic",(d.w-24)/6);
    ui_text(d.x+12,d.y+28,"Dates: YYYY-MM-DD; intervals: 7d, 1w, 1mo, 1y",(d.w-24)/6);
    ui_text(d.x+12,d.y+40,"Ticks start at minimum. Tab: next; Enter: apply.",(d.w-24)/6);
    static const char *const labels[] = {"Minimum","Maximum","Major interval","Minor interval"};
    int width = (d.w-36)/2;
    for (int a = 0; a < 2; ++a) {
        int x = d.x+12+a*(width+12);
        ui_text(x,d.y+58,a ? "Y axis" : "X axis",width/6);
        for (int f = 0; f < 4; ++f) {
            ui_text(x,d.y+76+f*34,labels[f],width/6);
            char label[40];
            snprintf(label,sizeof(label),"%s%s",axes_focus == a*4+f ? "> " : "",*axes_draft.axes[a][f] ? axes_draft.axes[a][f] : "Auto");
            budo_button_draw(sheet_host,x,d.y+88+f*34,width,22,label,axes_focus == a*4+f ? BUDO_BUTTON_PRESSED : 0);
        }
    }
    ui_text(d.x+12,d.y+d.h-52,axes_status,(d.w-24)/6);
    budo_button_draw(sheet_host,d.x+12,d.y+d.h-30,80,22,"Reset auto",0);
    budo_button_draw(sheet_host,d.x+d.w-184,d.y+d.h-30,80,22,"Apply",BUDO_BUTTON_DEFAULT);
    budo_button_draw(sheet_host,d.x+d.w-96,d.y+d.h-30,80,22,"Cancel",0);
}
static int sheet_axes_mouse(const SheetLayout *l, int x, int y)
{
    SheetDataLayout d = sheet_data_layout(l);
    int width = (d.w-36)/2;
    for (int a = 0; a < 2; ++a) {
        for (int f = 0; f < 4; ++f) {
            if (sheet_host->point_in_rect(x,y,d.x+12+a*(width+12),d.y+88+f*34,width,22)) {
                axes_focus = a*4+f;
                axes_select = 1;
                return 1;
            }
        }
    }
    if (sheet_host->point_in_rect(x,y,d.x+12,d.y+d.h-30,80,22)) {
        memset(axes_draft.axes,0,sizeof(axes_draft.axes));
        axes_select = 1;
        *axes_status = 0;
    } else if (sheet_host->point_in_rect(x,y,d.x+d.w-184,d.y+d.h-30,80,22)) sheet_axes_apply();
    else if (sheet_host->point_in_rect(x,y,d.x+d.w-96,d.y+d.h-30,80,22)) axes_choosing = 0;
    return 1;
}
static int sheet_axes_key(int key)
{
    char *field = axes_draft.axes[axes_focus/4][axes_focus%4];
    size_t length = strlen(field);
    if (key == 27) axes_choosing = 0;
    else if (key == 13) sheet_axes_apply();
    else if (key == 9) { axes_focus = (axes_focus+1)%8; axes_select = 1; }
    else if (key == 1) axes_select = 1;
    else if (key == 8 || key == 127) {
        if (axes_select) memset(field,0,32);
        else if (length) field[length-1] = 0;
        axes_select = 0;
    } else if (key >= 32 && key <= 126) {
        if (axes_select) {
            length = 0;
            memset(field,0,32);
        }
        axes_select = 0;
        if (length < 31) { field[length] = (char)key; field[length+1] = 0; }
    }
    return 1;
}
static const char *const menu_labels[] = {"File","Edit","Format","Sheet","Figure","Help"};
static const int menu_offsets[] = {4,48,92,148,198,254};
static const char *const file_items[] = {"New  Ctrl+N","Open  Ctrl+O","Save  Ctrl+S","Save as...","Export CSV values...","Export TSV values...","Export figure SVG...","Export figure PCX..."};
static const char *const edit_items[] = {"Undo  Ctrl+Z","Redo  Ctrl+Y","Copy  Ctrl+C","Cut  Ctrl+X","Paste  Ctrl+V","Clear  Delete","Fill down  Ctrl+D","Fill right  Ctrl+R","Sort ascending","Sort descending"};
static const char *const format_items[] = {"General","Fixed (2 decimals)","Percent","Scientific","Money (2 decimals)","Date (YYYY-MM-DD)","Align automatic","Align left","Align center","Align right","Align middle","Align top","Align bottom","Column width...","Row height..."};
static const char *const tab_items[] = {"Add sheet","Rename sheet...","Previous sheet","Next sheet","Insert row","Delete row","Insert column","Delete column"};
static const char *const chart_items[] = {"Line from selection","Scatter from selection","Bar from selection","Histogram from selection","Pie from selection","Grid / Figure","Title...","X axis label...","Y axis label...","Horizontal major grid","Vertical major grid","Horizontal minor grid","Vertical minor grid","Data series...","Axes..."};
static const char *const help_items[] = {"Keyboard and formulas","Function catalog"};
static const char *const *sheet_menu_items(int which, int *count)
{
    static const int counts[] = {8,10,15,8,15,2};
    static const char *const *const lists[] = {file_items,edit_items,format_items,tab_items,chart_items,help_items};
    *count = counts[which];
    return lists[which];
}
static void sheet_menu_action(int which, int item)
{
    if (which == 0) {
        if (item == 0) sheet_action(SA_NEW);
        else if (item == 1) sheet_action(SA_OPEN);
        else if (item == 2) (void)sheet_save();
        else if (item == 3) sheet_dialog(SD_SAVE);
        else if (item == 4) sheet_dialog(SD_CSV);
        else if (item == 5) sheet_dialog(SD_TSV);
        else if (book->charts[active_sheet].enabled) sheet_dialog(item == 6 ? SD_SVG : SD_PCX);
        else sheet_status("Create a figure from a selection first.");
    } else if (which == 1) {
        if (item < 2) sheet_undo(item ? 1 : -1);
        else if (item == 2 || item == 3) {
            sheet_copy();
            if (item == 3) sheet_mutate(4);
        }
        else if (item == 4) sheet_mutate(3);
        else if (item == 5) sheet_mutate(4);
        else if (item < 8) sheet_mutate(item == 6 ? 1 : 2);
        else sheet_mutate(item == 8 ? 5 : 6);
    } else if (which == 2) {
        if (item < 6) sheet_mutate(10+item);
        else if (item < 13) sheet_mutate(20+item-6);
        else sheet_begin_edit(item == 13 ? 5 : 6,0);
    }
    else if (which == 3) {
        if (item >= 4) {
            int axis = item >= 6, delta = item%2 ? -1 : 1;
            if (!sheet_structure(staging,book,active_sheet,axis,axis ? col : row,delta)) {
                sheet_status("Structural edit would lose cells or exceed limits; workbook preserved.");
                return;
            }
            *book = *staging;
            sheet_changed();
            sheet_status("Rows/columns and cross-sheet references updated.");
            return;
        }
        if (item == 0) {
            if (book->tabs == SHEET_TABS) {
                sheet_status("Workbook supports at most 8 sheets.");
                return;
            }
            char name[32];
            int suffix = book->tabs + 1;
            for (;;) {
                int found = 0;
                snprintf(name,sizeof(name),"Sheet%d",suffix++);
                for (int i = 0; i < book->tabs; ++i) if (!strcmp(name,book->names[i])) found = 1;
                if (!found) break;
            }
            active_sheet = book->tabs++;
            sheet_string(book->names[active_sheet],32,name);
            sheet_changed();
        } else if (item == 1) sheet_begin_edit(1,0);
        else active_sheet = (active_sheet + (item == 2 ? book->tabs - 1 : 1)) % book->tabs;
        row = col = anchor_row = anchor_col = 0;
        sheet_hscroll.position = sheet_vscroll.position = 0;
        chart_view = 0;
    } else if (which == 4) {
        if (item < 5) sheet_chart_create(item);
        else if (item == 5) chart_view = book->charts[active_sheet].enabled ? !chart_view : 0;
        else if (item < 9) sheet_begin_edit(item - 4,0);
        else if (item == 13) sheet_data_open();
        else if (item == 14) sheet_axes_open();
        else {
            static const unsigned int flags[] = {SHEET_GRID_H_MAJOR,SHEET_GRID_V_MAJOR,SHEET_GRID_H_MINOR,SHEET_GRID_V_MINOR};
            book->charts[active_sheet].grids ^= flags[item-9];
            sheet_changed();
        }
    } else help_view = item + 1;
}
static void ui_text(int x,int y,const char *s,int chars)
{
    if (chars > 0) sheet_host->draw_text(x,y,s,ui_color(BUDO_SYS_COLOR_TEXT),chars);
}
static void sheet_screen_line(void *context,int x,int y,int xx,int yy,unsigned int color)
{
    (void)context;
    int dx = abs(xx-x), sx = x < xx ? 1 : -1, dy = -abs(yy-y), sy = y < yy ? 1 : -1, error = dx + dy;
    for (;;) {
        sheet_host->fill_rect_rgb(x,y,1,1,color);
        if (x == xx && y == yy) break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
}
static void sheet_screen_rect(void *context,int x,int y,int w,int h,unsigned int color)
{
    (void)context;
    sheet_host->fill_rect_rgb(x,y,w,h,color);
}
static void sheet_screen_text(void *context,int x,int y,const char *text,unsigned int color)
{
    SheetLayout *l = context;
    (void)color;
    if (x >= l->x && x < l->x+l->w && y >= l->y && y+8 <= l->y+l->h)
        ui_text(x,y,text,(l->x+l->w-x)/6);
}
static void sheet_draw_help(const SheetLayout *l)
{
    int x = l->x + 10, y = l->y + 72, w = l->w - 20, h = l->tabs - y;
    sheet_host->draw_sunken_panel(x,y,w,h,ui_color(BUDO_SYS_COLOR_SURFACE));
    if (help_view == 1) {
        const char *const lines[] = {
            "Spreadsheet - Enter/F2 edits; Esc cancels or returns to grid.",
            "Arrows/Tab navigate; Shift+arrows or drag selects a range.",
            "Ctrl+C/X/V: internal range clipboard; Z/Y: undo/redo.",
            "Ctrl+D/R: fill down/right. Two numbers seed a linear series.",
            "Ctrl+N/O/S: new/open/save. Delete clears the selection.",
            "Formulas: =SUM(A1:B5), =SIN(A1)*2, =$A$1+B2, =50%",
            "Cross-sheet: =Sheet2!A1 or ='Test data'!$B$4",
            "Operators: + - * / ^ % & = <> < > <= >=; radians for trig.",
            "IF and IFERROR evaluate only the branch that is required.",
            "Figures: select X/labels in first column, Y in following columns.",
            "Date/number X axes: choose columns in Figure > Data series.",
            "Histogram uses first column; pie supports 8 nonnegative rows.",
            "CSV/TSV import literal data; native .bsheet keeps all content.",
            "Limits: 8 sheets, 1024 rows x 128 columns, 8192 stored cells.",
            "192-byte cell storage; 16 undo snapshots; formulas depth <=64.",
            "Drag header edges to resize rows/columns; Format sets alignment.",
            "Figure menu toggles major/minor grids; File exports SVG or PCX.",
            "Money uses decimal display; calculations use double precision."
        };
        for (size_t i = 0; i < sizeof(lines)/sizeof(*lines) && y+12+(int)i*16+8 < l->tabs; ++i)
            ui_text(x+8,y+12+(int)i*16,lines[i],(w-16)/6);
    } else {
        int page_rows = sheet_max(1,(h-34)/16), offset = help_offset;
        ui_text(x+8,y+8,"Functions (Up/Down to scroll; Esc to close)",(w-16)/6);
        for (int i = 0; i < page_rows; ++i) {
            int idx = i + offset;
            if ((size_t)idx >= sizeof(sheet_functions)/sizeof(*sheet_functions)) break;
            ui_text(x+8,y+28+i*16,sheet_functions[idx].syntax,(w-16)/6);
        }
    }
}
static void sheet_draw(void)
{
    SheetLayout l;
    if (!book || !sheet_layout(&l)) return;
    sheet_host->fill_rect(l.x,l.y,l.w,l.h,ui_color(BUDO_SYS_COLOR_FACE));
    for (int i = 0; i < 6; ++i) budo_menu_bar_item(sheet_host,l.x+menu_offsets[i],l.y+2,i == 2 || i == 4 ? 54 : 42,menu_labels[i],menu == i);
    const char *const labels[] = {"Save","Undo","Redo","Fill v","Fill >","Figure"};
    for (int i = 0; i < 6; ++i) budo_button_draw(sheet_host,l.x+4+i*62,l.y+26,58,22,labels[i],0);
    char address[24], text[SHEET_TEXT];
    sheet_address(row,col,address,sizeof(address));
    ui_text(l.x+6,l.y+59,address,5);
    sheet_host->draw_sunken_panel(l.x+44,l.y+53,l.w-48,23,ui_color(BUDO_SYS_COLOR_SURFACE));
    const SheetCell *current = sheet_cell(book,active_sheet,row,col);
    int input_width = (l.w-62)/6, offset = editing && edit_cursor >= input_width ? edit_cursor-input_width+1 : 0;
    const char *input = editing ? edit_buffer : current ? current->raw : "";
    if (editing && edit_select) sheet_host->fill_rect(l.x+47,l.y+56,l.w-54,17,ui_color(BUDO_SYS_COLOR_SELECTION_BG));
    sheet_host->draw_text(l.x+49,l.y+60,input+offset,ui_color(editing && edit_select ? BUDO_SYS_COLOR_SELECTION_TEXT : BUDO_SYS_COLOR_TEXT),input_width);
    if (editing) budo_text_caret(sheet_host,l.x+49+(edit_cursor-offset)*6,l.y+57,15,ui_color(BUDO_SYS_COLOR_TEXT));
    sheet_host->pointer_region(l.x+44,l.y+53,l.w-48,23,BUDO_CURSOR_TEXT,"Edit cell, sheet name, or figure label. Enter applies; Esc cancels.");
    if (chart_view && book->charts[active_sheet].enabled && !help_view) {
        SheetCanvas v = {.context=&l,.line=sheet_screen_line,.rect=sheet_screen_rect,.text=sheet_screen_text};
        (void)sheet_chart_render(&v,calculation,&book->charts[active_sheet],l.x+4,l.y+80,l.w-8,l.tabs-l.y-84);
    } else if (!help_view) {
        SheetRange a = selection();
        for (int c = 0; c < l.cols; ++c) {
            int x = l.gx+l.xs[c], width = l.xs[c+1]-l.xs[c];
            sheet_address(0,first_col+c,address,sizeof(address));
            char *digit = address;
            while (isalpha((unsigned char)*digit)) ++digit;
            *digit = 0;
            if (width >= 12) budo_button_draw(sheet_host,x,l.gy-16,width,16,address,0);
        }
        for (int r = 0; r < l.rows; ++r) {
            int y = l.gy+l.ys[r], height = l.ys[r+1]-l.ys[r];
            snprintf(text,sizeof(text),"%d",first_row+r+1);
            if (height >= 12) budo_button_draw(sheet_host,l.x+2,y,34,height,text,0);
            for (int c = 0; c < l.cols; ++c) {
                int rr = first_row+r, cc = first_col+c, x = l.gx+l.xs[c], width = l.xs[c+1]-l.xs[c];
                int selected = rr >= a.r0 && rr <= a.r1 && cc >= a.c0 && cc <= a.c1;
                sheet_host->fill_rect(x,y,width,height,ui_color(selected ? BUDO_SYS_COLOR_SELECTION_BG : BUDO_SYS_COLOR_SURFACE));
                sheet_host->draw_rect(x,y,width,height,ui_color(BUDO_SYS_COLOR_MIDGRAY));
                const SheetCell *cell = sheet_cell(book,active_sheet,rr,cc);
                SheetValue v = sheet_eval(calculation,active_sheet,rr,cc);
                sheet_display(v,cell ? cell->format : 0,text,sizeof(text));
                int chars = sheet_min(SHEET_TEXT-1,(width-8)/6);
                if (chars > 0 && height >= 12) {
                    if (v.type == SV_NUMBER && (int)strlen(text) > chars) {
                        memset(text,'#',(size_t)chars);
                        text[chars] = 0;
                    }
                    int length = sheet_min(chars,(int)strlen(text));
                    int horizontal = cell ? cell->align_h : 0, vertical = cell ? cell->align_v : 0;
                    if (!horizontal) horizontal = v.type == SV_NUMBER ? 3 : 1;
                    int tx = horizontal == 3 ? x+width-4-length*6 : horizontal == 2 ? x+(width-length*6)/2 : x+4;
                    int ty = vertical == 1 ? y+3 : vertical == 2 ? y+height-11 : y+(height-8)/2;
                    sheet_host->draw_text(tx,ty,text,ui_color(selected ? BUDO_SYS_COLOR_SELECTION_TEXT : BUDO_SYS_COLOR_TEXT),chars);
                }
                if (rr == row && cc == col && width > 3 && height > 3)
                    sheet_host->draw_rect(x+1,y+1,width-2,height-2,ui_color(BUDO_SYS_COLOR_TEXT));
            }
        }
        if (a.r1 >= first_row && a.r1 < first_row+l.rows && a.c1 >= first_col && a.c1 < first_col+l.cols &&
            l.xs[a.c1-first_col+1]-l.xs[a.c1-first_col] >= 7 && l.ys[a.r1-first_row+1]-l.ys[a.r1-first_row] >= 7) {
            int hx = l.gx+l.xs[a.c1-first_col+1]-5, hy = l.gy+l.ys[a.r1-first_row+1]-5;
            sheet_host->fill_rect(hx,hy,5,5,ui_color(BUDO_SYS_COLOR_TEXT));
            sheet_host->pointer_region(hx-2,hy-2,7,7,BUDO_CURSOR_CROSSHAIR,"Drag right or down to autofill formulas, patterns, or a two-number series.");
        }
        /* Register splitters after all header buttons so both sides drag immediately. */
        for (int c = 0; c < l.cols; ++c) {
            if (l.xs[c+1]-l.xs[c] == sheet_width(book,active_sheet,first_col+c))
                sheet_host->pointer_region(l.gx+l.xs[c+1]-3,l.gy-16,6,16,BUDO_CURSOR_RESIZE_EW,"Drag to resize column");
        }
        for (int r = 0; r < l.rows; ++r) {
            if (l.ys[r+1]-l.ys[r] == sheet_height(book,active_sheet,first_row+r))
                sheet_host->pointer_region(l.x+2,l.gy+l.ys[r+1]-3,34,6,BUDO_CURSOR_RESIZE_NS,"Drag to resize row");
        }
        budo_scroll_draw(sheet_host,&sheet_hscroll);
        budo_scroll_draw(sheet_host,&sheet_vscroll);
        sheet_host->pointer_region(l.gx,l.gy,l.gw,l.gh,BUDO_CURSOR_CROSSHAIR,"Drag to select cells; Enter edits. Fill down/right in Edit menu.");
    }
    for (int i = 0; i < book->tabs; ++i) {
        int tw = sheet_min(88,(l.w-34)/book->tabs);
        budo_button_draw(sheet_host,l.x+4+i*tw,l.tabs,tw-2,20,book->names[i],i == active_sheet ? BUDO_BUTTON_PRESSED : 0);
    }
    budo_button_draw(sheet_host,l.x+l.w-26,l.tabs,22,20,"+",0);
    sheet_host->fill_rect(l.x,l.status,l.w,l.y+l.h-l.status,ui_color(BUDO_SYS_COLOR_STATUS_BG));
    SheetRange a = selection();
    double sum = 0;
    int count = 0;
    /* Selection aggregates visit populated cells only. */
    for (int i = 0; i < book->count; ++i) {
        int key = book->cells[i].key, r = key/SHEET_COLS%SHEET_ROWS, c = key%SHEET_COLS;
        if (key/(SHEET_ROWS*SHEET_COLS) != active_sheet || r < a.r0 || r > a.r1 || c < a.c0 || c > a.c1) continue;
        SheetValue v = sheet_eval(calculation,active_sheet,r,c);
        if (v.type == SV_NUMBER) {
            sum += v.number;
            ++count;
        }
    }
    snprintf(text,sizeof(text),"n=%d Sum=%.8g Avg=%.8g  %.90s",count,sum,count ? sum/count : 0,status);
    ui_text(l.x+4,l.status+3,text,(l.w-8)/6);
    if (help_view) sheet_draw_help(&l);
    if (data_choosing) sheet_data_draw(&l);
    if (axes_choosing) sheet_axes_draw(&l);
    if (menu >= 0) {
        int count_items;
        const char *const *items = sheet_menu_items(menu,&count_items);
        BudoMenuItem entries[16];
        for (int i = 0; i < count_items; ++i) {
            entries[i] = (BudoMenuItem){items[i],1,0};
            if (menu == 4 && i >= 9 && i < 13) entries[i].checked = (book->charts[active_sheet].grids & (1U << (i-9))) != 0;
        }
        budo_menu_items_draw(sheet_host,l.x+sheet_min(menu_offsets[menu],l.w-214),l.y+22,214,entries,count_items);
    }
}
static int sheet_hit_cell(const SheetLayout *l,int x,int y,int *r,int *c)
{
    if (x < l->gx || x >= l->gx+l->gw || y < l->gy || y >= l->gy+l->gh) return 0;
    int ri = 0, ci = 0;
    while (ri < l->rows && y-l->gy >= l->ys[ri+1]) ++ri;
    while (ci < l->cols && x-l->gx >= l->xs[ci+1]) ++ci;
    if (ri == l->rows || ci == l->cols) return 0;
    *r = first_row+ri;
    *c = first_col+ci;
    return 1;
}
static int sheet_mouse_down(int x,int y,int buttons)
{
    SheetLayout l;
    if (!(buttons&1) || !book || !sheet_layout(&l)) return 0;
    if (confirming || dialog_kind) return 1;
    if (axes_choosing) return sheet_axes_mouse(&l,x,y);
    if (data_choosing) return sheet_data_mouse(&l,x,y);
    if (editing) {
        sheet_edit_commit();
        if (editing) return 1;
    }
    if (help_view) {
        help_view = 0;
        return 1;
    }
    for (int i = 0; i < 6; ++i) {
        if (sheet_host->point_in_rect(x,y,l.x+menu_offsets[i],l.y+2,i == 2 || i == 4 ? 54 : 42,18)) {
            menu = menu == i ? -1 : i;
            return 1;
        }
    }
    if (menu >= 0) {
        int count, which = menu;
        (void)sheet_menu_items(menu,&count);
        int item = budo_menu_hit(x,y,l.x+sheet_min(menu_offsets[menu],l.w-214),l.y+22,214,count);
        menu = -1;
        if (item >= 0) sheet_menu_action(which,item);
        return 1;
    }
    for (int i = 0; i < 6; ++i) {
        if (sheet_host->point_in_rect(x,y,l.x+4+i*62,l.y+26,58,22)) {
            if (i == 0) (void)sheet_save();
            else if (i < 3) sheet_undo(i == 1 ? -1 : 1);
            else if (i < 5) sheet_mutate(i == 3 ? 1 : 2);
            else if (book->charts[active_sheet].enabled) chart_view = !chart_view;
            else sheet_chart_create(0);
            return 1;
        }
    }
    if (sheet_host->point_in_rect(x,y,l.x+44,l.y+53,l.w-48,23)) {
        sheet_begin_edit(0,0);
        return 1;
    }
    for (int i = 0; i < book->tabs; ++i) {
        int tw = sheet_min(88,(l.w-34)/book->tabs);
        if (sheet_host->point_in_rect(x,y,l.x+4+i*tw,l.tabs,tw-2,20)) {
            active_sheet = i;
            chart_view = 0;
            row = col = anchor_row = anchor_col = 0;
            sheet_hscroll.position = sheet_vscroll.position = 0;
            return 1;
        }
    }
    if (sheet_host->point_in_rect(x,y,l.x+l.w-26,l.tabs,22,20)) {
        sheet_menu_action(3,0);
        return 1;
    }
    if (chart_view) return 1;
    for (int c = 0; c < l.cols; ++c) {
        int edge = l.gx+l.xs[c+1];
        if (l.xs[c+1]-l.xs[c] == sheet_width(book,active_sheet,first_col+c) &&
            sheet_host->point_in_rect(x,y,edge-3,l.gy-16,6,16)) {
            resize_axis = 1;
            resize_index = first_col+c;
            resize_start = x;
            resize_original = book->widths[active_sheet][resize_index];
            return 1;
        }
    }
    for (int r = 0; r < l.rows; ++r) {
        int edge = l.gy+l.ys[r+1];
        if (l.ys[r+1]-l.ys[r] == sheet_height(book,active_sheet,first_row+r) &&
            sheet_host->point_in_rect(x,y,l.x+2,edge-3,34,6)) {
            resize_axis = 2;
            resize_index = first_row+r;
            resize_start = y;
            resize_original = book->heights[active_sheet][resize_index];
            return 1;
        }
    }
    if (budo_scroll_pointer_host(sheet_host,&sheet_hscroll,x,y,BUDO_POINTER_DOWN) ||
        budo_scroll_pointer_host(sheet_host,&sheet_vscroll,x,y,BUDO_POINTER_DOWN)) return 1;
    SheetRange a = selection();
    int visible_end = a.r1 >= first_row && a.r1 < first_row+l.rows && a.c1 >= first_col && a.c1 < first_col+l.cols;
    int hx = visible_end ? l.gx+l.xs[a.c1-first_col+1]-5 : -100;
    int hy = visible_end ? l.gy+l.ys[a.r1-first_row+1]-5 : -100;
    if (visible_end && l.xs[a.c1-first_col+1]-l.xs[a.c1-first_col] >= 7 &&
        l.ys[a.r1-first_row+1]-l.ys[a.r1-first_row] >= 7 && sheet_host->point_in_rect(x,y,hx-2,hy-2,7,7)) {
        fill_seed = a;
        fill_drag = dragging = 1;
        fill_horizontal = 0;
        last_cell_key = -1;
        return 1;
    }
    int r,c;
    if (sheet_hit_cell(&l,x,y,&r,&c)) {
        int key = sheet_key(active_sheet,r,c);
        unsigned long long now = sheet_host->get_time_ms();
        if (last_cell_key == key && now-last_cell_click < 350 && !(sheet_host->get_keyboard_modifiers()&7)) {
            row = anchor_row = r;
            col = anchor_col = c;
            last_cell_key = -1;
            sheet_begin_edit(0,0);
            return 1;
        }
        last_cell_key = key;
        last_cell_click = now;
        if (!(sheet_host->get_keyboard_modifiers()&3)) {
            anchor_row = r;
            anchor_col = c;
        }
        row = r;
        col = c;
        dragging = 1;
        return 1;
    }
    return 0;
}
static int sheet_mouse_move(int x,int y,int buttons)
{
    SheetLayout l;
    if (!book || confirming || dialog_kind || editing || data_choosing || axes_choosing || chart_view || help_view || menu >= 0 || !sheet_layout(&l)) return 0;
    if (resize_axis) {
        unsigned short *size = resize_axis == 1 ? &book->widths[active_sheet][resize_index] : &book->heights[active_sheet][resize_index];
        int original = resize_original ? resize_original : resize_axis == 1 ? SHEET_COL_WIDTH : SHEET_ROW_HEIGHT;
        *size = (unsigned short)sheet_clamp(original+(resize_axis == 1 ? x : y)-resize_start,SHEET_SIZE_MIN,SHEET_SIZE_MAX);
        return 1;
    }
    if (budo_scroll_pointer_host(sheet_host,&sheet_hscroll,x,y,BUDO_POINTER_MOVE) ||
        budo_scroll_pointer_host(sheet_host,&sheet_vscroll,x,y,BUDO_POINTER_MOVE)) return 1;
    if (!dragging || !(buttons&1)) return 0;
    if (x < l.gx) --sheet_hscroll.position;
    if (x >= l.gx+l.gw) ++sheet_hscroll.position;
    if (y < l.gy) --sheet_vscroll.position;
    if (y >= l.gy+l.gh) ++sheet_vscroll.position;
    (void)sheet_layout(&l);
    int hit_r, hit_c;
    if (sheet_hit_cell(&l,sheet_clamp(x,l.gx,l.gx+l.xs[l.cols]-1),
                       sheet_clamp(y,l.gy,l.gy+l.ys[l.rows]-1),&hit_r,&hit_c)) {
        row = hit_r;
        col = hit_c;
    }
    if (fill_drag) {
        fill_horizontal = col > fill_seed.c1 && (row <= fill_seed.r1 || col-fill_seed.c1 > row-fill_seed.r1);
        anchor_row = fill_seed.r0;
        anchor_col = fill_seed.c0;
        if (fill_horizontal) {
            row = fill_seed.r1;
            col = sheet_max(col,fill_seed.c1);
        }
        else {
            col = fill_seed.c1;
            row = sheet_max(row,fill_seed.r1);
        }
    }
    return 1;
}
static int sheet_mouse_up(int x,int y,int buttons)
{
    (void)buttons;
    if (resize_axis) {
        (void)sheet_mouse_move(x,y,1);
        resize_axis = 0;
        sheet_changed();
        return 1;
    }
    if (fill_drag) {
        *staging = *book;
        if (sheet_extend_fill(staging,book,fill_seed,selection(),fill_horizontal)) {
            *book = *staging;
            sheet_changed();
        }
        else sheet_status("Autofill exceeds limits; workbook preserved.");
        fill_drag = 0;
    }
    dragging = 0;
    (void)budo_scroll_pointer_host(sheet_host,&sheet_hscroll,x,y,BUDO_POINTER_UP);
    (void)budo_scroll_pointer_host(sheet_host,&sheet_vscroll,x,y,BUDO_POINTER_UP);
    return 1;
}
static int sheet_edit_key(int k)
{
    size_t n = strlen(edit_buffer);
    if (k == 27) {
        editing = 0;
        return 1;
    }
    if (k == 19) {
        sheet_edit_commit();
        if (!editing) (void)sheet_save();
        return 1;
    }
    if (k == 13 || k == 9) {
        sheet_edit_commit();
        if (!editing && !edit_kind) {
            if (k == 9) col = sheet_min(SHEET_COLS-1,col+1);
            else row = sheet_min(SHEET_ROWS-1,row+1);
            anchor_row = row;
            anchor_col = col;
            sheet_reveal();
        }
        return 1;
    }
    if (k == 1) {
        edit_select = 1;
        return 1;
    }
    if (k == (0x100|75) || k == (0x100|77) || k == (0x100|71) || k == (0x100|79)) {
        edit_cursor = k == (0x100|71) ? 0 : k == (0x100|79) ? (int)n :
            sheet_clamp(edit_cursor+(k == (0x100|75) ? -1 : 1),0,(int)n);
        edit_select = 0;
        return 1;
    }
    int erase = k == 8 || k == 127 || k == (0x100|83);
    if ((k >= 32 && k < 127) || erase) {
        if (edit_select) {
            edit_buffer[0] = 0;
            n = 0;
            edit_cursor = 0;
            edit_select = 0;
        }
        if (erase) {
            int at = k == 8 ? edit_cursor-1 : edit_cursor;
            if (at >= 0 && at < (int)n) {
                memmove(edit_buffer+at,edit_buffer+at+1,n-(size_t)at);
                if (k == 8) --edit_cursor;
            }
        } else if (n+1 < sizeof(edit_buffer)) {
            memmove(edit_buffer+edit_cursor+1,edit_buffer+edit_cursor,n-(size_t)edit_cursor+1);
            edit_buffer[edit_cursor++] = (char)k;
        } else sheet_status("Cell text limit reached.");
    }
    return 1;
}
static int sheet_key_input(int k)
{
    if (!book) return 0;
    if (dialog_kind || confirming) return 1;
    if (axes_choosing) return sheet_axes_key(k);
    if (data_choosing) return sheet_data_key(k);
    if (resize_axis) {
        if (k == 27) {
            unsigned short *size = resize_axis == 1 ? &book->widths[active_sheet][resize_index] : &book->heights[active_sheet][resize_index];
            *size = (unsigned short)resize_original;
            resize_axis = 0;
        }
        return 1;
    }
    if (editing) return sheet_edit_key(k);
    if (help_view) {
        if (k == 27) help_view = 0;
        else if (k == (0x100|72)) help_offset = sheet_max(0,help_offset-1);
        else if (k == (0x100|80)) help_offset = sheet_min((int)(sizeof(sheet_functions)/sizeof(*sheet_functions))-1,help_offset+1);
        return 1;
    }
    if (k == 27) {
        menu = -1;
        chart_view = 0;
        dragging = fill_drag = 0;
        return 1;
    }
    if (k == 14) sheet_action(SA_NEW);
    else if (k == 15) sheet_action(SA_OPEN);
    else if (k == 19) (void)sheet_save();
    else if (k == 26) sheet_undo(-1);
    else if (k == 25) sheet_undo(1);
    else if (k == 3 || k == 24) {
        sheet_copy();
        if (k == 24) sheet_mutate(4);
    }
    else if (k == 22) sheet_mutate(3);
    else if (k == 4) sheet_mutate(1);
    else if (k == 18) sheet_mutate(2);
    else if (k == (0x100|59)) help_view = 1;
    else if (k == 1) {
        anchor_row = anchor_col = 0;
        row = SHEET_ROWS-1;
        col = SHEET_COLS-1;
    }
    else if (k == (0x100|83) || k == 127) sheet_mutate(4);
    else if (chart_view) return 1;
    else if (k == 13 || k == (0x100|60)) sheet_begin_edit(0,0);
    else if (k >= 32 && k < 127) {
        sheet_begin_edit(0,1);
        (void)sheet_edit_key(k);
    }
    else {
        unsigned int mods = sheet_host->get_keyboard_modifiers();
        int shift = mods&3;
        if (k == 9) col = sheet_clamp(col+(shift ? -1 : 1),0,SHEET_COLS-1);
        else if (k == (0x100|75)) col = sheet_max(0,col-1);
        else if (k == (0x100|77)) col = sheet_min(SHEET_COLS-1,col+1);
        else if (k == (0x100|72)) row = sheet_max(0,row-1);
        else if (k == (0x100|80)) row = sheet_min(SHEET_ROWS-1,row+1);
        else if (k == (0x100|73)) row = sheet_max(0,row-10);
        else if (k == (0x100|81)) row = sheet_min(SHEET_ROWS-1,row+10);
        else if (k == (0x100|201)) --sheet_vscroll.position;
        else if (k == (0x100|202)) ++sheet_vscroll.position;
        else if (k == (0x100|71)) {
            col = 0;
            if (mods&4) row = 0;
        }
        else return 0;
        if (!shift || k == 9) {
            anchor_row = row;
            anchor_col = col;
        }
        if (k != (0x100|201) && k != (0x100|202)) sheet_reveal();
    }
    return 1;
}
static void sheet_close(void)
{
    free(book);
    free(staging);
    free(snapshots);
    free(clipboard);
    free(calculation);
    book = staging = snapshots = clipboard = NULL;
    calculation = NULL;
}
static int sheet_allocate(void)
{
    if (book) return 1;
    book = calloc(1,sizeof(*book));
    staging = calloc(1,sizeof(*staging));
    snapshots = calloc(SHEET_HISTORY,sizeof(*snapshots));
    clipboard = calloc(1,sizeof(*clipboard));
    calculation = calloc(1,sizeof(*calculation));
    if (!book || !staging || !snapshots || !clipboard || !calculation) {
        perror("Spreadsheet: allocate workbook");
        sheet_close();
        return 0;
    }
    sheet_new(book);
    saved_hash = sheet_hash();
    sheet_history_reset();
    sheet_status("Enter edits a cell. F1 shows formulas and shortcuts.");
    return 1;
}
static int sheet_open(void)
{
    if (!sheet_allocate()) return 0;
    if (!sheet_host->window_create(12,20,620,450,"Spreadsheet",BUDO_WINDOW_DEFAULT_BUTTONS) ||
        !sheet_host->window_set_min_size(460,320)) { sheet_close();
        return 0;
    }
    sheet_title();
    return 1;
}
static int sheet_open_file(const char *path)
{
    if (!path || !sheet_allocate()) return 0;
    if (dirty) {
        sheet_string(pending_path,sizeof(pending_path),path);
        sheet_action(SA_PATH);
        return 1;
    }
    if (!sheet_load(path)) {
        sheet_close();
        return 0;
    }
    return sheet_open();
}
static void sheet_icon(int x,int y)
{
    sheet_host->fill_rect_rgb(x+3,y+2,26,28,0xf0f0e8);
    sheet_host->fill_rect_rgb(x+3,y+2,26,6,0x208040);
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c)
        sheet_host->fill_rect_rgb(x+5+c*8,y+10+r*5,6,3,r == 2 && c == 1 ? 0x208040 : 0xb8c8b8);
}
int bwa_entry(const BwaHostApi *host,BwaAppDefinition *app)
{
    if (!host || !app || host->abi_major != BWA_ABI_MAJOR || host->abi_minor < 14 ||
        !host->window_create || !host->window_get_client_rect || !host->window_set_min_size ||
        !host->window_set_title || !host->window_close || !host->get_system_color || !host->get_system_metric ||
        !host->fill_rect || !host->fill_rect_rgb || !host->draw_rect || !host->draw_text ||
        !host->draw_standard_button || !host->draw_button_state || !host->draw_sunken_panel || !host->point_in_rect ||
        !host->file_dialog || !host->confirm_dialog || !host->pointer_region || !host->get_time_ms || !host->get_keyboard_modifiers) return 0;
    sheet_host = host;
    budo_app_definition_clear(host,app);
    app->app_id = "spreadsheet";
    app->name = "Spreadsheet";
    app->callbacks.open = sheet_open;
    app->callbacks.open_file = sheet_open_file;
    app->callbacks.draw = sheet_draw;
    app->callbacks.draw_icon = sheet_icon;
    app->callbacks.mouse_down = sheet_mouse_down;
    app->callbacks.mouse_move = sheet_mouse_move;
    app->callbacks.mouse_up = sheet_mouse_up;
    app->callbacks.key = sheet_key_input;
    app->callbacks.close = sheet_close;
    app->request_close = sheet_request_close;
    app->file_selected = sheet_file_selected;
    app->confirm_result = sheet_confirm;
    return 1;
}
