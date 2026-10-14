#ifndef BUDO_SHEET_AXIS_H
#define BUDO_SHEET_AXIS_H
#include "sheet_formula.h"

/* Units are deliberately independent for major and minor intervals. Calendar
 * ticks are calculated from the original anchor, avoiding month-end drift. */
typedef struct SheetAxis {
    double value[4];
    int set[4], unit[4]; /* 0: numeric/days, 1: months, 2: years */
    double min, max;
} SheetAxis;
#define SHEET_AXIS_TICKS 2048
/* dates: 0 numeric, 1 date domain, -1 syntax-only workbook validation. */
static inline int sheet_axis_parse(const void *fields_data, int dates, SheetAxis *axis)
{
    const char *fields = fields_data;
    memset(axis,0,sizeof(*axis));
    for (int i = 0; i < 4; ++i) {
        const char *p = fields+i*32;
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) continue;
        double value;
        char *end;
        if (i < 2 && dates && sheet_iso_date(p,&value)) end = (char *)p+strlen(p);
        else {
            value = strtod(p,&end);
            if (end == p || !isfinite(value)) return 0;
        }
        if (i >= 2 && dates) {
            if (!strncmp(end,"mo",2)) { axis->unit[i] = 1; end += 2; }
            else if (*end == 'y') { axis->unit[i] = 2; ++end; }
            else if (*end == 'w') { value *= 7; ++end; }
            else if (*end == 'd') ++end;
        }
        while (isspace((unsigned char)*end)) ++end;
        if (*end || !isfinite(value) || (i >= 2 && value <= 0)) return 0;
        if (axis->unit[i] && (value != floor(value) || value > 120000)) return 0;
        if (i < 2 && dates > 0 && (value < 1 || value > 2958465)) return 0;
        axis->set[i] = 1;
        axis->value[i] = value;
    }
    return !(axis->set[0] && axis->set[1] && axis->value[0] >= axis->value[1]);
}
static inline double sheet_axis_tick(const SheetAxis *a, int field, int index)
{
    double step = a->set[field] ? a->value[field] : (a->max-a->min)/(field == 2 ? 4 : 20);
    if (!a->unit[field]) return a->min+index*step;
    int year, month, day;
    if (!sheet_date_parts(a->min,&year,&month,&day)) return INFINITY;
    double months = (double)(year-1900)*12+month-1+(double)index*step*(a->unit[field] == 2 ? 12 : 1);
    if (months < 0 || months >= 97200) return INFINITY;
    year = 1900+(int)months/12;
    month = (int)months%12+1;
    double first = sheet_date_first(year,month);
    double next = month == 12 ? sheet_date_first(year+1,1) : sheet_date_first(year,month+1);
    return first+fmin(day-1,next-first-1)+(a->min-floor(a->min));
}
static inline int sheet_axis_resolve(SheetAxis *a, double min, double max)
{
    a->min = a->set[0] ? a->value[0] : min;
    a->max = a->set[1] ? a->value[1] : max;
    double span = a->max-a->min;
    if (!isfinite(span) || span <= 0) return 0;
    for (int field = 2; field < 4; ++field) {
        double first = sheet_axis_tick(a,field,1);
        if (first <= a->min || !isfinite(first-a->min)) {
            if (first == INFINITY && a->unit[field]) continue;
            return 0;
        }
        /* Reject excessive density rather than silently changing user spacing. */
        if (sheet_axis_tick(a,field,SHEET_AXIS_TICKS) <= a->max) return 0;
    }
    return 1;
}
#endif
