#include <stdio.h>
#include <string.h>

/*
 * Design Principles:
 * - Written in plain C using -std=c11 and only standard libraries.
 * - No header files are created; everything is contained in a single file.
 * - The code includes comments to clarify design decisions.
 * - If the program is started with the "-a" argument (i.e. "help -a"),
 *   a reserved section is printed for future hidden features or advanced help.
 */
int main() {
  // Print header information
  printf("\n"
         "================== GITTER - I remember so you don't have to!  ================\n"
         "\n"
         "  git log --name-only         : display commits and related files\n\n"
         "  git commit -m 'message'     : make a commit with message\n\n"
         "  git commit -m 'closes #nn'  : close issue with commit message\n\n"
         "  git fetch origin            : fetch available branches\n\n"
         "  git checkout <branch_name>  : switch into a selected branch\n\n"
         "  git merge <branch_name>     : merge selected branch into current branch\n\n"
         "  git tag --sort=-v:refname   : list tags from newest to oldest\n\n"
         "  git revert <commit_hash>    : reverts a bad commit, do 'git push' afterwards\n\n"
         "  git ls-files --others       : display non tracked files\n\n"
         "                              : force branch as new main HEAD\n"
         "  git checkout main\n"
         "  git reset --hard feature/new-architecture\n"
         "  git push --force origin main\n\n");
                
  return 0;
}
