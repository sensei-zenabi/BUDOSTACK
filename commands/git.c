#define _POSIX_C_SOURCE 200809L
/*
 * git.c
 *
 * Lightweight wrapper around the system `git` binary providing a few
 * convenience shortcuts while still allowing full access to standard
 * git functionality.  When none of the custom sub‑commands match, the
 * arguments are forwarded verbatim to `/usr/bin/git`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

typedef struct {
    char *name;
    int count;
} Entry;

static void add_entry(Entry **arr, int *used, int *cap, const char *name) {
    for (int i = 0; i < *used; ++i) {
        if (strcmp((*arr)[i].name, name) == 0) {
            (*arr)[i].count++;
            return;
        }
    }
    if (*used == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *arr = realloc(*arr, *cap * sizeof(Entry));
    }
    (*arr)[*used].name = strdup(name);
    (*arr)[*used].count = 1;
    (*used)++;
}

static int cmp_count_desc(const void *a, const void *b) {
    const Entry *ea = a, *eb = b;
    return eb->count - ea->count;
}

static int cmp_key_desc(const void *a, const void *b) {
    const Entry *ea = a, *eb = b;
    return strcmp(eb->name, ea->name);
}

static void free_entries(Entry *arr, int used) {
    for (int i = 0; i < used; ++i)
        free(arr[i].name);
    free(arr);
}

static int is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void forward_to_git(int argc, char *argv[]) {
    (void)argc;
    argv[0] = "git";                 /* ensure argv[0] is just "git" */
    execvp("/usr/bin/git", argv);
    perror("execvp failed");
    exit(EXIT_FAILURE);
}

static int show_commits_per_day(void) {
    FILE *fp = popen("/usr/bin/git log --date=format:%Y-%m-%d --pretty=format:%ad", "r");
    if (!fp) {
        perror("popen");
        return EXIT_FAILURE;
    }
    Entry *arr = NULL; int used = 0, cap = 0;
    char line[128];
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0])
            add_entry(&arr, &used, &cap, line);
    }
    pclose(fp);
    if (used > 0)
        qsort(arr, used, sizeof(Entry), cmp_key_desc);
    printf("\nNumber of Commits per Day:\n");
    for (int i = 0; i < used; ++i)
        printf("%s: %d\n", arr[i].name, arr[i].count);
    free_entries(arr, used);
    return EXIT_SUCCESS;
}

static int show_commits_per_file(void) {
    FILE *fp = popen("/usr/bin/git log --pretty=format: --name-only", "r");
    if (!fp) {
        perror("popen");
        return EXIT_FAILURE;
    }
    Entry *arr = NULL; int used = 0, cap = 0;
    char line[1024];
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0])
            add_entry(&arr, &used, &cap, line);
    }
    pclose(fp);
    if (used > 0)
        qsort(arr, used, sizeof(Entry), cmp_count_desc);
    printf("\nNumber of Commits per File:\n");
    for (int i = 0; i < used; ++i)
        printf("%s: %d\n", arr[i].name, arr[i].count);
    free_entries(arr, used);
    return EXIT_SUCCESS;
}

static void print_help(const char *prog_name) {
    const char *base_name = strrchr(prog_name, '/');
    base_name = base_name ? base_name + 1 : prog_name;

    printf("Usage:\n");
    printf("  %s                : Equivalent to 'git status'\n", base_name);
    printf("  %s <file>         : Show log for <file>\n", base_name);
    printf("  %s changes        : Display all commits with stats\n", base_name);
    printf("  %s commits        : Display commit counts per file\n", base_name);
    printf("  %s rate           : Display commit counts per day\n", base_name);
    printf("  %s [git args...]  : Forward other arguments to git\n", base_name);
    printf("  %s -h, --help     : Display this help message\n", base_name);
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        char *args[] = {"git", "status", NULL};
        execvp("/usr/bin/git", args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    }

    if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-help") == 0) {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[1], "changes") == 0) {
            char *args[] = {"git", "log", "--stat", "--graph", NULL};
            execvp("/usr/bin/git", args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        if (strcmp(argv[1], "rate") == 0)
            return show_commits_per_day();
        if (strcmp(argv[1], "commits") == 0)
            return show_commits_per_file();
        if (is_file(argv[1])) {
            char *args[] = {"git", "log", "--stat", "--graph", "--follow", "--", argv[1], NULL};
            execvp("/usr/bin/git", args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
    }

    forward_to_git(argc, argv);
    return EXIT_SUCCESS; /* not reached */
}
