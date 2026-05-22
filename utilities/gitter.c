#include <stdio.h>

/*
 * Beginner-first Git reference:
 * keep only the minimum commands needed for daily work.
 */
int main(void)
{
    printf(
        "\n"
        "==================== GITTER: GIT BASICS FOR YOUR DAILY JOB ====================\n"
        "\n"
        "1) First check: \"what is going on?\"\n"
        "  git status\n"
        "    Show changed files, staged files, and current branch.\n"
        "\n"
        "2) Save your work (commit)\n"
        "  git add <file>\n"
        "    Put file into the next commit.\n"
        "  git commit -m \"what you changed\"\n"
        "    Save snapshot with a clear message.\n"
        "\n"
        "3) Get latest team changes\n"
        "  git pull\n"
        "    Download and merge latest remote changes.\n"
        "\n"
        "4) Start a new task safely (branch)\n"
        "  git switch -c my-task\n"
        "    Create and switch to a new branch.\n"
        "\n"
        "5) Undo simple mistakes\n"
        "  git restore <file>\n"
        "    Throw away local edits in one file.\n"
        "  git restore --staged <file>\n"
        "    Unstage file, keep your edits.\n"
        "  git reset HEAD~1\n"
        "    Undo last local commit, keep the file changes.\n"
        "\n"
        "6) If you already pushed a bad commit\n"
        "  git revert <commit-id>\n"
        "    Make a new commit that undoes the bad one.\n"
        "\n"
        "7) Send your branch to remote\n"
        "  git push -u origin my-task\n"
        "    Push branch first time and set upstream.\n"
        "\n"
        "8) When Git says \"conflict\"\n"
        "  git status\n"
        "    Shows files you must fix.\n"
        "  (edit files, remove conflict markers)\n"
        "  git add <fixed-file>\n"
        "  git commit\n"
        "    Finish merge after conflicts are fixed.\n"
        "\n"
        "Simple rules\n"
        "  - Run 'git status' often.\n"
        "  - Do not use force push unless a senior asks you.\n"
        "  - Write commit messages that explain the change.\n"
        "  - Build and test before push.\n"
        "===============================================================================\n"
        "\n");

    return 0;
}
