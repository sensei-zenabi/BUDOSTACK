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

#define FONTS_DIR "./fonts"
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

int main(void) {
    DIR *dir = opendir(FONTS_DIR);
    if (!dir) {
        fprintf(stderr, "Error: could not open %s: %s\n", FONTS_DIR, strerror(errno));
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
        fprintf(stderr, "No .psf fonts found in %s\n", FONTS_DIR);
        free(fonts);
        return 1;
    }

    qsort(fonts, count, sizeof(*fonts), cmpstr);

    printf("Available .psf fonts in %s:\n\n", FONTS_DIR);
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
    if (snprintf(path, sizeof(path), "%s/%s", FONTS_DIR, selected) >= (int)sizeof(path)) {
        fprintf(stderr, "Path too long.\n");
        goto cleanup_err;
    }

    printf("\nRunning: setfont %s %s\n\n", use_double ? "-d" : "", path);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        goto cleanup_err;
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
            goto cleanup_err;
        }
        if (WIFEXITED(status)) {
            int rc = WEXITSTATUS(status);
            if (rc == 0) {
                printf("Font applied successfully.\n");
            } else {
                printf("setfont exited with code %d.\n", rc);
            }
        } else if (WIFSIGNALED(status)) {
            printf("setfont terminated by signal %d.\n", WTERMSIG(status));
        } else {
            printf("setfont ended abnormally.\n");
        }
    }

    for (size_t i = 0; i < count; ++i) free(fonts[i]);
    free(fonts);
    return 0;

cleanup_err:
    for (size_t i = 0; i < count; ++i) free(fonts[i]);
    free(fonts);
    return 1;
}
