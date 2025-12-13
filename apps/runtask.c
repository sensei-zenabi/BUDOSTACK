/*
* runtask.c - A simplified script engine with PRINT, WAIT, GOTO, RUN, and CLEAR commands.
*
* Changes in this version:
* - CMD removed.
* - RUN executes an app by name from ./apps/, ./commands/, or ./utilities, and falls
*   back to PATH-resolved system commands if none of those match. Blocking is the
*   default; prepend BLOCKING or NONBLOCKING to control the run mode explicitly.
*   Arguments are passed as-is.
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

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <threads.h> // thrd_sleep
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>   // PATH_MAX
#include <math.h>
#include <sys/stat.h>

#define MAX_VARIABLES 128
#define MAX_LABELS 256
#define MAX_FUNCTIONS 64
#define MAX_FUNCTION_PARAMS 8
#define MAX_SCOPES 16
#define MAX_INCLUDE_DEPTH 16
#define MAX_INCLUDES_PER_FILE 32

typedef struct Value Value;

static char *xstrdup(const char *s);
static void set_initial_argv0(const char *argv0);
static void free_value(Value *value);
static bool copy_value(Value *dest, const Value *src);
static bool parse_expression(const char **cursor, Value *out, const char *terminators, int line, int debug);
static bool value_as_double(const Value *value, double *out);
static char *value_to_string(const Value *value);
static bool evaluate_expression_statement(const char *expr, int line, int debug);
static bool parse_boolean_literal(const char *expr, bool *out, const char **end_out);
static bool evaluate_truthy_expression(const char *expr, int line, int debug, bool *out);
static bool set_echo_enabled(bool enabled);
static void restore_terminal_settings(void);
static bool parse_label_definition(const char *line, char *out_name, size_t name_size);
static int resolve_task_path(const char *arg, const char *cwd, char *out, size_t out_size);
static int task_dirname(const char *path, char *out, size_t out_size);

typedef enum {
    VALUE_UNSET = 0,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_STRING,
    VALUE_ARRAY
} ValueType;

struct Value {
    ValueType type;
    long long int_val;
    double float_val;
    char *str_val;
    bool owns_string;
    Value *array_val;
    size_t array_len;
    bool owns_array;
};

typedef struct {
    char name[64];
    ValueType type;
    long long int_val;
    double float_val;
    char *str_val;
    Value *array_val;
    size_t array_len;
} Variable;

typedef struct {
    Variable vars[MAX_VARIABLES];
    size_t count;
} VariableScope;

static VariableScope scopes[MAX_SCOPES];
static size_t scope_depth = 0; // includes global scope
static VariableScope static_scopes[MAX_FUNCTIONS];
static int current_function_index = -1;

typedef struct {
    bool result;
    bool true_branch_done;
    bool else_encountered;
    bool else_branch_done;
    bool expects_end;
    int indent;
    int line_number;
} IfContext;

typedef struct {
    int for_line_pc;
    int body_start_pc;
    int indent;
    char condition[256];
    char step[128];
} ForContext;

typedef struct {
    int while_line_pc;
    int body_start_pc;
    int indent;
    char condition[256];
} WhileContext;

#define MAX_REF_INDICES 4

typedef struct {
    char name[64];
    size_t index_count;
    size_t indices[MAX_REF_INDICES];
} VariableRef;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Global flag to signal termination (set by SIGINT handler)
volatile sig_atomic_t stop = 0;

static char initial_argv0[PATH_MAX];
static FILE *log_file = NULL;
static char log_file_path[PATH_MAX];
static struct termios saved_termios;
static bool saved_termios_valid = false;
static bool echo_disabled = false;
static char task_workdir[PATH_MAX];

static void set_initial_argv0(const char *argv0) {
    if (!argv0) {
        initial_argv0[0] = '\0';
        return;
    }

    if (snprintf(initial_argv0, sizeof(initial_argv0), "%s", argv0) >= (int)sizeof(initial_argv0)) {
        initial_argv0[sizeof(initial_argv0) - 1] = '\0';
    }
}

static void stop_logging(void) {
    if (log_file) {
        if (fclose(log_file) != 0) {
            perror("_TOFILE: fclose");
        }
        log_file = NULL;
    }
    log_file_path[0] = '\0';
}

static bool ensure_saved_termios(void) {
    if (saved_termios_valid) {
        return true;
    }
    if (tcgetattr(STDIN_FILENO, &saved_termios) == -1) {
        perror("ECHO: tcgetattr");
        return false;
    }
    saved_termios_valid = true;
    return true;
}

static bool set_echo_enabled(bool enabled) {
    if (!ensure_saved_termios()) {
        return false;
    }

    struct termios current;
    if (tcgetattr(STDIN_FILENO, &current) == -1) {
        perror("ECHO: tcgetattr");
        return false;
    }

    if (enabled) {
        current.c_lflag |= ECHO;
    } else {
        current.c_lflag &= (tcflag_t)~ECHO;
    }

    if (tcsetattr(STDIN_FILENO, TCSANOW, &current) == -1) {
        perror("ECHO: tcsetattr");
        return false;
    }

    echo_disabled = !enabled;
    return true;
}

static void restore_terminal_settings(void) {
    if (!saved_termios_valid) {
        return;
    }
    if (tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) == -1) {
        perror("ECHO: tcsetattr restore");
    }
    echo_disabled = false;
}

static void cache_task_workdir(const char *dir) {
    if (!dir || !*dir) {
        task_workdir[0] = '\0';
        return;
    }

    if (snprintf(task_workdir, sizeof(task_workdir), "%s", dir) >= (int)sizeof(task_workdir)) {
        task_workdir[sizeof(task_workdir) - 1] = '\0';
    }
}

static void ensure_task_workdir(void) {
    if (task_workdir[0] == '\0') {
        return;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) && strcmp(cwd, task_workdir) == 0) {
        return;
    }

    if (chdir(task_workdir) != 0) {
        fprintf(stderr, "Warning: failed to restore task working directory '%s': %s\n", task_workdir, strerror(errno));
    }
}

static int start_logging(const char *path) {
    if (!path || !*path) {
        fprintf(stderr, "_TOFILE: missing file path for --start\n");
        return -1;
    }

    if (snprintf(log_file_path, sizeof(log_file_path), "%s", path) >= (int)sizeof(log_file_path)) {
        fprintf(stderr, "_TOFILE: log path too long\n");
        log_file_path[0] = '\0';
        return -1;
    }

    char parent_dir[PATH_MAX];
    if (snprintf(parent_dir, sizeof(parent_dir), "%s", path) >= (int)sizeof(parent_dir)) {
        fprintf(stderr, "_TOFILE: log path too long\n");
        log_file_path[0] = '\0';
        return -1;
    }

    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash != NULL && last_slash != parent_dir) {
        *last_slash = '\0';
    } else if (last_slash == parent_dir) {
        parent_dir[1] = '\0';
    }

    if (last_slash != NULL) {
        struct stat sb;
        if (stat(parent_dir, &sb) != 0) {
            perror("_TOFILE: parent directory");
            log_file_path[0] = '\0';
            return -1;
        }
        if (!S_ISDIR(sb.st_mode)) {
            fprintf(stderr, "_TOFILE: parent path is not a directory: %s\n", parent_dir);
            log_file_path[0] = '\0';
            return -1;
        }
        if (access(parent_dir, W_OK) != 0) {
            perror("_TOFILE: directory not writable");
            log_file_path[0] = '\0';
            return -1;
        }
    }

    stop_logging();

    log_file = fopen(path, "w");
    if (!log_file) {
        perror("_TOFILE: fopen");
        log_file_path[0] = '\0';
        return -1;
    }

    if (setvbuf(log_file, NULL, _IOLBF, 0) != 0) {
        perror("_TOFILE: setvbuf");
        stop_logging();
        return -1;
    }

    printf("_TOFILE: logging started to %s\n", log_file_path);
    return 0;
}

static void log_output(const char *data, size_t len) {
    if (!log_file || !data || len == 0) {
        return;
    }

    size_t written = fwrite(data, 1, len, log_file);
    if (written < len) {
        perror("_TOFILE: fwrite");
        stop_logging();
        return;
    }

    fflush(log_file);
}

static const char *get_base_dir(void) {
    static char cached[PATH_MAX];
    static int initialized = 0;

    if (!initialized) {
        initialized = 1;
        const char *env = getenv("BUDOSTACK_BASE");
        if (env && env[0] != '\0') {
            if (!realpath(env, cached)) {
                strncpy(cached, env, sizeof(cached) - 1);
                cached[sizeof(cached) - 1] = '\0';
            }
        } else {
            char resolved[PATH_MAX];
            const char *source = NULL;

            if (initial_argv0[0] != '\0' && realpath(initial_argv0, resolved)) {
                source = resolved;
            } else {
                ssize_t len = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
                if (len >= 0) {
                    resolved[len] = '\0';
                    source = resolved;
                } else if (initial_argv0[0] != '\0') {
                    strncpy(resolved, initial_argv0, sizeof(resolved) - 1);
                    resolved[sizeof(resolved) - 1] = '\0';
                    source = resolved;
                }
            }

            if (source) {
                strncpy(cached, source, sizeof(cached) - 1);
                cached[sizeof(cached) - 1] = '\0';
                char *last_slash = strrchr(cached, '/');
                if (last_slash) {
                    *last_slash = '\0';
                    char *component = strrchr(cached, '/');
                    if (component) {
                        const char *name = component + 1;
                        if (strcmp(name, "apps") == 0 || strcmp(name, "commands") == 0 ||
                            strcmp(name, "utilities") == 0 || strcmp(name, "games") == 0) {
                            if (component == cached) {
                                cached[0] = '/';
                                cached[1] = '\0';
                            } else {
                                *component = '\0';
                            }
                        }
                    }
                }
            } else {
                cached[0] = '\0';
            }

            if (cached[0] != '\0') {
                if (setenv("BUDOSTACK_BASE", cached, 1) != 0) {
                    perror("setenv BUDOSTACK_BASE");
                }
            }
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

static int task_dirname(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) {
        return -1;
    }

    const char *last_slash = strrchr(path, '/');
    size_t len;

    if (last_slash) {
        len = (size_t)(last_slash - path);
        if (len == 0) {
            len = 1; // Root directory
        }
    } else {
        len = 1;
        path = ".";
    }

    if (len >= out_size) {
        return -1;
    }

    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int resolve_task_path(const char *arg, const char *cwd, char *out, size_t out_size) {
    if (!arg || !cwd || !out || out_size == 0) {
        return -1;
    }

    if (arg[0] == '/') {
        return realpath(arg, out) ? 0 : -1;
    }

    char candidate[PATH_MAX];

    if (snprintf(candidate, sizeof(candidate), "%s/%s", cwd, arg) < (int)sizeof(candidate)) {
        if (realpath(candidate, out)) {
            return 0;
        }
    }

    if (strchr(arg, '/') || arg[0] == '.') {
        return -1;
    }

    if (snprintf(candidate, sizeof(candidate), "tasks/%s", arg) >= (int)sizeof(candidate)) {
        return -1;
    }

    char built[PATH_MAX];
    if (build_from_base(candidate, built, sizeof(built)) == 0 && realpath(built, out)) {
        return 0;
    }

    return -1;
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

static int query_cursor_position(long *out_row, long *out_col) {
    if (!out_row || !out_col) {
        return -1;
    }

    struct termios original;
    if (tcgetattr(STDIN_FILENO, &original) == -1) {
        perror("_GETPOS: tcgetattr");
        return -1;
    }

    struct termios raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("_GETPOS: tcsetattr");
        return -1;
    }

    int saved_errno = 0;
    int rc = -1;

    const char query[] = "\033[6n";
    size_t offset = 0;
    while (offset < sizeof(query) - 1) {
        ssize_t written = write(STDOUT_FILENO, query + offset, (sizeof(query) - 1) - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_GETPOS: write");
            goto restore;
        }
        offset += (size_t)written;
    }

    if (fflush(stdout) == EOF) {
        perror("_GETPOS: fflush");
        goto restore;
    }

    char response[64];
    size_t index = 0;
    while (index < sizeof(response) - 1) {
        char ch;
        ssize_t result = read(STDIN_FILENO, &ch, 1);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("_GETPOS: read");
            goto restore;
        }
        if (result == 0) {
            fprintf(stderr, "_GETPOS: unexpected EOF while reading cursor position\n");
            goto restore;
        }
        response[index++] = ch;
        if (ch == 'R') {
            break;
        }
    }

    if (index == sizeof(response) - 1 && response[index - 1] != 'R') {
        fprintf(stderr, "_GETPOS: cursor response too long\n");
        goto restore;
    }

    response[index] = '\0';

    if (index < 3 || response[0] != '\033' || response[1] != '[') {
        fprintf(stderr, "_GETPOS: invalid cursor response '%s'\n", response);
        goto restore;
    }

    char *endptr = NULL;
    errno = 0;
    long row = strtol(response + 2, &endptr, 10);
    if (errno != 0 || endptr == response + 2 || *endptr != ';') {
        fprintf(stderr, "_GETPOS: failed to parse row from response '%s'\n", response);
        goto restore;
    }

    const char *col_start = endptr + 1;
    errno = 0;
    long col = strtol(col_start, &endptr, 10);
    if (errno != 0 || endptr == col_start || *endptr != 'R') {
        fprintf(stderr, "_GETPOS: failed to parse column from response '%s'\n", response);
        goto restore;
    }

    if (row <= 0 || col <= 0) {
        fprintf(stderr, "_GETPOS: invalid row (%ld) or column (%ld)\n", row, col);
        goto restore;
    }

    *out_row = row;
    *out_col = col;
    rc = 0;

restore:
    saved_errno = errno;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &original) == -1) {
        perror("_GETPOS: tcsetattr restore");
        rc = -1;
    }
    errno = saved_errno;
    return rc;
}

static bool read_keypress_sequence(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return false;
    }

    buffer[0] = '\0';

    struct termios original;
    if (tcgetattr(STDIN_FILENO, &original) == -1) {
        perror("INPUT: tcgetattr");
        return false;
    }

    struct termios raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("INPUT: tcsetattr");
        return false;
    }

    bool success = false;
    size_t index = 0;

    while (index + 1 < size) {
        char ch;
        ssize_t result = read(STDIN_FILENO, &ch, 1);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("INPUT: read");
            goto restore;
        }
        if (result == 0) {
            goto restore;
        }
        buffer[index++] = ch;
        success = true;
        break;
    }

    if (success) {
        int pending = 0;
        if (ioctl(STDIN_FILENO, FIONREAD, &pending) == -1) {
            pending = 0;
        }
        while (pending > 0 && index + 1 < size) {
            char ch;
            ssize_t result = read(STDIN_FILENO, &ch, 1);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("INPUT: read");
                break;
            }
            if (result == 0) {
                break;
            }
            buffer[index++] = ch;
            pending--;
            if (pending == 0) {
                if (ioctl(STDIN_FILENO, FIONREAD, &pending) == -1) {
                    pending = 0;
                }
            }
        }
    }

restore:
    buffer[index] = '\0';
    int saved_errno = errno;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &original) == -1) {
        perror("INPUT: tcsetattr restore");
        success = false;
    }
    errno = saved_errno;
    return success;
}

static VariableScope *current_scope(void) {
    if (scope_depth == 0) {
        return NULL;
    }
    return &scopes[scope_depth - 1];
}

static VariableScope *current_static_scope(void) {
    if (current_function_index < 0 || current_function_index >= (int)(sizeof(static_scopes) / sizeof(static_scopes[0]))) {
        return NULL;
    }
    return &static_scopes[current_function_index];
}

static Variable *find_variable_in_scope(VariableScope *scope, const char *name) {
    if (!scope || !name) {
        return NULL;
    }
    for (size_t i = 0; i < scope->count; ++i) {
        if (strcmp(scope->vars[i].name, name) == 0) {
            return &scope->vars[i];
        }
    }
    return NULL;
}

static void clear_scope(VariableScope *scope) {
    if (!scope) {
        return;
    }
    for (size_t i = 0; i < scope->count; ++i) {
        if (scope->vars[i].type == VALUE_STRING && scope->vars[i].str_val) {
            free(scope->vars[i].str_val);
            scope->vars[i].str_val = NULL;
        }
        if (scope->vars[i].type == VALUE_ARRAY && scope->vars[i].array_val) {
            for (size_t j = 0; j < scope->vars[i].array_len; ++j) {
                free_value(&scope->vars[i].array_val[j]);
            }
            free(scope->vars[i].array_val);
            scope->vars[i].array_val = NULL;
            scope->vars[i].array_len = 0;
        }
        scope->vars[i].type = VALUE_UNSET;
    }
    scope->count = 0;
}

static void init_static_scopes(void) {
    for (size_t i = 0; i < MAX_FUNCTIONS; ++i) {
        clear_scope(&static_scopes[i]);
    }
}

static void init_scopes(void) {
    for (size_t i = 0; i < MAX_SCOPES; ++i) {
        scopes[i].count = 0;
    }
    scope_depth = 1; // global scope
}

static bool push_scope(void) {
    if (scope_depth >= MAX_SCOPES) {
        fprintf(stderr, "Variable scope limit reached (%d)\n", MAX_SCOPES);
        return false;
    }
    scopes[scope_depth].count = 0;
    scope_depth++;
    return true;
}

static void pop_scope(void) {
    if (scope_depth == 0) {
        return;
    }
    VariableScope *scope = &scopes[scope_depth - 1];
    clear_scope(scope);
    scope_depth--;
}

static Variable *find_variable(const char *name, bool create) {
    if (!name || !*name) {
        return NULL;
    }

    VariableScope *scope = current_scope();
    if (!scope) {
        return NULL;
    }

    bool in_function_scope = scope_depth > 1;
    Variable *found = find_variable_in_scope(scope, name);
    if (found) {
        return found;
    }

    Variable *static_var = find_variable_in_scope(current_static_scope(), name);
    if (static_var) {
        return static_var;
    }

    if (!in_function_scope || !create) {
        for (size_t depth = scope_depth; depth > 1; --depth) {
            VariableScope *parent = &scopes[depth - 2];
            found = find_variable_in_scope(parent, name);
            if (found) {
                return found;
            }
        }

        if (scope_depth > 0) {
            found = find_variable_in_scope(&scopes[0], name);
            if (found) {
                return found;
            }
        }
    }

    if (!create) {
        return NULL;
    }

    if (scope->count >= MAX_VARIABLES) {
        fprintf(stderr, "Variable limit reached in scope (%d)\n", MAX_VARIABLES);
        return NULL;
    }

    Variable *var = &scope->vars[scope->count++];
    memset(var, 0, sizeof(*var));
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->name[sizeof(var->name) - 1] = '\0';
    var->type = VALUE_UNSET;
    var->array_val = NULL;
    var->array_len = 0;
    return var;
}

static Variable *find_static_variable(const char *name, bool create) {
    if (!name || !*name) {
        return NULL;
    }

    VariableScope *scope = current_static_scope();
    if (!scope) {
        return NULL;
    }

    Variable *existing = find_variable_in_scope(scope, name);
    if (existing || !create) {
        return existing;
    }

    if (scope->count >= MAX_VARIABLES) {
        fprintf(stderr, "Variable limit reached in static scope (%d)\n", MAX_VARIABLES);
        return NULL;
    }

    Variable *var = &scope->vars[scope->count++];
    memset(var, 0, sizeof(*var));
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->name[sizeof(var->name) - 1] = '\0';
    var->type = VALUE_UNSET;
    var->array_val = NULL;
    var->array_len = 0;
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
    if (var->type == VALUE_ARRAY && var->array_val) {
        for (size_t i = 0; i < var->array_len; ++i) {
            free_value(&var->array_val[i]);
        }
        free(var->array_val);
        var->array_val = NULL;
        var->array_len = 0;
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
    } else if (value->type == VALUE_ARRAY) {
        var->array_len = value->array_len;
        if (value->array_len > 0) {
            var->array_val = (Value *)calloc(value->array_len, sizeof(Value));
            if (!var->array_val) {
                perror("calloc");
                exit(EXIT_FAILURE);
            }
            for (size_t i = 0; i < value->array_len; ++i) {
                copy_value(&var->array_val[i], &value->array_val[i]);
            }
        }
        var->int_val = 0;
        var->float_val = 0.0;
    } else {
        var->int_val = 0;
        var->float_val = 0.0;
    }
}

static bool set_array_element(Variable *var, size_t index, const Value *value) {
    if (!var || !value) {
        return false;
    }

    if (var->type != VALUE_ARRAY) {
        Value tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.type = VALUE_ARRAY;
        tmp.array_len = 0;
        tmp.array_val = NULL;
        tmp.owns_array = true;
        assign_variable(var, &tmp);
        free_value(&tmp);
    }

    if (index >= var->array_len) {
        size_t new_len = index + 1;
        Value *resized = (Value *)realloc(var->array_val, new_len * sizeof(Value));
        if (!resized) {
            perror("realloc");
            return false;
        }
        for (size_t i = var->array_len; i < new_len; ++i) {
            memset(&resized[i], 0, sizeof(Value));
            resized[i].type = VALUE_UNSET;
        }
        var->array_val = resized;
        var->array_len = new_len;
    }

    free_value(&var->array_val[index]);
    copy_value(&var->array_val[index], value);
    return true;
}

static void free_value(Value *value) {
    if (!value) {
        return;
    }
    if (value->type == VALUE_STRING && value->owns_string && value->str_val) {
        free(value->str_val);
        value->str_val = NULL;
    }
    if (value->type == VALUE_ARRAY && value->owns_array && value->array_val) {
        for (size_t i = 0; i < value->array_len; ++i) {
            free_value(&value->array_val[i]);
        }
        free(value->array_val);
        value->array_val = NULL;
        value->array_len = 0;
    }
    value->type = VALUE_UNSET;
    value->owns_string = false;
    value->owns_array = false;
    value->int_val = 0;
    value->float_val = 0.0;
    value->array_val = NULL;
    value->array_len = 0;
}

static bool copy_value(Value *dest, const Value *src) {
    if (!dest || !src) {
        return false;
    }

    free_value(dest);

    dest->type = src->type;
    dest->owns_string = false;
    dest->owns_array = false;
    dest->str_val = NULL;
    dest->array_val = NULL;
    dest->array_len = 0;
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
    } else if (src->type == VALUE_ARRAY) {
        dest->array_len = src->array_len;
        if (src->array_len > 0) {
            dest->array_val = (Value *)calloc(src->array_len, sizeof(Value));
            if (!dest->array_val) {
                perror("calloc");
                exit(EXIT_FAILURE);
            }
            dest->owns_array = true;
            for (size_t i = 0; i < src->array_len; ++i) {
                copy_value(&dest->array_val[i], &src->array_val[i]);
            }
        }
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
    } else if (var->type == VALUE_ARRAY) {
        v.array_val = var->array_val;
        v.array_len = var->array_len;
        v.owns_array = false;
    } else {
        v.str_val = NULL;
        v.owns_string = false;
    }
    return v;
}

static const Value *walk_indices(const Value *root, const VariableRef *ref, int line, int debug) {
    const Value *current = root;
    for (size_t i = 0; i < ref->index_count; ++i) {
        size_t idx = ref->indices[i];
        if (!current || current->type != VALUE_ARRAY || idx >= current->array_len) {
            if (debug) {
                fprintf(stderr, "Line %d: array access out of range or not an array\n", line);
            }
            return NULL;
        }
        current = &current->array_val[idx];
    }
    return current;
}

static bool ensure_value_array(Value *value) {
    if (!value) {
        return false;
    }
    if (value->type != VALUE_ARRAY) {
        free_value(value);
        memset(value, 0, sizeof(*value));
        value->type = VALUE_ARRAY;
        value->array_len = 0;
        value->array_val = NULL;
        value->owns_array = true;
    }
    return true;
}

static bool ensure_value_array_capacity(Value *value, size_t index) {
    if (!ensure_value_array(value)) {
        return false;
    }
    if (index >= value->array_len) {
        size_t new_len = index + 1;
        Value *resized = (Value *)realloc(value->array_val, new_len * sizeof(Value));
        if (!resized) {
            perror("realloc");
            return false;
        }
        for (size_t i = value->array_len; i < new_len; ++i) {
            memset(&resized[i], 0, sizeof(Value));
            resized[i].type = VALUE_UNSET;
        }
        value->array_val = resized;
        value->array_len = new_len;
    }
    return true;
}

static bool set_value_at_path(Value *value, const size_t *indices, size_t depth, const Value *source) {
    if (depth == 0) {
        free_value(value);
        copy_value(value, source);
        return true;
    }

    if (!ensure_value_array_capacity(value, indices[0])) {
        return false;
    }

    return set_value_at_path(&value->array_val[indices[0]], indices + 1, depth - 1, source);
}

static bool set_variable_from_ref(Variable *var, const VariableRef *ref, const Value *value) {
    if (!var || !ref || !value) {
        return false;
    }

    if (ref->index_count == 0) {
        assign_variable(var, value);
        return true;
    }

    if (var->type != VALUE_ARRAY) {
        Value tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.type = VALUE_ARRAY;
        tmp.array_len = 0;
        tmp.array_val = NULL;
        tmp.owns_array = true;
        assign_variable(var, &tmp);
        free_value(&tmp);
    }

    if (!set_array_element(var, ref->indices[0], &(Value){ .type = VALUE_UNSET })) {
        return false;
    }

    if (ref->index_count == 1) {
        return set_array_element(var, ref->indices[0], value);
    }

    Value *slot = &var->array_val[ref->indices[0]];
    return set_value_at_path(slot, &ref->indices[1], ref->index_count - 1, value);
}

static bool resolve_variable_reference(const VariableRef *ref, Value *out, int line, int debug) {
    if (!ref || !out) {
        return false;
    }

    Variable *var = find_variable(ref->name, false);
    if (!var) {
        memset(out, 0, sizeof(*out));
        out->type = VALUE_UNSET;
        return false;
    }

    Value root = variable_to_value(var);
    const Value *target = walk_indices(&root, ref, line, debug);
    if (!target) {
        memset(out, 0, sizeof(*out));
        out->type = VALUE_UNSET;
        return false;
    }

    copy_value(out, target);
    return true;
}

static void cleanup_variables(void) {
    while (scope_depth > 0) {
        pop_scope();
    }
    init_scopes();
    init_static_scopes();
    current_function_index = -1;
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
    while (*s && !isspace((unsigned char)*s)) {
        if (is_token_delim(*s, delims)) {
            if (!(len == 0 && (*s == '-' || *s == '+') && s[1] && (isdigit((unsigned char)s[1]) || s[1] == '.' || s[1] == '$'))) {
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

static bool convert_value_to_index(const Value *value, size_t *index_out, int line, int debug) {
    if (!value || !index_out) {
        return false;
    }

    double idx_num = 0.0;
    if (!value_as_double(value, &idx_num)) {
        if (debug) {
            fprintf(stderr, "Line %d: array index must be numeric\n", line);
        }
        return false;
    }

    if (idx_num < 0.0 || fabs(idx_num - floor(idx_num)) > 1e-9) {
        if (debug) {
            fprintf(stderr, "Line %d: array index must be a non-negative integer\n", line);
        }
        return false;
    }

    *index_out = (size_t)idx_num;
    return true;
}

static bool evaluate_index_expression(const char *expr, size_t *index_out, int line, int debug) {
    if (!expr || !index_out) {
        return false;
    }

    const char *cursor = expr;
    Value idx_value;
    if (!parse_expression(&cursor, &idx_value, NULL, line, debug)) {
        return false;
    }

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '\0') {
        if (debug) {
            fprintf(stderr, "Line %d: invalid array index expression\n", line);
        }
        free_value(&idx_value);
        return false;
    }

    size_t idx = 0;
    bool ok = convert_value_to_index(&idx_value, &idx, line, debug);
    free_value(&idx_value);
    if (!ok) {
        return false;
    }

    *index_out = idx;
    return true;
}

static bool parse_variable_reference_token(const char *token, VariableRef *ref, int line, int debug) {
    if (!token || !ref || token[0] != '$') {
        return false;
    }

    memset(ref, 0, sizeof(*ref));
    token++;
    size_t name_len = 0;
    while (*token && *token != '[') {
        if (!isalnum((unsigned char)*token) && *token != '_') {
            return false;
        }
        if (name_len + 1 >= sizeof(ref->name)) {
            return false;
        }
        ref->name[name_len++] = *token++;
    }
    ref->name[name_len] = '\0';
    if (name_len == 0) {
        return false;
    }

    while (*token == '[') {
        if (ref->index_count >= MAX_REF_INDICES) {
            if (debug) {
                fprintf(stderr, "Line %d: too many array dimensions (max %d)\n", line, MAX_REF_INDICES);
            }
            return false;
        }

        token++;
        const char *end = strchr(token, ']');
        if (!end) {
            return false;
        }

        size_t expr_len = (size_t)(end - token);
        char *expr = (char *)malloc(expr_len + 1);
        if (!expr) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        memcpy(expr, token, expr_len);
        expr[expr_len] = '\0';

        size_t idx = 0;
        bool ok = evaluate_index_expression(expr, &idx, line, debug);
        free(expr);
        if (!ok) {
            return false;
        }

        ref->indices[ref->index_count++] = idx;
        token = end + 1;
    }

    if (*token != '\0') {
        return false;
    }

    return true;
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

static bool parse_value_from_string(const char *text, Value *out, int line, int debug) {
    if (!text || !out) {
        return false;
    }

    const char *cursor = text;
    if (!parse_expression(&cursor, out, "", line, debug)) {
        return false;
    }

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor != '\0') {
        free_value(out);
        return false;
    }

    return true;
}

static bool parse_array_literal(const char **cursor, Value *out, int line, int debug) {
    if (!cursor || !out || !*cursor || **cursor != '{') {
        return false;
    }

    const char *s = *cursor;
    s++; // skip '{'

    size_t cap = 4;
    size_t len = 0;
    Value *elements = NULL;
    if (cap > 0) {
        elements = (Value *)calloc(cap, sizeof(Value));
        if (!elements) {
            perror("calloc");
            exit(EXIT_FAILURE);
        }
    }

    while (1) {
        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == '}') {
            s++;
            break;
        }

        Value element;
        if (!parse_expression(&s, &element, ",}", line, debug)) {
            if (debug) {
                fprintf(stderr, "Line %d: invalid array element\n", line);
            }
            if (elements) {
                for (size_t i = 0; i < len; ++i) {
                    free_value(&elements[i]);
                }
                free(elements);
            }
            return false;
        }

        if (len >= cap) {
            cap = (cap == 0) ? 4 : cap * 2;
            Value *tmp = (Value *)realloc(elements, cap * sizeof(Value));
            if (!tmp) {
                perror("realloc");
                for (size_t i = 0; i < len; ++i) {
                    free_value(&elements[i]);
                }
                free(elements);
                exit(EXIT_FAILURE);
            }
            elements = tmp;
        }
        copy_value(&elements[len], &element);
        free_value(&element);
        len++;

        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == ',') {
            s++;
            continue;
        }
        if (*s == '}') {
            s++;
            break;
        }
        if (debug) {
            fprintf(stderr, "Line %d: expected ',' or '}' in array literal\n", line);
        }
        for (size_t i = 0; i < len; ++i) {
            free_value(&elements[i]);
        }
        free(elements);
        return false;
    }

    Value result;
    memset(&result, 0, sizeof(result));
    result.type = VALUE_ARRAY;
    result.array_val = elements;
    result.array_len = len;
    result.owns_array = true;
    *out = result;
    *cursor = s;
    return true;
}

static bool parse_value_token(const char **p, Value *out, const char *delims, int line, int debug) {
    if (!p || !out) {
        return false;
    }

    while (isspace((unsigned char)**p)) {
        (*p)++;
    }

    if (**p == '{') {
        return parse_array_literal(p, out, line, debug);
    }

    const char *s = *p;
    if (strncmp(s, "LEN(", 4) == 0) {
        s += 4;
        Value target;
        if (!parse_expression(&s, &target, ")", line, debug)) {
            if (debug) {
                fprintf(stderr, "Line %d: invalid LEN() argument\n", line);
            }
            return false;
        }
        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s != ')') {
            free_value(&target);
            if (debug) {
                fprintf(stderr, "Line %d: expected ')' to close LEN()\n", line);
            }
            return false;
        }
        Value result;
        memset(&result, 0, sizeof(result));
        result.type = VALUE_INT;
        if (target.type == VALUE_ARRAY) {
            result.int_val = (long long)target.array_len;
            result.float_val = (double)target.array_len;
        } else if (target.type == VALUE_STRING && target.str_val) {
            result.int_val = (long long)strlen(target.str_val);
            result.float_val = (double)result.int_val;
        } else {
            char *tmp = value_to_string(&target);
            result.int_val = (long long)strlen(tmp);
            result.float_val = (double)result.int_val;
            free(tmp);
        }
        free_value(&target);
        *out = result;
        *p = s + 1;
        return true;
    }

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
        VariableRef ref;
        if (!parse_variable_reference_token(token, &ref, line, debug)) {
            if (debug) {
                fprintf(stderr, "Line %d: invalid variable name '%s'\n", line, token);
            }
            free(token);
            return false;
        }
        free(token);

        if (!resolve_variable_reference(&ref, &result, line, debug)) {
            result.type = VALUE_UNSET;
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
    if (value->type == VALUE_ARRAY) {
        size_t cap = 16;
        char *buf = (char *)malloc(cap);
        if (!buf) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        size_t len = 0;
        buf[len++] = '{';
        for (size_t i = 0; i < value->array_len; ++i) {
            char *elem = value_to_string(&value->array_val[i]);
            size_t elem_len = strlen(elem);
            size_t needed = len + (i > 0 ? 2 : 0) + elem_len + 2;
            if (needed > cap) {
                while (needed > cap) {
                    cap *= 2;
                }
                char *tmp = (char *)realloc(buf, cap);
                if (!tmp) {
                    perror("realloc");
                    free(buf);
                    free(elem);
                    exit(EXIT_FAILURE);
                }
                buf = tmp;
            }
            if (i > 0) {
                buf[len++] = ',';
                buf[len++] = ' ';
            }
            memcpy(buf + len, elem, elem_len);
            len += elem_len;
            free(elem);
        }
        if (len + 1 >= cap) {
            char *tmp = (char *)realloc(buf, cap + 1);
            if (!tmp) {
                perror("realloc");
                free(buf);
                exit(EXIT_FAILURE);
            }
            buf = tmp;
        }
        buf[len++] = '}';
        buf[len] = '\0';
        return buf;
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

static bool value_negate(Value *value) {
    if (!value) {
        return false;
    }

    if (value->type == VALUE_INT) {
        value->int_val = -value->int_val;
        value->float_val = (double)value->int_val;
        return true;
    }

    if (value->type == VALUE_FLOAT) {
        value->float_val = -value->float_val;
        value->int_val = (long long)value->float_val;
        return true;
    }

    if (value->type == VALUE_STRING && value->str_val) {
        long long iv = 0;
        double fv = 0.0;
        ValueType vt = detect_numeric_type(value->str_val, &iv, &fv);
        if (vt == VALUE_INT) {
            if (value->owns_string) {
                free(value->str_val);
            }
            value->owns_string = false;
            value->str_val = NULL;
            value->type = VALUE_INT;
            value->int_val = -iv;
            value->float_val = (double)value->int_val;
            return true;
        }
        if (vt == VALUE_FLOAT) {
            if (value->owns_string) {
                free(value->str_val);
            }
            value->owns_string = false;
            value->str_val = NULL;
            value->type = VALUE_FLOAT;
            value->float_val = -fv;
            value->int_val = (long long)value->float_val;
            return true;
        }
        return false;
    }

    if (value->type == VALUE_UNSET) {
        value->type = VALUE_INT;
        value->int_val = 0;
        value->float_val = 0.0;
        value->str_val = NULL;
        value->owns_string = false;
        return true;
    }

    return false;
}

static bool parse_expression(const char **cursor, Value *out, const char *terminators, int line, int debug) {
    if (!cursor || !out) {
        return false;
    }

    Value accumulator;
    memset(&accumulator, 0, sizeof(accumulator));
    bool have_term = false;
    char pending_op = '+';

    const char *delims = "+-";
    char delim_buf[64];
    if (terminators && *terminators) {
        size_t term_len = strlen(terminators);
        if (term_len > sizeof(delim_buf) - 3) {
            term_len = sizeof(delim_buf) - 3;
        }
        delim_buf[0] = '+';
        delim_buf[1] = '-';
        memcpy(&delim_buf[2], terminators, term_len);
        delim_buf[term_len + 2] = '\0';
        delims = delim_buf;
    }

    while (1) {
        while (isspace((unsigned char)**cursor)) {
            (*cursor)++;
        }

        char current_op = have_term ? pending_op : '+';
        if (!have_term && (**cursor == '+' || **cursor == '-')) {
            current_op = **cursor;
            (*cursor)++;
            while (isspace((unsigned char)**cursor)) {
                (*cursor)++;
            }
        }

        Value term;
        if (!parse_value_token(cursor, &term, delims, line, debug)) {
            free_value(&accumulator);
            return false;
        }

        if (current_op == '-') {
            if (!value_negate(&term)) {
                if (debug) {
                    fprintf(stderr, "Line %d: unable to apply '-' to value\n", line);
                }
                free_value(&term);
                free_value(&accumulator);
                return false;
            }
        }

        if (!value_add_inplace(&accumulator, &term)) {
            free_value(&term);
            free_value(&accumulator);
            return false;
        }
        free_value(&term);
        have_term = true;
        pending_op = '+';

        while (isspace((unsigned char)**cursor)) {
            (*cursor)++;
        }
        if (**cursor == '+' || **cursor == '-') {
            pending_op = **cursor;
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

static bool match_keyword(const char *cursor, const char *keyword, const char **end_out) {
    if (!cursor || !keyword) {
        return false;
    }
    size_t len = strlen(keyword);
    for (size_t i = 0; i < len; ++i) {
        if (cursor[i] == '\0') {
            return false;
        }
        if (tolower((unsigned char)cursor[i]) != tolower((unsigned char)keyword[i])) {
            return false;
        }
    }
    char next = cursor[len];
    if (next != '\0' && !isspace((unsigned char)next)) {
        return false;
    }
    if (end_out) {
        *end_out = cursor + len;
    }
    return true;
}

static bool parse_boolean_literal(const char *expr, bool *out, const char **end_out) {
    if (!expr || !out) {
        return false;
    }

    const char *cursor = expr;
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }

    errno = 0;
    char *endptr = NULL;
    long value = strtol(cursor, &endptr, 10);
    if (cursor == endptr || errno == ERANGE) {
        return false;
    }

    const char *rest = endptr;
    while (isspace((unsigned char)*rest)) {
        rest++;
    }

    if (*rest != '\0') {
        return false;
    }

    *out = (value != 0);
    if (end_out) {
        *end_out = rest;
    }
    return true;
}

static bool evaluate_truthy_expression(const char *expr, int line, int debug, bool *out) {
    if (!expr || !out) {
        return false;
    }

    const char *cursor = expr;
    Value value;
    if (!parse_expression(&cursor, &value, NULL, line, debug)) {
        return false;
    }

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor != '\0') {
        free_value(&value);
        return false;
    }

    bool truthy = false;
    switch (value.type) {
        case VALUE_INT:
            truthy = (value.int_val != 0);
            break;
        case VALUE_FLOAT:
            truthy = (value.float_val != 0.0);
            break;
        case VALUE_STRING:
            truthy = value.str_val && value.str_val[0] != '\0';
            break;
        case VALUE_ARRAY:
            truthy = value.array_len > 0;
            break;
        case VALUE_UNSET:
        default:
            truthy = false;
            break;
    }

    free_value(&value);
    *out = truthy;
    return true;
}

static bool parse_comparison_condition(const char **cursor, bool *out, int line, int debug) {
    if (!cursor || !out) {
        return false;
    }

    Value lhs;
    if (!parse_expression(cursor, &lhs, "<>!=", line, debug)) {
        return false;
    }

    while (isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }

    char op[3] = { 0 };
    if ((*cursor)[0] == '=' && (*cursor)[1] == '=') {
        op[0] = '=';
        op[1] = '=';
        (*cursor) += 2;
    } else if ((*cursor)[0] == '!' && (*cursor)[1] == '=') {
        op[0] = '!';
        op[1] = '=';
        (*cursor) += 2;
    } else if ((*cursor)[0] == '>' && (*cursor)[1] == '=') {
        op[0] = '>';
        op[1] = '=';
        (*cursor) += 2;
    } else if ((*cursor)[0] == '<' && (*cursor)[1] == '=') {
        op[0] = '<';
        op[1] = '=';
        (*cursor) += 2;
    } else if ((*cursor)[0] == '>') {
        op[0] = '>';
        (*cursor) += 1;
    } else if ((*cursor)[0] == '<') {
        op[0] = '<';
        (*cursor) += 1;
    } else {
        if (debug) {
            fprintf(stderr, "IF: invalid or missing operator at %d\n", line);
        }
        free_value(&lhs);
        return false;
    }

    Value rhs;
    if (!parse_expression(cursor, &rhs, NULL, line, debug)) {
        free_value(&lhs);
        return false;
    }

    bool cond_result = false;
    if (!evaluate_comparison(&lhs, &rhs, op, &cond_result, line, debug)) {
        cond_result = false;
    }

    free_value(&lhs);
    free_value(&rhs);
    *out = cond_result;
    return true;
}

static bool parse_conjunction_condition(const char **cursor, bool *out, int line, int debug) {
    if (!cursor || !out) {
        return false;
    }

    bool result = false;
    if (!parse_comparison_condition(cursor, &result, line, debug)) {
        return false;
    }

    while (1) {
        const char *p = *cursor;
        while (isspace((unsigned char)*p)) {
            p++;
        }

        const char *after_keyword = NULL;
        if (!match_keyword(p, "AND", &after_keyword)) {
            *cursor = p;
            break;
        }

        *cursor = after_keyword;
        bool rhs = false;
        if (!parse_comparison_condition(cursor, &rhs, line, debug)) {
            return false;
        }
        result = result && rhs;
    }

    *out = result;
    return true;
}

static bool parse_condition(const char **cursor, bool *out, int line, int debug) {
    if (!cursor || !out) {
        return false;
    }

    bool result = false;
    if (!parse_conjunction_condition(cursor, &result, line, debug)) {
        return false;
    }

    while (1) {
        const char *p = *cursor;
        while (isspace((unsigned char)*p)) {
            p++;
        }

        const char *after_keyword = NULL;
        if (!match_keyword(p, "OR", &after_keyword)) {
            *cursor = p;
            break;
        }

        *cursor = after_keyword;
        bool rhs = false;
        if (!parse_conjunction_condition(cursor, &rhs, line, debug)) {
            return false;
        }
        result = result || rhs;
    }

    while (isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }

    *out = result;
    return true;
}

static void note_branch_progress(IfContext *stack, int *sp) {
    if (!stack || !sp || *sp <= 0) {
        return;
    }
    IfContext *ctx = &stack[*sp - 1];
    if (ctx->expects_end) {
        return;
    }
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
        if (!ctx->expects_end) {
            (*sp)--;
        }
    }
}

static void copy_trimmed_segment(const char *start, const char *end, char *dest, size_t size) {
    if (!start || !end || !dest || size == 0 || end < start) {
        return;
    }

    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    size_t len = (size_t)(end - start);
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dest, start, len);
    dest[len] = '\0';
}

static bool evaluate_expression_statement(const char *expr, int line, int debug) {
    if (!expr) {
        return false;
    }

    const char *cursor = expr;
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '\0') {
        return true;
    }

    Value value;
    if (!parse_expression(&cursor, &value, NULL, line, debug)) {
        return false;
    }
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '\0' && debug) {
        fprintf(stderr, "Expression: unexpected characters at %d\n", line);
    }

    free_value(&value);
    return true;
}

static bool process_assignment_statement(const char *statement, int line, int debug) {
    if (!statement) {
        return false;
    }

    const char *cursor = statement;
    char *var_token = NULL;
    bool quoted = false;
    if (!parse_token(&cursor, &var_token, &quoted, "=") || quoted) {
        if (debug) fprintf(stderr, "Assignment: expected variable at line %d\n", line);
        free(var_token);
        return false;
    }

    VariableRef ref;
    if (!parse_variable_reference_token(var_token, &ref, line, debug)) {
        if (debug) fprintf(stderr, "Assignment: invalid variable name at line %d\n", line);
        free(var_token);
        return false;
    }
    free(var_token);

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '=') {
        if (debug) fprintf(stderr, "Assignment: expected '=' at line %d\n", line);
        return false;
    }
    cursor++;
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }

    Value value;
    if (!parse_expression(&cursor, &value, NULL, line, debug)) {
        return false;
    }
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '\0' && debug) {
        fprintf(stderr, "Assignment: unexpected characters at %d\n", line);
    }

    Variable *var = find_variable(ref.name, true);
    if (var) {
        if (!set_variable_from_ref(var, &ref, &value) && debug) {
            fprintf(stderr, "Assignment: failed to set variable at line %d\n", line);
        }
    }
    free_value(&value);
    return true;
}

static bool apply_increment_step(const char *expr, int line, int debug) {
    if (!expr) {
        return false;
    }

    char trimmed[128];
    copy_trimmed_segment(expr, expr + strlen(expr), trimmed, sizeof(trimmed));

    if (strchr(trimmed, '=')) {
        if (process_assignment_statement(trimmed, line, debug)) {
            return true;
        }
        if (debug) {
            fprintf(stderr, "FOR: failed to evaluate step assignment at line %d\n", line);
        }
        return false;
    }

    const char *cursor = trimmed;
    if (*cursor == '$') {
        cursor++;
    }

    char name[64];
    size_t len = 0;
    bool too_long = false;
    while (isalnum((unsigned char)*cursor) || *cursor == '_') {
        if (len + 1 >= sizeof(name)) {
            too_long = true;
        } else {
            name[len++] = *cursor;
        }
        cursor++;
    }
    name[len] = '\0';

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }

    bool increment = false;
    if (strncmp(cursor, "++", 2) == 0) {
        increment = true;
        cursor += 2;
    } else if (strncmp(cursor, "--", 2) == 0) {
        increment = false;
        cursor += 2;
    } else {
        bool ok = evaluate_expression_statement(trimmed, line, debug);
        if (!ok && debug) {
            fprintf(stderr, "FOR: unsupported step at line %d\n", line);
        }
        return ok;
    }

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '\0') {
        if (debug) fprintf(stderr, "FOR: unexpected characters after step at line %d\n", line);
        return false;
    }

    if (len == 0 || too_long) {
        if (debug) fprintf(stderr, "FOR: invalid step variable at line %d\n", line);
        return false;
    }

    Variable *var = find_variable(name, true);
    if (!var) {
        return false;
    }

    if (var->type == VALUE_STRING && var->str_val) {
        free(var->str_val);
        var->str_val = NULL;
    }

    long long current = 0;
    if (var->type == VALUE_INT) {
        current = var->int_val;
    } else if (var->type == VALUE_FLOAT) {
        current = (long long)var->float_val;
    }

    if (increment) {
        current++;
    } else {
        current--;
    }

    var->type = VALUE_INT;
    var->int_val = current;
    var->float_val = (double)current;
    return true;
}

static bool evaluate_condition_string(const char *expr, int line, int debug, bool *out) {
    if (!expr || !out) {
        return false;
    }

    bool result = false;
    const char *cursor = expr;
    bool condition_parsed = false;

    if (parse_boolean_literal(expr, &result, &cursor)) {
        condition_parsed = true;
    } else if (parse_condition(&cursor, &result, line, debug)) {
        condition_parsed = true;
    } else if (evaluate_truthy_expression(expr, line, debug, &result)) {
        condition_parsed = true;
    }

    if (condition_parsed) {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor != '\0') {
            if (debug) fprintf(stderr, "Condition: unexpected trailing characters at line %d\n", line);
            return false;
        }
    } else {
        return false;
    }
    *out = result;
    return true;
}

static void print_help(void) {
    printf("\nRuntask Help\n");
    printf("============\n\n");
    printf("Commands:\n");
    printf("  SET $VAR = value\n");
    printf("    Store integers, floats, strings, or arrays in a variable. Arrays use\n");
    printf("    braces: {1, 2, 3} or {\"a\", \"b\"}. Access elements with\n");
    printf("    $VAR[index].\n");
    printf("  INPUT $VAR [-wait on|off]\n");
    printf("    Read input into $VAR. Default waits for Enter. OFF captures the first key\n");
    printf("    press.\n");
    printf("  IF (<lhs> op <rhs>):\n");
    printf("    Begin a block terminated by END. Chain with AND/OR. Use ELSE for an\n");
    printf("    alternate branch.\n");
    printf("  WHILE(<condition>):\n");
    printf("    Repeat a block terminated by END while the condition remains true.\n");
    printf("  FOR (init; cond; step)\n");
    printf("    Loop with inline init/condition/step terminated by END. Supports $VAR++/--\n");
    printf("    as well as assignment-style steps (e.g., $I=$I+2).\n");
    printf("  PRINT expr\n");
    printf("    Print literals and variables (use '+' to concatenate). Supports array\n");
    printf("    elements (e.g., PRINT $ARR[0]) and LEN($ARR).\n");
    printf("  FUNCTION name($A, $B):\n");
    printf("    Define a callable block. Body ends when indentation returns to the\n");
    printf("    function's column or the file ends.\n");
    printf("  EVAL name(args...) [TO $VAR]\n");
    printf("    Invoke a FUNCTION. Optionally store RETURN value into $VAR.\n");
    printf("  RETURN [value]\n");
    printf("    Exit the current FUNCTION with an optional return value.\n");
    printf("  WAIT milliseconds\n");
    printf("    Wait for <milliseconds>.\n");
    printf("  ECHO ON|OFF\n");
    printf("    Toggle terminal echo so key presses are hidden or shown.\n");
    printf("  GOTO label\n");
    printf("    Jump to the line marked with @label (literal or in $VAR).\n");
    printf("  RUN [BLOCKING|NONBLOCKING] <cmd [args...]>\n");
    printf("    Execute from ./apps, ./commands, or ./utilities; otherwise fall back to\n");
    printf("    PATH. Default is BLOCKING. If the command contains '/', it's executed as\n");
    printf("    given.\n");
    printf("    Append 'TO $VAR' to capture stdout into $VAR (blocking mode only).\n");
    printf("  CLEAR\n");
    printf("    Clear the screen.\n\n");
    printf("Usage:\n");
    printf("  ./runtask taskfile [-d]\n\n");
    printf("Notes:\n");
    printf("- Task files are loaded from 'tasks/' automatically (e.g., tasks/demo.task).\n");
    printf("- Place executables in ./apps, ./commands, or ./utilities and make them\n");
    printf("  executable.\n");
    printf("- External commands available in PATH are also accepted.\n\n");
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

static int brace_balance_delta(const char *s) {
    bool in_string = false;
    bool escape = false;
    int delta = 0;

    for (; *s; ++s) {
        if (escape) {
            escape = false;
            continue;
        }
        if (*s == '\\') {
            escape = true;
            continue;
        }
        if (*s == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (*s == '{') {
            delta++;
        } else if (*s == '}') {
            delta--;
        }
    }

    return delta;
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
    LINE_LABEL,
    LINE_FUNCTION
} LineType;

#define SCRIPT_TEXT_MAX 8192
#define SCRIPT_MAX_LINES 1024

typedef struct {
    int source_line;   // original file line number for diagnostics
    LineType type;
    int indent;        // leading whitespace count for block handling
    char text[SCRIPT_TEXT_MAX];
} ScriptLine;

typedef struct {
    char name[64];
    int index;        // index into script array
} Label;

typedef struct {
    char name[64];
    int definition_pc;   // index where FUNCTION line is located
    int start_pc;        // first executable line inside function
    int end_pc;          // first line after the function block
    int indent;          // indent level of the definition line
    int param_count;
    char params[MAX_FUNCTION_PARAMS][sizeof(((Variable *)0)->name)];
} FunctionDef;

typedef struct {
    int return_pc;
    int function_end_pc;
    bool has_return_target;
    char return_target[sizeof(((Variable *)0)->name)];
    bool has_return_value;
    Value return_value;
    int saved_if_sp;
    int saved_for_sp;
    int saved_while_sp;
    bool saved_skipping_block;
    int saved_skip_indent;
    int saved_skip_context_index;
    bool saved_skip_for_true_branch;
    bool saved_skip_progress_pending;
    bool saved_skip_consumed_first;
    int function_index;
    int previous_function_index;
} CallFrame;

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

static int find_function_index(const FunctionDef *functions, int function_count, const char *name) {
    if (!functions || !name) {
        return -1;
    }
    for (int i = 0; i < function_count; ++i) {
        if (equals_ignore_case(functions[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static bool parse_function_definition(const char *line, FunctionDef *out) {
    if (!line || !out) {
        return false;
    }
    const char *cursor = line;
    if (!match_keyword(cursor, "FUNCTION", &cursor)) {
        return false;
    }
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    const char *name_start = cursor;
    while (*cursor && (isalnum((unsigned char)*cursor) || *cursor == '_')) {
        cursor++;
    }
    size_t name_len = (size_t)(cursor - name_start);
    if (name_len == 0 || name_len >= sizeof(out->name)) {
        return false;
    }
    memcpy(out->name, name_start, name_len);
    out->name[name_len] = '\0';

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '(') {
        return false;
    }
    cursor++;

    out->param_count = 0;
    while (1) {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == ')') {
            cursor++;
            break;
        }
        if (out->param_count >= MAX_FUNCTION_PARAMS) {
            return false;
        }
        const char *param_start = cursor;
        while (*cursor && *cursor != ',' && *cursor != ')') {
            cursor++;
        }
        size_t param_len = (size_t)(cursor - param_start);
        if (param_len == 0 || param_len >= sizeof(out->params[0])) {
            return false;
        }
        char token[sizeof(out->params[0])];
        memcpy(token, param_start, param_len);
        token[param_len] = '\0';
        char name_buf[sizeof(out->params[0])];
        if (!parse_variable_name_token(token, name_buf, sizeof(name_buf))) {
            return false;
        }
        snprintf(out->params[out->param_count], sizeof(out->params[0]), "%s", name_buf);
        out->param_count++;

        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == ')') {
            cursor++;
            break;
        }
        return false;
    }

    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != ':') {
        return false;
    }
    cursor++;
    while (isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return *cursor == '\0';
}

static FunctionDef *find_function_by_definition(FunctionDef *functions, int function_count, int pc) {
    if (!functions) {
        return NULL;
    }
    for (int i = 0; i < function_count; ++i) {
        if (functions[i].definition_pc == pc) {
            return &functions[i];
        }
    }
    return NULL;
}

static bool record_script_line(const char *line, int indent, int source_line, ScriptLine *script, int script_cap, int *count, Label *labels, int *label_count, FunctionDef *functions, int *function_count) {
    if (!line || !script || !count || !labels || !label_count || !functions || !function_count) {
        return false;
    }

    if (*count >= script_cap) {
        fprintf(stderr, "Error: script too long (max %d lines)\n", script_cap);
        return false;
    }

    FunctionDef def_tmp;
    bool is_function = parse_function_definition(line, &def_tmp);
    if (is_function) {
        script[*count].source_line = source_line;
        script[*count].type = LINE_FUNCTION;
        script[*count].indent = indent;
        strncpy(script[*count].text, line, sizeof(script[*count].text) - 1);
        script[*count].text[sizeof(script[*count].text) - 1] = '\0';

        def_tmp.definition_pc = *count;
        def_tmp.start_pc = *count + 1;
        def_tmp.end_pc = -1;
        def_tmp.indent = indent;

        int existing = find_function_index(functions, *function_count, def_tmp.name);
        if (existing >= 0) {
            functions[existing] = def_tmp;
        } else if (*function_count < MAX_FUNCTIONS) {
            functions[*function_count] = def_tmp;
            (*function_count)++;
        } else {
            fprintf(stderr, "Error: too many functions (max %d)\n", MAX_FUNCTIONS);
        }

        (*count)++;
        return true;
    }

    if (*line == '@') {
        char label_name[64];
        if (!parse_label_definition(line, label_name, sizeof(label_name))) {
            fprintf(stderr, "Error: invalid label definition at line %d: %s\n", source_line, line);
            return false;
        }
        script[*count].source_line = source_line;
        script[*count].type = LINE_LABEL;
        script[*count].indent = indent;
        strncpy(script[*count].text, line, sizeof(script[*count].text) - 1);
        script[*count].text[sizeof(script[*count].text) - 1] = '\0';

        char normalized[64];
        normalize_label_name(label_name, normalized, sizeof(normalized));
        int existing = find_label_index(labels, *label_count, normalized);
        if (existing >= 0) {
            labels[existing].index = *count;
        } else {
            if (*label_count >= MAX_LABELS) {
                fprintf(stderr, "Error: too many labels (max %d)\n", MAX_LABELS);
            } else {
                snprintf(labels[*label_count].name, sizeof(labels[*label_count].name), "%s", normalized);
                labels[*label_count].index = *count;
                (*label_count)++;
            }
        }
        (*count)++;
        return true;
    }

    script[*count].source_line = source_line;
    script[*count].type = LINE_COMMAND;
    script[*count].indent = indent;
    strncpy(script[*count].text, line, sizeof(script[*count].text) - 1);
    script[*count].text[sizeof(script[*count].text) - 1] = '\0';
    (*count)++;
    return true;
}

static void apply_return_value(const CallFrame *frame) {
    if (!frame || !frame->has_return_target) {
        return;
    }
    Value tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (frame->has_return_value) {
        copy_value(&tmp, &frame->return_value);
    } else {
        tmp.type = VALUE_UNSET;
    }
    Variable *dest = find_variable(frame->return_target, true);
    if (dest) {
        assign_variable(dest, &tmp);
    }
    free_value(&tmp);
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

static bool load_task_file(const char *task_path, const char *task_dir, ScriptLine *script, int script_cap, int *script_count, Label *labels, int *label_count, FunctionDef *functions, int *function_count, int depth, int debug) {
    typedef struct {
        char text[SCRIPT_TEXT_MAX];
        int indent;
        int source_line;
    } PendingLine;

    typedef struct {
        char path[PATH_MAX];
        int source_line;
    } IncludeRequest;

    if (!task_path || !script || !script_count || !labels || !label_count || !functions || !function_count) {
        return false;
    }

    (void)debug;

    if (depth >= MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "Error: include nesting too deep at '%s'\n", task_path);
        return false;
    }

    FILE *fp = fopen(task_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Could not open task file '%s'\n", task_path);
        return false;
    }

    char dirbuf[PATH_MAX];
    const char *base_dir = task_dir;
    if (!base_dir || !*base_dir) {
        if (task_dirname(task_path, dirbuf, sizeof(dirbuf)) == 0) {
            base_dir = dirbuf;
        } else {
            base_dir = ".";
        }
    }

    PendingLine *pending_lines = (PendingLine *)calloc((size_t)script_cap, sizeof(PendingLine));
    if (!pending_lines) {
        perror("calloc");
        fclose(fp);
        return false;
    }

    IncludeRequest includes[MAX_INCLUDES_PER_FILE];
    int include_count = 0;
    int pending_count = 0;

    char linebuf[256];
    char combined[SCRIPT_TEXT_MAX];
    int brace_balance = 0;
    bool combining = false;
    int pending_indent = 0;
    int pending_source_line = 0;
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

        if (!combining) {
            snprintf(combined, sizeof(combined), "%s", line);
            brace_balance = brace_balance_delta(combined);
            combining = (brace_balance > 0);
            pending_indent = indent;
            pending_source_line = file_line;
            if (combining) {
                continue;
            }
        } else {
            size_t cur_len = strlen(combined);
            size_t add_len = strlen(line);
            if (cur_len + 1 + add_len >= sizeof(combined)) {
                fprintf(stderr, "Error: combined line too long near source line %d\n", pending_source_line);
                continue;
            }
            combined[cur_len] = ' ';
            memcpy(combined + cur_len + 1, line, add_len + 1);
            brace_balance += brace_balance_delta(line);
            if (brace_balance > 0) {
                continue;
            }
            combining = false;
            indent = pending_indent;
            line = combined;
        }

        int effective_line = pending_source_line ? pending_source_line : file_line;

        if (brace_balance < 0) {
            fprintf(stderr, "Error: unmatched closing brace at line %d\n", file_line);
            brace_balance = 0;
            combining = false;
            continue;
        }

        if (strncmp(line, "INCLUDE", 7) == 0 && (line[7] == '\0' || isspace((unsigned char)line[7]))) {
            const char *after = line + 7;
            while (isspace((unsigned char)*after)) {
                after++;
            }
            char *include_target = NULL;
            const char *cursor = after;
            if (!parse_string_literal(&cursor, &include_target)) {
                fprintf(stderr, "Error: invalid INCLUDE path at line %d\n", effective_line);
                continue;
            }
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0') {
                fprintf(stderr, "Error: unexpected characters after INCLUDE path at line %d\n", effective_line);
                free(include_target);
                continue;
            }

            char resolved[PATH_MAX];
            if (resolve_task_path(include_target, base_dir, resolved, sizeof(resolved)) != 0) {
                fprintf(stderr, "Error: could not resolve INCLUDE '%s' at line %d\n", include_target, effective_line);
                free(include_target);
                continue;
            }
            free(include_target);

            if (include_count >= MAX_INCLUDES_PER_FILE) {
                fprintf(stderr, "Error: too many INCLUDE directives in '%s' (max %d)\n", task_path, MAX_INCLUDES_PER_FILE);
                continue;
            }

            snprintf(includes[include_count].path, sizeof(includes[include_count].path), "%s", resolved);
            includes[include_count].source_line = effective_line;
            include_count++;
            continue;
        }

        if (pending_count >= script_cap) {
            fprintf(stderr, "Error: script too long (max %d lines)\n", script_cap);
            free(pending_lines);
            fclose(fp);
            return false;
        }

        pending_lines[pending_count].source_line = effective_line;
        pending_lines[pending_count].indent = indent;
        strncpy(pending_lines[pending_count].text, line, sizeof(pending_lines[pending_count].text) - 1);
        pending_lines[pending_count].text[sizeof(pending_lines[pending_count].text) - 1] = '\0';
        pending_count++;
    }

    fclose(fp);

    for (int i = 0; i < include_count; ++i) {
        char include_dir[PATH_MAX];
        const char *include_base = base_dir;
        if (task_dirname(includes[i].path, include_dir, sizeof(include_dir)) == 0) {
            include_base = include_dir;
        }
        if (!load_task_file(includes[i].path, include_base, script, script_cap, script_count, labels, label_count, functions, function_count, depth + 1, debug)) {
            free(pending_lines);
            return false;
        }
    }

    for (int i = 0; i < pending_count; ++i) {
        if (!record_script_line(pending_lines[i].text, pending_lines[i].indent, pending_lines[i].source_line, script, script_cap, script_count, labels, label_count, functions, function_count)) {
            free(pending_lines);
            return false;
        }
    }

    free(pending_lines);
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

typedef struct {
    const char *cursor;
} InlineMathParser;

static void inline_math_skip_ws(InlineMathParser *parser) {
    if (!parser) {
        return;
    }
    while (parser->cursor && isspace((unsigned char)*parser->cursor)) {
        parser->cursor++;
    }
}

static bool inline_math_parse_expression(InlineMathParser *parser, double *out);

static bool inline_math_parse_number(InlineMathParser *parser, double *out) {
    if (!parser || !out) {
        return false;
    }
    inline_math_skip_ws(parser);
    if (!parser->cursor) {
        return false;
    }
    errno = 0;
    char *endptr = NULL;
    double value = strtod(parser->cursor, &endptr);
    if (errno != 0 || endptr == parser->cursor) {
        return false;
    }
    parser->cursor = endptr;
    *out = value;
    return true;
}

static bool inline_math_parse_factor(InlineMathParser *parser, double *out) {
    if (!parser || !out) {
        return false;
    }
    inline_math_skip_ws(parser);
    if (!parser->cursor) {
        return false;
    }
    char ch = *parser->cursor;
    if (ch == '+') {
        parser->cursor++;
        return inline_math_parse_factor(parser, out);
    }
    if (ch == '-') {
        parser->cursor++;
        if (!inline_math_parse_factor(parser, out)) {
            return false;
        }
        *out = -*out;
        return true;
    }
    if (ch == '(') {
        parser->cursor++;
        if (!inline_math_parse_expression(parser, out)) {
            return false;
        }
        inline_math_skip_ws(parser);
        if (*parser->cursor != ')') {
            return false;
        }
        parser->cursor++;
        return true;
    }
    return inline_math_parse_number(parser, out);
}

static bool inline_math_parse_term(InlineMathParser *parser, double *out) {
    if (!inline_math_parse_factor(parser, out)) {
        return false;
    }
    while (1) {
        inline_math_skip_ws(parser);
        char op = *parser->cursor;
        if (op != '*' && op != '/') {
            break;
        }
        parser->cursor++;
        double rhs = 0.0;
        if (!inline_math_parse_factor(parser, &rhs)) {
            return false;
        }
        if (op == '*') {
            *out *= rhs;
        } else {
            if (rhs == 0.0) {
                return false;
            }
            *out /= rhs;
        }
    }
    return true;
}

static bool inline_math_parse_expression(InlineMathParser *parser, double *out) {
    if (!inline_math_parse_term(parser, out)) {
        return false;
    }
    while (1) {
        inline_math_skip_ws(parser);
        char op = *parser->cursor;
        if (op != '+' && op != '-') {
            break;
        }
        parser->cursor++;
        double rhs = 0.0;
        if (!inline_math_parse_term(parser, &rhs)) {
            return false;
        }
        if (op == '+') {
            *out += rhs;
        } else {
            *out -= rhs;
        }
    }
    return true;
}

static bool inline_math_evaluate(const char *text, double *out) {
    if (!text || !out) {
        return false;
    }
    InlineMathParser parser = { .cursor = text };
    if (!inline_math_parse_expression(&parser, out)) {
        return false;
    }
    inline_math_skip_ws(&parser);
    return *parser.cursor == '\0';
}

static bool looks_like_math_expression(const char *token) {
    if (!token || !*token) {
        return false;
    }
    bool has_digit = false;
    bool has_operator = false;
    for (const char *p = token; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (isdigit(ch)) {
            has_digit = true;
            continue;
        }
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
            has_operator = true;
            continue;
        }
        if (ch == '.' || ch == '(' || ch == ')' || isspace(ch)) {
            continue;
        }
        return false;
    }
    return has_digit && has_operator;
}

static char *try_evaluate_math_token(const char *token) {
    if (!looks_like_math_expression(token)) {
        return NULL;
    }
    double value = 0.0;
    if (!inline_math_evaluate(token, &value)) {
        return NULL;
    }
    double integral_part = 0.0;
    double fractional = modf(value, &integral_part);
    if (fabs(fractional) < 1e-9 && integral_part >= (double)LLONG_MIN && integral_part <= (double)LLONG_MAX) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%lld", (long long)integral_part);
        return xstrdup(buf);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", value);
    return xstrdup(buf);
}

static void append_chunk(char **buffer, size_t *length, size_t *capacity, const char *chunk, size_t chunk_len) {
    if (!chunk || chunk_len == 0) {
        return;
    }

    size_t needed = *length + chunk_len + 1;
    if (*capacity < needed) {
        size_t new_cap = (*capacity == 0) ? needed : *capacity;
        while (new_cap < needed) {
            if (new_cap > SIZE_MAX / 2) {
                new_cap = needed;
                break;
            }
            new_cap *= 2;
        }
        char *tmp = (char *)realloc(*buffer, new_cap);
        if (!tmp) {
            perror("realloc");
            free(*buffer);
            exit(EXIT_FAILURE);
        }
        *buffer = tmp;
        *capacity = new_cap;
    }
    memcpy(*buffer + *length, chunk, chunk_len);
    *length += chunk_len;
    (*buffer)[*length] = '\0';
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
        if (!strchr(token, '$')) {
            continue;
        }

        char *result = NULL;
        size_t res_len = 0;
        size_t res_cap = 0;
        const char *cursor = token;
        bool substituted = false;

        while (*cursor) {
            const char *dollar = strchr(cursor, '$');
            if (!dollar) {
                append_chunk(&result, &res_len, &res_cap, cursor, strlen(cursor));
                break;
            }

            append_chunk(&result, &res_len, &res_cap, cursor, (size_t)(dollar - cursor));

            const char *scan = dollar + 1;
            if (*scan == '\0') {
                append_chunk(&result, &res_len, &res_cap, "$", 1);
                cursor = scan;
                continue;
            }

            if (!isalnum((unsigned char)*scan) && *scan != '_') {
                append_chunk(&result, &res_len, &res_cap, "$", 1);
                cursor = scan;
                continue;
            }

            while (isalnum((unsigned char)*scan) || *scan == '_') {
                scan++;
            }

            while (*scan == '[') {
                const char *closing = strchr(scan + 1, ']');
                if (!closing) {
                    if (debug) {
                        fprintf(stderr, "RUN: missing closing ']' in '%s' at line %d\n", token, line);
                    }
                    append_chunk(&result, &res_len, &res_cap, "$", 1);
                    cursor = scan + 1;
                    continue;
                }
                scan = closing + 1;
            }

            size_t ref_len = (size_t)(scan - dollar);
            char *ref_token = (char *)malloc(ref_len + 1);
            if (!ref_token) {
                perror("malloc");
                free(result);
                exit(EXIT_FAILURE);
            }
            memcpy(ref_token, dollar, ref_len);
            ref_token[ref_len] = '\0';

            VariableRef ref;
            if (!parse_variable_reference_token(ref_token, &ref, line, debug)) {
                if (debug) {
                    fprintf(stderr, "RUN: invalid variable reference '%s' at line %d\n", ref_token, line);
                }
                free(ref_token);
                append_chunk(&result, &res_len, &res_cap, "$", 1);
                cursor = dollar + 1;
                continue;
            }
            free(ref_token);

            Value value;
            memset(&value, 0, sizeof(value));
            if (!resolve_variable_reference(&ref, &value, line, debug)) {
                if (debug) {
                    fprintf(stderr, "RUN: undefined variable '%s' at line %d\n", ref.name, line);
                }
                value.type = VALUE_UNSET;
            }

            char *replacement = value_to_string(&value);
            if (!replacement) {
                replacement = xstrdup("");
            }
            append_chunk(&result, &res_len, &res_cap, replacement, strlen(replacement));
            free(replacement);
            substituted = true;
            cursor = scan;
        }

        if (substituted) {
            if (!result) {
                result = xstrdup("");
            }
            free(argv[i]);
            argv[i] = result;
            char *evaluated = try_evaluate_math_token(argv[i]);
            if (evaluated) {
                free(argv[i]);
                argv[i] = evaluated;
            }
        } else {
            free(result);
        }
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

    atexit(restore_terminal_settings);

    set_initial_argv0((argc > 0) ? argv[0] : NULL);
    init_scopes();
    init_static_scopes();
    current_function_index = -1;

    // Initialize base directory cache for resolving bundled executables.
    (void)get_base_dir();

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

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        return 1;
    }

    char task_path[PATH_MAX];
    if (resolve_task_path(argv[1], cwd, task_path, sizeof(task_path)) != 0) {
        fprintf(stderr, "Error: could not resolve task path for '%s'\n", argv[1]);
        return 1;
    }

    char task_directory[PATH_MAX];
    task_directory[0] = '\0';
    if (task_dirname(task_path, task_directory, sizeof(task_directory)) == 0) {
        if (chdir(task_directory) != 0) {
            fprintf(stderr, "Warning: failed to change directory to '%s': %s\n", task_directory, strerror(errno));
        } else {
            char resolved_task_dir[PATH_MAX];
            if (getcwd(resolved_task_dir, sizeof(resolved_task_dir))) {
                cache_task_workdir(resolved_task_dir);
            } else {
                cache_task_workdir(task_directory);
            }
        }
    }

    ScriptLine *script = (ScriptLine *)calloc(SCRIPT_MAX_LINES, sizeof(ScriptLine));
    if (!script) {
        perror("calloc");
        return 1;
    }
    Label labels[MAX_LABELS];
    FunctionDef functions[MAX_FUNCTIONS];
    memset(labels, 0, sizeof(labels));
    memset(functions, 0, sizeof(functions));
    int label_count = 0;
    int function_count = 0;
    int count = 0;
    int script_cap = SCRIPT_MAX_LINES;
    if (!load_task_file(task_path, task_directory, script, script_cap, &count, labels, &label_count, functions, &function_count, 0, debug)) {
        free(script);
        return 1;
    }

    for (int i = 0; i < function_count; ++i) {
        int end_pc = count;
        int indent = functions[i].indent;
        int start_pc = functions[i].start_pc;
        if (start_pc < 0) {
            start_pc = functions[i].definition_pc + 1;
        }
        for (int pc = start_pc; pc < count; ++pc) {
            if (script[pc].indent <= indent && script[pc].type != LINE_LABEL) {
                end_pc = pc;
                break;
            }
        }
        functions[i].start_pc = start_pc;
        functions[i].end_pc = end_pc;
    }

    IfContext if_stack[64];
    int if_sp = 0;
    ForContext for_stack[64];
    int for_sp = 0;
    WhileContext while_stack[64];
    int while_sp = 0;
    CallFrame call_stack[16];
    int call_sp = 0;
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
        bool pc_changed = false;

        if (call_sp > 0) {
            CallFrame *frame = &call_stack[call_sp - 1];
            if (pc >= frame->function_end_pc) {
                current_function_index = frame->previous_function_index;
                pop_scope();
                apply_return_value(frame);
                if (frame->has_return_value) {
                    free_value(&frame->return_value);
                }
                if_sp = frame->saved_if_sp;
                for_sp = frame->saved_for_sp;
                while_sp = frame->saved_while_sp;
                skipping_block = frame->saved_skipping_block;
                skip_indent = frame->saved_skip_indent;
                skip_context_index = frame->saved_skip_context_index;
                skip_for_true_branch = frame->saved_skip_for_true_branch;
                skip_progress_pending = frame->saved_skip_progress_pending;
                skip_consumed_first = frame->saved_skip_consumed_first;
                call_sp--;
                pc = frame->return_pc;
                pc_changed = true;
                continue;
            }
        }

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

        if (script[pc].type == LINE_FUNCTION) {
            FunctionDef *fn = find_function_by_definition(functions, function_count, pc);
            if (fn && fn->end_pc > pc) {
                pc = fn->end_pc - 1;
                pc_changed = true;
            }
            continue;
        }

        if (script[pc].type == LINE_LABEL) {
            continue;
        }

        if (if_sp > 0) {
            IfContext *ctx = &if_stack[if_sp - 1];
            if (!ctx->expects_end && ctx->true_branch_done && !ctx->else_encountered) {
                if (!(strncmp(command, "ELSE", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4])))) {
                    if_sp--;
                }
            }
        }

        if (strncmp(command, "IF", 2) == 0 && (command[2] == '\0' || isspace((unsigned char)command[2]) || command[2] == '(')) {
        const char *after_if = command + 2;
        while (isspace((unsigned char)*after_if)) {
            after_if++;
        }

        const char *colon = strrchr(after_if, ':');
        if (!colon) {
            if (debug) fprintf(stderr, "IF: expected ':' before END-delimited block at %d\n", script[pc].source_line);
            continue;
        }

        const char *cond_end = colon;
        while (cond_end > after_if && isspace((unsigned char)*(cond_end - 1))) {
            cond_end--;
        }

        if (cond_end <= after_if) {
            if (debug) fprintf(stderr, "IF: missing condition before ':' at %d\n", script[pc].source_line);
            continue;
        }

        char cond_buf[256];
        size_t cond_len = (size_t)(cond_end - after_if);
        if (cond_len >= sizeof(cond_buf)) {
            if (debug) fprintf(stderr, "IF: condition too long at %d\n", script[pc].source_line);
            continue;
        }
        memcpy(cond_buf, after_if, cond_len);
        cond_buf[cond_len] = '\0';

        size_t start_off = 0;
        size_t end_off = cond_len;
        while (start_off < end_off && isspace((unsigned char)cond_buf[start_off])) {
            start_off++;
        }
        while (end_off > start_off && isspace((unsigned char)cond_buf[end_off - 1])) {
            end_off--;
        }
        while (end_off > start_off && cond_buf[start_off] == '(' && cond_buf[end_off - 1] == ')') {
            start_off++;
            end_off--;
            while (start_off < end_off && isspace((unsigned char)cond_buf[start_off])) {
                start_off++;
            }
            while (end_off > start_off && isspace((unsigned char)cond_buf[end_off - 1])) {
                end_off--;
            }
        }

        if (start_off >= end_off) {
            if (debug) fprintf(stderr, "IF: empty condition after trimming at %d\n", script[pc].source_line);
            continue;
        }

        memmove(cond_buf, cond_buf + start_off, end_off - start_off);
        cond_buf[end_off - start_off] = '\0';

        const char *cursor = cond_buf;
        bool cond_result = false;
        bool condition_parsed = false;
        if (parse_boolean_literal(cond_buf, &cond_result, &cursor)) {
            condition_parsed = true;
        } else if (parse_condition(&cursor, &cond_result, script[pc].source_line, debug)) {
            condition_parsed = true;
        } else if (evaluate_truthy_expression(cond_buf, script[pc].source_line, debug, &cond_result)) {
            condition_parsed = true;
        }

        if (condition_parsed) {
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "IF: unexpected characters in condition at %d\n", script[pc].source_line);
            }
        }

        const char *after_colon = colon + 1;
        while (isspace((unsigned char)*after_colon)) {
            after_colon++;
        }
        if (*after_colon != '\0' && debug) {
            fprintf(stderr, "IF: unexpected characters after ':' at %d\n", script[pc].source_line);
        }
            if (if_sp >= (int)(sizeof(if_stack) / sizeof(if_stack[0]))) {
                if (debug) fprintf(stderr, "IF: nesting limit reached at line %d\n", script[pc].source_line);
                continue;
            }
            IfContext ctx = { .result = cond_result, .true_branch_done = false, .else_encountered = false, .else_branch_done = false, .expects_end = true, .indent = script[pc].indent, .line_number = script[pc].source_line };
            if_stack[if_sp++] = ctx;
            if (!cond_result) {
                skipping_block = true;
                skip_indent = script[pc].indent;
                skip_context_index = if_sp - 1;
                skip_for_true_branch = true;
                skip_progress_pending = true;
                skip_consumed_first = false;
            }
            continue;
        }
        else if (strncmp(command, "WHILE", 5) == 0 && (command[5] == '\0' || isspace((unsigned char)command[5]) || command[5] == '(')) {
            if (while_sp >= (int)(sizeof(while_stack) / sizeof(while_stack[0]))) {
                if (debug) fprintf(stderr, "WHILE: nesting limit reached at line %d\n", script[pc].source_line);
                continue;
            }

            const char *after_while = command + 5;
            while (isspace((unsigned char)*after_while)) {
                after_while++;
            }

            const char *colon = strrchr(after_while, ':');
            if (!colon) {
                if (debug) fprintf(stderr, "WHILE: expected ':' before END-delimited block at %d\n", script[pc].source_line);
                continue;
            }

            const char *cond_end = colon;
            while (cond_end > after_while && isspace((unsigned char)*(cond_end - 1))) {
                cond_end--;
            }

            if (cond_end <= after_while) {
                if (debug) fprintf(stderr, "WHILE: missing condition before ':' at %d\n", script[pc].source_line);
                continue;
            }

            char cond_buf[256];
            size_t cond_len = (size_t)(cond_end - after_while);
            if (cond_len >= sizeof(cond_buf)) {
                if (debug) fprintf(stderr, "WHILE: condition too long at %d\n", script[pc].source_line);
                continue;
            }
            memcpy(cond_buf, after_while, cond_len);
            cond_buf[cond_len] = '\0';

            size_t start_off = 0;
            size_t end_off = cond_len;
            while (start_off < end_off && isspace((unsigned char)cond_buf[start_off])) {
                start_off++;
            }
            while (end_off > start_off && isspace((unsigned char)cond_buf[end_off - 1])) {
                end_off--;
            }
            while (end_off > start_off && cond_buf[start_off] == '(' && cond_buf[end_off - 1] == ')') {
                start_off++;
                end_off--;
                while (start_off < end_off && isspace((unsigned char)cond_buf[start_off])) {
                    start_off++;
                }
                while (end_off > start_off && isspace((unsigned char)cond_buf[end_off - 1])) {
                    end_off--;
                }
            }

            if (start_off >= end_off) {
                if (debug) fprintf(stderr, "WHILE: empty condition after trimming at %d\n", script[pc].source_line);
                continue;
            }

            memmove(cond_buf, cond_buf + start_off, end_off - start_off);
            cond_buf[end_off - start_off] = '\0';

            const char *cursor = cond_buf;
            bool cond_result = false;
            bool condition_parsed = false;
            if (parse_boolean_literal(cond_buf, &cond_result, &cursor)) {
                condition_parsed = true;
            } else if (parse_condition(&cursor, &cond_result, script[pc].source_line, debug)) {
                condition_parsed = true;
            } else if (evaluate_truthy_expression(cond_buf, script[pc].source_line, debug, &cond_result)) {
                condition_parsed = true;
            }

            if (condition_parsed) {
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (*cursor != '\0' && debug) {
                    fprintf(stderr, "WHILE: unexpected characters in condition at %d\n", script[pc].source_line);
                }
            }

            const char *after_colon = colon + 1;
            while (isspace((unsigned char)*after_colon)) {
                after_colon++;
            }
            if (*after_colon != '\0' && debug) {
                fprintf(stderr, "WHILE: unexpected characters after ':' at %d\n", script[pc].source_line);
            }

            if (!cond_result) {
                skipping_block = true;
                skip_indent = script[pc].indent;
                skip_context_index = -1;
                skip_for_true_branch = false;
                skip_progress_pending = false;
                skip_consumed_first = false;
                note_branch_progress(if_stack, &if_sp);
                continue;
            }

            WhileContext wctx;
            wctx.while_line_pc = pc;
            wctx.body_start_pc = pc + 1;
            wctx.indent = script[pc].indent;
            snprintf(wctx.condition, sizeof(wctx.condition), "%s", cond_buf);
            while_stack[while_sp++] = wctx;
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "FOR", 3) == 0 && (command[3] == '\0' || isspace((unsigned char)command[3]))) {
            if (for_sp >= (int)(sizeof(for_stack) / sizeof(for_stack[0]))) {
                if (debug) fprintf(stderr, "FOR: nesting limit reached at line %d\n", script[pc].source_line);
                continue;
            }

            const char *cursor = command + 3;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }

            const char *line_end = cursor + strlen(cursor);
            while (line_end > cursor && isspace((unsigned char)*(line_end - 1))) {
                line_end--;
            }

            bool has_colon = false;
            if (line_end > cursor && *(line_end - 1) == ':') {
                has_colon = true;
                line_end--;
                while (line_end > cursor && isspace((unsigned char)*(line_end - 1))) {
                    line_end--;
                }
            }

            if (line_end == cursor) {
                if (debug) fprintf(stderr, "FOR: expected loop body after header at line %d\n", script[pc].source_line);
                continue;
            }

            bool has_paren = false;
            if (*cursor == '(') {
                has_paren = true;
                cursor++;
            }

            const char *first_semi = strchr(cursor, ';');
            if (!first_semi) {
                if (debug) fprintf(stderr, "FOR: missing first ';' at line %d\n", script[pc].source_line);
                continue;
            }
            const char *second_semi = strchr(first_semi + 1, ';');
            if (!second_semi) {
                if (debug) fprintf(stderr, "FOR: missing second ';' at line %d\n", script[pc].source_line);
                continue;
            }

            const char *step_end = line_end;
            if (has_paren) {
                const char *closing = strchr(second_semi + 1, ')');
                if (!closing) {
                    if (debug) fprintf(stderr, "FOR: missing closing ')' at line %d\n", script[pc].source_line);
                    continue;
                }
                step_end = closing;
                if (has_colon && closing >= line_end) {
                    if (debug) fprintf(stderr, "FOR: ':' must appear after ')' at line %d\n", script[pc].source_line);
                    continue;
                }
            } else {
                while (step_end > second_semi + 1 && isspace((unsigned char)*(step_end - 1))) {
                    step_end--;
                }
            }


            char init_buf[256];
            char cond_buf[256];
            char step_buf[128];
            memset(init_buf, 0, sizeof(init_buf));
            memset(cond_buf, 0, sizeof(cond_buf));
            memset(step_buf, 0, sizeof(step_buf));

            copy_trimmed_segment(cursor, first_semi, init_buf, sizeof(init_buf));
            copy_trimmed_segment(first_semi + 1, second_semi, cond_buf, sizeof(cond_buf));
            copy_trimmed_segment(second_semi + 1, step_end, step_buf, sizeof(step_buf));

            if (init_buf[0] != '\0') {
                if (!process_assignment_statement(init_buf, script[pc].source_line, debug) &&
                    !evaluate_expression_statement(init_buf, script[pc].source_line, debug)) {
                    continue;
                }
            }

            bool cond_ok = true;
            if (cond_buf[0] != '\0') {
                cond_ok = false;
                if (!evaluate_condition_string(cond_buf, script[pc].source_line, debug, &cond_ok)) {
                    continue;
                }
            }

            if (!cond_ok) {
                skipping_block = true;
                skip_indent = script[pc].indent;
                skip_context_index = -1;
                skip_for_true_branch = false;
                skip_progress_pending = false;
                skip_consumed_first = false;
                note_branch_progress(if_stack, &if_sp);
                continue;
            }

            if (step_buf[0] == '\0') {
                if (debug) fprintf(stderr, "FOR: missing step at line %d\n", script[pc].source_line);
                continue;
            }

            ForContext fctx;
            fctx.for_line_pc = pc;
            fctx.body_start_pc = pc + 1;
            fctx.indent = script[pc].indent;
            snprintf(fctx.condition, sizeof(fctx.condition), "%s", cond_buf);
            snprintf(fctx.step, sizeof(fctx.step), "%s", step_buf);
            for_stack[for_sp++] = fctx;
            note_branch_progress(if_stack, &if_sp);
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
            if (*cursor == ':') {
                cursor++;
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
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
            ctx->true_branch_done = true;
            if (ctx->result) {
                skipping_block = true;
                skip_indent = script[pc].indent;
                skip_context_index = if_sp - 1;
                skip_for_true_branch = false;
                skip_progress_pending = true;
                skip_consumed_first = false;
            }
        }
        else if (strncmp(command, "END", 3) == 0 && (command[3] == '\0' || isspace((unsigned char)command[3]))) {
            const char *cursor = command + 3;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "END: unexpected characters at %d\n", script[pc].source_line);
            }

            bool matched = false;
            if (while_sp > 0) {
                WhileContext *ctx = &while_stack[while_sp - 1];
                if (script[pc].indent == ctx->indent) {
                    matched = true;
                    bool cond_result = false;
                    /* Re-evaluate the stored condition on every END to decide whether
                     * to loop again. */
                    if (!evaluate_condition_string(ctx->condition, script[ctx->while_line_pc].source_line, debug, &cond_result)) {
                        cond_result = false;
                    }

                    if (cond_result) {
                        pc = ctx->body_start_pc - 1;
                        pc_changed = true;
                    } else {
                        while_sp--;
                    }
                    note_branch_progress(if_stack, &if_sp);
                }
            }

            if (!matched && for_sp > 0) {
                ForContext *ctx = &for_stack[for_sp - 1];
                if (script[pc].indent == ctx->indent) {
                    matched = true;
                    if (!apply_increment_step(ctx->step, script[ctx->for_line_pc].source_line, debug)) {
                        for_sp--;
                        continue;
                    }

                    bool cond_result = true;
                    if (ctx->condition[0] != '\0') {
                        cond_result = false;
                        if (!evaluate_condition_string(ctx->condition, script[ctx->for_line_pc].source_line, debug, &cond_result)) {
                            for_sp--;
                            continue;
                        }
                    }

                    if (cond_result) {
                        pc = ctx->body_start_pc - 1;
                        pc_changed = true;
                    } else {
                        for_sp--;
                    }
                    note_branch_progress(if_stack, &if_sp);
                }
            }

            if (!matched && if_sp > 0) {
                IfContext *ctx = &if_stack[if_sp - 1];
                if (script[pc].indent == ctx->indent) {
                    matched = true;
                    if (ctx->else_encountered) {
                        ctx->else_branch_done = true;
                    } else {
                        ctx->true_branch_done = true;
                    }
                    if_sp--;
                }
            }

            if (!matched && debug) {
                fprintf(stderr, "END without matching FOR/WHILE/IF at line %d\n", script[pc].source_line);
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
            bool wait_for_enter = true;
            char *option_token = NULL;
            bool option_quoted = false;
            if (parse_token(&cursor, &option_token, &option_quoted, NULL)) {
                if (option_quoted) {
                    if (debug) fprintf(stderr, "INPUT: unexpected quoted argument at line %d\n", script[pc].source_line);
                    free(option_token);
                    continue;
                }
                if (equals_ignore_case(option_token, "-wait")) {
                    free(option_token);
                    option_token = NULL;
                    char *value_token = NULL;
                    bool value_quoted = false;
                    if (!parse_token(&cursor, &value_token, &value_quoted, NULL) || value_quoted) {
                        if (debug) fprintf(stderr, "INPUT: -wait expects ON or OFF at line %d\n", script[pc].source_line);
                        free(value_token);
                        continue;
                    }
                    if (equals_ignore_case(value_token, "on")) {
                        wait_for_enter = true;
                    } else if (equals_ignore_case(value_token, "off")) {
                        wait_for_enter = false;
                    } else {
                        if (debug) fprintf(stderr, "INPUT: -wait expects ON or OFF at line %d\n", script[pc].source_line);
                        free(value_token);
                        continue;
                    }
                    free(value_token);
                } else {
                    if (debug) fprintf(stderr, "INPUT: unexpected argument '%s' at line %d\n", option_token, script[pc].source_line);
                    free(option_token);
                    continue;
                }
            }
            free(option_token);
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
            if (wait_for_enter) {
                if (!fgets(buffer, sizeof(buffer), stdin)) {
                    if (debug) fprintf(stderr, "INPUT: failed to read input at line %d\n", script[pc].source_line);
                    buffer[0] = '\0';
                } else {
                    size_t len = strcspn(buffer, "\r\n");
                    buffer[len] = '\0';
                }
            } else {
                if (!read_keypress_sequence(buffer, sizeof(buffer))) {
                    if (debug) fprintf(stderr, "INPUT: failed to read key press at line %d\n", script[pc].source_line);
                    buffer[0] = '\0';
                }
            }
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
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "SET", 3) == 0 && (command[3] == '\0' || isspace((unsigned char)command[3]))) {
            const char *cursor = command + 3;
            char *var_token = NULL;
            bool quoted = false;
            bool static_target = false;
            if (!parse_token(&cursor, &var_token, &quoted, NULL) || quoted) {
                if (debug) fprintf(stderr, "SET: expected variable at line %d\n", script[pc].source_line);
                free(var_token);
                continue;
            }
            if (equals_ignore_case(var_token, "STATIC")) {
                static_target = true;
                free(var_token);
                var_token = NULL;
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (!parse_token(&cursor, &var_token, &quoted, NULL) || quoted) {
                    if (debug) fprintf(stderr, "SET: expected variable after STATIC at line %d\n", script[pc].source_line);
                    free(var_token);
                    continue;
                }
            }
            VariableRef ref;
            if (!parse_variable_reference_token(var_token, &ref, script[pc].source_line, debug)) {
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
            Variable *var = NULL;
            if (static_target) {
                var = find_static_variable(ref.name, true);
                if (!var && debug) {
                    fprintf(stderr, "SET: STATIC not allowed outside of a function at line %d\n", script[pc].source_line);
                }
            }
            if (!var) {
                var = find_variable(ref.name, true);
            }
            if (var && !set_variable_from_ref(var, &ref, &value) && debug) {
                fprintf(stderr, "SET: failed to set variable at line %d\n", script[pc].source_line);
            }
            free_value(&value);
            note_branch_progress(if_stack, &if_sp);
        }
        else if (command[0] == '$') {
            process_assignment_statement(command, script[pc].source_line, debug);
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
                        log_output(out_buf, len);
                    }
		    free(out_buf);
                    note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "EVAL", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4]))) {
            const char *cursor = command + 4;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            const char *name_start = cursor;
            while (*cursor && (isalnum((unsigned char)*cursor) || *cursor == '_')) {
                cursor++;
            }
            size_t name_len = (size_t)(cursor - name_start);
            char func_name[sizeof(((FunctionDef *)0)->name)];
            if (name_len == 0 || name_len >= sizeof(func_name)) {
                if (debug) fprintf(stderr, "EVAL: invalid function name at line %d\n", script[pc].source_line);
                continue;
            }
            memcpy(func_name, name_start, name_len);
            func_name[name_len] = '\0';

            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '(') {
                if (debug) fprintf(stderr, "EVAL: expected '(' after function name at line %d\n", script[pc].source_line);
                continue;
            }
            cursor++;

            Value args[MAX_FUNCTION_PARAMS];
            int arg_count = 0;
            bool ok = true;
            memset(args, 0, sizeof(args));
            while (1) {
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (*cursor == ')') {
                    cursor++;
                    break;
                }
                if (arg_count >= MAX_FUNCTION_PARAMS) {
                    if (debug) fprintf(stderr, "EVAL: too many arguments at line %d\n", script[pc].source_line);
                    ok = false;
                    break;
                }
                if (!parse_expression(&cursor, &args[arg_count], ",)", script[pc].source_line, debug)) {
                    ok = false;
                    break;
                }
                arg_count++;
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (*cursor == ',') {
                    cursor++;
                    continue;
                }
                if (*cursor == ')') {
                    cursor++;
                    break;
                }
                ok = false;
                break;
            }

            char target_var[sizeof(((Variable *)0)->name)];
            bool has_target = false;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0') {
                const char *after_to = NULL;
                if (!match_keyword(cursor, "TO", &after_to)) {
                    if (debug) fprintf(stderr, "EVAL: expected TO after arguments at line %d\n", script[pc].source_line);
                    ok = false;
                } else {
                    cursor = after_to;
                    while (isspace((unsigned char)*cursor)) {
                        cursor++;
                    }
                    char *var_token = NULL;
                    bool quoted = false;
                    if (!parse_token(&cursor, &var_token, &quoted, NULL) || quoted) {
                        if (debug) fprintf(stderr, "EVAL: expected variable after TO at line %d\n", script[pc].source_line);
                        free(var_token);
                        ok = false;
                    } else {
                        if (!parse_variable_name_token(var_token, target_var, sizeof(target_var))) {
                            if (debug) fprintf(stderr, "EVAL: invalid variable name after TO at line %d\n", script[pc].source_line);
                            ok = false;
                        } else {
                            has_target = true;
                        }
                        free(var_token);
                        while (isspace((unsigned char)*cursor)) {
                            cursor++;
                        }
                        if (*cursor != '\0') {
                            if (debug) fprintf(stderr, "EVAL: unexpected characters at line %d\n", script[pc].source_line);
                            ok = false;
                        }
                    }
                }
            }

            if (!ok) {
                for (int i = 0; i < arg_count; ++i) {
                    free_value(&args[i]);
                }
                continue;
            }

            int fn_index = find_function_index(functions, function_count, func_name);
            if (fn_index < 0) {
                if (debug) fprintf(stderr, "EVAL: unknown function '%s' at line %d\n", func_name, script[pc].source_line);
                for (int i = 0; i < arg_count; ++i) {
                    free_value(&args[i]);
                }
                continue;
            }

            FunctionDef *fn = &functions[fn_index];
            if (arg_count != fn->param_count) {
                if (debug) fprintf(stderr, "EVAL: argument count mismatch for %s at line %d\n", func_name, script[pc].source_line);
                for (int i = 0; i < arg_count; ++i) {
                    free_value(&args[i]);
                }
                continue;
            }

            if (call_sp >= (int)(sizeof(call_stack) / sizeof(call_stack[0]))) {
                if (debug) fprintf(stderr, "EVAL: call stack limit reached at line %d\n", script[pc].source_line);
                for (int i = 0; i < arg_count; ++i) {
                    free_value(&args[i]);
                }
                continue;
            }

            int previous_function_index = current_function_index;
            current_function_index = fn_index;

            if (!push_scope()) {
                current_function_index = previous_function_index;
                for (int i = 0; i < arg_count; ++i) {
                    free_value(&args[i]);
                }
                continue;
            }

            for (int i = 0; i < arg_count; ++i) {
                Variable *param = find_variable(fn->params[i], true);
                if (param) {
                    assign_variable(param, &args[i]);
                }
                free_value(&args[i]);
            }

            CallFrame *frame = &call_stack[call_sp++];
            memset(frame, 0, sizeof(*frame));
            frame->return_pc = pc;
            frame->function_end_pc = fn->end_pc;
            frame->has_return_target = has_target;
            if (has_target) {
                snprintf(frame->return_target, sizeof(frame->return_target), "%s", target_var);
            }
            frame->has_return_value = false;
            frame->saved_if_sp = if_sp;
            frame->saved_for_sp = for_sp;
            frame->saved_while_sp = while_sp;
            frame->saved_skipping_block = skipping_block;
            frame->saved_skip_indent = skip_indent;
            frame->saved_skip_context_index = skip_context_index;
            frame->saved_skip_for_true_branch = skip_for_true_branch;
            frame->saved_skip_progress_pending = skip_progress_pending;
            frame->saved_skip_consumed_first = skip_consumed_first;
            frame->previous_function_index = previous_function_index;
            frame->function_index = fn_index;

            current_function_index = fn_index;

            if_sp = 0;
            for_sp = 0;
            while_sp = 0;
            skipping_block = false;
            skip_indent = 0;
            skip_context_index = -1;
            skip_for_true_branch = false;
            skip_progress_pending = false;
            skip_consumed_first = false;

            pc = fn->start_pc - 1;
            pc_changed = true;
            note_branch_progress(if_stack, &if_sp);
            continue;
        }
        else if (strncmp(command, "ECHO", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4]))) {
            const char *cursor = command + 4;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }

            char *mode_token = NULL;
            bool quoted = false;
            if (!parse_token(&cursor, &mode_token, &quoted, NULL) || quoted) {
                if (debug) fprintf(stderr, "ECHO: expected ON or OFF at line %d\n", script[pc].source_line);
                free(mode_token);
                continue;
            }

            bool enable = true;
            if (equals_ignore_case(mode_token, "ON")) {
                enable = true;
            } else if (equals_ignore_case(mode_token, "OFF")) {
                enable = false;
            } else {
                if (debug) fprintf(stderr, "ECHO: expected ON or OFF at line %d\n", script[pc].source_line);
                free(mode_token);
                continue;
            }
            free(mode_token);

            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (*cursor != '\0' && debug) {
                fprintf(stderr, "ECHO: unexpected characters at %d\n", script[pc].source_line);
            }

            if (!set_echo_enabled(enable) && debug) {
                fprintf(stderr, "ECHO: failed to update terminal state at line %d\n", script[pc].source_line);
            }
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "WAIT", 4) == 0) {
            int ms;
            if (sscanf(command, "WAIT %d", &ms) == 1) {
                delay_ms(ms);
            } else if (debug) {
                fprintf(stderr, "WAIT: invalid format at %d: %s\n", script[pc].source_line, command);
            }
            note_branch_progress(if_stack, &if_sp);
        }
        else if (strncmp(command, "GOTO", 4) == 0 && (command[4] == '\0' || isspace((unsigned char)command[4]))) {
            const char *cursor = command + 4;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }

            char label_token[64];
            bool label_ok = true;
            label_token[0] = '\0';

            if (*cursor == '$') {
                cursor++;
                char var_name[sizeof(((Variable *)0)->name)];
                size_t name_len = 0;
                bool name_too_long = false;
                while (isalnum((unsigned char)*cursor) || *cursor == '_') {
                    if (!name_too_long) {
                        if (name_len + 1 >= sizeof(var_name)) {
                            name_too_long = true;
                        } else {
                            var_name[name_len++] = *cursor;
                        }
                    }
                    cursor++;
                }
                if (name_len == 0 || name_too_long) {
                    if (debug) {
                        fprintf(stderr, "GOTO: invalid variable reference at %d: %s\n", script[pc].source_line, command);
                    }
                    label_ok = false;
                } else if (*cursor != '\0' && !isspace((unsigned char)*cursor) && *cursor != ':') {
                    if (debug) {
                        fprintf(stderr, "GOTO: invalid variable reference at %d: %s\n", script[pc].source_line, command);
                    }
                    label_ok = false;
                } else {
                    var_name[name_len] = '\0';
                    Variable *var = find_variable(var_name, false);
                    Value value = variable_to_value(var);
                    char *resolved = value_to_string(&value);
                    free_value(&value);
                    if (!resolved) {
                        resolved = xstrdup("");
                    }
                    char *resolved_raw = resolved;
                    char *start = resolved_raw;
                    while (isspace((unsigned char)*start)) {
                        start++;
                    }
                    char *end = start + strlen(start);
                    while (end > start && isspace((unsigned char)*(end - 1))) {
                        end--;
                    }
                    *end = '\0';
                    if (start != resolved_raw) {
                        memmove(resolved_raw, start, (size_t)(end - start + 1));
                    }
                    char *label_source = resolved_raw;
                    if (label_source[0] == '@') {
                        label_source++;
                    }
                    size_t len = strlen(label_source);
                    if (len == 0) {
                        if (debug) {
                            fprintf(stderr, "GOTO: variable '%s' is empty at %d\n", var_name, script[pc].source_line);
                        }
                        label_ok = false;
                    } else if (len >= sizeof(label_token)) {
                        if (debug) {
                            fprintf(stderr, "GOTO: label from variable '%s' too long at %d\n", var_name, script[pc].source_line);
                        }
                        label_ok = false;
                    } else {
                        memcpy(label_token, label_source, len + 1);
                    }
                    free(resolved_raw);
                }
            } else {
                if (*cursor == '@') {
                    cursor++;
                }
                if (*cursor == '\0') {
                    if (debug) {
                        fprintf(stderr, "GOTO: missing label at %d: %s\n", script[pc].source_line, command);
                    }
                    label_ok = false;
                } else {
                    size_t len = 0;
                    bool too_long = false;
                    while (*cursor && !isspace((unsigned char)*cursor) && *cursor != ':') {
                        if (len + 1 >= sizeof(label_token)) {
                            too_long = true;
                        } else {
                            label_token[len++] = *cursor;
                        }
                        cursor++;
                    }
                    label_token[len] = '\0';
                    if (len == 0) {
                        if (debug) {
                            fprintf(stderr, "GOTO: empty label at %d\n", script[pc].source_line);
                        }
                        label_ok = false;
                    } else if (too_long) {
                        if (debug) {
                            fprintf(stderr, "GOTO: label too long at %d\n", script[pc].source_line);
                        }
                        label_ok = false;
                    }
                }
            }

            if (!label_ok) {
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
                pc_changed = true;
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

            ensure_task_workdir();

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
            VariableRef capture_ref;
            char *captured_output = NULL;
            size_t captured_len = 0;
            size_t captured_cap = 0;
            bool use_execvp = false;
            bool explicit_path_requested = false;
            char resolved[PATH_MAX];

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
                if (!parse_variable_reference_token(argv_heap[argcnt - 1], &capture_ref, script[pc].source_line, debug)) {
                    fprintf(stderr, "RUN: invalid variable name after TO at line %d\n", script[pc].source_line);
                    free_argv(argv_heap);
                    continue;
                }
                capture_var = find_variable(capture_ref.name, true);
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

            if (argcnt > 0 && strcmp(argv_heap[0], "_TOFILE") == 0) {
                int start_flag = 0;
                int stop_flag = 0;
                const char *path = NULL;

                for (int i = 1; i < argcnt; ++i) {
                    if (strcmp(argv_heap[i], "-file") == 0 && i + 1 < argcnt) {
                        path = argv_heap[i + 1];
                        i++;
                    } else if (strcmp(argv_heap[i], "--start") == 0) {
                        start_flag = 1;
                    } else if (strcmp(argv_heap[i], "--stop") == 0) {
                        stop_flag = 1;
                    }
                }

                if (start_flag && stop_flag) {
                    fprintf(stderr, "_TOFILE: cannot use --start and --stop together\n");
                } else if (start_flag) {
                    (void)start_logging(path);
                } else if (stop_flag) {
                    if (log_file != NULL) {
                        printf("_TOFILE: logging stopped (%s)\n", log_file_path[0] != '\0' ? log_file_path : "<unknown>");
                    } else {
                        printf("_TOFILE: logging was not active\n");
                    }
                    stop_logging();
                } else {
                    fprintf(stderr, "Usage: _TOFILE -file <path> --start | _TOFILE --stop\n");
                }

                free_argv(argv_heap);
                note_branch_progress(if_stack, &if_sp);
                continue;
            }

            if (argcnt > 0) {
                explicit_path_requested = strchr(argv_heap[0], '/') != NULL;
            }

            if (capture_output && blocking_mode && argcnt > 0) {
                bool handled = false;
                if (equals_ignore_case(argv_heap[0], "_GETROW") ||
                    equals_ignore_case(argv_heap[0], "_GETCOL")) {
                    long row = 0;
                    long col = 0;
                    if (query_cursor_position(&row, &col) == 0) {
                        Value value;
                        memset(&value, 0, sizeof(value));
                        value.type = VALUE_INT;
                        if (equals_ignore_case(argv_heap[0], "_GETCOL")) {
                            value.int_val = col;
                            value.float_val = (double)col;
                        } else {
                            value.int_val = row;
                            value.float_val = (double)row;
                        }
                        assign_variable(capture_var, &value);
                        free_value(&value);
                    } else if (debug) {
                        fprintf(stderr, "RUN: failed to query cursor position at line %d\n",
                                script[pc].source_line);
                    }
                    handled = true;
                }

                if (handled) {
                    free_argv(argv_heap);
                    note_branch_progress(if_stack, &if_sp);
                    continue;
                }
            }

            // Resolve executable path for internal commands; fall back to system PATH.
            if (resolve_exec_path(argv_heap[0], resolved, sizeof(resolved)) == 0) {
                free(argv_heap[0]);
                argv_heap[0] = xstrdup(resolved);
            } else if (!explicit_path_requested) {
                use_execvp = true;
            } else {
                fprintf(stderr, "RUN: executable not found or not executable: %s\n", argv_heap[0]);
                free_argv(argv_heap);
                continue;
            }

            if (debug) {
                fprintf(stderr, "RUN: %s %s", use_execvp ? "execvp" : "execv", argv_heap[0]);
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
                        if (use_execvp) {
                            execvp(argv_heap[0], argv_heap);
                            perror("execvp");
                        } else {
                            execv(argv_heap[0], argv_heap);
                            perror("execv");
                        }
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

            bool log_child_output = (log_file != NULL && blocking_mode && !capture_output);
            bool need_pipe = capture_output || log_child_output;

            int pipefd[2] = { -1, -1 };
            if (need_pipe) {
                if (pipe(pipefd) < 0) {
                    perror("pipe");
                    free_argv(argv_heap);
                    continue;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                if (need_pipe) {
                    close(pipefd[0]);
                    close(pipefd[1]);
                }
                free_argv(argv_heap);
                continue;
            } else if (pid == 0) {
                if (need_pipe) {
                    close(pipefd[0]);
                    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                    if (dup2(pipefd[1], STDERR_FILENO) < 0) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                    close(pipefd[1]);
                }
                if (use_execvp) {
                    execvp(argv_heap[0], argv_heap);
                    perror("execvp");
                } else {
                    execv(argv_heap[0], argv_heap);
                    perror("execv");
                }
                _exit(EXIT_FAILURE);
            } else {
                if (need_pipe) {
                    close(pipefd[1]);
                    char buffer[4096];
                    ssize_t rd;
                    while (1) {
                        rd = read(pipefd[0], buffer, sizeof(buffer));
                        if (rd > 0) {
                            if (capture_output) {
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
                            }
                            if (log_child_output) {
                                if (fwrite(buffer, 1, (size_t)rd, stdout) < (size_t)rd) {
                                    perror("write");
                                }
                                fflush(stdout);
                                log_output(buffer, (size_t)rd);
                            }
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
                    if (capture_output) {
                        if (captured_output) {
                            captured_output[captured_len] = '\0';
                        } else {
                            captured_output = xstrdup("");
                            captured_len = 0;
                            captured_cap = 1;
                        }
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
                    Value value;
                    memset(&value, 0, sizeof(value));
                    bool parsed = parse_value_from_string(captured_output, &value, script[pc].source_line, debug);
                    bool keep_captured_buffer = false;
                    if (!parsed) {
                        long long iv = 0;
                        double fv = 0.0;
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
                            keep_captured_buffer = true;
                        }
                    }
                    set_variable_from_ref(capture_var, &capture_ref, &value);
                    if (!keep_captured_buffer) {
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
        else if (strncmp(command, "RETURN", 6) == 0 && (command[6] == '\0' || isspace((unsigned char)command[6]))) {
            if (call_sp <= 0) {
                if (debug) fprintf(stderr, "RETURN outside of function at line %d\n", script[pc].source_line);
                continue;
            }
            const char *cursor = command + 6;
            while (isspace((unsigned char)*cursor)) {
                cursor++;
            }
            Value ret;
            memset(&ret, 0, sizeof(ret));
            bool has_value = false;
            if (*cursor != '\0') {
                if (!parse_expression(&cursor, &ret, NULL, script[pc].source_line, debug)) {
                    continue;
                }
                has_value = true;
                while (isspace((unsigned char)*cursor)) {
                    cursor++;
                }
                if (*cursor != '\0') {
                    if (debug) fprintf(stderr, "RETURN: unexpected characters at line %d\n", script[pc].source_line);
                    free_value(&ret);
                    continue;
                }
            }

            CallFrame *frame = &call_stack[call_sp - 1];
            frame->has_return_value = has_value;
            if (has_value) {
                free_value(&frame->return_value);
                copy_value(&frame->return_value, &ret);
            }
            free_value(&ret);

            current_function_index = frame->previous_function_index;
            pop_scope();
            apply_return_value(frame);
            if (frame->has_return_value) {
                free_value(&frame->return_value);
                frame->has_return_value = false;
            }
            if_sp = frame->saved_if_sp;
            for_sp = frame->saved_for_sp;
            while_sp = frame->saved_while_sp;
            skipping_block = frame->saved_skipping_block;
            skip_indent = frame->saved_skip_indent;
            skip_context_index = frame->saved_skip_context_index;
            skip_for_true_branch = frame->saved_skip_for_true_branch;
            skip_progress_pending = frame->saved_skip_progress_pending;
            skip_consumed_first = frame->saved_skip_consumed_first;
            call_sp--;
            pc = frame->return_pc;
            pc_changed = true;
            continue;
        }
        else if (strncmp(command, "CLEAR", 5) == 0) {
            printf("\033[H\033[J");
            fflush(stdout);
            note_branch_progress(if_stack, &if_sp);
        }
        else {
            if (debug) fprintf(stderr, "Unrecognized command at %d: %s\n", script[pc].source_line, command);
        }

        if (!pc_changed && for_sp > 0) {
            ForContext *ctx = &for_stack[for_sp - 1];
            int next_pc = pc + 1;
            if (next_pc >= count || script[next_pc].indent <= ctx->indent) {
                if (!apply_increment_step(ctx->step, script[ctx->for_line_pc].source_line, debug)) {
                    for_sp--;
                    continue;
                }

                bool cond_result = true;
                if (ctx->condition[0] != '\0') {
                    cond_result = false;
                    if (!evaluate_condition_string(ctx->condition, script[ctx->for_line_pc].source_line, debug, &cond_result)) {
                        for_sp--;
                        continue;
                    }
                }

                if (cond_result) {
                    pc = ctx->body_start_pc - 1;
                } else {
                    for_sp--;
                }
            }
        }
    }

    if (skipping_block && skip_progress_pending) {
        finalize_skipped_branch(if_stack, &if_sp, skip_context_index, skip_for_true_branch);
        skip_context_index = -1;
        skip_progress_pending = false;
        skip_consumed_first = false;
    }

    if (echo_disabled) {
        restore_terminal_settings();
    }

    stop_logging();
    cleanup_variables();
    free(script);
    return 0;
}



