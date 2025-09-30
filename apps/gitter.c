// gitter-lite.c - Simple POSIX-only Git console (numbers-only input)
// Build:   gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 -o gitter-lite gitter-lite.c
// Run:     ./gitter-lite
// Notes:   No Windows code paths. All actions use numeric selections only
//          (no free-form text input). Scope is intentionally smaller.
//
// Features:
//   1) Status (short)
//   2) Log (recent)
//   3) List local branches
//   4) Switch branch (choose from list)
//   5) Fetch --all --prune
//   6) Pull (choose mode)
//   7) Push (normal or set-upstream to origin/<current>)
//   8) Stage files (choose from modified/untracked list)
//   9) Unstage files (choose from staged list)
//  10) Discard changes (all or choose files)
//  11) Diff a file (choose from modified list)
//  12) Commit (choose a canned message)
//  13) Format changed C/C++ files with clang-format (if available)
//   0) Exit

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define CLEAR_CMD "clear"
#define BUF_SZ 4096
#define MAX_ITEMS 2048

static void press_enter(void){
    printf("\nPress ENTER to continue...");
    fflush(stdout);
    int c; while((c=getchar())!='\n' && c!=EOF){}
}

static void trim_newline(char *s){
    if(!s) return;
    size_t n = strlen(s);
    if(n && s[n-1]=='\n') {
        s[n-1] = '\0';
    }
}

static int run_pipe(const char *cmd){
    FILE *p=popen(cmd,"r"); if(!p){ fprintf(stderr,"Failed: %s (errno %d)\n",cmd,errno); return -1; }
    char buf[BUF_SZ]; while(fgets(buf,sizeof(buf),p)){ fputs(buf,stdout);} int rc=pclose(p);
    return (rc==-1)?-1:(WIFEXITED(rc)?WEXITSTATUS(rc):rc);
}

static int run_sys(const char *cmd){
    int rc=system(cmd); if(rc==-1){ fprintf(stderr,"Failed to exec: %s\n",cmd); return -1; }
    return WIFEXITED(rc)?WEXITSTATUS(rc):rc;
}

static int ensure_git_repo(void){
    int rc=system("git rev-parse --is-inside-work-tree >/dev/null 2>&1");
    if(rc!=0){ puts("Not a Git repository. Run inside a repo."); return 0; }
    return 1;
}

static void header(void){
    run_sys(CLEAR_CMD);
    puts("====================================");
    puts("            GITTER-LITE             ");
    puts("      POSIX Git Console (C11)       ");
    puts("      Numbers-only interactions     ");
    puts("====================================\n");
}

static int read_choice(int minv, int maxv){
    // Reads an integer on a single line. Non-numeric input is treated as invalid.
    char line[64];
    if(!fgets(line,sizeof(line),stdin)) return -1;
    trim_newline(line);
    // skip leading spaces
    char *p=line; while(*p && isspace((unsigned char)*p)) ++p;
    if(!*p) return -1;
    char *end=NULL; long v=strtol(p,&end,10);
    if(end==p || *end!='\0') return -1;
    if(v<minv || v>maxv) return -1;
    return (int)v;
}

// --- list helpers ----------------------------------------------------------

typedef struct { char *items[MAX_ITEMS]; int count; } list_t;

static void list_free(list_t *L){
    for(int i=0;i<L->count;++i) free(L->items[i]);
    L->count=0;
}

static int list_from_cmd(list_t *L, const char *cmd){
    L->count=0; FILE *p=popen(cmd,"r"); if(!p) return -1; char line[2048];
    while(fgets(line,sizeof(line),p)){
        trim_newline(line);
        if(line[0]=='\0') continue;
        if(L->count>=MAX_ITEMS) break;
        L->items[L->count]=strdup(line);
        if(!L->items[L->count]){ pclose(p); return -1; }
        L->count++;
    }
    pclose(p); return 0;
}

static int current_branch(char *out, size_t outsz){
    FILE *p=popen("git rev-parse --abbrev-ref HEAD 2>/dev/null","r"); if(!p) return -1;
    if(!fgets(out,(int)outsz,p)){ pclose(p); return -1; } trim_newline(out); pclose(p); return 0;
}

// --- actions ---------------------------------------------------------------

static void status_short(void){ puts(""); run_pipe("git status -sb"); }

static void show_log(void){
    puts("\nRecent commits (last 30):\n");
    const char *fmt=
        "git log --graph --decorate --abbrev-commit --date=relative "
        "--pretty=format:'%C(yellow)%h%Creset - %s %Cgreen(%cr)%Creset %C(cyan)<%an>%Creset %C(auto)%d%Creset' "
        "-n 30";
    run_pipe(fmt);
}

static void list_branches(void){
    puts("\nLocal branches (current marked with *):\n");
    run_pipe("git branch -vv");
}

static void switch_branch(void){
    list_t L; if(list_from_cmd(&L, "git for-each-ref --format='%(refname:short)' refs/heads")!=0){
        puts("Failed to get branches."); return; }
    if(L.count==0){ puts("No local branches."); return; }
    puts("\nSelect branch to switch to:\n");
    for(int i=0;i<L.count;++i){ printf(" %2d) %s\n", i+1, L.items[i]); }
    printf("\nYour choice [1-%d, 0=cancel]: ", L.count);
    int ch=read_choice(0,L.count); if(ch<=0){ list_free(&L); puts("Canceled."); return; }
    char cmd[1024]; snprintf(cmd,sizeof(cmd),"git switch %s", L.items[ch-1]);
    run_sys(cmd);
    list_free(&L);
}

static void fetch_prune(void){
    puts("\nFetching all remotes (prune stale)...\n"); run_pipe("git fetch --all --prune");
}

static void pull_menu(void){
    puts("\nPull:\n  1) git pull\n  2) git pull --rebase\n  3) fetch + rebase\n  0) cancel\n");
    printf("Choose: "); int ch=read_choice(0,3);
    if(ch==1) run_pipe("git pull");
    else if(ch==2) run_pipe("git pull --rebase");
    else if(ch==3){ run_pipe("git fetch --all --prune"); run_pipe("git rebase"); }
    else puts("Canceled.");
}

static void push_menu(void){
    char br[256]; if(current_branch(br,sizeof(br))!=0){ puts("Cannot get current branch."); return; }
    puts("\nPush:\n  1) git push\n  2) git push -u origin <current>\n  0) cancel\n");
    printf("Choose: "); int ch=read_choice(0,2);
    if(ch==1) run_pipe("git push");
    else if(ch==2){ char cmd[512]; snprintf(cmd,sizeof(cmd),"git push -u origin %s", br); run_sys(cmd);} else puts("Canceled.");
}

static void list_modified_untracked(list_t *L){
    // Show modified and untracked (working tree), one path per line
    // Using: git ls-files -m -o --exclude-standard
    list_from_cmd(L, "git ls-files -m -o --exclude-standard");
}

static void list_staged(list_t *L){
    // Staged files
    list_from_cmd(L, "git diff --cached --name-only");
}

static void choose_many_and_apply(list_t *L, const char *prefix_cmd){
    if(L->count==0){ puts("(none)"); return; }
    puts("Select files by number, separated by spaces. 0 to finish.");
    for(int i=0;i<L->count;++i) printf(" %2d) %s\n", i+1, L->items[i]);
    printf("\nLine: ");
    // Read a line of numbers and apply command to each chosen file
    char line[4096]; if(!fgets(line,sizeof(line),stdin)) return; trim_newline(line);
    const char *delims = " \t,;:";
    char *tok=strtok(line, delims);
    while(tok){
        int idx=atoi(tok);
        if(idx==0) break;
        if(idx>=1 && idx<=L->count){
            char cmd[2048]; snprintf(cmd,sizeof(cmd),"%s -- '%s'", prefix_cmd, L->items[idx-1]);
            run_sys(cmd);
        }
        tok=strtok(NULL, delims);
    }
}

static void stage_files(void){
    list_t L; list_modified_untracked(&L);
    puts("\nStage files (working tree):\n");
    if(L.count==0){ puts("Nothing to stage."); return; }
    choose_many_and_apply(&L, "git add");
    list_free(&L);
}

static void unstage_files(void){
    list_t L; list_staged(&L);
    puts("\nUnstage files (index):\n");
    if(L.count==0){ puts("Nothing staged."); return; }
    choose_many_and_apply(&L, "git reset");
    list_free(&L);
}

static void discard_changes(void){
    puts("\nDiscard changes:\n  1) Reset ALL to HEAD (HARD)\n  2) Restore selected files\n  0) cancel\n");
    printf("Choose: "); int ch=read_choice(0,2);
    if(ch==1){
        puts("This CANNOT be undone.");
        puts("  1) YES, reset --hard\n  0) No");
        printf("Confirm: "); int c=read_choice(0,1);
        if(c==1) run_pipe("git reset --hard HEAD"); else puts("Aborted.");
        return;
    }
    if(ch==2){
        list_t L; list_modified_untracked(&L);
        puts("\nSelect files to restore from index/HEAD:\n");
        if(L.count==0){ puts("Nothing to restore."); return; }
        choose_many_and_apply(&L, "git restore --staged --worktree");
        list_free(&L);
        return;
    }
    puts("Canceled.");
}

static void diff_file(void){
    list_t L; list_modified_untracked(&L);
    puts("\nDiff a file:\n");
    if(L.count==0){ puts("Nothing to diff."); return; }
    for(int i=0;i<L.count;++i) printf(" %2d) %s\n", i+1, L.items[i]);
    printf("\nChoose [1-%d, 0=cancel]: ", L.count);
    int ch=read_choice(0,L.count); if(ch<=0){ list_free(&L); puts("Canceled."); return; }
    char cmd[2048]; snprintf(cmd,sizeof(cmd),"git diff -- '%s'", L.items[ch-1]);
    run_pipe(cmd);
    list_free(&L);
}

static void commit_templates(void){
    static const char *msgs[]={
        "wip",
        "update",
        "fix",
        "refactor",
        "docs",
        "chore",
        "test",
    };
    const int N=(int)(sizeof(msgs)/sizeof(msgs[0]));
    puts("\nCommit (choose a canned message):\n");
    puts("  1) wip\n  2) update\n  3) fix\n  4) refactor\n  5) docs\n  6) chore\n  7) test\n  8) Amend (no-edit)\n  0) cancel\n");
    printf("Choose: "); int ch=read_choice(0,8);
    if(ch>=1 && ch<=N){ char cmd[256]; snprintf(cmd,sizeof(cmd),"git commit -m '%s'", msgs[ch-1]); run_sys(cmd); return; }
    if(ch==8){ run_pipe("git commit --amend --no-edit"); return; }
    puts("Canceled.");
}

static int tool_exists(const char *exe){
    char cmd[512]; snprintf(cmd,sizeof(cmd),"command -v %s >/dev/null 2>&1", exe);
    return run_sys(cmd)==0;
}

static int is_c_cpp(const char *p){
    const char *dot=strrchr(p,'.'); if(!dot) return 0;
    return strcmp(dot, ".c")==0 || strcmp(dot, ".h")==0 || strcmp(dot, ".hpp")==0 || strcmp(dot, ".hh")==0 || strcmp(dot, ".cpp")==0 || strcmp(dot, ".cc")==0 || strcmp(dot, ".cxx")==0;
}

static void format_changed(void){
    if(!tool_exists("clang-format")){ puts("clang-format not found."); return; }
    list_t L; list_from_cmd(&L, "git diff --name-only HEAD");
    if(L.count==0){ puts("No changed files vs HEAD."); return; }
    puts("\nFormatting changed C/C++ files...\n");
    for(int i=0;i<L.count;++i){ if(is_c_cpp(L.items[i])){ char cmd[2048]; snprintf(cmd,sizeof(cmd),"clang-format -i -- '%s'", L.items[i]); run_sys(cmd);} }
    list_free(&L);
}

static void menu(void){
    puts("Choose an action:\n");
    puts(" 1) Status (short)");
    puts(" 2) Log (recent)");
    puts(" 3) List branches");
    puts(" 4) Switch branch");
    puts(" 5) Fetch & prune");
    puts(" 6) Pull");
    puts(" 7) Push");
    puts(" 8) Stage files");
    puts(" 9) Unstage files");
    puts("10) Discard changes");
    puts("11) Diff file");
    puts("12) Commit (canned message)");
    puts("13) Format changed (clang-format)");
    puts(" 0) Exit\n");
}

int main(void){
    if(!ensure_git_repo()) return 1;
    for(;;){
        header();
        menu();
        printf("\nYour choice: ");
        int sel=read_choice(0,13);
        switch(sel){
            case 1: status_short(); break;
            case 2: show_log(); break;
            case 3: list_branches(); break;
            case 4: switch_branch(); break;
            case 5: fetch_prune(); break;
            case 6: pull_menu(); break;
            case 7: push_menu(); break;
            case 8: stage_files(); break;
            case 9: unstage_files(); break;
            case 10: discard_changes(); break;
            case 11: diff_file(); break;
            case 12: commit_templates(); break;
            case 13: format_changed(); break;
            case 0: puts("Bye!"); return 0;
            default: puts("Invalid choice."); break;
        }
        press_enter();
    }
    return 0;
}
