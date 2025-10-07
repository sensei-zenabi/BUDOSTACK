/*
* runtask.c - A simplified script engine with PRINT, WAIT, GOTO, RUN, and CLEAR commands.
*
* Changes in this version:
* - CMD removed.
* - RUN executes an app by name from ./apps/, ./commands/, or ./utilities. Blocking is
*   the default; prepend BLOCKING or NONBLOCKING to control the run mode explicitly.
*   Arguments undergo variable expansion (e.g., $VAR and ${VAR}) before exec.
* - RUN optionally supports `TO $VAR` to capture stdout into a variable (type auto-detected).
* - If RUN's first token contains '/', it's treated as an explicit path and executed directly.
* - Fixed argv lifetime: arguments are now heap-allocated (no static buffers). RUN reliably
*   passes switches/arguments (e.g., "setfont -d small2.psf") to the child.
*
* Examples (assuming an executable "mytool" exists in ./apps or ./commands or ./utilities):
*   RUN mytool -v "arg with spaces"
*   RUN utilities/cleanup.sh --dry-run
*   RUN ./apps/build.sh all
*
* Compile:
*   gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 -o runtask apps/runtask.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <threads.h> // thrd_sleep
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>   // PATH_MAX
#include <math.h>

#define MAX_VARIABLES 128
#define MAX_LABELS 256

static char *xstrdup(const char *s);

typedef enum {
    VALUE_UNSET = 0,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING
} ValueType;

typedef struct {
    ValueType type;
    long long int_val;
    double float_val;
    char *str_val;
    bool owns_string;
} Value;

static char *value_to_string(const Value *value);

typedef struct {
    char name[64];
    ValueType type;
    long long int_val;
    double float_val;
    char *str_val;
} Variable;

static Variable variables[MAX_VARIABLES];
static size_t variable_count = 0;

typedef struct {
    bool result;
    bool true_branch_done;
    bool else_encountered;
    bool else_branch_done;
    int line_number;
} IfContext;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Global flag to signal termination (set by SIGINT handler)
volatile sig_atomic_t stop = 0;

static const char *get_base_dir(void) {
    static char cached[PATH_MAX];
    static int initialized = 0;

    if (!initialized) {
        initialized = 1;
        const char *env = getenv("BUDOSTACK_BASE");
        if (env && env[0] != '\0') {
            strncpy(cached, env, sizeof(cached) - 1);
            cached[sizeof(cached) - 1] = '\0';
        }
    }

    return cached[0] ? cached : NULL;
}

static int build_from_base(const char *suffix, char *buffer, size_t size) {
    const char *base = get_base_dir();

    if (!suffix || !*suffix)
        return -1;

    if (suffix[0] == '/') {
        if (snprintf(buffer, size, "%s", suffix) >= (int)size)
            return -1;
        return 0;
    }

    if (base && base[0] != '\0') {
        size_t len = strlen(base);
        const char *fmt = (len > 0 && base[len - 1] == '/') ? "%s%s" : "%s/%s";
        if (snprintf(buffer, size, fmt, base, suffix) >= (int)size)
            return -1;
    } else {
        if (snprintf(buffer, size, "%s", suffix) >= (int)size)
            return -1;
    }
    return 0;
}

static bool equals_ignore_case(const char *a, const char *b) {
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void sigint_handler(int signum) {
    (void)signum;
    stop = 1;
}

static Variable *find_variable(const char *name, bool create) {
    if (!name || !*name) {
        return NULL;
    }
    for (size_t i = 0; i < variable_count; ++i) {
        if (strcmp(variables[i].name, name) == 0) {
            return &variables[i];
        }
    }
    if (!create) {
        return NULL;
    }
    if (variable_count >= MAX_VARIABLES) {
        fprintf(stderr, "Variable limit reached (%d)\n", MAX_VARIABLES);
        return NULL;
    }
    Variable *var = &variables[variable_count++];
    memset(var, 0, sizeof(*var));
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->name[sizeof(var->name) - 1] = '\0';
    var->type = VALUE_UNSET;
    return var;
}

static void assign_variable(Variable *var, const Value *value) {
    if (!var || !value) {
        return;
    }
    if (var->type == VALUE_STRING && var->str_val) {
        free(var->str_val);
        var->str_val = NULL;
    }
    var->type = value->type;
    if (value->type == VALUE_INT) {
        var->int_val = value->int_val;
        var->float_val = (double)value->int_val;
    } else if (value->type == VALUE_FLOAT) {
        var->float_val = value->float_val;
        var->int_val = (long long)value->float_val;
    } else if (value->type == VALUE_STRING) {
        var->str_val = value->str_val ? xstrdup(value->str_val) : xstrdup("");
    } else {
        var->int_val = 0;
        var->float_val = 0.0;
    }
}

static void free_value(Value *value) {
    if (!value) {
        return;
    }
    if (value->type == VALUE_STRING && value->owns_string && value->str_val) {
        free(value->str_val);
        value->str_val = NULL;
    }
    value->type = VALUE_UNSET;
    value->owns_string = false;
    value->int_val = 0;
    value->float_val = 0.0;
}

static bool copy_value(Value *dest, const Value *src) {
    if (!dest || !src) {
        return false;
    }

    free_value(dest);

    dest->type = src->type;
    dest->owns_string = false;
    dest->str_val = NULL;
    dest->int_val = src->int_val;
    dest->float_val = src->float_val;

    if (src->type == VALUE_STRING) {
        dest->str_val = xstrdup(src->str_val ? src->str_val : "");
        dest->owns_string = true;
        dest->int_val = 0;
        dest->float_val = 0.0;
    } else if (src->type == VALUE_INT) {
        dest->float_val = (double)src->int_val;
    } else if (src->type == VALUE_FLOAT) {
        dest->int_val = (long long)src->float_val;
    }

    return true;
}

static Value variable_to_value(const Variable *var) {
    Value v;
    memset(&v, 0, sizeof(v));
    if (!var) {
        v.type = VALUE_UNSET;
        return v;
    }
    v.type = var->type;
    v.int_val = var->int_val;
    v.float_val = var->float_val;
    if (var->type == VALUE_STRING) {
        v.str_val = var->str_val;
        v.owns_string = false;
    } else {
        v.str_val = NULL;
        v.owns_string = false;
    }
    return v;
}

static void cleanup_variables(void) {
    for (size_t i = 0; i < variable_count; ++i) {
        if (variables[i].type == VALUE_STRING && variables[i].str_val) {
            free(variables[i].str_val);
            variables[i].str_val = NULL;
        }
        variables[i].type = VALUE_UNSET;
    }
    variable_count = 0;
}

static bool is_token_delim(char c, const char *delims) {
    if (!delims) {
        return false;
    }
    for (const char *d = delims; *d; ++d) {
        if (*d == c) {
            return true;
        }
    }
    return false;
}

static bool parse_string_literal(const char **p, char **out) {
    const char *s = *p;
    if (*s != '"') {
        return false;
    }
    ++s; // skip opening quote
    size_t cap = 32;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    while (*s && *s != '"') {
        char ch = *s++;
        if (ch == '\\' && *s) {
            char esc = *s++;
            switch (esc) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'r': ch = '\r'; break;
                case '"': ch = '"';  break;
                case '\\': ch = '\\'; break;
                default:
                    // Unknown escape → take the char literally (e.g. \x → 'x')
                    ch = esc;
                    break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) {
                perror("realloc");
                free(buf);
                exit(EXIT_FAILURE);
            }
            buf = tmp;
        }
        buf[len++] = ch;
    }
    if (*s != '"') {
        free(buf);
        return false;
    }
    buf[len] = '\0';
    *out = buf;
    *p = (*s == '"') ? s + 1 : s;
    return true;
}

static bool parse_token(const char **p, char **out, bool *quoted, const char *delims) {
    const char *s = *p;
    while (isspace((unsigned char)*s)) {
        ++s;
    }
    if (!*s) {
        return false;
    }
    if (*s == '"') {
        if (!parse_string_literal(&s, out)) {
            return false;
        }
        if (quoted) {
            *quoted = true;
        }
        *p = s;
        return true;
    }
    size_t cap = 32;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    while (*s && !isspace((unsigned char)*s) && !is_token_delim(*s, delims)) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) {
                perror("realloc");
                free(buf);
                exit(EXIT_FAILURE);
            }
            buf = tmp;
        }
        buf[len++] = *s++;
    }
    buf[len] = '\0';
    *out = buf;
    if (quoted) {
        *quoted = false;
    }
    *p = s;
    return true;
}

static bool is_var_name_char(char c) {
    return (bool)(isalnum((unsigned char)c) || c == '_');
}

static bool is_var_name_start(char c) {
    return is_var_name_char(c);
}

static bool parse_variable_name_token(const char *token, char *out, size_t size) {
    if (!token || token[0] != '$') {
        return false;
    }
    token++;
    if (!*token) {
        return false;
    }
    size_t len = 0;
    while (*token) {
        if (!isalnum((unsigned char)*token) && *token != '_') {
            return false;
        }
        if (len + 1 >= size) {
            return false;
        }
        out[len++] = *token++;
    }
    out[len] = '\0';
    return true;
}

static char *expand_token_variables(const char *token, int line, int debug) {
    if (!token) {
        return NULL;
    }

    const char *cursor = token;
    bool had_variable = false;
    size_t cap = strlen(token) + 1;
    size_t len = 0;
    char *out = (char *)malloc(cap);
    if (!out) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    while (*cursor) {
        if (*cursor == '$') {
            const char *start = cursor + 1;
            char name_buf[64];
            size_t name_len = 0;
            bool overflow = false;
            if (*start == '{') {
                start++;
                while (*start && *start != '}') {
                    if (!is_var_name_char(*start)) {
                        break;
                    }
                    if (name_len + 1 >= sizeof(name_buf)) {
                        overflow = true;
                        break;
                    }
                    name_buf[name_len++] = *start++;
                }
                if (overflow) {
                    if (debug) {
                        fprintf(stderr, "RUN: variable name too long in '%s' at line %d\n", token, line);
                    }
                    goto literal_dollar;
                }
                if (*start == '}') {
                    cursor = start + 1;
                } else {
                    if (debug) {
                        fprintf(stderr, "RUN: invalid variable reference in '%s' at line %d\n", token, line);
                    }
                    goto literal_dollar;
                }
            } else {
                const char *walk = start;
                if (!is_var_name_start(*walk)) {
                    goto literal_dollar;
                }
                while (*walk && is_var_name_char(*walk)) {
                    if (name_len + 1 >= sizeof(name_buf)) {
                        overflow = true;
                        break;
                    }
                    name_buf[name_len++] = *walk++;
                }
                if (overflow) {
                    if (debug) {
                        fprintf(stderr, "RUN: variable name too long in '%s' at line %d\n", token, line);
                    }
                    goto literal_dollar;
                }
                cursor = walk;
            }

            if (name_len == 0) {
                goto literal_dollar;
            }
            name_buf[name_len] = '\0';

            had_variable = true;
            Variable *var = find_variable(name_buf, false);
            Value value = variable_to_value(var);
            char *replacement = value_to_string(&value);
            if (!replacement) {
                replacement = xstrdup("");
            }
            size_t rep_len = strlen(replacement);
            if (len + rep_len + 1 > cap) {
                while (len + rep_len + 1 > cap) {
                    cap *= 2;
                }
                char *tmp = (char *)realloc(out, cap);
                if (!tmp) {
                    perror("realloc");
                    free(out);
                    free(replacement);
                    exit(EXIT_FAILURE);
                }
                out = tmp;
            }
            memcpy(out + len, replacement, rep_len);
            len += rep_len;
            free(replacement);
            continue;
        }

literal_dollar:
        if (len + 2 > cap) {
            while (len + 2 > cap) {
                cap *= 2;
            }
            char *tmp = (char *)realloc(out, cap);
            if (!tmp) {
                perror("realloc");
                free(out);
                exit(EXIT_FAILURE);
            }
            out = tmp;
        }
        out[len++] = *cursor++;
    }

    out[len] = '\0';
    if (!had_variable) {
        free(out);
        return NULL;
    }
    return out;
}

static ValueType detect_numeric_type(const char *token, long long *out_int, double *out_float) {
    if (!token || !*token) {
        return VALUE_UNSET;
    }
    errno = 0;
    char *endptr = NULL;
    long long iv = strtoll(token, &endptr, 10);
    if (errno == 0 && endptr && *endptr == '\0') {
        if (out_int) {
            *out_int = iv;
        }
        if (out_float) {
            *out_float = (double)iv;
        }
        return VALUE_INT;
    }
    errno = 0;
    endptr = NULL;
    double dv = strtod(token, &endptr);
    if (errno == 0 && endptr && *endptr == '\0') {
        if (out_float) {
            *out_float = dv;
        }
        if (out_int) {
            *out_int = (long long)dv;
        }
        return VALUE_FLOAT;
    }
    return VALUE_UNSET;
}

static bool parse_value_token(const char **p, Value *out, const char *delims, int line, int debug) {
    char *token = NULL;
    bool quoted = false;
    if (!parse_token(p, &token, &quoted, delims)) {
        if (debug) {
            fprintf(stderr, "Line %d: failed to parse value\n", line);
        }
        return false;
    }
    Value result;
    memset(&result, 0, sizeof(result));
    if (quoted) {
        result.type = VALUE_STRING;
        result.str_val = token;
        result.owns_string = true;
    } else if (token[0] == '$') {
        char name[64];
        if (!parse_variable_name_token(token, name, sizeof(name))) {
            if (debug) {
                fprintf(stderr, "Line %d: invalid variable name '%s'\n", line, token);
            }
            free(token);
            return false;
        }
        free(token);
        Variable *var = find_variable(name, false);
        result = variable_to_value(var);
        if (result.type == VALUE_UNSET) {
            result.int_val = 0;
            result.float_val = 0.0;
            result.str_val = NULL;
            result.owns_string = false;
        }
    } else {
        long long iv = 0;
        double fv = 0.0;
        ValueType vt = detect_numeric_type(token, &iv, &fv);
        if (vt == VALUE_INT) {
            result.type = VALUE_INT;
            result.int_val = iv;
            result.float_val = (double)iv;
            result.str_val = NULL;
            result.owns_string = false;
            free(token);
        } else if (vt == VALUE_FLOAT) {
            result.type = VALUE_FLOAT;
            result.float_val = fv;
            result.int_val = (long long)fv;
            result.str_val = NULL;
            result.owns_string = false;
            free(token);
        } else {
            result.type = VALUE_STRING;
            result.str_val = token;
            result.owns_string = true;
        }
    }
    *out = result;
    return true;
}

static bool value_as_double(const Value *value, double *out) {
    if (!value || !out) {
        return false;
    }
    if (value->type == VALUE_INT) {
        *out = (double)value->int_val;
        return true;
    }
    if (value->type == VALUE_FLOAT) {
        *out = value->float_val;
        return true;
    }
    if (value->type == VALUE_STRING && value->str_val) {
        errno = 0;
        char *endptr = NULL;
        double dv = strtod(value->str_val, &endptr);
        if (errno == 0 && endptr && *endptr == '\0') {
            *out = dv;
            return true;
        }
    }
    return false;
}

static char *value_to_string(const Value *value) {
    if (!value) {
        return xstrdup("");
    }
    if (value->type == VALUE_STRING) {
        return xstrdup(value->str_val ? value->str_val : "");
    }
    if (value->type == VALUE_INT) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld", value->int_val);
        return xstrdup(buf);
    }
    if (value->type == VALUE_FLOAT) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", value->float_val);
        return xstrdup(buf);
    }
    return xstrdup("");
}

static bool value_add_inplace(Value *acc, const Value *term) {
    if (!acc || !term) {
        return false;
    }

    if (acc->type == VALUE_UNSET) {
        return copy_value(acc, term);
    }

    double acc_num = 0.0;
    double term_num = 0.0;
    bool acc_numeric = value_as_double(acc, &acc_num);
    bool term_numeric = value_as_double(term, &term_num);

    if (acc_numeric && term_numeric) {
        bool both_int = (acc->type == VALUE_INT && term->type == VALUE_INT);
        Value result;
        memset(&result, 0, sizeof(result));
        if (both_int) {
            result.type = VALUE_INT;
            result.int_val = acc->int_val + term->int_val;
            result.float_val = (double)result.int_val;
        } else {
            result.type = VALUE_FLOAT;
            result.float_val = acc_num + term_num;
            result.int_val = (long long)result.float_val;
        }
        free_value(acc);
        *acc = result;
        return true;
    }

    char *acc_str = value_to_string(acc);
    char *term_str = value_to_string(term);
    size_t acc_len = strlen(acc_str);
    size_t term_len = strlen(term_str);
    char *combined = (char *)malloc(acc_len + term_len + 1);
    if (!combined) {
        perror("malloc");
        free(acc_str);
        free(term_str);
        exit(EXIT_FAILURE);
    }
    memcpy(combined, acc_str, acc_len);
    memcpy(combined + acc_len, term_str, term_len + 1);
    free(acc_str);
    free(term_str);

    free_value(acc);
    Value result;
    memset(&result, 0, sizeof(result));
    result.type = VALUE_STRING;
    result.str_val = combined;
    result.owns_string = true;
    *acc = result;
    return true;
}

static bool parse_expression(const char **cursor, Value *out, const char *terminators, int line, int debug) {
    if (!cursor || !out) {
        return false;
    }

    Value accumulator;
    memset(&accumulator, 0, sizeof(accumulator));
    bool have_term = false;

    const char *delims = "+";
    char delim_buf[32];
    if (terminators && *terminators) {
        size_t term_len = strlen(terminators);
        if (term_len > sizeof(delim_buf) - 2) {
            term_len = sizeof(delim_buf) - 2;
        }
        delim_buf[0] = '+';
        memcpy(&delim_buf[1], terminators, term_len);
        delim_buf[term_len + 1] = '\0';
        delims = delim_buf;
    }

    while (1) {
        Value term;
        if (!parse_value_token(cursor, &term, delims, line, debug)) {
            free_value(&accumulator);
            return false;
        }
        have_term = true;
        if (!value_add_inplace(&accumulator, &term)) {
            free_value(&term);
            free_value(&accumulator);
            return false;
        }
        free_value(&term);

        while (isspace((unsigned char)**cursor)) {
            (*cursor)++;
        }
        if (**cursor == '+') {
            (*cursor)++;
            continue;
        }
        break;
    }

    if (!have_term) {
        free_value(&accumulator);
        return false;
    }

    *out = accumulator;
    return true;
}

static bool evaluate_comparison(const Value *lhs, const Value *rhs, const char *op, bool *out_result, int line, int debug) {
    if (!lhs || !rhs || !op || !out_result) {
        return false;
    }
    bool equality = (strcmp(op, "==") == 0) || (strcmp(op, "!=") == 0);
    bool relational = !equality;
    if (relational && (strcmp(op, ">") != 0 && strcmp(op, "<") != 0 && strcmp(op, ">=") != 0 && strcmp(op, "<=") != 0)) {
        if (debug) {
            fprintf(stderr, "Line %d: unsupported operator '%s'\n", line, op);
        }
        return false;
    }

    if (lhs->type == VALUE_UNSET || rhs->type == VALUE_UNSET) {
        if (equality && lhs->type == VALUE_UNSET && rhs->type == VALUE_UNSET && strcmp(op, "==") == 0) {
            *out_result = true;
        } else if (equality && lhs->type == VALUE_UNSET && rhs->type == VALUE_UNSET && strcmp(op, "!=") == 0) {
            *out_result = false;
        } else {
            *out_result = false;
        }
        return true;
    }

    if (relational) {
        double lnum = 0.0, rnum = 0.0;
        bool l_ok = value_as_double(lhs, &lnum);
        bool r_ok = value_as_double(rhs, &rnum);
        if (l_ok && r_ok) {
            if (strcmp(op, ">") == 0) {
                *out_result = lnum > rnum;
            } else if (strcmp(op, "<") == 0) {
                *out_result = lnum < rnum;
            } else if (strcmp(op, ">=") == 0) {
                *out_result = lnum >= rnum;
            } else if (strcmp(op, "<=") == 0) {
                *out_result = lnum <= rnum;
            } else {
                *out_result = false;
            }
            return true;
        }
        char *lstr = value_to_string(lhs);
        char *rstr = value_to_string(rhs);
        int cmp = strcmp(lstr, rstr);
        free(lstr);
        free(rstr);
        if (strcmp(op, ">") == 0) {
            *out_result = cmp > 0;
        } else if (strcmp(op, "<") == 0) {
            *out_result = cmp < 0;
        } else if (strcmp(op, ">=") == 0) {
            *out_result = cmp >= 0;
        } else if (strcmp(op, "<=") == 0) {
            *out_result = cmp <= 0;
        } else {
            *out_result = false;
        }
        return true;
    }

    double lnum = 0.0, rnum = 0.0;
    bool l_numeric = value_as_double(lhs, &lnum);
    bool r_numeric = value_as_double(rhs, &rnum);
    if (l_numeric && r_numeric) {
        double diff = lnum - rnum;
        bool eq = fabs(diff) < 1e-9;
        if (strcmp(op, "==") == 0) {
            *out_result = eq;
        } else {
            *out_result = !eq;
        }
        return true;
    }
    char *lstr = value_to_string(lhs);
    char *rstr = value_to_string(rhs);
    int cmp = strcmp(lstr, rstr);
    free(lstr);
    free(rstr);
    if (strcmp(op, "==") == 0) {
        *out_result = (cmp == 0);
    } else {
        *out_result = (cmp != 0);
    }
    return true;
}

static void note_branch_progress(IfContext *stack, int *sp) {
    if (!stack || !sp || *sp <= 0) {
        return;
    }
    IfContext *ctx = &stack[*sp - 1];
    if (!ctx->true_branch_done) {
        ctx->true_branch_done = true;
        return;
    }
    if (ctx->else_encountered && !ctx->else_branch_done) {
        ctx->else_branch_done = true;
        (*sp)--;
    }
}

static void finalize_skipped_branch(IfContext *stack, int *sp, int context_index, bool skipping_true_branch) {
    if (!stack || !sp || *sp <= 0) {
        return;
    }
    if (context_index < 0 || context_index != *sp - 1) {
        return;
    }
    IfContext *ctx = &stack[*sp - 1];
    if (skipping_true_branch) {
        ctx->true_branch_done = true;
    } else {
        ctx->else_branch_done = true;
        (*sp)--;
    }
}

static void print_help(void) {
    printf("\nRuntask Help\n");
    printf("============\n\n");
    printf("Commands:\n");
    printf("  SET $VAR = value   : Store integers, floats, or strings in a variable\n");
    printf("  INPUT $VAR         : Read a line from stdin into $VAR (numbers auto-detected)\n");
    printf("  IF <lhs> op <rhs>  : Compare values. Use ELSE for an alternate branch.\n");
    printf("  PRINT expr         : Print literals and variables (use '+' to concatenate).\n");
    printf("  WAIT value         : Waits for <value> milliseconds (supports variables)\n");
    printf("  GOTO @label        : Jumps to the line marked with @label\n");
    printf("  RUN [BLOCKING|NONBLOCKING] <cmd [args...]>:\n");
    printf("                       Executes an executable from ./apps, ./commands, or ./utilities.\n");
    printf("                       Default is BLOCKING. If the command contains '/', it's executed as given.\n");
    printf("                       Append 'TO $VAR' to capture stdout into $VAR (blocking mode only).\n");
    printf("                       $VAR and ${VAR} sequences expand using task variables.\n");
    printf("  CLEAR              : Clears the screen\n\n");
    printf("Usage:\n");
    printf("  ./runtask taskfile [-d]\n\n");
    printf("Notes:\n");
    printf("- Task files are loaded from 'tasks/' automatically (e.g., tasks/demo.task)\n");
    printf("- Place your executables in ./apps, ./commands, or ./utilities and make them executable.\n\n");
    printf("Compilation:\n");
    printf("  gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 -o runtask apps/runtask.c\n\n");
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) e--;
    e[1] = '\0';
    return s;
}

static void delay_ms(int ms) {
    int elapsed = 0;
    while (elapsed < ms && !stop) {
        int slice = (ms - elapsed > 50) ? 50 : (ms - elapsed);
        struct timespec ts = { .tv_sec = slice / 1000, .tv_nsec = (slice % 1000) * 1000000L };
        thrd_sleep(&ts, NULL);
        elapsed += slice;
    }
}

typedef enum {
    LINE_COMMAND = 0,
    LINE_LABEL
} LineType;

typedef struct {
    int source_line;   // original file line number for diagnostics
    LineType type;
    int indent;        // leading whitespace count for block handling
    char text[256];
} ScriptLine;

typedef struct {
    char name[64];
    int index;        // index into script array
} Label;

static void normalize_label_name(const char *input, char *output, size_t size) {
    if (!input || !output || size == 0) {
        return;
    }
    size_t i = 0;
    for (; input[i] && i + 1 < size; ++i) {
        output[i] = (char)toupper((unsigned char)input[i]);
    }
    output[i] = '\0';
}

static int find_label_index(const Label *labels, int label_count, const char *name) {
    if (!labels || !name) {
        return -1;
    }
    for (int i = 0; i < label_count; ++i) {
        if (equals_ignore_case(labels[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static bool parse_label_definition(const char *line, char *out_name, size_t name_size) {
    if (!line) {
        return false;
    }
    const char *cursor = line;
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '@') {
        return false;
    }
    cursor++;
    size_t len = 0;
    while (*cursor && !isspace((unsigned char)*cursor) && *cursor != ':') {
        if (len + 1 >= name_size) {
            return false;
        }
        out_name[len++] = *cursor++;
    }
    if (len == 0) {
        return false;
    }
    out_name[len] = '\0';
    if (*cursor == ':') {
        cursor++;
    }
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '\0') {
        return false;
    }
    return true;
}

/* Portable strdup replacement to stay ISO C compliant under -std=c11 -pedantic */
static char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = (char*)malloc(len);
    if (!p) { perror("malloc"); exit(EXIT_FAILURE); }
    memcpy(p, s, len);
    return p;
}

/* --- Heap-based argv tokenizer supporting quotes and backslash escapes ---
   - Splits by whitespace.
   - Supports "double quoted" and 'single quoted' args.
   - Supports backslash escapes inside double quotes and unquoted text.
   - Returns a NULL-terminated argv array; *out_argc has argc.
   - Caller must free with free_argv().
*/
static char **split_args_heap(const char *cmdline, int *out_argc) {
    char **argv = NULL;
    int argc = 0, cap = 0;

    const char *p = cmdline;
    while (*p) {
        // skip leading spaces
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        bool in_sq = false, in_dq = false;
        // grow token buffer dynamically to avoid truncation
        size_t tcap = 64, ti = 0;
        char *token = (char*)malloc(tcap);
        if (!token) { perror("malloc"); exit(EXIT_FAILURE); }

        while (*p) {
            if (!in_dq && *p == '\'') { in_sq = !in_sq; p++; continue; }
            if (!in_sq && *p == '"')  { in_dq = !in_dq; p++; continue; }
            if (!in_sq && *p == '\\') {
                p++;
                if (*p) {
                    if (ti + 1 >= tcap) { tcap *= 2; token = (char*)realloc(token, tcap); if (!token) { perror("realloc"); exit(EXIT_FAILURE); } }
                    token[ti++] = *p++;
                }
                continue;
            }
            if (!in_sq && !in_dq && isspace((unsigned char)*p)) break;

            if (ti + 1 >= tcap) { tcap *= 2; token = (char*)realloc(token, tcap); if (!token) { perror("realloc"); exit(EXIT_FAILURE); } }
            token[ti++] = *p++;
        }
        if (in_sq || in_dq) {
            // Unmatched quotes: close them implicitly
            // (Alternative: error out. Here we just proceed.)
        }
        if (ti + 1 >= tcap) { tcap += 1; token = (char*)realloc(token, tcap); if (!token) { perror("realloc"); exit(EXIT_FAILURE); } }
        token[ti] = '\0';

        if (argc == cap) {
            cap = cap ? cap * 2 : 8;
            char **newv = (char**)realloc(argv, (size_t)(cap + 1) * sizeof(char *));
            if (!newv) { perror("realloc"); exit(EXIT_FAILURE); }
            argv = newv;
        }
        argv[argc++] = token;
    }

    if (!argv) {
        argv = (char**)malloc(2 * sizeof(char *));
        if (!argv) { perror("malloc"); exit(EXIT_FAILURE); }
        argv[0] = NULL;
        if (out_argc) *out_argc = 0;
        return argv;
    }
    argv[argc] = NULL;
    if (out_argc) *out_argc = argc;
    return argv;
}

static void free_argv(char **argv) {
    if (!argv) return;
    for (char **p = argv; *p; ++p) free(*p);
    free(argv);
}

static void expand_argv_variables(char **argv, int argc, int line, int debug) {
    if (!argv) {
        return;
    }
    for (int i = 0; i < argc; ++i) {
        char *token = argv[i];
        if (!token) {
            continue;
        }
        char *expanded = expand_token_variables(token, line, debug);
        if (!expanded) {
            continue;
        }
        free(argv[i]);
        argv[i] = expanded;
    }
}

/* Resolve executable path:
   - If argv0 contains '/', use as-is.
   - Else try "apps/argv0", then "commands/argv0", then "utilities/argv0".
   - If found and executable, write into resolved (size bytes) and return 0; else -1.
*/
static int resolve_exec_path(const char *argv0, char *resolved, size_t size) {
    if (!argv0 || !*argv0) return -1;

    if (strchr(argv0, '/')) {
        // explicit relative/absolute path
        if (build_from_base(argv0, resolved, size) != 0) return -1;
        if (access(resolved, X_OK) != 0) return -1;
        return 0;
    }

    const char *dirs[] = { "apps", "commands", "utilities" };
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
        char candidate[PATH_MAX];
        if (snprintf(candidate, sizeof(candidate), "%s/%s", dirs[i], argv0) >= (int)sizeof(candidate))
            continue;
        if (build_from_base(candidate, resolved, size) != 0)
            continue;
        if (access(resolved, X_OK) == 0) {
            return 0;
        }
    }
    return -1;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);

    if (argc >= 2 && strcmp(argv[1], "-help") == 0) {
        print_help();
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: %s taskfile [-d]\n", argv[0]);
        return 1;
    }
    int debug = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) { debug = 1; break; }
    }

    // Prepend tasks/ like before but resolve relative to Budostack base when available
    char suffix[PATH_MAX];
    if (argv[1][0] == '/' || argv[1][0] == '.') {
        if (snprintf(suffix, sizeof(suffix), "%s", argv[1]) >= (int)sizeof(suffix)) {
            fprintf(stderr, "Error: task path too long: %s\n", argv[1]);
            return 1;
        }
    } else {
        if (snprintf(suffix, sizeof(suffix), "tasks/%s", argv[1]) >= (int)sizeof(suffix)) {
            fprintf(stderr, "Error: task name too long: %s\n", argv[1]);
            return 1;
        }
    }

    char task_path[PATH_MAX];
    if (build_from_base(suffix, task_path, sizeof(task_path)) != 0) {
        fprintf(stderr, "Error: could not resolve task path for '%s'\n", argv[1]);
        return 1;
    }

    FILE *fp = fopen(task_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Could not open task file '%s'\n", task_path);
        return 1;
    }

    // Load script
    ScriptLine script[1024];
    Label labels[MAX_LABELS];
    memset(labels, 0, sizeof(labels));
    int label_count = 0;
    int count = 0;
    char linebuf[256];
    int file_line = 0;
    while (fgets(linebuf, sizeof(linebuf), fp)) {
        file_line++;
        char *line = trim(linebuf);
        int indent = (int)(line - linebuf);
        if (indent < 0) {
            indent = 0;
        }
        if (!*line) {
            continue;
        }
        if (count >= (int)(sizeof(script) / sizeof(script[0]))) {
            fprintf(stderr, "Error: script too long (max %zu lines)\n", sizeof(script) / sizeof(script[0]));
            break;
        }

        if (*line == '@') {
            char label_name[64];
            if (!parse_label_definition(line, label_name, sizeof(label_name))) {
                fprintf(stderr, "Error: invalid label definition at line %d: %s\n", file_line, line);
                continue;
            }
            script[count].source_line = file_line;
            script[count].type = LINE_LABEL;
            script[count].indent = indent;
            strncpy(script[count].text, line, sizeof(script[count].text) - 1);
            script[count].text[sizeof(script[count].text) - 1] = '\0';

            char normalized[64];
            normalize_label_name(label_name, normalized, sizeof(normalized));
            int existing = find_label_index(labels, label_count, normalized);
            if (existing >= 0) {
                labels[existing].index = count;
            } else {
                if (label_count >= MAX_LABELS) {
                    fprintf(stderr, "Error: too many labels (max %d)\n", MAX_LABELS);
                } else {
                    snprintf(labels[label_count].name, sizeof(labels[label_count].name), "%s", normalized);
                    labels[label_count].index = count;
                    label_count++;
                }
            }
            count++;
            continue;
        }

        script[count].source_line = file_line;
        script[count].type = LINE_COMMAND;
        script[count].indent = indent;
        strncpy(script[count].text, line, sizeof(script[count].text) - 1);
        script[count].text[sizeof(script[count].text) - 1] = '\0';
        count++;
    }
    fclose(fp);

    IfContext if_stack[64];
    int if_sp = 0;
    bool skipping_block = false;
    int skip_indent = 0;
    int skip_context_index = -1;
    bool skip_for_true_branch = false;
    bool skip_progress_pending = false;
    bool skip_consumed_first = false;

    // Run
    for (int pc = 0; pc < count && !stop; pc++) {
        if (debug) {
            if (script[pc].type == LINE_LABEL) {
                fprintf(stderr, "Encountered label at line %d: %s\n", script[pc].source_line, script[pc].text);
            } else {
                fprintf(stderr, "Executing line %d: %s\n", script[pc].source_line, script[pc].text);
            }
        }

        char *command = script[pc].text;

        if (skipping_block) {
            int current_indent = script[pc].indent;
            if (script[pc].type == LINE_LABEL) {
                if (current_indent > skip_indent) {
                    continue;
                }
                if (skip_progress_pending) {
                    finalize_skipped_branch(if_stack, &if_sp, skip_context_index, skip_for_true_branch);
                    skip_progress_pending = false;
                }
                skipping_block = false;
                skip_context_index = -1;
                skip_for_true_branch = false;
                skip_consumed_first = false;
            } else if (command && current_indent <= skip_indent &&
                       strncmp(command, "ELSE", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4]))) {
                if (skip_progress_pending) {
                    finalize_skipped_branch(if_stack, &if_sp, skip_context_index, skip_for_true_branch);
                    skip_progress_pending = false;
                }
                skipping_block = false;
                skip_context_index = -1;
                skip_for_true_branch = false;
                skip_consumed_first = false;
            } else if (!skip_consumed_first) {
                skip_consumed_first = true;
                continue;
            } else if (current_indent > skip_indent) {
                continue;
            } else {
                if (skip_progress_pending) {
                    finalize_skipped_branch(if_stack, &if_sp, skip_context_index, skip_for_true_branch);
                    skip_progress_pending = false;
                }
                skipping_block = false;
                skip_context_index = -1;
                skip_for_true_branch = false;
                skip_consumed_first = false;
            }
        }

        if (script[pc].type == LINE_LABEL) {
            continue;
        }

        if (if_sp > 0) {
            IfContext *ctx = &if_stack[if_sp - 1];
            if (ctx->true_branch_done && !ctx->else_encountered) {
                if (!(strncmp(command, "ELSE", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4])))) {
                    if_sp--;
                }
            }
        }

        if (strncmp(command, "IF", 2) == 0 && (command[2] == '\0' || isspace((unsigned char)command[2]))) {
            const char *cursor = command + 2;
            Value lhs;
            if (!parse_expression(&cursor, &lhs, "<>!=", script[pc].source_line, debug)) {
                continue;
            }
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            char op[3] = { 0 };
            if (cursor[0] == '=' && cursor[1] == '=') { op[0] = '='; op[1] = '='; cursor += 2; }
            else if (cursor[0] == '!' && cursor[1] == '=') { op[0] = '!'; op[1] = '='; cursor += 2; }
            else if (cursor[0] == '>' && cursor[1] == '=') { op[0] = '>'; op[1] = '='; cursor += 2; }
            else if (cursor[0] == '<' && cursor[1] == '=') { op[0] = '<'; op[1] = '='; cursor += 2; }
            else if (cursor[0] == '>') { op[0] = '>'; cursor += 1; }
            else if (cursor[0] == '<') { op[0] = '<'; cursor += 1; }
            else {
                if (debug) fprintf(stderr, "IF: invalid or missing operator at %d\n", script[pc].source_line);
                free_value(&lhs);
                continue;
            }
            Value rhs;
            if (!parse_expression(&cursor, &rhs, NULL, script[pc].source_line, debug)) {
                free_value(&lhs);
                continue;
            }
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "IF: unexpected characters at %d\n", script[pc].source_line);
            }
            bool cond_result = false;
            if (!evaluate_comparison(&lhs, &rhs, op, &cond_result, script[pc].source_line, debug)) {
                cond_result = false;
            }
            free_value(&lhs);
            free_value(&rhs);
            if (if_sp >= (int)(sizeof(if_stack) / sizeof(if_stack[0]))) {
                if (debug) fprintf(stderr, "IF: nesting limit reached at line %d\n", script[pc].source_line);
                continue;
            }
            IfContext ctx = { .result = cond_result, .true_branch_done = false, .else_encountered = false, .else_branch_done = false, .line_number = script[pc].source_line };
            if_stack[if_sp++] = ctx;
            if (!cond_result) {
                skipping_block = true;
                skip_indent = script[pc].indent;
                skip_context_index = if_sp - 1;
                skip_for_true_branch = true;
                skip_progress_pending = true;
                skip_consumed_first = false;
            }
        }
        else if (strncmp(command, "ELSE", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4]))) {
            if (if_sp <= 0) {
                if (debug) fprintf(stderr, "ELSE without matching IF at line %d\n", script[pc].source_line);
                continue;
            }
            const char *cursor = command + 4;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "ELSE: unexpected characters at %d\n", script[pc].source_line);
            }
            IfContext *ctx = &if_stack[if_sp - 1];
            if (ctx->else_encountered) {
                if (debug) fprintf(stderr, "ELSE already processed for IF at line %d\n", ctx->line_number);
                continue;
            }
            ctx->else_encountered = true;
            if (ctx->result) {
                skipping_block = true;
                skip_indent = script[pc].indent;
                skip_context_index = if_sp - 1;
                skip_for_true_branch = false;
                skip_progress_pending = true;
                skip_consumed_first = false;
            }
        }
        else if (strncmp(command, "INPUT", 5) == 0 && (command[5] == '\0' || isspace((unsigned char)command[5]))) {
            const char *cursor = command + 5;
            char *var_token = NULL;
            bool quoted = false;
            if (!parse_token(&cursor, &var_token, &quoted, NULL) || quoted) {
                if (debug) fprintf(stderr, "INPUT: expected variable at line %d\n", script[pc].source_line);
                free(var_token);
                continue;
            }
            char name[64];
            if (!parse_variable_name_token(var_token, name, sizeof(name))) {
                if (debug) fprintf(stderr, "INPUT: invalid variable name at line %d\n", script[pc].source_line);
                free(var_token);
                continue;
            }
            free(var_token);
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "INPUT: unexpected characters at %d\n", script[pc].source_line);
            }
            Variable *var = find_variable(name, true);
            if (!var) {
                continue;
            }
            fflush(stdout);
            char buffer[512];
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                if (debug) fprintf(stderr, "INPUT: failed to read input at line %d\n", script[pc].source_line);
                Value empty = { .type = VALUE_STRING, .str_val = xstrdup(""), .owns_string = true };
                assign_variable(var, &empty);
                free_value(&empty);
            } else {
                size_t len = strcspn(buffer, "\r\n");
                buffer[len] = '\0';
                long long iv = 0;
                double fv = 0.0;
                Value val;
                memset(&val, 0, sizeof(val));
                ValueType vt = detect_numeric_type(buffer, &iv, &fv);
                if (vt == VALUE_INT) {
                    val.type = VALUE_INT;
                    val.int_val = iv;
                    val.float_val = (double)iv;
                } else if (vt == VALUE_FLOAT) {
                    val.type = VALUE_FLOAT;
                    val.float_val = fv;
                    val.int_val = (long long)fv;
                } else {
                    val.type = VALUE_STRING;
                    val.str_val = xstrdup(buffer);
                    val.owns_string = true;
                }
                assign_variable(var, &val);
                free_value(&val);
            }
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "SET", 3) == 0 && (command[3] == '\0' || isspace((unsigned char)command[3]))) {
            const char *cursor = command + 3;
            char *var_token = NULL;
            bool quoted = false;
            if (!parse_token(&cursor, &var_token, &quoted, NULL) || quoted) {
                if (debug) fprintf(stderr, "SET: expected variable at line %d\n", script[pc].source_line);
                free(var_token);
                continue;
            }
            char name[64];
            if (!parse_variable_name_token(var_token, name, sizeof(name))) {
                if (debug) fprintf(stderr, "SET: invalid variable name at line %d\n", script[pc].source_line);
                free(var_token);
                continue;
            }
            free(var_token);
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '=') {
                if (debug) fprintf(stderr, "SET: expected '=' at line %d\n", script[pc].source_line);
                continue;
            }
            cursor++;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            Value value;
            if (!parse_expression(&cursor, &value, NULL, script[pc].source_line, debug)) {
                continue;
            }
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "SET: unexpected characters at %d\n", script[pc].source_line);
            }
            Variable *var = find_variable(name, true);
            if (var) {
                assign_variable(var, &value);
            }
            free_value(&value);
            note_branch_progress(if_stack, &if_sp);
        }
        else if (command[0] == '$') {
            const char *cursor = command;
            char *var_token = NULL;
            bool quoted = false;
            if (!parse_token(&cursor, &var_token, &quoted, "=") || quoted) {
                if (debug) fprintf(stderr, "Assignment: expected variable at line %d\n", script[pc].source_line);
                free(var_token);
                continue;
            }
            char name[64];
            if (!parse_variable_name_token(var_token, name, sizeof(name))) {
                if (debug) fprintf(stderr, "Assignment: invalid variable name at line %d\n", script[pc].source_line);
                free(var_token);
                continue;
            }
            free(var_token);
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '=') {
                if (debug) fprintf(stderr, "Assignment: expected '=' at line %d\n", script[pc].source_line);
                continue;
            }
            cursor++;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            Value value;
            if (!parse_expression(&cursor, &value, NULL, script[pc].source_line, debug)) {
                continue;
            }
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "Assignment: unexpected characters at %d\n", script[pc].source_line);
            }
            Variable *var = find_variable(name, true);
            if (var) {
                assign_variable(var, &value);
            }
            free_value(&value);
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "PRINT", 5) == 0 && (command[5] == '\0' || isspace((unsigned char)command[5]))) {
            const char *cursor = command + 5;
            size_t out_cap = 128;
            size_t out_len = 0;
            char *out_buf = (char *)malloc(out_cap);
            if (!out_buf) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            bool ok = true;
            while (1) {
                Value term;
                if (!parse_value_token(&cursor, &term, "+", script[pc].source_line, debug)) {
                    ok = false;
                    break;
                }
                char *as_str = value_to_string(&term);
                size_t need = strlen(as_str);
                if (out_len + need + 1 > out_cap) {
                    while (out_len + need + 1 > out_cap) {
                        out_cap *= 2;
                    }
                    char *tmp = (char *)realloc(out_buf, out_cap);
                    if (!tmp) {
                        perror("realloc");
                        free(out_buf);
                        free(as_str);
                        free_value(&term);
                        exit(EXIT_FAILURE);
                    }
                    out_buf = tmp;
                }
                memcpy(out_buf + out_len, as_str, need);
                out_len += need;
                free(as_str);
                free_value(&term);
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (*cursor == '+') {
                    cursor++;
                    continue;
                }
                break;
            }
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0') {
                ok = false;
                if (debug) {
                    fprintf(stderr, "PRINT: unexpected characters at %d\n", script[pc].source_line);
                }
            }
		    if (ok) {
		        out_buf[out_len] = '\0';
		        size_t len = strlen(out_buf);
		        if (len > 0 && out_buf[len - 1] == '\n') {
		            fputs(out_buf, stdout);
		        } else {
		            fputs(out_buf, stdout);
		            fflush(stdout); // keep INPUT on the same line if needed
		        }
		    }
		    free(out_buf);
		    note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "WAIT", 4) == 0) {
            const char *cursor = command + 4;
            Value wait_value;
            memset(&wait_value, 0, sizeof(wait_value));

            if (!parse_value_token(&cursor, &wait_value, "", script[pc].source_line, debug)) {
                if (debug) {
                    fprintf(stderr, "WAIT: invalid value at %d: %s\n", script[pc].source_line, command);
                }
            } else {
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (*cursor != '\0' && debug) {
                    fprintf(stderr, "WAIT: unexpected trailing characters at %d: %s\n", script[pc].source_line, command);
                }

                double delay = 0.0;
                if (!value_as_double(&wait_value, &delay)) {
                    if (debug) {
                        fprintf(stderr, "WAIT: value is not numeric at %d: %s\n", script[pc].source_line, command);
                    }
                } else {
                    if (delay < 0.0) {
                        delay = 0.0;
                    }
                    if (delay > (double)INT_MAX) {
                        delay = (double)INT_MAX;
                    }
                    int ms = (int)llround(delay);
                    delay_ms(ms);
                }
            }

            free_value(&wait_value);
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "GOTO", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4]))) {
            const char *cursor = command + 4;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '@') {
                if (debug) {
                    fprintf(stderr, "GOTO: expected '@label' at %d: %s\n", script[pc].source_line, command);
                }
                note_branch_progress(if_stack, &if_sp);
                continue;
            }
            cursor++;
            char label_token[64];
            size_t len = 0;
            bool too_long = false;
            while (*cursor && !isspace((unsigned char)*cursor) && *cursor != ':') {
                if (len + 1 >= sizeof(label_token)) {
                    too_long = true;
                    cursor++;
                    continue;
                }
                label_token[len++] = *cursor++;
            }
            label_token[len] = '\0';
            if (len == 0) {
                if (debug) {
                    fprintf(stderr, "GOTO: empty label at %d\n", script[pc].source_line);
                }
                note_branch_progress(if_stack, &if_sp);
                continue;
            }
            if (too_long) {
                if (debug) {
                    fprintf(stderr, "GOTO: label too long at %d\n", script[pc].source_line);
                }
                note_branch_progress(if_stack, &if_sp);
                continue;
            }
            if (*cursor == ':') {
                cursor++;
            }
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "GOTO: unexpected characters at %d\n", script[pc].source_line);
            }
            char normalized[64];
            normalize_label_name(label_token, normalized, sizeof(normalized));
            int label_index = find_label_index(labels, label_count, normalized);
            if (label_index < 0) {
                if (debug) {
                    fprintf(stderr, "GOTO: label '%s' not found at %d\n", label_token, script[pc].source_line);
                }
            } else {
                int target_index = labels[label_index].index;
                pc = target_index - 1; // -1 because loop will ++pc
            }
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "RUN", 3) == 0) {
            const char *after = command + 3;
            char *cmdline = trim((char*)after);
            if (!*cmdline) {
                if (debug) fprintf(stderr, "RUN: missing command at line %d\n", script[pc].source_line);
                continue;
            }

            // Tokenize to argv[] (heap-based)
            int argcnt = 0;
            char **argv_heap = split_args_heap(cmdline, &argcnt);
            if (argcnt <= 0) {
                if (debug) fprintf(stderr, "RUN: failed to parse command at line %d\n", script[pc].source_line);
                free_argv(argv_heap);
                continue;
            }

            bool blocking_mode = true;
            bool capture_output = false;
            Variable *capture_var = NULL;
            char *captured_output = NULL;
            size_t captured_len = 0;
            size_t captured_cap = 0;

            if (argcnt > 0) {
                if (equals_ignore_case(argv_heap[0], "BLOCKING")) {
                    blocking_mode = true;
                    free(argv_heap[0]);
                    for (int i = 1; i < argcnt; ++i) {
                        argv_heap[i - 1] = argv_heap[i];
                    }
                    argv_heap[argcnt - 1] = NULL;
                    argcnt--;
                } else if (equals_ignore_case(argv_heap[0], "NONBLOCKING") ||
                           equals_ignore_case(argv_heap[0], "NON-BLOCKING")) {
                    blocking_mode = false;
                    free(argv_heap[0]);
                    for (int i = 1; i < argcnt; ++i) {
                        argv_heap[i - 1] = argv_heap[i];
                    }
                    argv_heap[argcnt - 1] = NULL;
                    argcnt--;
                }
            }

            if (argcnt <= 0) {
                if (debug) fprintf(stderr, "RUN: missing executable at line %d\n", script[pc].source_line);
                free_argv(argv_heap);
                continue;
            }

            if (argcnt >= 3 && equals_ignore_case(argv_heap[argcnt - 2], "TO")) {
                char name[64];
                if (!parse_variable_name_token(argv_heap[argcnt - 1], name, sizeof(name))) {
                    fprintf(stderr, "RUN: invalid variable name after TO at line %d\n", script[pc].source_line);
                    free_argv(argv_heap);
                    continue;
                }
                capture_var = find_variable(name, true);
                if (!capture_var) {
                    free_argv(argv_heap);
                    continue;
                }
                capture_output = true;
                free(argv_heap[argcnt - 1]);
                free(argv_heap[argcnt - 2]);
                argv_heap[argcnt - 2] = NULL;
                argv_heap[argcnt - 1] = NULL;
                argcnt -= 2;
                argv_heap[argcnt] = NULL;
                if (argcnt <= 0) {
                    fprintf(stderr, "RUN: missing executable before TO at line %d\n", script[pc].source_line);
                    free_argv(argv_heap);
                    continue;
                }
            }

            expand_argv_variables(argv_heap, argcnt, script[pc].source_line, debug);

            // Resolve executable path
            char resolved[PATH_MAX];
            if (resolve_exec_path(argv_heap[0], resolved, sizeof(resolved)) != 0) {
                fprintf(stderr, "RUN: executable not found or not executable: %s (searched apps/, commands/, utilities/)\n", argv_heap[0]);
                free_argv(argv_heap);
                continue;
            }

            // Replace argv[0] with a heap copy of the resolved path
            free(argv_heap[0]);
            argv_heap[0] = xstrdup(resolved);

            if (debug) {
                fprintf(stderr, "RUN: execv %s", argv_heap[0]);
                for (int i = 1; i < argcnt; ++i) fprintf(stderr, " [%s]", argv_heap[i]);
                if (capture_output) fprintf(stderr, " -> TO $%s", capture_var ? capture_var->name : "?");
                fprintf(stderr, " (%s)\n", blocking_mode ? "blocking" : "non-blocking");
            }

            if (!blocking_mode && capture_output) {
                fprintf(stderr, "RUN: cannot capture output in non-blocking mode at line %d\n", script[pc].source_line);
                free_argv(argv_heap);
                if (captured_output) {
                    free(captured_output);
                }
                continue;
            }

            if (!blocking_mode) {
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    free_argv(argv_heap);
                    continue;
                } else if (pid == 0) {
                    pid_t gpid = fork();
                    if (gpid < 0) {
                        perror("fork");
                        _exit(EXIT_FAILURE);
                    }
                    if (gpid == 0) {
                        execv(argv_heap[0], argv_heap);
                        perror("execv");
                        _exit(EXIT_FAILURE);
                    }
                    _exit(EXIT_SUCCESS);
                } else {
                    int status;
                    while (waitpid(pid, &status, 0) < 0) {
                        if (errno != EINTR) {
                            perror("waitpid");
                            break;
                        }
                    }
                }
                free_argv(argv_heap);
                note_branch_progress(if_stack, &if_sp);
                continue;
            }

            int pipefd[2] = { -1, -1 };
            if (capture_output) {
                if (pipe(pipefd) < 0) {
                    perror("pipe");
                    free_argv(argv_heap);
                    continue;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                if (capture_output) {
                    close(pipefd[0]);
                    close(pipefd[1]);
                }
                free_argv(argv_heap);
                continue;
            } else if (pid == 0) {
                if (capture_output) {
                    close(pipefd[0]);
                    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                    close(pipefd[1]);
                }
                execv(argv_heap[0], argv_heap);
                perror("execv");
                _exit(EXIT_FAILURE);
            } else {
                if (capture_output) {
                    close(pipefd[1]);
                    char buffer[4096];
                    ssize_t rd;
                    while (1) {
                        rd = read(pipefd[0], buffer, sizeof(buffer));
                        if (rd > 0) {
                            if (captured_len + (size_t)rd + 1 > captured_cap) {
                                size_t new_cap = captured_cap ? captured_cap : 128;
                                while (captured_len + (size_t)rd + 1 > new_cap) {
                                    new_cap *= 2;
                                }
                                char *tmp = (char *)realloc(captured_output, new_cap);
                                if (!tmp) {
                                    perror("realloc");
                                    free(captured_output);
                                    captured_output = NULL;
                                    captured_cap = captured_len = 0;
                                    break;
                                }
                                captured_output = tmp;
                                captured_cap = new_cap;
                            }
                            memcpy(captured_output + captured_len, buffer, (size_t)rd);
                            captured_len += (size_t)rd;
                        } else if (rd == 0) {
                            break;
                        } else {
                            if (errno == EINTR) {
                                continue;
                            }
                            perror("read");
                            break;
                        }
                    }
                    if (captured_output) {
                        captured_output[captured_len] = '\0';
                    } else {
                        captured_output = xstrdup("");
                        captured_len = 0;
                        captured_cap = 1;
                    }
                    close(pipefd[0]);
                }

                int status;
                while (waitpid(pid, &status, 0) < 0) {
                    if (errno != EINTR) { perror("waitpid"); break; }
                }
                if (debug) {
                    if (WIFEXITED(status))
                        fprintf(stderr, "RUN: exited with %d\n", WEXITSTATUS(status));
                    else if (WIFSIGNALED(status))
                        fprintf(stderr, "RUN: killed by signal %d\n", WTERMSIG(status));
                }

                if (capture_output && capture_var && captured_output) {
                    while (captured_len > 0 && (captured_output[captured_len - 1] == '\n' || captured_output[captured_len - 1] == '\r')) {
                        captured_output[--captured_len] = '\0';
                    }
                    long long iv = 0;
                    double fv = 0.0;
                    Value value;
                    memset(&value, 0, sizeof(value));
                    ValueType vt = detect_numeric_type(captured_output, &iv, &fv);
                    if (vt == VALUE_INT) {
                        value.type = VALUE_INT;
                        value.int_val = iv;
                        value.float_val = (double)iv;
                    } else if (vt == VALUE_FLOAT) {
                        value.type = VALUE_FLOAT;
                        value.float_val = fv;
                        value.int_val = (long long)fv;
                    } else {
                        value.type = VALUE_STRING;
                        value.str_val = captured_output;
                        value.owns_string = true;
                    }
                    assign_variable(capture_var, &value);
                    if (value.type != VALUE_STRING) {
                        free(captured_output);
                    }
                    captured_output = NULL;
                    free_value(&value);
                } else if (captured_output) {
                    free(captured_output);
                }

                free_argv(argv_heap);
            }
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "CLEAR", 5) == 0) {
            printf("\033[H\033[J");
            fflush(stdout);
            note_branch_progress(if_stack, &if_sp);
        }
        else {
            if (debug) fprintf(stderr, "Unrecognized command at %d: %s\n", script[pc].source_line, command);
        }
    }

    if (skipping_block && skip_progress_pending) {
        finalize_skipped_branch(if_stack, &if_sp, skip_context_index, skip_for_true_branch);
        skip_context_index = -1;
        skip_progress_pending = false;
        skip_consumed_first = false;
    }

    cleanup_variables();
    return 0;
}



