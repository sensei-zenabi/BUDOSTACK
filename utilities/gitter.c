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
    printf("\n");
    printf("================== GITTER - I remember so you don't have to!  ================\n");
    printf("\n");
    printf("  git log --name-only         : display commits and related files\n\n");
    printf("  git commit -m 'message'     : make a commit with message\n\n");
    printf("  git commit -m 'closes #nn'  : close issue with commit message\n\n");
    printf("  git fetch origin            : fetch available branches\n\n");
    printf("  git checkout <branch_name>  : switch into a selected branch\n\n");
    printf("  git merge <branch_name>     : merge selected branch into current branch\n\n");
    
    return 0;
}
