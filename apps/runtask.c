/*
* runtask.c - A simplified script engine with PRINT, WAIT, GOTO, RUN, and CLEAR commands.
*
* Changes in this version:
* - CMD removed.
* - RUN executes an app by name from ./apps/, ./commands/, or ./utilities/ (blocking),
*   similar to how task files are forced under tasks/. Arguments are passed as-is.
* - If RUN's first token contains '/', it's treated as an explicit path and executed directly.
*
* Examples (assuming an executable "mytool" exists in ./apps or ./commands or ./utilities):
*   RUN mytool -v "arg with spaces"
*   RUN utilities/cleanup.sh --dry-run
*   RUN ./apps/build.sh all
*
* Compile:
*   gcc -std=c11 -o runtask runtask.c
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

void sigint_handler(int signum) {
    (void)signum;
    stop = 1;
}

void print_help(void) {
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
    printf("  gcc -std=c11 -o runtask runtask.c\n\n");
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

/* --- Simple argv tokenizer that supports quotes and backslash escapes ---
   - Splits by whitespace.
   - Supports "double quoted" and 'single quoted' args.
   - Supports backslash escapes inside double quotes and unquoted text.
   - Returns argc, fills argv[] with pointers into an internal buffer.
*/
static int tokenize_args(const char *cmdline, char **argv, int max_args) {
    static char buf[1024];
    size_t n = strlen(cmdline);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, cmdline, n);
    buf[n] = '\0';

    int argc = 0;
    char *p = buf;

    while (*p && argc < max_args) {
        // skip leading spaces
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char *arg = p;
        char *out = p;
        bool in_squote = false, in_dquote = false;

        while (*p) {
            if (!in_squote && *p == '"' ) { in_dquote = !in_dquote; p++; continue; }
            if (!in_dquote && *p == '\'') { in_squote = !in_squote; p++; continue; }

            if (!in_squote && *p == '\\') {
                // backslash escape (always consume next char if present)
                p++;
                if (*p) *out++ = *p++;
                continue;
            }

            if (!in_squote && !in_dquote && isspace((unsigned char)*p)) {
                // end of arg
                break;
            }

            *out++ = *p++;
        }
        *out = '\0';
        argv[argc++] = arg;

        while (*p && !isspace((unsigned char)*p)) p++; // (safety)
        while (isspace((unsigned char)*p)) p++;
    }

    argv[argc] = NULL;
    return argc;
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

            // Tokenize to argv[]
            char *argvv[64];
            int argcnt = tokenize_args(cmdline, argvv, (int)(sizeof(argvv)/sizeof(argvv[0]) - 1));
            if (argcnt <= 0) {
                if (debug) fprintf(stderr, "RUN: failed to parse command at line %d\n", script[pc].number);
                continue;
            }

            // Resolve executable path
            char resolved[PATH_MAX];
            if (resolve_exec_path(argvv[0], resolved, sizeof(resolved)) != 0) {
                fprintf(stderr, "RUN: executable not found or not executable: %s (searched apps/, commands/, utilities/)\n", argvv[0]);
                continue;
            }
            argvv[0] = resolved; // replace with full path

            if (debug) {
                fprintf(stderr, "RUN: execv %s", argvv[0]);
                for (int i = 1; i < argcnt; ++i) fprintf(stderr, " [%s]", argvv[i]);
                fprintf(stderr, "\n");
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                continue;
            } else if (pid == 0) {
                execv(argvv[0], argvv);
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

