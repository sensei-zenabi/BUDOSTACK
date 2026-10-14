#ifndef BUDO_SHEET_FORMULA_H
#define BUDO_SHEET_FORMULA_H
#include "sheet_model.h"
#include <time.h>

enum { SV_NUMBER, SV_TEXT, SV_BLANK, SV_RANGE, SV_ERROR };
enum { SE_REF = 1, SE_VALUE, SE_DIV, SE_NAME, SE_NUM, SE_CYCLE, SE_LIMIT };
static const char *const sheet_errors[] = {"", "#REF!", "#VALUE!", "#DIV/0!", "#NAME?", "#NUM!", "#CYCLE!", "#LIMIT!"};
typedef struct SheetValue {
    int type, error;
    double number;
    SheetRange range;
    char text[SHEET_TEXT];
} SheetValue;
typedef struct SheetCalc {
    const SheetBook *book;
    SheetValue values[SHEET_CELLS];
    unsigned char state[SHEET_CELLS];
    int depth, budget, today;
} SheetCalc;
typedef struct SheetParser {
    SheetCalc *calc;
    const char *p;
    int sheet, evaluate, depth;
} SheetParser;
static inline SheetValue sv_number(double n)
{
    SheetValue v = {0};
    v.number = n;
    if (!isfinite(n)) {
        v.type = SV_ERROR;
        v.error = SE_NUM;
    }
    return v;
}
static inline SheetValue sv_error(int e)
{
    SheetValue v = {0};
    v.type = SV_ERROR;
    v.error = e;
    return v;
}
static inline double sv_numeric(SheetValue v)
{
    return v.type == SV_NUMBER ? v.number : 0;
}
static SheetValue sheet_eval(SheetCalc *calc, int s, int r, int c);
static SheetValue sheet_expression(SheetParser *p);
static inline void sheet_space(SheetParser *p)
{
    while (isspace((unsigned char)*p->p)) ++p->p;
}
static inline int sheet_take(SheetParser *p, char ch)
{
    sheet_space(p);
    if (*p->p != ch) return 0;
    ++p->p;
    return 1;
}
static inline SheetValue sheet_scalar(SheetCalc *calc, SheetValue v)
{
    if (v.type == SV_RANGE) return sheet_eval(calc, v.range.sheet, v.range.r0, v.range.c0);
    return v;
}
static inline int sheet_truth(SheetValue v)
{
    return v.type == SV_NUMBER ? v.number != 0 : v.type == SV_TEXT && *v.text;
}
static inline void sheet_calc_reset(SheetCalc *c, const SheetBook *b)
{
    memset(c->state, 0, sizeof(c->state));
    c->book = b;
    c->depth = 0;
    c->budget = 2000000;
    c->today = (int)(time(NULL)/86400)+25569;
}
/* Functions use one stable callback signature. Ranges stay lazy until visited. */
typedef SheetValue (*SheetFunctionCall)(SheetCalc *, const SheetValue *, int, int);
typedef struct SheetFunction {
    const char *name, *syntax;
    int min_args, max_args, operation, lazy;
    SheetFunctionCall call;
} SheetFunction;
enum {
    SF_SUM, SF_AVG, SF_COUNT, SF_COUNTA, SF_MIN, SF_MAX, SF_PRODUCT, SF_STDEV, SF_VAR,
    SF_ABS, SF_SQRT, SF_EXP, SF_LN, SF_LOG10, SF_SIN, SF_COS, SF_TAN, SF_ASIN, SF_ACOS,
    SF_ATAN, SF_FLOOR, SF_CEIL, SF_INT, SF_ROUND, SF_POWER, SF_MOD, SF_ATAN2,
    SF_PI, SF_AND, SF_OR, SF_NOT, SF_IF, SF_IFERROR, SF_LEN, SF_UPPER, SF_LOWER,
    SF_CONCAT, SF_PMT, SF_PV, SF_FV, SF_NPV, SF_SLOPE, SF_INTERCEPT, SF_CORREL, SF_MEDIAN, SF_COUNTIF, SF_SUMIF,
    SF_INDEX, SF_MATCH, SF_VLOOKUP, SF_LOG, SF_DEGREES, SF_RADIANS, SF_SUMSQ, SF_STDEVP, SF_VARP, SF_DATE, SF_YEAR, SF_MONTH, SF_DAY, SF_TODAY
};
static SheetValue sheet_builtin(SheetCalc *calc, const SheetValue *args, int n, int op);
#define SF(name, lo, hi, op, syntax) {name, syntax, lo, hi, op, 0, sheet_builtin}
static const SheetFunction sheet_functions[] = {
    SF("SUM",1,32,SF_SUM,"SUM(range,...)"), SF("AVERAGE",1,32,SF_AVG,"AVERAGE(range,...)"),
    SF("COUNT",1,32,SF_COUNT,"COUNT(range,...)"), SF("COUNTA",1,32,SF_COUNTA,"COUNTA(range,...)"),
    SF("MIN",1,32,SF_MIN,"MIN(range,...)"), SF("MAX",1,32,SF_MAX,"MAX(range,...)"),
    SF("PRODUCT",1,32,SF_PRODUCT,"PRODUCT(range,...)"), SF("STDEV",1,32,SF_STDEV,"STDEV(sample,...)"),
    SF("VAR",1,32,SF_VAR,"VAR(sample,...)"), SF("ABS",1,1,SF_ABS,"ABS(x)"),
    SF("SQRT",1,1,SF_SQRT,"SQRT(x)"), SF("EXP",1,1,SF_EXP,"EXP(x)"),
    SF("LN",1,1,SF_LN,"LN(x)"), SF("LOG10",1,1,SF_LOG10,"LOG10(x)"),
    SF("SIN",1,1,SF_SIN,"SIN(radians)"), SF("COS",1,1,SF_COS,"COS(radians)"),
    SF("TAN",1,1,SF_TAN,"TAN(radians)"), SF("ASIN",1,1,SF_ASIN,"ASIN(x)"),
    SF("ACOS",1,1,SF_ACOS,"ACOS(x)"), SF("ATAN",1,1,SF_ATAN,"ATAN(x)"),
    SF("FLOOR",1,1,SF_FLOOR,"FLOOR(x)"), SF("CEILING",1,1,SF_CEIL,"CEILING(x)"),
    SF("INT",1,1,SF_INT,"INT(x)"), SF("ROUND",1,2,SF_ROUND,"ROUND(x,digits)"),
    SF("POWER",2,2,SF_POWER,"POWER(x,y)"), SF("MOD",2,2,SF_MOD,"MOD(x,y)"),
    SF("ATAN2",2,2,SF_ATAN2,"ATAN2(x,y)"), SF("PI",0,0,SF_PI,"PI()"),
    SF("AND",1,32,SF_AND,"AND(condition,...)"), SF("OR",1,32,SF_OR,"OR(condition,...)"),
    SF("NOT",1,1,SF_NOT,"NOT(condition)"),
    {"IF","IF(condition,true,false)",3,3,SF_IF,1,sheet_builtin},
    {"IFERROR","IFERROR(value,fallback)",2,2,SF_IFERROR,1,sheet_builtin},
    SF("LEN",1,1,SF_LEN,"LEN(text)"), SF("UPPER",1,1,SF_UPPER,"UPPER(text)"),
    SF("LOWER",1,1,SF_LOWER,"LOWER(text)"), SF("CONCAT",1,32,SF_CONCAT,"CONCAT(text,...)"),
    SF("PMT",3,5,SF_PMT,"PMT(rate,nper,pv,fv,type)"), SF("PV",3,5,SF_PV,"PV(rate,nper,pmt,fv,type)"),
    SF("FV",3,5,SF_FV,"FV(rate,nper,pmt,pv,type)"), SF("NPV",2,32,SF_NPV,"NPV(rate,cashflows,...)"),
    SF("SLOPE",2,2,SF_SLOPE,"SLOPE(y_range,x_range)"),
    SF("INTERCEPT",2,2,SF_INTERCEPT,"INTERCEPT(y_range,x_range)"),
    SF("CORREL",2,2,SF_CORREL,"CORREL(y_range,x_range)"),
    SF("MEDIAN",1,32,SF_MEDIAN,"MEDIAN(range,...)"),
    SF("COUNTIF",2,2,SF_COUNTIF,"COUNTIF(range,criterion)"),
    SF("SUMIF",2,3,SF_SUMIF,"SUMIF(range,criterion,sum_range)"),
    SF("INDEX",2,3,SF_INDEX,"INDEX(range,row,column)"),
    SF("MATCH",2,3,SF_MATCH,"MATCH(value,range,0) [exact]"),
    SF("VLOOKUP",3,4,SF_VLOOKUP,"VLOOKUP(value,table,column,FALSE)"),
    SF("LOG",1,2,SF_LOG,"LOG(x,base)"),
    SF("DEGREES",1,1,SF_DEGREES,"DEGREES(radians)"),
    SF("RADIANS",1,1,SF_RADIANS,"RADIANS(degrees)"),
    SF("SUMSQ",1,32,SF_SUMSQ,"SUMSQ(range,...)"),
    SF("STDEV.P",1,32,SF_STDEVP,"STDEV.P(population,...)"),
    SF("VAR.P",1,32,SF_VARP,"VAR.P(population,...)"),
    SF("DATE",3,3,SF_DATE,"DATE(year,month,day)"),
    SF("YEAR",1,1,SF_YEAR,"YEAR(serial_date)"),
    SF("MONTH",1,1,SF_MONTH,"MONTH(serial_date)"),
    SF("DAY",1,1,SF_DAY,"DAY(serial_date)"),
    SF("TODAY",0,0,SF_TODAY,"TODAY() [UTC]")
};
#undef SF
static inline const SheetFunction *sheet_function(const char *name)
{
    for (size_t i = 0; i < sizeof(sheet_functions) / sizeof(*sheet_functions); ++i)
        if (!strcmp(name, sheet_functions[i].name)) return &sheet_functions[i];
    return NULL;
}
static inline int sheet_value_count(SheetValue v)
{
    return v.type == SV_RANGE ? (v.range.r1 - v.range.r0 + 1) * (v.range.c1 - v.range.c0 + 1) : 1;
}
static inline SheetValue sheet_value_at(SheetCalc *calc, SheetValue v, int i)
{
    if (v.type != SV_RANGE) return v;
    if (--calc->budget < 0) return sv_error(SE_LIMIT);
    int w = v.range.c1 - v.range.c0 + 1;
    return sheet_eval(calc, v.range.sheet, v.range.r0 + i / w, v.range.c0 + i % w);
}
static inline int sheet_leap(int year)
{
    return year%4 == 0 && (year%100 != 0 || year%400 == 0);
}
static inline int sheet_month_days(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1]+(month == 2 && sheet_leap(year));
}
static inline int sheet_date_first(int year, int month)
{
    int serial = 1;
    for (int y = 1900; y < year; ++y) serial += 365+sheet_leap(y);
    for (int m = 1; m < month; ++m) serial += sheet_month_days(year,m);
    if (year > 1900 || month > 2) ++serial;
    return serial;
}
static inline int sheet_date_parts(double serial, int *year, int *month, int *day)
{
    if (!isfinite(serial) || serial < 1 || serial >= 2958466) return 0;
    int n = (int)floor(serial);
    if (n == 60) {
        *year=1900;
        *month=2;
        *day=29;
        return 1;
    }
    if (n > 60) --n;
    *year = 1900;
    while (n > 365+sheet_leap(*year)) {
        n -= 365+sheet_leap(*year);
        ++*year;
    }
    *month = 1;
    while (n > sheet_month_days(*year,*month)) {
        n -= sheet_month_days(*year,*month);
        ++*month;
    }
    *day = n;
    return 1;
}
static inline int sheet_compare_values(SheetValue a, SheetValue b)
{
    if (a.type == SV_NUMBER && b.type == SV_NUMBER) return a.number < b.number ? -1 : a.number > b.number;
    if (a.type != b.type) return a.type < b.type ? -1 : 1;
    const unsigned char *x = (const unsigned char *)a.text, *y = (const unsigned char *)b.text;
    while (*x && *y && toupper(*x) == toupper(*y)) {
        ++x;
        ++y;
    }
    return toupper(*x) - toupper(*y);
}
static inline int sheet_criterion(SheetValue value, SheetValue criterion)
{
    int comparison = 0;
    if (criterion.type == SV_TEXT) {
        const char *p = criterion.text;
        if (*p == '<') {
            ++p;
            comparison = *p == '>' ? 5 : *p == '=' ? 2 : 1;
            if (*p == '>' || *p == '=') ++p;
        }
        else if (*p == '>') {
            ++p;
            comparison = *p == '=' ? 4 : 3;
            if (*p == '=') ++p;
        }
        else if (*p == '=') ++p;
        char *end;
        double number = strtod(p,&end);
        if (end != p && !*end) criterion = sv_number(number);
        else {
            char text[SHEET_TEXT];
            sheet_string(text,sizeof(text),p);
            sheet_string(criterion.text,sizeof(criterion.text),text);
        }
    }
    if (criterion.type == SV_NUMBER && value.type == SV_BLANK) value = sv_number(0);
    if (criterion.type == SV_TEXT && value.type == SV_BLANK) {
        value.type = SV_TEXT;
        value.text[0] = 0;
    }
    if (value.type != criterion.type && comparison != 5) return 0;
    int cmp = sheet_compare_values(value,criterion);
    return comparison == 1 ? cmp < 0 : comparison == 2 ? cmp <= 0 : comparison == 3 ? cmp > 0 :
           comparison == 4 ? cmp >= 0 : comparison == 5 ? cmp != 0 : cmp == 0;
}
static int sheet_double_compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static SheetValue sheet_builtin(SheetCalc *calc, const SheetValue *args, int n, int op)
{
    if (op == SF_MEDIAN) {
        double *values = malloc(SHEET_CELLS*sizeof(*values));
        if (!values) return sv_error(SE_LIMIT);
        int count = 0;
        SheetValue result = sv_error(SE_DIV);
        for (int a = 0; a < n; ++a) {
            for (int i = 0; i < sheet_value_count(args[a]); ++i) {
                SheetValue v = sheet_value_at(calc,args[a],i);
                if (v.type == SV_ERROR) {
                    result = v;
                    goto median_done;
                }
                if (v.type != SV_NUMBER) continue;
                if (count == SHEET_CELLS) {
                    result = sv_error(SE_LIMIT);
                    goto median_done;
                }
                values[count++] = v.number;
            }
        }
        qsort(values,(size_t)count,sizeof(*values),sheet_double_compare);
        if (count) result = sv_number(count%2 ? values[count/2] : values[count/2-1]/2+values[count/2]/2);
median_done:
        free(values);
        return result;
    }
    if (op == SF_COUNTIF || op == SF_SUMIF) {
        SheetValue criterion = sheet_scalar(calc,args[1]);
        if (criterion.type == SV_ERROR) return criterion;
        if (n == 3 && sheet_value_count(args[0]) != sheet_value_count(args[2])) return sv_error(SE_VALUE);
        double total = 0;
        for (int i = 0; i < sheet_value_count(args[0]); ++i) {
            SheetValue v = sheet_value_at(calc,args[0],i);
            if (v.type == SV_ERROR) return v;
            if (!sheet_criterion(v,criterion)) continue;
            if (op == SF_COUNTIF) ++total;
            else {
                SheetValue sum = n == 3 ? sheet_value_at(calc,args[2],i) : v;
                if (sum.type == SV_ERROR) return sum;
                if (sum.type == SV_NUMBER) total += sum.number;
            }
        }
        return sv_number(total);
    }
    if (op == SF_INDEX) {
        if (args[0].type != SV_RANGE) return sv_error(SE_VALUE);
        SheetValue rv = sheet_scalar(calc,args[1]), cv = n == 3 ? sheet_scalar(calc,args[2]) : sv_number(1);
        if (rv.type == SV_ERROR) return rv;
        if (cv.type == SV_ERROR) return cv;
        SheetRange a = args[0].range;
        if (rv.type != SV_NUMBER || cv.type != SV_NUMBER || rv.number != floor(rv.number) || cv.number != floor(cv.number) ||
            rv.number < 1 || rv.number > a.r1-a.r0+1 || cv.number < 1 || cv.number > a.c1-a.c0+1) return sv_error(SE_REF);
        return sheet_eval(calc,a.sheet,a.r0+(int)rv.number-1,a.c0+(int)cv.number-1);
    }
    if (op == SF_MATCH || op == SF_VLOOKUP) {
        SheetValue needle = sheet_scalar(calc,args[0]);
        if (needle.type == SV_ERROR) return needle;
        if (args[1].type != SV_RANGE) return sv_error(SE_VALUE);
        SheetRange a = args[1].range;
        if (op == SF_MATCH && a.r1>a.r0 && a.c1>a.c0) return sv_error(SE_VALUE);
        if ((op == SF_MATCH && n == 3) || (op == SF_VLOOKUP && n == 4)) {
            SheetValue exact = sheet_scalar(calc,args[n-1]);
            if (exact.type != SV_NUMBER || exact.number != 0) return sv_error(SE_VALUE);
        }
        int column = 1;
        if (op == SF_VLOOKUP) {
            SheetValue index = sheet_scalar(calc,args[2]);
            if (index.type != SV_NUMBER || index.number != floor(index.number) || index.number<1 || index.number>a.c1-a.c0+1) return sv_error(SE_REF);
            column = (int)index.number;
        }
        int length = op == SF_VLOOKUP ? a.r1-a.r0+1 : sheet_value_count(args[1]);
        for (int i = 0; i < length; ++i) {
            SheetValue v = op == SF_VLOOKUP ? sheet_eval(calc,a.sheet,a.r0+i,a.c0) : sheet_value_at(calc,args[1],i);
            if (v.type == SV_ERROR) return v;
            if (!sheet_compare_values(v,needle)) return op == SF_MATCH ? sv_number(i+1) : sheet_eval(calc,a.sheet,a.r0+i,a.c0+column-1);
        }
        return sv_error(SE_REF);
    }

    if (op <= SF_VAR || op == SF_AND || op == SF_OR || op == SF_NPV || op == SF_SUMSQ || op == SF_STDEVP || op == SF_VARP) {
        double sum = 0, product = 1, min = 0, max = 0, mean = 0, m2 = 0, rate = 0;
        int count = 0, nonempty = 0, truth = op == SF_AND, period = 0;
        if (op == SF_NPV) {
            SheetValue v = sheet_scalar(calc, args[0]);
            if (v.type == SV_ERROR) return v;
            if (v.type != SV_NUMBER || v.number <= -1) return sv_error(SE_NUM);
            rate = v.number;
        }
        for (int a = op == SF_NPV ? 1 : 0; a < n; ++a) {
            int size = sheet_value_count(args[a]);
            for (int i = 0; i < size; ++i) {
                SheetValue v = sheet_value_at(calc, args[a], i);
                if (v.type == SV_ERROR && op != SF_COUNT && op != SF_COUNTA) return v;
                if (v.type != SV_BLANK) ++nonempty;
                if (v.type != SV_NUMBER) continue;
                ++count;
                sum += op == SF_NPV ? v.number / pow(1 + rate, ++period) : op == SF_SUMSQ ? v.number*v.number : v.number;
                product *= v.number;
                if (count == 1 || v.number < min) min = v.number;
                if (count == 1 || v.number > max) max = v.number;
                double delta = v.number - mean;
                mean += delta / count;
                m2 += delta * (v.number - mean);
                if (op == SF_AND) truth = truth && sheet_truth(v);
                if (op == SF_OR) truth = truth || sheet_truth(v);
            }
        }
        switch (op) {
        case SF_AVG: return count ? sv_number(mean) : sv_error(SE_DIV);
        case SF_COUNT: return sv_number(count);
        case SF_COUNTA: return sv_number(nonempty);
        case SF_MIN: return sv_number(min);
        case SF_MAX: return sv_number(max);
        case SF_PRODUCT: return sv_number(count ? product : 0);
        case SF_STDEV: return count > 1 ? sv_number(sqrt(m2 / (count - 1))) : sv_error(SE_DIV);
        case SF_VAR: return count > 1 ? sv_number(m2 / (count - 1)) : sv_error(SE_DIV);
        case SF_STDEVP: return count ? sv_number(sqrt(m2/count)) : sv_error(SE_DIV);
        case SF_VARP: return count ? sv_number(m2/count) : sv_error(SE_DIV);
        case SF_AND: case SF_OR: return sv_number(truth);
        default: return sv_number(sum);
        }
    }
    if (op >= SF_SLOPE && op <= SF_CORREL) {
        if (sheet_value_count(args[0]) != sheet_value_count(args[1])) return sv_error(SE_VALUE);
        double mx = 0, my = 0, xx = 0, yy = 0, xy = 0;
        int count = 0;
        for (int i = 0; i < sheet_value_count(args[0]); ++i) {
            SheetValue y = sheet_value_at(calc, args[0], i), x = sheet_value_at(calc, args[1], i);
            if (x.type == SV_ERROR) return x;
            if (y.type == SV_ERROR) return y;
            if (x.type != SV_NUMBER || y.type != SV_NUMBER) continue;
            ++count;
            double dx = x.number - mx, dy = y.number - my;
            mx += dx / count;
            my += dy / count;
            xx += dx * (x.number - mx);
            yy += dy * (y.number - my);
            xy += dx * (y.number - my);
        }
        if (count < 2 || xx == 0 || (op == SF_CORREL && yy == 0)) return sv_error(SE_DIV);
        return sv_number(op == SF_SLOPE ? xy / xx : op == SF_INTERCEPT ? my - xy / xx * mx : xy / sqrt(xx * yy));
    }
    SheetValue a[32];
    for (int i = 0; i < n; ++i) {
        a[i] = sheet_scalar(calc, args[i]);
        if (a[i].type == SV_ERROR) return a[i];
    }
    if (op == SF_TODAY) return sv_number(calc->today);
    if (op == SF_PI) return sv_number(acos(-1));
    if (op == SF_NOT) return sv_number(!sheet_truth(a[0]));
    if (op >= SF_LEN && op <= SF_CONCAT) {
        SheetValue result = {0};
        result.type = SV_TEXT;
        for (int i = 0; i < n; ++i) {
            char s[SHEET_TEXT];
            if (a[i].type == SV_NUMBER) snprintf(s, sizeof(s), "%.15g", a[i].number);
            else sheet_string(s, sizeof(s), a[i].text);
            if (op == SF_LEN) return sv_number(strlen(s));
            for (char *p = s; *p; ++p) {
                if (op == SF_UPPER) *p = (char)toupper((unsigned char)*p);
                if (op == SF_LOWER) *p = (char)tolower((unsigned char)*p);
            }
            if (strlen(result.text) + strlen(s) >= SHEET_TEXT) return sv_error(SE_LIMIT);
            memcpy(result.text+strlen(result.text),s,strlen(s)+1);
        }
        return result;
    }
    for (int i = 0; i < n; ++i)
        if (a[i].type != SV_NUMBER && a[i].type != SV_BLANK) return sv_error(SE_VALUE);
    double x = sv_numeric(a[0]), y = n > 1 ? sv_numeric(a[1]) : 0;
    if (op == SF_DATE) {
        double day = sv_numeric(a[2]);
        if (x < 1900 || x > 9999 || y < -120000 || y > 120000 || fabs(day)>3660000) return sv_error(SE_NUM);
        int months = (int)floor(x)*12+(int)floor(y)-1;
        int year = months/12, month = months%12+1;
        if (year < 1900 || year > 9999) return sv_error(SE_NUM);
        double serial = sheet_date_first(year,month)+floor(day)-1;
        return serial >= 1 && serial < 2958466 ? sv_number(serial) : sv_error(SE_NUM);
    }
    if (op == SF_YEAR || op == SF_MONTH || op == SF_DAY) {
        int year,month,day;
        if (!sheet_date_parts(x,&year,&month,&day)) return sv_error(SE_NUM);
        return sv_number(op == SF_YEAR ? year : op == SF_MONTH ? month : day);
    }
    if (op >= SF_PMT && op <= SF_FV) {
        double rate = x, periods = y, amount = sv_numeric(a[2]);
        double end = n > 3 ? sv_numeric(a[3]) : 0, type = n > 4 ? sv_numeric(a[4]) : 0;
        if (rate <= -1 || periods <= 0 || (type != 0 && type != 1)) return sv_error(SE_NUM);
        double growth = pow(1 + rate, periods);
        double annuity = rate == 0 ? periods : expm1(periods * log1p(rate)) / rate;
        annuity *= 1 + rate * type;
        if (!isfinite(growth) || !isfinite(annuity) || annuity == 0) return sv_error(SE_NUM);
        if (op == SF_PMT) return sv_number(-(amount * growth + end) / annuity);
        if (op == SF_PV) return growth ? sv_number(-(amount * annuity + end) / growth) : sv_error(SE_DIV);
        return sv_number(-(end * growth + amount * annuity));
    }
    switch (op) {
    case SF_LOG: return x > 0 && (n == 1 || (y > 0 && y != 1)) ? sv_number(log(x)/log(n == 1 ? 10 : y)) : sv_error(SE_NUM);
    case SF_DEGREES: return sv_number(x*180/acos(-1));
    case SF_RADIANS: return sv_number(x*acos(-1)/180);
    case SF_ABS: return sv_number(fabs(x));
    case SF_SQRT: return sv_number(sqrt(x));
    case SF_EXP: return sv_number(exp(x));
    case SF_LN: return sv_number(log(x));
    case SF_LOG10: return sv_number(log10(x));
    case SF_SIN: return sv_number(sin(x));
    case SF_COS: return sv_number(cos(x));
    case SF_TAN: return sv_number(tan(x));
    case SF_ASIN: return sv_number(asin(x));
    case SF_ACOS: return sv_number(acos(x));
    case SF_ATAN: return sv_number(atan(x));
    case SF_FLOOR: case SF_INT: return sv_number(floor(x));
    case SF_CEIL: return sv_number(ceil(x));
    case SF_ROUND: {
        if (y < -15 || y > 15 || y != floor(y)) return sv_error(SE_NUM);
        double scale = pow(10, y);
        return sv_number(round(x * scale) / scale);
    }
    case SF_POWER: return sv_number(pow(x, y));
    case SF_MOD: return y ? sv_number(x - y * floor(x / y)) : sv_error(SE_DIV);
    case SF_ATAN2: return x || y ? sv_number(atan2(y, x)) : sv_error(SE_DIV);
    default: return sv_error(SE_NAME);
    }
}
static SheetValue sheet_primary(SheetParser *p)
{
    sheet_space(p);
    if (++p->depth > 64) {
        --p->depth;
        return sv_error(SE_LIMIT);
    }
    SheetValue v = sv_error(SE_VALUE);
    if (sheet_take(p, '(')) {
        v = sheet_expression(p);
        if (!sheet_take(p, ')')) v = sv_error(SE_VALUE);
    } else if (*p->p == '"') {
        ++p->p;
        v.type = SV_TEXT;
        v.error = 0;
        size_t n = 0;
        int closed = 0;
        while (*p->p) {
            if (*p->p == '"') {
                ++p->p;
                if (*p->p != '"') {
                    closed = 1;
                    break;
                }
            }
            if (n + 1 >= sizeof(v.text)) {
                v = sv_error(SE_LIMIT);
                break;
            }
            v.text[n++] = *p->p++;
        }
        v.text[n] = 0;
        if (!closed) v = sv_error(SE_VALUE);
    } else if (isdigit((unsigned char)*p->p) || *p->p == '.') {
        char *end;
        double number = strtod(p->p, &end);
        if (end != p->p) {
            p->p = end;
            v = sv_number(number);
        }
    } else if (!strncmp(p->p, "#REF!", 5)) {
        p->p += 5;
        v = sv_error(SE_REF);
    } else {
        const char *start = p->p;
        char name[64] = {0};
        size_t n = 0;
        int quoted = *p->p == '\'';
        if (quoted) {
            ++p->p;
            while (*p->p && n < sizeof(name) - 1) {
                if (*p->p == '\'') {
                    ++p->p;
                    if (*p->p != '\'') break;
                }
                name[n++] = *p->p++;
            }
        } else {
            while ((isalnum((unsigned char)*p->p) || *p->p == '_' || *p->p == '.') && n < sizeof(name) - 1)
                name[n++] = *p->p++;
        }
        int s = p->sheet;
        if (*p->p == '!') {
            ++p->p;
            s = -1;
            for (int i = 0; i < p->calc->book->tabs; ++i) {
                int same = 1;
                for (size_t j = 0; j <= n; ++j) {
                    if (j >= sizeof(p->calc->book->names[i]) || toupper((unsigned char)name[j]) != toupper((unsigned char)p->calc->book->names[i][j])) {
                        same = 0;
                        break;
                    }
                    if (!name[j]) break;
                }
                if (same) s = i;
            }
            start = p->p;
        } else {
            for (size_t i = 0; i < n; ++i) name[i] = (char)toupper((unsigned char)name[i]);
            sheet_space(p);
            if (*p->p == '(' && !quoted) {
                ++p->p;
                const SheetFunction *fn = sheet_function(name);
                SheetValue args[32];
                int argc = 0, evaluate = p->evaluate;
                if (!sheet_take(p, ')')) {
                    do {
                        if (argc == 32) {
                            v = sv_error(SE_LIMIT);
                            goto done;
                        }
                        if (fn && fn->lazy && argc && evaluate) {
                            SheetValue first = sheet_scalar(p->calc, args[0]);
                            p->evaluate = evaluate && (fn->operation == SF_IFERROR ? first.type == SV_ERROR :
                                first.type != SV_ERROR && (argc == 1 ? sheet_truth(first) : !sheet_truth(first)));
                        }
                        args[argc++] = sheet_expression(p);
                        p->evaluate = evaluate;
                    } while (sheet_take(p, ','));
                    if (!sheet_take(p, ')')) {
                        v = sv_error(SE_VALUE);
                        goto done;
                    }
                }
                if (!fn) v = sv_error(SE_NAME);
                else if (argc < fn->min_args || argc > fn->max_args) v = sv_error(SE_VALUE);
                else if (!evaluate) v = sv_number(0);
                else if (fn->operation == SF_IF || fn->operation == SF_IFERROR) {
                    SheetValue first = sheet_scalar(p->calc, args[0]);
                    v = fn->operation == SF_IFERROR ? (first.type == SV_ERROR ? args[1] : first) :
                        first.type == SV_ERROR ? first : args[sheet_truth(first) ? 1 : 2];
                } else v = fn->call(p->calc, args, argc, fn->operation);
                goto done;
            }
            if (!strcmp(name, "TRUE") || !strcmp(name, "FALSE")) {
                v = sv_number(!strcmp(name, "TRUE"));
                goto done;
            }
            p->p = start;
        }
        if (!strncmp(p->p,"#REF!",5)) {
            p->p += 5;
            v = sv_error(SE_REF);
            goto done;
        }
        int r, c, ar, ac;
        if (sheet_reference(&p->p, &r, &c, &ar, &ac)) {
            if (sheet_take(p, ':')) {
                int r1, c1;
                if (!sheet_reference(&p->p, &r1, &c1, &ar, &ac)) v = sv_error(SE_REF);
                else {
                    v.type = SV_RANGE;
                    v.error = 0;
                    v.range = sheet_range(s, r, c, r1, c1);
                }
            } else v = p->evaluate ? sheet_eval(p->calc, s, r, c) : sv_number(0);
            if (s < 0) v = sv_error(SE_REF);
        } else {
            p->p = start;
            while (isalnum((unsigned char)*p->p) || *p->p == '_' || *p->p == '$') ++p->p;
            v = sv_error(SE_NAME);
        }
    }
done:
    --p->depth;
    return v;
}
static SheetValue sheet_unary(SheetParser *p);
static SheetValue sheet_power(SheetParser *p)
{
    SheetValue a = sheet_primary(p);
    if (sheet_take(p, '^')) {
        SheetValue b = sheet_unary(p);
        a = sheet_scalar(p->calc, a);
        b = sheet_scalar(p->calc, b);
        if (!p->evaluate) return sv_number(0);
        if (a.type == SV_ERROR) return a;
        if (b.type == SV_ERROR) return b;
        if ((a.type != SV_NUMBER && a.type != SV_BLANK) || (b.type != SV_NUMBER && b.type != SV_BLANK)) return sv_error(SE_VALUE);
        a = sv_number(pow(sv_numeric(a), sv_numeric(b)));
    }
    while (sheet_take(p, '%')) {
        a = sheet_scalar(p->calc, a);
        if (a.type == SV_ERROR) return a;
        if (a.type != SV_NUMBER && a.type != SV_BLANK) return sv_error(SE_VALUE);
        a = sv_number(sv_numeric(a) / 100);
    }
    return a;
}
static SheetValue sheet_unary(SheetParser *p)
{
    sheet_space(p);
    if (*p->p == '+' || *p->p == '-') {
        int minus = *p->p++ == '-';
        if (++p->depth > 64) {
            --p->depth;
            return sv_error(SE_LIMIT);
        }
        SheetValue v = sheet_scalar(p->calc, sheet_unary(p));
        --p->depth;
        if (v.type == SV_ERROR) return v;
        if (v.type != SV_NUMBER && v.type != SV_BLANK) return sv_error(SE_VALUE);
        return sv_number((minus ? -1 : 1) * sv_numeric(v));
    }
    return sheet_power(p);
}
static SheetValue sheet_binary(SheetParser *p, int level)
{
    SheetValue a = level == 0 ? sheet_unary(p) : sheet_binary(p, level - 1);
    for (;;) {
        sheet_space(p);
        char op = *p->p;
        if (!(level == 0 ? op == '*' || op == '/' : level == 1 ? op == '+' || op == '-' :
              level == 2 ? op == '&' : op == '=' || op == '<' || op == '>')) break;
        ++p->p;
        char second = 0;
        if (level == 3 && (*p->p == '=' || (op == '<' && *p->p == '>'))) second = *p->p++;
        SheetValue b = level == 0 ? sheet_unary(p) : sheet_binary(p, level - 1);
        if (!p->evaluate) {
            a = sv_number(0);
            continue;
        }
        a = sheet_scalar(p->calc, a);
        b = sheet_scalar(p->calc, b);
        if (a.type == SV_ERROR) continue;
        if (b.type == SV_ERROR) {
            a = b;
            continue;
        }
        if (op == '&') {
            char left[SHEET_TEXT], right[SHEET_TEXT];
            if (a.type == SV_NUMBER) snprintf(left, sizeof(left), "%.15g", a.number);
            else sheet_string(left, sizeof(left), a.text);
            if (b.type == SV_NUMBER) snprintf(right, sizeof(right), "%.15g", b.number);
            else sheet_string(right, sizeof(right), b.text);
            if (strlen(left) + strlen(right) >= SHEET_TEXT) a = sv_error(SE_LIMIT);
            else {
                a.type = SV_TEXT;
                memcpy(a.text,left,strlen(left)+1);
                memcpy(a.text+strlen(a.text),right,strlen(right)+1);
            }
        } else if (level == 3) {
            int cmp;
            if (a.type == SV_BLANK) {
                if (b.type == SV_TEXT) a.type = SV_TEXT;
                else a = sv_number(0);
            }
            if (b.type == SV_BLANK) {
                if (a.type == SV_TEXT) b.type = SV_TEXT;
                else b = sv_number(0);
            }
            cmp = sheet_compare_values(a,b);
            a = sv_number(op == '=' ? cmp == 0 : second == '>' ? cmp != 0 :
                          op == '<' ? (second ? cmp <= 0 : cmp < 0) : (second ? cmp >= 0 : cmp > 0));
        } else if ((a.type != SV_NUMBER && a.type != SV_BLANK) || (b.type != SV_NUMBER && b.type != SV_BLANK)) a = sv_error(SE_VALUE);
        else {
            double x = sv_numeric(a), y = sv_numeric(b);
            a = op == '/' && y == 0 ? sv_error(SE_DIV) : sv_number(op == '+' ? x + y : op == '-' ? x - y : op == '*' ? x * y : x / y);
        }
    }
    return a;
}
static SheetValue sheet_expression(SheetParser *p)
{
    return sheet_binary(p, 3);
}
static SheetValue sheet_eval(SheetCalc *calc, int s, int r, int c)
{
    if (s < 0 || s >= calc->book->tabs || r < 0 || r >= SHEET_ROWS || c < 0 || c >= SHEET_COLS) return sv_error(SE_REF);
    if (!calc->depth) {
        int today = (int)(time(NULL)/86400)+25569;
        if (today != calc->today) {
            memset(calc->state,0,sizeof(calc->state));
            calc->today=today;
        }
        calc->budget = 2000000;
    }
    const SheetCell *cell = sheet_cell(calc->book, s, r, c);
    if (!cell || !*cell->raw) {
        SheetValue v = {0};
        v.type = SV_BLANK;
        return v;
    }
    int i = (int)(cell - calc->book->cells);
    if (calc->state[i] == 2) return calc->values[i];
    if (calc->state[i] == 1) return sv_error(SE_CYCLE);
    if (calc->depth >= 64 || --calc->budget < 0) return sv_error(SE_LIMIT);
    calc->state[i] = 1;
    ++calc->depth;
    SheetValue v = {0};
    if (*cell->raw == '=') {
        SheetParser p = {calc, cell->raw + 1, s, 1, 0};
        v = sheet_scalar(calc, sheet_expression(&p));
        sheet_space(&p);
        if (*p.p) v = sv_error(SE_VALUE);
    } else {
        char *end;
        double value = strtod(cell->raw, &end);
        while (isspace((unsigned char)*end)) ++end;
        if (*end == '%' && !end[1]) {
            value /= 100;
            ++end;
        }
        if (end != cell->raw && !*end) v = sv_number(value);
        else {
            v.type = SV_TEXT;
            sheet_string(v.text, sizeof(v.text), cell->raw + (*cell->raw == '\''));
        }
    }
    --calc->depth;
    calc->values[i] = v;
    calc->state[i] = 2;
    return v;
}
static inline void sheet_display(SheetValue v, int format, char *out, size_t size)
{
    if (v.type == SV_ERROR) sheet_string(out, size, sheet_errors[v.error]);
    else if (v.type == SV_TEXT) sheet_string(out, size, v.text);
    else if (v.type == SV_BLANK) *out = 0;
    else if (format == 1) snprintf(out, size, "%.2f", v.number);
    else if (format == 2) snprintf(out, size, "%.2f%%", v.number * 100);
    else if (format == 3) snprintf(out, size, "%.6e", v.number);
    else if (format == 4) snprintf(out, size, "%.2f", v.number);
    else if (format == 5) {
        int year,month,day;
        if (sheet_date_parts(v.number,&year,&month,&day)) snprintf(out,size,"%04d-%02d-%02d",year,month,day);
        else sheet_string(out,size,"#NUM!");
    }
    else snprintf(out, size, "%.12g", v.number);
}
#endif
