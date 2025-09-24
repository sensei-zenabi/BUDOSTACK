/*
* runtask.c - A simplified script engine with PRINT, WAIT, GOTO, RUN, and CLEAR commands.
*
* Changes in this version:
* - CMD removed.
* - RUN executes an app by name from ./apps/, ./commands/, or ./utilities/ (blocking),
*   similar to how task files are forced under tasks/. Arguments are passed as-is.
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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Global flag to signal termination (set by SIGINT handler)
volatile sig_atomic_t stop = 0;

static void sigint_handler(int signum) {
    (void)signum;
    stop = 1;
}

static void print_help(void) {
    printf("\nRuntask Help\n");
    printf("============\n\n");
    printf("Commands:\n");
    printf("  PRINT \"message\"    : Prints any message\n");
    printf("  WAIT milliseconds  : Waits for <milliseconds>\n");
    printf("  GOTO line_number   : Jumps to the specified line number\n");
    printf("  RUN <cmd [args...]>: Executes an executable from ./apps, ./commands, or ./utilities\n");
    printf("                       (blocking). If the command contains '/', it's executed as given.\n");
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

typedef struct {
    int number;
    char text[256];
} ScriptLine;

static int cmpScriptLine(const void *a, const void *b) {
    const ScriptLine *A = (const ScriptLine *)a;
    const ScriptLine *B = (const ScriptLine *)b;
    return A->number - B->number;
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

/* Resolve executable path:
   - If argv0 contains '/', use as-is.
   - Else try "apps/argv0", then "commands/argv0", then "utilities/argv0".
   - If found and executable, write into resolved (size bytes) and return 0; else -1.
*/
static int resolve_exec_path(const char *argv0, char *resolved, size_t size) {
    if (!argv0 || !*argv0) return -1;

    if (strchr(argv0, '/')) {
        // explicit relative/absolute path
        if (snprintf(resolved, size, "%s", argv0) >= (int)size) return -1;
        return access(resolved, X_OK) == 0 ? 0 : -1;
    }

    const char *dirs[] = { "apps", "commands", "utilities" };
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
        if (snprintf(resolved, size, "%s/%s", dirs[i], argv0) >= (int)size) continue;
        if (access(resolved, X_OK) == 0) return 0;
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

    // Prepend tasks/ like before
    char task_path[512];
    snprintf(task_path, sizeof(task_path), "tasks/%s", argv[1]);
    FILE *fp = fopen(task_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Could not open task file '%s'\n", task_path);
        return 1;
    }

    // Load script
    ScriptLine script[1024];
    int count = 0;
    char linebuf[256];
    while (fgets(linebuf, sizeof(linebuf), fp)) {
        char *line = trim(linebuf);
        if (!*line) continue;
        int ln = 0, off = 0;
        if (sscanf(line, "%d%n", &ln, &off) != 1) {
            if (debug) fprintf(stderr, "Error: Missing/invalid line number: %s\n", line);
            continue;
        }
        script[count].number = ln;
        char *cmdpart = trim(line + off);
        strncpy(script[count].text, cmdpart, sizeof(script[count].text) - 1);
        script[count].text[sizeof(script[count].text) - 1] = '\0';
        count++;
    }
    fclose(fp);

    qsort(script, count, sizeof(ScriptLine), cmpScriptLine);

    // Run
    for (int pc = 0; pc < count && !stop; pc++) {
        if (debug) fprintf(stderr, "Executing line %d: %s\n", script[pc].number, script[pc].text);

        if (strncmp(script[pc].text, "PRINT", 5) == 0) {
            char *start = strchr(script[pc].text, '"');
            if (!start) { if (debug) fprintf(stderr, "PRINT: missing opening quote at %d\n", script[pc].number); continue; }
            start++;
            char *end = strchr(start, '"');
            if (!end)  { if (debug) fprintf(stderr, "PRINT: missing closing quote at %d\n", script[pc].number); continue; }
            size_t len = (size_t)(end - start);
            char msg[256];
            if (len >= sizeof(msg)) { if (debug) fprintf(stderr, "PRINT: truncating at %d\n", script[pc].number); len = sizeof(msg) - 1; }
            strncpy(msg, start, len); msg[len] = '\0';
            printf("%s\n", msg);
        }
        else if (strncmp(script[pc].text, "WAIT", 4) == 0) {
            int ms;
            if (sscanf(script[pc].text, "WAIT %d", &ms) == 1) delay_ms(ms);
            else if (debug) fprintf(stderr, "WAIT: invalid format at %d: %s\n", script[pc].number, script[pc].text);
        }
        else if (strncmp(script[pc].text, "GOTO", 4) == 0) {
            int target;
            if (sscanf(script[pc].text, "GOTO %d", &target) == 1) {
                int found = -1;
                for (int i = 0; i < count; i++) if (script[i].number == target) { found = i; break; }
                if (found == -1) {
                    if (debug) fprintf(stderr, "GOTO: target %d not found at %d\n", target, script[pc].number);
                } else {
                    pc = found - 1; // -1 because loop will ++pc
                }
            } else if (debug) fprintf(stderr, "GOTO: invalid format at %d: %s\n", script[pc].number, script[pc].text);
        }
        else if (strncmp(script[pc].text, "RUN", 3) == 0) {
            const char *after = script[pc].text + 3;
            char *cmdline = trim((char*)after);
            if (!*cmdline) {
                if (debug) fprintf(stderr, "RUN: missing command at line %d\n", script[pc].number);
                continue;
            }

            // Tokenize to argv[] (heap-based)
            int argcnt = 0;
            char **argv_heap = split_args_heap(cmdline, &argcnt);
            if (argcnt <= 0) {
                if (debug) fprintf(stderr, "RUN: failed to parse command at line %d\n", script[pc].number);
                free_argv(argv_heap);
                continue;
            }

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
                fprintf(stderr, "\n");
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                free_argv(argv_heap);
                continue;
            } else if (pid == 0) {
                execv(argv_heap[0], argv_heap);
                perror("execv");
                _exit(EXIT_FAILURE);
            } else {
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
                free_argv(argv_heap);
            }
        }
        else if (strncmp(script[pc].text, "CLEAR", 5) == 0) {
            printf("\033[H\033[J");
            fflush(stdout);
        }
        else {
            if (debug) fprintf(stderr, "Unrecognized command at %d: %s\n", script[pc].number, script[pc].text);
        }
    }

    return 0;
}
