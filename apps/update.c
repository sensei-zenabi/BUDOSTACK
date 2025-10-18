#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct BranchList {
    char **names;
    size_t count;
};

static void free_branch_list(struct BranchList *list) {
    if (list == NULL || list->names == NULL) {
        return;
    }

    for (size_t i = 0; i < list->count; ++i) {
        free(list->names[i]);
    }
    free(list->names);
    list->names = NULL;
    list->count = 0;
}

static int append_branch(struct BranchList *list, const char *name) {
    if (list == NULL || name == NULL) {
        return -1;
    }

    char **new_items = realloc(list->names, (list->count + 1) * sizeof(*new_items));
    if (new_items == NULL) {
        perror("realloc");
        return -1;
    }
    list->names = new_items;

    size_t length = strlen(name);
    list->names[list->count] = malloc(length + 1);
    if (list->names[list->count] == NULL) {
        perror("malloc");
        return -1;
    }

    memcpy(list->names[list->count], name, length + 1);
    list->count += 1;
    return 0;
}

static int run_system_command(const char *command, const char *friendly_name) {
    if (command == NULL || friendly_name == NULL) {
        fprintf(stderr, "Internal error: missing command description.\n");
        return -1;
    }

    printf("\n%s\n", friendly_name);
    fflush(stdout);

    int status = system(command);
    if (status == -1) {
        perror("system");
        fprintf(stderr, "Unable to run '%s'.\n", command);
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        fprintf(stderr, "The step '%s' did not finish successfully (code %d).\n", friendly_name, code);
        return -1;
    }

    printf("Done.\n");
    return 0;
}

static int git_available(void) {
    int status = system("git --version > /dev/null 2>&1");
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Git is required but not available. Please install Git and try again.\n");
        return -1;
    }
    return 0;
}

static int detect_repository_root(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return -1;
    }

    FILE *pipe = popen("git rev-parse --show-toplevel 2>/dev/null", "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    if (fgets(buffer, (int)size, pipe) == NULL) {
        fprintf(stderr, "This tool must be run inside the BUDOSTACK repository.\n");
        pclose(pipe);
        return -1;
    }
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    int exit_status = pclose(pipe);
    if (exit_status == -1) {
        perror("pclose");
        return -1;
    }
    if (!WIFEXITED(exit_status) || WEXITSTATUS(exit_status) != 0) {
        fprintf(stderr, "Failed to determine the repository root.\n");
        return -1;
    }

    if (chdir(buffer) != 0) {
        perror("chdir");
        fprintf(stderr, "Unable to switch to repository root '%s'.\n", buffer);
        return -1;
    }
    return 0;
}

static int check_worktree_clean(bool *dirty) {
    if (dirty == NULL) {
        return -1;
    }
    *dirty = false;

    FILE *pipe = popen("git status --porcelain", "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    char line[256];
    if (fgets(line, sizeof(line), pipe) != NULL) {
        *dirty = true;
    }

    int status = pclose(pipe);
    if (status == -1) {
        perror("pclose");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Unable to inspect repository status.\n");
        return -1;
    }

    return 0;
}

static bool ask_yes_no(const char *question) {
    char answer[16];

    while (true) {
        printf("%s [y/n]: ", question);
        fflush(stdout);
        if (fgets(answer, sizeof(answer), stdin) == NULL) {
            return false;
        }
        size_t len = strlen(answer);
        if (len > 0 && answer[len - 1] == '\n') {
            answer[len - 1] = '\0';
            len -= 1;
        }
        if (len == 0) {
            continue;
        }
        char c = (char)tolower((unsigned char)answer[0]);
        if (c == 'y') {
            return true;
        }
        if (c == 'n') {
            return false;
        }
        printf("Please answer with 'y' or 'n'.\n");
    }
}

static int fetch_release_branches(struct BranchList *list) {
    if (list == NULL) {
        return -1;
    }

    FILE *pipe = popen("git for-each-ref --format='%(refname:short)' 'refs/remotes/origin/release*' 2>/dev/null", "r");
    if (pipe == NULL) {
        perror("popen");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[len - 1] = '\0';
            len -= 1;
        }
        if (len == 0) {
            continue;
        }

        const char *name = line;
        const char prefix[] = "origin/";
        size_t prefix_len = sizeof(prefix) - 1;
        if (strncmp(line, prefix, prefix_len) == 0) {
            name = line + prefix_len;
        }

        if (append_branch(list, name) != 0) {
            pclose(pipe);
            return -1;
        }
    }

    int status = pclose(pipe);
    if (status == -1) {
        perror("pclose");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Unable to read release branches from the remote repository.\n");
        return -1;
    }

    return 0;
}

static int prompt_main_choice(bool have_release) {
    char input[32];

    while (true) {
        printf("\nPlease choose how you would like to update BUDOSTACK:\n");
        printf("  1) Latest features (main branch)\n");
        if (have_release) {
            printf("  2) Stable release (pick from official release branches)\n");
        }
        printf("  q) Cancel and return to the previous menu\n");
        printf("Your choice: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            return 0;
        }
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        if (strcmp(input, "1") == 0) {
            return 1;
        }
        if (have_release && strcmp(input, "2") == 0) {
            return 2;
        }
        if (strcmp(input, "q") == 0 || strcmp(input, "Q") == 0) {
            return 0;
        }

        printf("I did not understand that choice. Please try again.\n");
    }
}

static int prompt_release_selection(const struct BranchList *list) {
    if (list == NULL || list->count == 0) {
        return -1;
    }

    char input[32];
    while (true) {
        printf("\nAvailable release branches:\n");
        for (size_t i = 0; i < list->count; ++i) {
            printf("  %zu) %s\n", i + 1, list->names[i]);
        }
        printf("  0) Go back\n");
        printf("Enter the number of the release you want to use: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            return -1;
        }
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }
        if (len == 0) {
            continue;
        }

        char *endptr = NULL;
        errno = 0;
        long value = strtol(input, &endptr, 10);
        if (errno != 0 || endptr == input || *endptr != '\0') {
            printf("Please enter a valid number from the list.\n");
            continue;
        }
        if (value == 0) {
            return -1;
        }
        if (value < 0 || value > (long)list->count) {
            printf("That number is not in the list.\n");
            continue;
        }
        return (int)(value - 1);
    }
}

static int checkout_main_branch(void) {
    if (run_system_command("git checkout main 2>&1", "Switching to the main branch...") != 0) {
        fprintf(stderr, "Unable to switch to the main branch.\n");
        return -1;
    }
    if (run_system_command("git pull --ff-only origin main 2>&1", "Downloading the latest main branch changes...") != 0) {
        fprintf(stderr, "Unable to update the main branch.\n");
        return -1;
    }
    return 0;
}

static int checkout_release_branch(const char *branch) {
    if (branch == NULL) {
        return -1;
    }

    char command[PATH_MAX];
    int written = snprintf(command, sizeof(command), "git checkout -B %s origin/%s 2>&1", branch, branch);
    if (written < 0 || written >= (int)sizeof(command)) {
        fprintf(stderr, "Branch name '%s' is too long.\n", branch);
        return -1;
    }
    if (run_system_command(command, "Preparing the selected release branch...") != 0) {
        fprintf(stderr, "Unable to switch to release branch '%s'.\n", branch);
        return -1;
    }

    return 0;
}

static void print_intro(void) {
    printf("==============================================\n");
    printf(" Welcome to the BUDOSTACK Update Assistant\n");
    printf("==============================================\n\n");
    printf("This helper will guide you through updating BUDOSTACK\n");
    printf("without needing any Git knowledge.\n\n");
}

int main(void) {
    print_intro();

    if (git_available() != 0) {
        return EXIT_FAILURE;
    }

    char repo_root[PATH_MAX];
    if (detect_repository_root(repo_root, sizeof(repo_root)) != 0) {
        return EXIT_FAILURE;
    }

    printf("Using repository at: %s\n", repo_root);

    if (run_system_command("git fetch --tags --prune origin 2>&1", "Checking GitHub for available updates...") != 0) {
        return EXIT_FAILURE;
    }

    bool dirty = false;
    if (check_worktree_clean(&dirty) != 0) {
        return EXIT_FAILURE;
    }

    if (dirty) {
        printf("\n⚠️  You have local changes that are not committed.\n");
        printf("These changes could be overwritten by the update.\n");
        if (!ask_yes_no("Do you want to continue anyway")) {
            printf("Update cancelled. Your files were left untouched.\n");
            return EXIT_SUCCESS;
        }
    }

    struct BranchList releases = {0};
    if (fetch_release_branches(&releases) != 0) {
        free_branch_list(&releases);
        return EXIT_FAILURE;
    }

    if (releases.count == 0U) {
        printf("\nNo release branches were found on the remote. You can still update to the main branch.\n");
    }

    int choice = prompt_main_choice(releases.count > 0U);
    if (choice == 0) {
        printf("No changes were made.\n");
        free_branch_list(&releases);
        return EXIT_SUCCESS;
    }

    if (choice == 1) {
        printf("\nYou chose to update to the newest features from the main branch.\n");
        if (checkout_main_branch() != 0) {
            free_branch_list(&releases);
            return EXIT_FAILURE;
        }
    } else if (choice == 2) {
        int index = prompt_release_selection(&releases);
        if (index < 0) {
            printf("No changes were made.\n");
            free_branch_list(&releases);
            return EXIT_SUCCESS;
        }
        printf("\nYou chose release branch: %s\n", releases.names[index]);
        if (checkout_release_branch(releases.names[index]) != 0) {
            free_branch_list(&releases);
            return EXIT_FAILURE;
        }
    }

    free_branch_list(&releases);

    printf("\nUpdating build files...\n");
    if (run_system_command("make clean 2>&1", "Cleaning old build artifacts...") != 0) {
        fprintf(stderr, "Please resolve the issue above and run 'make clean' manually if needed.\n");
        return EXIT_FAILURE;
    }

    printf("\nTriggering the official restart command so BUDOSTACK can rebuild itself.\n");
    int restart_status = run_system_command("restart", "Restarting BUDOSTACK (this may take a few moments)...");
    if (restart_status != 0) {
        printf("\nThe automatic 'restart' command did not finish correctly.\n");
        printf("Attempting a fallback method to rebuild using the BUDOSTACK shell...\n");
        restart_status = run_system_command("printf 'restart\n' | ./budostack -f 2>&1", "Fallback restart in progress...");
    }

    if (restart_status != 0) {
        printf("\nManual action required: please run 'make' followed by './budostack' to start the updated system.\n");
        return EXIT_FAILURE;
    }

    printf("\nAll done! BUDOSTACK is rebuilding now. Once the restart completes, you can continue using the system.\n");
    return EXIT_SUCCESS;
}
