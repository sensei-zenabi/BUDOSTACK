#ifndef BUDO_SHEET_CHART_DATA_H
#define BUDO_SHEET_CHART_DATA_H
#include "sheet_formula.h"

static inline int chart_series_count(const SheetChart *chart)
{
    return chart->configured ? chart->y_count : sheet_min(SHEET_SERIES,sheet_max(1,chart->source.c1-chart->source.c0));
}
static inline int chart_series_column(const SheetChart *chart, int series)
{
    if (chart->configured) return chart->y_cols[series];
    return chart->source.c0+(chart->source.c1 > chart->source.c0 ? series+1 : 0);
}
static inline int chart_first_row(const SheetChart *chart)
{
    return chart->source.r0+(chart->configured && chart->header);
}
static inline void chart_column_label(SheetCalc *calc, const SheetChart *chart, int column, char *out, size_t size)
{
    SheetValue heading = sheet_eval(calc,chart->source.sheet,chart->source.r0,column);
    if ((!chart->configured || chart->header) && heading.type == SV_TEXT && *heading.text) {
        sheet_string(out,size,heading.text);
    } else {
        char address[24];
        sheet_address(0,column,address,sizeof(address));
        char *digit = address;
        while (isalpha((unsigned char)*digit)) ++digit;
        *digit = 0;
        snprintf(out,size,"Column %s",address);
    }
}
static inline void chart_default_labels(SheetCalc *calc, SheetChart *chart)
{
    chart_column_label(calc,chart,chart->configured ? chart->x_col : chart->source.c0,chart->xlabel,sizeof(chart->xlabel));
    if (chart_series_count(chart) == 1)
        chart_column_label(calc,chart,chart_series_column(chart,0),chart->ylabel,sizeof(chart->ylabel));
    else sheet_string(chart->ylabel,sizeof(chart->ylabel),"Value");
}
static inline void chart_configure(SheetCalc *calc, SheetChart *chart)
{
    chart->configured = 1;
    chart->x_col = chart->source.c0;
    chart->y_count = sheet_min(SHEET_SERIES,sheet_max(1,chart->source.c1-chart->source.c0));
    memset(chart->y_cols,0,sizeof(chart->y_cols));
    for (int i = 0; i < chart->y_count; ++i)
        chart->y_cols[i] = chart->source.c0+(chart->source.c1 > chart->source.c0 ? i+1 : 0);
    chart->x_mode = chart->source.c1 > chart->source.c0 || chart->type == 1 ? SHEET_X_AUTO : SHEET_X_SAMPLE;
    SheetValue first = sheet_eval(calc,chart->source.sheet,chart->source.r0,chart->x_col);
    chart->header = chart->source.r1 > chart->source.r0 && first.type == SV_TEXT;
    if (chart->header) {
        for (int i = 0; i < chart->y_count; ++i) {
            SheetValue heading = sheet_eval(calc,chart->source.sheet,chart->source.r0,chart->y_cols[i]);
            if (heading.type != SV_TEXT && heading.type != SV_BLANK) chart->header = 0;
        }
    }
}
static inline int chart_date_axis(SheetCalc *calc, const SheetChart *chart)
{
    if (!chart->configured) return 0;
    if (chart->x_mode == SHEET_X_DATE) return 1;
    if (chart->x_mode != SHEET_X_AUTO) return 0;
    for (int row = chart_first_row(chart); row <= chart->source.r1; ++row) {
        SheetValue value = sheet_eval(calc,chart->source.sheet,row,chart->x_col);
        if (value.type == SV_BLANK || value.type == SV_ERROR) continue;
        const SheetCell *cell = sheet_cell(calc->book,chart->source.sheet,row,chart->x_col);
        double serial;
        if (value.date || (cell && cell->format == 5) ||
            (value.type == SV_TEXT && sheet_iso_date(value.text,&serial))) return 1;
    }
    return 0;
}
static inline int chart_y_date_axis(SheetCalc *calc, const SheetChart *chart)
{
    for (int s = 0; s < chart_series_count(chart); ++s) {
        int column = chart_series_column(chart,s);
        for (int row = chart_first_row(chart); row <= chart->source.r1; ++row) {
            SheetValue value = sheet_eval(calc,chart->source.sheet,row,column);
            const SheetCell *cell = sheet_cell(calc->book,chart->source.sheet,row,column);
            if (value.type == SV_NUMBER && (value.date || (cell && cell->format == 5))) return 1;
        }
    }
    return 0;
}
/* Resolve once per renderer so a date column cannot mix dates and row indices. */
static inline int chart_x_mode(SheetCalc *calc, const SheetChart *chart)
{
    if (!chart->configured) return chart->type == 1 ? SHEET_X_NUMBER : SHEET_X_SAMPLE;
    if (chart->x_mode != SHEET_X_AUTO) return chart->x_mode;
    if (chart_date_axis(calc,chart)) return SHEET_X_DATE;
    if (chart->type != 1) {
        for (int row = chart_first_row(chart); row <= chart->source.r1; ++row) {
            SheetValue value = sheet_eval(calc,chart->source.sheet,row,chart->x_col);
            if (value.type == SV_TEXT) return SHEET_X_SAMPLE;
        }
    }
    return SHEET_X_NUMBER;
}
static inline int chart_point(SheetCalc *calc, const SheetChart *chart, int row, int series, double *x, double *y)
{
    if (row < chart_first_row(chart)) return 0;
    SheetRange a = chart->source;
    int x_column = chart->configured ? chart->x_col : a.c0;
    int column = chart_series_column(chart,series);
    SheetValue xv = sheet_eval(calc,a.sheet,row,x_column);
    SheetValue yv = sheet_eval(calc,a.sheet,row,column);
    if (yv.type != SV_NUMBER) return 0;
    int mode = chart_x_mode(calc,chart);
    if (mode == SHEET_X_SAMPLE) *x = row-chart_first_row(chart);
    else if (xv.type == SV_NUMBER) {
        if (mode == SHEET_X_DATE) {
            int year,month,day;
            if (!sheet_date_parts(xv.number,&year,&month,&day)) return 0;
        }
        *x = xv.number;
    } else if (xv.type == SV_TEXT && mode == SHEET_X_DATE && sheet_iso_date(xv.text,x)) {
        /* ISO dates returned by text formulas or imported as literal text. */
    } else return 0;
    *y = yv.number;
    return 1;
}
#endif
