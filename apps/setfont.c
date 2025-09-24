#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <strings.h>  // for strcasecmp
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_FONTS_DIR "./fonts"
#define EXT ".psf"

static int has_psf_ext(const char *name) {
    size_t n = strlen(name);
    size_t e = strlen(EXT);
    if (n < e) return 0;
    const char *p = name + (n - e);
    return (p[0] == '.' && (p[1] == 'p' || p[1] == 'P') && (p[2] == 's' || p[2] == 'S') && (p[3] == 'f' || p[3] == 'F'));
}

static int cmpstr(const void *a, const void *b) {
    const char * const *sa = (const char * const *)a;
    const char * const *sb = (const char * const *)b;
    // use POSIX strcasecmp declared in <strings.h>
    return strcasecmp(*sa, *sb);
}

static int file_readable(const char *path) {
    return access(path, R_OK) == 0;
}

static int is_pathlike(const char *s) {
    return strchr(s, '/') != NULL;
}

static const char *get_fonts_dir(void) {
    static char cached[PATH_MAX];
    static int initialized = 0;

    if (!initialized) {
        initialized = 1;
        const char *base = getenv("BUDOSTACK_BASE");
        if (base && base[0] != '\0') {
            if (snprintf(cached, sizeof(cached), "%s/fonts", base) >= (int)sizeof(cached)) {
                cached[0] = '\0';
            }
        }
        if (cached[0] == '\0') {
            strncpy(cached, DEFAULT_FONTS_DIR, sizeof(cached) - 1);
            cached[sizeof(cached) - 1] = '\0';
        }
    }
    return cached;
}

static int run_setfont(const char *path, int use_double) {
    printf("\nRunning: setfont %s %s\n\n", use_double ? "-d" : "", path);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        if (use_double) {
            execlp("setfont", "setfont", "-d", path, (char *)NULL);
        } else {
            execlp("setfont", "setfont", path, (char *)NULL);
        }
        fprintf(stderr, "Failed to exec 'setfont': %s\n", strerror(errno));
        _exit(127);
    } else {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
            return 1;
        }
        if (WIFEXITED(status)) {
            int rc = WEXITSTATUS(status);
            if (rc == 0) {
                printf("Font applied successfully.\n");
                return 0;
            } else {
                printf("setfont exited with code %d.\n", rc);
                return rc ? rc : 1;
            }
        } else if (WIFSIGNALED(status)) {
            printf("setfont terminated by signal %d.\n", WTERMSIG(status));
            return 1;
        } else {
            printf("setfont ended abnormally.\n");
            return 1;
        }
    }
}

static void print_usage(const char *prog) {
    const char *fonts_dir = get_fonts_dir();
    fprintf(stderr,
        "Usage: %s [-d|--double] <fontfile.psf>\n"
        "       %s               (interactive mode)\n"
        "\nIf <fontfile.psf> has no '/' it is looked up under %s.\n",
        prog, prog, fonts_dir);
}

int main(int argc, char **argv) {
    /* --- Fast path: CLI bypass --- */
    if (argc > 1) {
        int use_double = 0;
        const char *font_arg = NULL;

        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--double") == 0) {
                use_double = 1;
            } else if (!font_arg) {
                font_arg = argv[i];
            } else {
                // Extra unexpected args: show usage and exit
                print_usage(argv[0]);
                return 2;
            }
        }

        if (font_arg) {
            char path[4096];

            if (is_pathlike(font_arg)) {
                // Treat as provided path
                if (snprintf(path, sizeof(path), "%s", font_arg) >= (int)sizeof(path)) {
                    fprintf(stderr, "Path too long.\n");
                    return 1;
                }
                if (!file_readable(path)) {
                    fprintf(stderr, "Font not readable: %s\n", path);
                    return 1;
                }
                return run_setfont(path, use_double);
            } else {
                // Treat as a font name under FONTS_DIR
                const char *fonts_dir = get_fonts_dir();
                if (snprintf(path, sizeof(path), "%s/%s", fonts_dir, font_arg) >= (int)sizeof(path)) {
                    fprintf(stderr, "Path too long.\n");
                    return 1;
                }
                if (!file_readable(path)) {
                    fprintf(stderr, "Font not found or not readable under %s: %s\n", fonts_dir, font_arg);
                    return 1;
                }
                return run_setfont(path, use_double);
            }
        }
        // If only -d/--double provided with no font, fall through to interactive
    }

    /* --- Original interactive UI --- */
    const char *fonts_dir = get_fonts_dir();
    DIR *dir = opendir(fonts_dir);
    if (!dir) {
        fprintf(stderr, "Error: could not open %s: %s\n", fonts_dir, strerror(errno));
        return 1;
    }

    size_t cap = 16, count = 0;
    char **fonts = malloc(cap * sizeof(*fonts));
    if (!fonts) {
        fprintf(stderr, "Out of memory.\n");
        closedir(dir);
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!has_psf_ext(ent->d_name)) continue;
        if (count == cap) {
            cap *= 2;
            char **nf = realloc(fonts, cap * sizeof(*nf));
            if (!nf) {
                fprintf(stderr, "Out of memory.\n");
                for (size_t i = 0; i < count; ++i) free(fonts[i]);
                free(fonts);
                closedir(dir);
                return 1;
            }
            fonts = nf;
        }
        fonts[count] = strdup(ent->d_name);
        if (!fonts[count]) {
            fprintf(stderr, "Out of memory.\n");
            for (size_t i = 0; i < count; ++i) free(fonts[i]);
            free(fonts);
            closedir(dir);
            return 1;
        }
        ++count;
    }
    closedir(dir);

    if (count == 0) {
        fprintf(stderr, "No .psf fonts found in %s\n", fonts_dir);
        free(fonts);
        return 1;
    }

    qsort(fonts, count, sizeof(*fonts), cmpstr);

    printf("Available .psf fonts in %s:\n\n", fonts_dir);
    for (size_t i = 0; i < count; ++i) {
        printf("%3zu) %s\n", i + 1, fonts[i]);
    }

    size_t choice = 0;
    while (1) {
        printf("\nSelect a font by number (1-%zu): ", count);
        fflush(stdout);
        char line[128];
        if (!fgets(line, sizeof(line), stdin)) {
            fprintf(stderr, "Input error.\n");
            goto cleanup_err;
        }
        char *endp = NULL;
        long v = strtol(line, &endp, 10);
        if (endp == line || v < 1 || v > (long)count) {
            printf("Invalid selection. Try again.\n");
            continue;
        }
        choice = (size_t)v;
        break;
    }

    const char *selected = fonts[choice - 1];

    int use_double = 0;
    while (1) {
        printf("Use original size or double? [o/d]: ");
        fflush(stdout);
        int c = getchar();
        if (c == EOF) {
            fprintf(stderr, "Input error.\n");
            goto cleanup_err;
        }
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) { }

        if (c == 'o' || c == 'O') { use_double = 0; break; }
        if (c == 'd' || c == 'D') { use_double = 1; break; }
        printf("Please enter 'o' or 'd'.\n");
    }

    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", fonts_dir, selected) >= (int)sizeof(path)) {
        fprintf(stderr, "Path too long.\n");
        goto cleanup_err;
    }

    {
        int rc = run_setfont(path, use_double);
        for (size_t i = 0; i < count; ++i) free(fonts[i]);
        free(fonts);
        return rc;
    }

cleanup_err:
    for (size_t i = 0; i < count; ++i) free(fonts[i]);
    free(fonts);
    return 1;
}
