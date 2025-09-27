// gitter.c - Pro-level single-file Git & Dev Console (ASCII-only, C11)
// Build:   gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 -o gitter gitter.c
// Run:     ./gitter        # interactive TUI
//          ./gitter -help  # detailed help of all actions
// Notes:   Uses popen/system to call Git and common dev tools (cmake/make/ctest/clang-format/cppcheck).
//          Designed for POSIX; basic Windows support via _popen/_pclose.

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifndef _WIN32
#include <sys/wait.h>
#else
#include <process.h>
#define popen  _popen
#define pclose _pclose
#endif

#ifdef _WIN32
#define CLEAR_CMD "cls"
#else
#define CLEAR_CMD "clear"
#endif

#define BUF_SZ 4096

static void press_enter(void){
    printf("\nPress ENTER to continue...");
    fflush(stdout);
    int c; while((c=getchar())!='\n' && c!=EOF){}
}

static void trim_newline(char *s){
    if(!s) return;
    size_t n = strlen(s);
    if(n && s[n-1]=='\n') s[n-1]='\0';
}

static void chomp(char *s){
    if(!s) return;
    size_t i=0, j=strlen(s);
    while(i<j && isspace((unsigned char)s[i])) i++;
    while(j>i && isspace((unsigned char)s[j-1])) j--;
    if(i>0) memmove(s, s+i, j-i);
    s[j-i]='\0';
}

static int run_pipe(const char *cmd){
    FILE *p = popen(cmd, "r");
    if(!p){
        fprintf(stderr, "Failed to run command: %s (errno %d)\n", cmd, errno);
        return -1;
    }
    char buf[BUF_SZ];
    while(fgets(buf, sizeof(buf), p)){
        fputs(buf, stdout);
    }
    int rc = pclose(p);
#ifdef _WIN32
    return rc;
#else
    return (rc == -1) ? -1 : (WIFEXITED(rc) ? WEXITSTATUS(rc) : rc);
#endif
}

static int run_capture_first_line(const char *cmd, char *out, size_t outsz){
    if(!out || outsz==0) return -1;
    out[0] = '\0';
    FILE *p = popen(cmd, "r");
    if(!p) return -1;
    if(fgets(out, (int)outsz, p)){
        trim_newline(out);
        chomp(out);
    }
    int rc = pclose(p);
#ifdef _WIN32
    (void)rc;
    return out[0] ? 0 : -1;
#else
    return (rc == -1) ? -1 : (out[0] ? 0 : -1);
#endif
}

static int run_sys(const char *cmd){
    int rc = system(cmd);
    if(rc == -1){
        fprintf(stderr, "Failed to execute system command: %s\n", cmd);
        return -1;
    }
#ifdef _WIN32
    return rc;
#else
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
#endif
}

static int tool_exists(const char *exe){
    char cmd[512];
#ifdef _WIN32
    if(snprintf(cmd,sizeof(cmd),"where %s >NUL 2>NUL", exe) >= (int)sizeof(cmd)) return 0;
#else
    if(snprintf(cmd,sizeof(cmd),"command -v %s >/dev/null 2>&1", exe) >= (int)sizeof(cmd)) return 0;
#endif
    int rc = run_sys(cmd);
    return (rc==0);
}

static int ensure_git_repo(void){
    int rc = run_sys("git rev-parse --is-inside-work-tree > /dev/null 2>&1");
    if(rc != 0){
        puts("This does not look like a Git repository. Run this tool inside a repo.");
        return 0;
    }
    return 1;
}

static void show_header(void){
    run_sys(CLEAR_CMD);
    puts("====================================");
    puts("              GITTER               ");
    puts("    Pro Git & Dev Console (C11)    ");
    puts("====================================\n");
}

static void print_help(void){
    puts("gitter -help\n");
    puts("This tool wraps common Git and C developer workflows in a simple console UI.\n");
    puts("Menu reference:");
    puts("  1) List branches - Local (-vv) and remote (-r).");
    puts("  2) Switch branch - Prefer 'git switch', fallback to 'git checkout'.");
    puts("  3) Compare branches - 'git diff --stat A..B' and optional full patch.");
    puts("  4) Discard changes - Reset all to HEAD, or restore/checkout a single file.");
    puts("  5) Merge (advanced) - Merge source->current or current->target, FF or --no-ff.");
    puts("  6) Log - Pretty graph with files changed, last 50.");
    puts("  7) Fetch & prune - 'git fetch --all --prune'.");
    puts("  8) Status - Short branch+status 'git status -sb'.");
    puts("  9) Stage/Unstage - Add all/one; reset all/one.");
    puts(" 10) Commit - Single-line commit message.");
    puts(" 11) Pull - Pull, pull --rebase, or fetch+rebase.");
    puts(" 12) Push - Push or set upstream 'git push -u origin <branch>'.");
    puts(" 13) Stash - Save/apply/pop/list/drop/clear; optional message.");
    puts(" 14) Create branch - From HEAD or specified base; switches to it.");
    puts(" 15) Delete branch - Safe (-d) or force (-D).");
    puts(" 16) Rebase current onto - Non-interactive 'git rebase <base>'.");
    puts(" 17) Cherry-pick - Apply a commit by hash onto current branch.");
    puts(" 18) Diff file - 'git diff -- <path>' against index/HEAD.");
    puts(" 19) Merge preview / squash - '--no-commit' preview or '--squash' merge.");
    puts(" 20) Amend last commit - With or without editing message.");
    puts(" 21) Revert commit - 'git revert <hash>' creates a new inverse commit.");
    puts(" 22) Upstream status - Show ahead/behind and lists incoming/outgoing commits.");
    puts(" 23) Clean workspace - 'git clean -fd' (or -fdx) with confirmations.");
    puts(" 24) Interactive rebase - Launch 'git rebase -i <base>' (uses $EDITOR).\n       After resolving, use 'git rebase --continue' or '--abort'.");
    puts(" 25) Bisect (guided) - Start/good/bad/next/log/reset walkthrough.");
    puts(" 26) Tags - List/create annotated/delete/push tags.");
    puts(" 27) Submodules - init/update --recursive; foreach pull.");
    puts(" 28) Worktrees - Add/list/prune worktrees.");
    puts(" 29) Build - Detect CMake/Make and build.");
    puts(" 30) Test - Run ctest or 'make test'/ctest based on build system.");
    puts(" 31) Format (clang-format) - Format changed C/C++ files.");
    puts(" 32) Lint (cppcheck) - Run cppcheck on source tree if available.\n");
    puts("Notes:");
    puts("- Destructive actions ask for confirmation.");
    puts("- Commands use your shell and tools; make sure they are installed.");
    puts("- Interactive operations (rebase -i, conflict resolves) open your configured editor.");
}

static void menu(void){
    puts("Choose an action:\n");
    puts(" 1) List branches (local & remote)");
    puts(" 2) Switch to a branch (checkout)");
    puts(" 3) Compare differences between branches");
    puts(" 4) Drop modified files (discard changes)");
    puts(" 5) Merge branches (advanced)");
    puts(" 6) Show clear git log (commits & files)");
    puts(" 7) Fetch & prune remotes");
    puts(" 8) Status (short)");
    puts(" 9) Stage/Unstage files");
    puts("10) Commit with message");
    puts("11) Pull (fast-forward / rebase)");
    puts("12) Push (with upstream if needed)");
    puts("13) Stash (save/apply/pop/list/drop)");
    puts("14) Create a new branch");
    puts("15) Delete a branch");
    puts("16) Rebase current onto another branch");
    puts("17) Cherry-pick a commit");
    puts("18) Show diff for a specific file");
    puts("19) Merge preview / squash");
    puts("20) Amend last commit");
    puts("21) Revert a commit");
    puts("22) Upstream status (ahead/behind)");
    puts("23) Clean workspace");
    puts("24) Interactive rebase (-i)");
    puts("25) Bisect (guided)");
    puts("26) Tags");
    puts("27) Submodules");
    puts("28) Worktrees");
    puts("29) Build");
    puts("30) Test");
    puts("31) Format (clang-format)");
    puts("32) Lint (cppcheck)\n");
    puts(" 0) Exit\n");
}

static void list_branches(void){
    puts("\nLocal branches (current marked with *):\n");
    run_pipe("git branch -vv");
    puts("\nRemote branches:\n");
    run_pipe("git branch -r");
}

static void switch_branch(void){
    char name[256];
    printf("Enter branch name to switch to: ");
    fflush(stdout);
    if(!fgets(name, sizeof(name), stdin)) return;
    trim_newline(name);
    chomp(name);
    if(!*name){
        puts("No branch specified.");
        return;
    }
    char cmd[300];
    if(snprintf(cmd, sizeof(cmd), "git switch %s", name) >= (int)sizeof(cmd)){
        puts("Branch name too long.");
        return;
    }
    int rc = run_sys(cmd);
    if(rc != 0){
        if(snprintf(cmd, sizeof(cmd), "git checkout %s", name) >= (int)sizeof(cmd)){
            puts("Branch name too long.");
            return;
        }
        run_sys(cmd);
    }
}

static void compare_branches(void){
    char a[256], b[256];
    printf("Base branch (e.g., main): ");
    if(!fgets(a,sizeof(a),stdin)) return;
    trim_newline(a);
    chomp(a);
    printf("Compare branch: ");
    if(!fgets(b,sizeof(b),stdin)) return;
    trim_newline(b);
    chomp(b);
    if(!*a || !*b){
        puts("Both branches are required.");
        return;
    }
    puts("\nSummary (files/insertions/deletions):\n");
    char cmd[600];
    if(snprintf(cmd, sizeof(cmd), "git diff --stat %s..%s", a, b) >= (int)sizeof(cmd)){
        puts("Branch names too long for diff.");
        return;
    }
    run_pipe(cmd);
    printf("\nShow full patch? (y/N): ");
    int c = getchar();
    if(c=='y' || c=='Y'){
        if(snprintf(cmd, sizeof(cmd), "git diff %s..%s", a, b) >= (int)sizeof(cmd)){
            puts("Branch names too long for diff.");
        } else {
            run_pipe(cmd);
        }
    }
    while(c!='\n' && c!=EOF) c=getchar();
}

static void drop_modified_files(void){
    puts("\nModified / staged files (git status --porcelain):\n");
    run_pipe("git status --porcelain");
    puts("\nOptions:");
    puts("  a) Discard ALL local changes (HARD RESET to HEAD)");
    puts("  s) Discard changes for a SINGLE file");
    puts("  q) Cancel\n");
    printf("Choose [a/s/q]: ");
    int choice = getchar();
    int tmp;
    while((tmp=getchar())!='\n' && tmp!=EOF){}
    if(choice=='a' || choice=='A'){
        puts("\nThis will reset the working tree and index to HEAD. This CANNOT be undone.");
        printf("Type 'YES' to confirm: ");
        char conf[8];
        if(!fgets(conf,sizeof(conf),stdin)) return;
        trim_newline(conf);
        if(strcmp(conf, "YES")!=0){
            puts("Aborted.");
            return;
        }
        run_pipe("git reset --hard HEAD");
    } else if(choice=='s' || choice=='S'){
        char file[1024];
        printf("Enter path to file to discard: ");
        if(!fgets(file,sizeof(file),stdin)) return;
        trim_newline(file);
        chomp(file);
        if(!*file){
            puts("No file provided.");
            return;
        }
        char cmd[1200];
        if(snprintf(cmd, sizeof(cmd), "git restore --staged --worktree -- '%s'", file) >= (int)sizeof(cmd)){
            puts("File path too long.");
            return;
        }
        int rc = run_sys(cmd);
        if(rc != 0){
            if(snprintf(cmd, sizeof(cmd), "git checkout -- '%s'", file) >= (int)sizeof(cmd)){
                puts("File path too long.");
                return;
            }
            run_sys(cmd);
        }
    } else {
        puts("Canceled.");
    }
}

static int merge_internal(const char *src, const char *target, int noff, int squash, int preview){
    char cmd[800];
    if(preview){
        if(snprintf(cmd,sizeof(cmd),"git merge --no-commit %s", src)>=(int)sizeof(cmd)) return -1;
        return run_sys(cmd);
    }
    if(squash){
        if(snprintf(cmd,sizeof(cmd),"git merge --squash %s", src)>=(int)sizeof(cmd)) return -1;
        return run_sys(cmd);
    }
    if(noff){
        char msg[256];
        int n = snprintf(msg, sizeof(msg), "Merge branch '%s' into %s", src, target);
        if(n < 0 || n >= (int)sizeof(msg)){
            strncpy(msg, "Merge", sizeof(msg)); msg[sizeof(msg)-1]='\0';
        }
        if(snprintf(cmd,sizeof(cmd),"git merge --no-ff -m \"%s\" %s", msg, src)>=(int)sizeof(cmd)) return -1;
        return run_sys(cmd);
    }
    if(snprintf(cmd,sizeof(cmd),"git merge %s", src)>=(int)sizeof(cmd)) return -1;
    return run_sys(cmd);
}

static void merge_branch(void){
    char current[256];
    if(run_capture_first_line("git rev-parse --abbrev-ref HEAD", current, sizeof(current))!=0){
        puts("Cannot determine current branch.");
        return;
    }
    puts("\nMerge options:");
    printf("  Current branch: %s\n", current);
    puts("  1) Merge another branch into the current branch");
    puts("  2) Merge the current branch into a selected target branch\n");
    printf("Choose [1/2]: ");
    int mode = getchar();
    int tmp; while((tmp=getchar())!='\n' && tmp!=EOF){}
    int noff=0, squash=0, preview=0;
    puts("\nStrategy: 1) Fast-forward  2) --no-ff  3) --squash  4) Preview (--no-commit)");
    printf("Choose [1/2/3/4]: ");
    int strat = getchar(); while((tmp=getchar())!='\n' && tmp!=EOF){}
    if(strat=='2') noff=1; else if(strat=='3') squash=1; else if(strat=='4') preview=1;
    if(mode=='1'){
        char src[256];
        printf("Enter branch to merge into current: ");
        if(!fgets(src,sizeof(src),stdin)) { return; }
        trim_newline(src);
        chomp(src);
        if(!*src){ puts("No source branch given."); return; }
        int rc = merge_internal(src, current, noff, squash, preview);
        if(rc!=0){
            puts("\nMerge reported conflicts or failed. Resolve and commit, or abort with:\n  git merge --abort");
        } else {
            puts("\nMerge complete.");
        }
        return;
    }
    if(mode=='2'){
        char target[256];
        printf("Enter TARGET branch to merge current into: ");
        if(!fgets(target,sizeof(target),stdin)) { return; }
        trim_newline(target);
        chomp(target);
        if(!*target){ puts("No target branch given."); return; }
        char cmd[600];
        if(snprintf(cmd, sizeof(cmd), "git switch %s", target) >= (int)sizeof(cmd)){
            puts("Target name too long."); return;
        }
        if(run_sys(cmd)!=0){
            if(snprintf(cmd, sizeof(cmd), "git checkout %s", target) >= (int)sizeof(cmd)){
                puts("Target name too long."); return;
            }
            if(run_sys(cmd)!=0){ puts("Failed to switch to target branch."); return; }
        }
        int rc = merge_internal(current, target, noff, squash, preview);
        if(rc!=0){
            puts("\nMerge reported conflicts or failed. Resolve and commit, or abort with:\n  git merge --abort");
        } else {
            puts("\nMerge complete.");
        }
        printf("\nSwitch back to original branch (%s)? [y/N]: ", current);
        int yn = getchar(); while((tmp=getchar())!='\n' && tmp!=EOF){}
        if(yn=='y' || yn=='Y'){
            if(snprintf(cmd, sizeof(cmd), "git switch %s", current) >= (int)sizeof(cmd)) return;
            if(run_sys(cmd)!=0){ snprintf(cmd, sizeof(cmd), "git checkout %s", current); run_sys(cmd); }
        }
        return;
    }
    puts("Canceled.");
}

static void show_log(void){
    puts("\nRecent commits with files (last 50):\n");
    const char *fmt =
        "git log --graph --decorate --abbrev-commit --date=relative "
        "--pretty=format:'%C(yellow)%h%Creset - %s %Cgreen(%cr)%Creset %C(cyan)<%an>%Creset %C(auto)%d%Creset' "
        "--name-status -n 50";
    run_pipe(fmt);
    puts("\nTip: add --merges to show only merges, or --no-merges to hide merges.");
}

static void fetch_prune(void){
    puts("\nFetching remotes with prune (removes stale remote branches)...\n");
    run_pipe("git fetch --all --prune");
}

static void status_short(void){
    puts("");
    run_pipe("git status -sb");
}

static void stage_unstage(void){
    puts("\nStage/Unstage options:\n  a) Stage ALL changes\n  f) Stage a SINGLE file\n  u) Unstage ALL (reset index)\n  r) Unstage a SINGLE file\n  q) Cancel\n");
    printf("Choose [a/f/u/r/q]: ");
    int ch = getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='a' || ch=='A'){ run_pipe("git add -A"); return; }
    if(ch=='f' || ch=='F'){
        char file[1024]; printf("File to stage: ");
        if(!fgets(file,sizeof(file),stdin)) { return; }
        trim_newline(file);
        chomp(file);
        if(!*file){ puts("No file given."); return; }
        char cmd[1200]; if(snprintf(cmd,sizeof(cmd),"git add -- '%s'",file)>=(int)sizeof(cmd)){ puts("Path too long."); return; }
        run_sys(cmd); return;
    }
    if(ch=='u' || ch=='U'){ run_pipe("git reset"); return; }
    if(ch=='r' || ch=='R'){
        char file[1024]; printf("File to unstage: ");
        if(!fgets(file,sizeof(file),stdin)) { return; }
        trim_newline(file);
        chomp(file);
        if(!*file){ puts("No file given."); return; }
        char cmd[1200]; if(snprintf(cmd,sizeof(cmd),"git reset -- '%s'",file)>=(int)sizeof(cmd)){ puts("Path too long."); return; }
        run_sys(cmd); return;
    }
    puts("Canceled.");
}

static void commit_with_message(void){
    char msg[1024];
    printf("Commit message: ");
    if(!fgets(msg,sizeof(msg),stdin)) { return; }
    trim_newline(msg);
    chomp(msg);
    if(!*msg){ puts("Empty message; aborted."); return; }
    char esc[1400]; size_t ei=0; for(size_t i=0; msg[i] && ei<sizeof(esc)-2; ++i){ if(msg[i]=='"') esc[ei++]='\\'; esc[ei++]=msg[i]; } esc[ei]='\0';
    char cmd[1600]; if(snprintf(cmd,sizeof(cmd),"git commit -m \"%s\"", esc)>=(int)sizeof(cmd)){ puts("Message too long."); return; }
    run_sys(cmd);
}

static void pull_menu(void){
    puts("\nPull options:\n  1) git pull\n  2) git pull --rebase\n  3) fetch + rebase onto upstream\n");
    printf("Choose [1/2/3]: ");
    int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='1'){ run_pipe("git pull"); return; }
    if(ch=='2'){ run_pipe("git pull --rebase"); return; }
    if(ch=='3'){ run_pipe("git fetch --all --prune"); run_pipe("git rebase"); return; }
    puts("Canceled.");
}

static void push_menu(void){
    char branch[256];
    if(run_capture_first_line("git rev-parse --abbrev-ref HEAD", branch, sizeof(branch))!=0){ puts("Cannot get current branch."); return; }
    puts("\nPush options:\n  1) git push\n  2) git push -u origin <current>\n");
    printf("Choose [1/2]: ");
    int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='1'){ run_pipe("git push"); return; }
    if(ch=='2'){
        char cmd[600]; if(snprintf(cmd,sizeof(cmd),"git push -u origin %s", branch)>=(int)sizeof(cmd)){ puts("Branch name too long."); return; }
        run_sys(cmd); return;
    }
    puts("Canceled.");
}

static void stash_menu(void){
    puts("\nStash options:\n  s) Save (stash)\n  a) Apply last\n  p) Pop last\n  l) List\n  d) Drop last\n  c) Clear all\n  q) Cancel\n");
    printf("Choose [s/a/p/l/d/c/q]: ");
    int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='s' || ch=='S'){
        char msg[256]; printf("Message (optional, ENTER to skip): ");
        if(!fgets(msg,sizeof(msg),stdin)) { return; }
        trim_newline(msg);
        chomp(msg);
        if(*msg){ char cmd[600]; if(snprintf(cmd,sizeof(cmd),"git stash push -u -m \"%s\"", msg)>=(int)sizeof(cmd)){ puts("Msg too long."); return; } run_sys(cmd);} else { run_pipe("git stash push -u"); }
        return;
    }
    if(ch=='a' || ch=='A'){ run_pipe("git stash apply"); return; }
    if(ch=='p' || ch=='P'){ run_pipe("git stash pop"); return; }
    if(ch=='l' || ch=='L'){ run_pipe("git stash list"); return; }
    if(ch=='d' || ch=='D'){ run_pipe("git stash drop"); return; }
    if(ch=='c' || ch=='C'){ run_pipe("git stash clear"); return; }
    puts("Canceled.");
}

static void create_branch(void){
    char name[256]; char base[256];
    printf("New branch name: "); if(!fgets(name,sizeof(name),stdin)) return; trim_newline(name); chomp(name);
    if(!*name){ puts("No name."); return; }
    printf("Base (ENTER for current HEAD): "); if(!fgets(base,sizeof(base),stdin)) return; trim_newline(base); chomp(base);
    char cmd[600];
    if(*base){ if(snprintf(cmd,sizeof(cmd),"git branch %s %s && git switch %s", name, base, name)>=(int)sizeof(cmd)){ puts("Names too long."); return; } }
    else { if(snprintf(cmd,sizeof(cmd),"git switch -c %s", name)>=(int)sizeof(cmd)){ puts("Name too long."); return; } }
    run_sys(cmd);
}

static void delete_branch(void){
    char name[256]; printf("Branch to delete (local): "); if(!fgets(name,sizeof(name),stdin)) return; trim_newline(name); chomp(name);
    if(!*name){ puts("No name."); return; }
    printf("Force delete? This will discard unmerged work. [y/N]: ");
    int yn=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    char cmd[400]; if(yn=='y' || yn=='Y'){
        if(snprintf(cmd,sizeof(cmd),"git branch -D %s", name)>=(int)sizeof(cmd)){ puts("Name too long."); return; }
    } else {
        if(snprintf(cmd,sizeof(cmd),"git branch -d %s", name)>=(int)sizeof(cmd)){ puts("Name too long."); return; }
    }
    run_sys(cmd);
}

static void rebase_onto(void){
    char onto[256]; printf("Rebase current onto: "); if(!fgets(onto,sizeof(onto),stdin)) return; trim_newline(onto); chomp(onto);
    if(!*onto){ puts("No branch."); return; }
    char cmd[400]; if(snprintf(cmd,sizeof(cmd),"git rebase %s", onto)>=(int)sizeof(cmd)){ puts("Name too long."); return; }
    run_sys(cmd);
}

static void cherry_pick(void){
    char hash[64]; printf("Commit hash to cherry-pick: "); if(!fgets(hash,sizeof(hash),stdin)) return; trim_newline(hash); chomp(hash);
    if(!*hash){ puts("No hash."); return; }
    char cmd[200]; if(snprintf(cmd,sizeof(cmd),"git cherry-pick %s", hash)>=(int)sizeof(cmd)){ puts("Hash too long."); return; }
    run_sys(cmd);
}

static void diff_file(void){
    char file[1024]; printf("File to diff (against index/HEAD): "); if(!fgets(file,sizeof(file),stdin)) return; trim_newline(file); chomp(file);
    if(!*file){ puts("No file."); return; }
    char cmd[1200]; if(snprintf(cmd,sizeof(cmd),"git diff -- '%s'", file)>=(int)sizeof(cmd)){ puts("Path too long."); return; }
    run_pipe(cmd);
}

static void merge_preview_squash(void){
    char src[256]; printf("Branch to merge: "); if(!fgets(src,sizeof(src),stdin)) return; trim_newline(src); chomp(src);
    if(!*src){ puts("No branch."); return; }
    puts("\nOptions:\n  1) Preview --no-commit\n  2) Squash\n  q) Cancel\n");
    printf("Choose [1/2/q]: ");
    int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='1'){ char cmd[600]; if(snprintf(cmd,sizeof(cmd),"git merge --no-commit %s", src)>=(int)sizeof(cmd)){ puts("Cmd too long."); return; } run_sys(cmd); return; }
    if(ch=='2'){ char cmd[600]; if(snprintf(cmd,sizeof(cmd),"git merge --squash %s", src)>=(int)sizeof(cmd)){ puts("Cmd too long."); return; } run_sys(cmd); return; }
    puts("Canceled.");
}

static void amend_last_commit(void){
    puts("\nAmend options:\n  1) Amend keeping message\n  2) Amend with new message\n");
    printf("Choose [1/2]: "); int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='1'){ run_pipe("git commit --amend --no-edit"); return; }
    if(ch=='2'){
        char msg[1024]; printf("New message: "); if(!fgets(msg,sizeof(msg),stdin)) return; trim_newline(msg); chomp(msg);
        if(!*msg){ puts("Empty message; aborted."); return; }
        char esc[1400]; size_t ei=0; for(size_t i=0; msg[i] && ei<sizeof(esc)-2; ++i){ if(msg[i]=='"') esc[ei++]='\\'; esc[ei++]=msg[i]; } esc[ei]='\0';
        char cmd[1600]; if(snprintf(cmd,sizeof(cmd),"git commit --amend -m \"%s\"", esc)>=(int)sizeof(cmd)){ puts("Msg too long."); return; }
        run_sys(cmd); return;
    }
    puts("Canceled.");
}

static void revert_commit(void){
    char hash[64]; printf("Commit hash to revert: "); if(!fgets(hash,sizeof(hash),stdin)) return; trim_newline(hash); chomp(hash);
    if(!*hash){ puts("No hash."); return; }
    char cmd[256]; if(snprintf(cmd,sizeof(cmd),"git revert %s", hash)>=(int)sizeof(cmd)){ puts("Hash too long."); return; }
    run_sys(cmd);
}

static void upstream_status(void){
    char branch[256]; if(run_capture_first_line("git rev-parse --abbrev-ref HEAD", branch, sizeof(branch))!=0){ puts("Cannot get current branch."); return; }
    puts("\nUpstream status (if set):\n");
    run_pipe("git rev-parse --abbrev-ref --symbolic-full-name @{u} 2>/dev/null");
    puts("\nAhead (to push):\n");
    run_pipe("git log --oneline @{u}..HEAD 2>/dev/null");
    puts("\nBehind (to pull):\n");
    run_pipe("git log --oneline HEAD..@{u} 2>/dev/null");
}

static void clean_workspace(void){
    puts("\nClean removes untracked files/dirs.\n  1) git clean -fd\n  2) git clean -fdx (also ignored files)\n");
    printf("Choose [1/2]: "); int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    printf("\nThis is destructive. Type 'YES' to proceed: ");
    char conf[8]; if(!fgets(conf,sizeof(conf),stdin)) return; trim_newline(conf);
    if(strcmp(conf,"YES")!=0){ puts("Aborted."); return; }
    if(ch=='2') run_pipe("git clean -fdx"); else run_pipe("git clean -fd");
}

static void interactive_rebase(void){
    char base[256]; printf("Interactive rebase onto: "); if(!fgets(base,sizeof(base),stdin)) return; trim_newline(base); chomp(base);
    if(!*base){ puts("No base."); return; }
    char cmd[400]; if(snprintf(cmd,sizeof(cmd),"git rebase -i %s", base)>=(int)sizeof(cmd)){ puts("Name too long."); return; }
    run_sys(cmd);
}

static void bisect_guided(void){
    puts("\nBisect guide:\n  1) Start\n  2) Mark GOOD\n  3) Mark BAD\n  4) Next (show current)\n  5) Log\n  6) Reset\n  q) Cancel\n");
    printf("Choose: "); int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='1'){ run_pipe("git bisect start"); return; }
    if(ch=='2'){ run_pipe("git bisect good"); return; }
    if(ch=='3'){ run_pipe("git bisect bad"); return; }
    if(ch=='4'){ run_pipe("git bisect visualize 2>/dev/null || git rev-parse --short HEAD"); return; }
    if(ch=='5'){ run_pipe("git bisect log"); return; }
    if(ch=='6'){ run_pipe("git bisect reset"); return; }
    puts("Canceled.");
}

static void tags_menu(void){
    puts("\nTags:\n  l) List\n  c) Create annotated\n  d) Delete local\n  p) Push tags\n  q) Cancel\n");
    printf("Choose [l/c/d/p/q]: "); int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='l' || ch=='L'){ run_pipe("git tag -n"); return; }
    if(ch=='c' || ch=='C'){
        char name[128], msg[256];
        printf("Tag name: "); if(!fgets(name,sizeof(name),stdin)) return; trim_newline(name); chomp(name);
        if(!*name){ puts("No name."); return; }
        printf("Message: "); if(!fgets(msg,sizeof(msg),stdin)) return; trim_newline(msg); chomp(msg);
        char cmd[600]; if(snprintf(cmd,sizeof(cmd),"git tag -a %s -m \"%s\"", name, msg)>=(int)sizeof(cmd)){ puts("Too long."); return; }
        run_sys(cmd); return;
    }
    if(ch=='d' || ch=='D'){
        char name[128]; printf("Tag to delete: "); if(!fgets(name,sizeof(name),stdin)) return; trim_newline(name); chomp(name);
        if(!*name){ puts("No name."); return; }
        char cmd[300]; if(snprintf(cmd,sizeof(cmd),"git tag -d %s", name)>=(int)sizeof(cmd)){ puts("Too long."); return; }
        run_sys(cmd); return;
    }
    if(ch=='p' || ch=='P'){ run_pipe("git push --tags"); return; }
    puts("Canceled.");
}

static void submodules_menu(void){
    puts("\nSubmodules:\n  1) init/update --recursive\n  2) foreach pull\n  q) Cancel\n");
    printf("Choose [1/2/q]: "); int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='1'){ run_pipe("git submodule update --init --recursive"); return; }
    if(ch=='2'){ run_pipe("git submodule foreach git pull"); return; }
    puts("Canceled.");
}

static void worktrees_menu(void){
    puts("\nWorktrees:\n  l) List\n  a) Add\n  p) Prune\n  q) Cancel\n");
    printf("Choose [l/a/p/q]: "); int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='l' || ch=='L'){ run_pipe("git worktree list"); return; }
    if(ch=='a' || ch=='A'){
        char path[512], branch[256];
        printf("Path: "); if(!fgets(path,sizeof(path),stdin)) return; trim_newline(path); chomp(path);
        printf("Branch (new or existing): "); if(!fgets(branch,sizeof(branch),stdin)) return; trim_newline(branch); chomp(branch);
        if(!*path || !*branch){ puts("Need both path and branch."); return; }
        char cmd[900]; if(snprintf(cmd,sizeof(cmd),"git worktree add '%s' %s", path, branch)>=(int)sizeof(cmd)){ puts("Too long."); return; }
        run_sys(cmd); return;
    }
    if(ch=='p' || ch=='P'){ run_pipe("git worktree prune"); return; }
    puts("Canceled.");
}

static int has_cmake(void){ return tool_exists("cmake"); }
static int has_ctest(void){ return tool_exists("ctest"); }
static int has_make(void){ return tool_exists("make"); }

static int file_exists(const char *path){
    FILE *f = fopen(path, "r"); if(!f) return 0; fclose(f); return 1;
}

static void build_menu(void){
    puts("\nBuild: detects CMake or Makefile.\n");
    int cmake = file_exists("CMakeLists.txt") && has_cmake();
    int makef = file_exists("Makefile") && has_make();
    if(cmake){
        puts("Detected CMake. Building in ./build ...\n");
        run_pipe("cmake -S . -B build");
        run_pipe("cmake --build build -j");
        return;
    }
    if(makef){
        puts("Detected Makefile. Running 'make -j'.\n");
        run_pipe("make -j");
        return;
    }
    puts("No known build system detected (no CMakeLists.txt or Makefile).");
}

static void test_menu(void){
    puts("\nTest: tries ctest first, then 'make test' or 'ctest --test-dir build'.\n");
    if(has_ctest()){
        if(file_exists("build")){
            run_pipe("ctest --test-dir build");
        } else {
            run_pipe("ctest");
        }
        return;
    }
    if(has_make()){
        run_pipe("make test");
        return;
    }
    puts("No test runner found (ctest/make).");
}

static int is_c_or_cpp(const char *p){
    const char *dot = strrchr(p, '.');
    if(!dot) return 0;
    return (strcmp(dot, ".c")==0 || strcmp(dot, ".h")==0 || strcmp(dot, ".hpp")==0 || strcmp(dot, ".hh")==0 || strcmp(dot, ".cpp")==0 || strcmp(dot, ".cc")==0 || strcmp(dot, ".cxx")==0);
}

static void format_clang_format(void){
    if(!tool_exists("clang-format")){ puts("clang-format not found in PATH."); return; }
    puts("Format which set?\n  1) Modified vs HEAD\n  2) Staged\n  3) All tracked C/C++ files\n");
    printf("Choose [1/2/3]: "); int ch=getchar(); int t; while((t=getchar())!='\n' && t!=EOF){}
    if(ch=='1'){
        puts("\nFiles modified vs HEAD:\n");
        run_pipe("git diff --name-only HEAD");
        FILE *p = popen("git diff --name-only HEAD", "r");
        if(!p){ puts("Failed to list files."); return; }
        char line[1024];
        while(fgets(line,sizeof(line),p)){
            trim_newline(line); chomp(line);
            if(is_c_or_cpp(line)){
                char cmd[1400]; if(snprintf(cmd,sizeof(cmd),"clang-format -i '%s'", line)<(int)sizeof(cmd)) run_sys(cmd);
            }
        }
        pclose(p);
        return;
    }
    if(ch=='2'){
        puts("\nStaged files:\n");
        run_pipe("git diff --cached --name-only");
        FILE *p = popen("git diff --cached --name-only", "r"); if(!p){ puts("Failed to list files."); return; }
        char line[1024]; while(fgets(line,sizeof(line),p)){
            trim_newline(line); chomp(line);
            if(is_c_or_cpp(line)){
                char cmd[1400]; if(snprintf(cmd,sizeof(cmd),"clang-format -i '%s'", line)<(int)sizeof(cmd)) run_sys(cmd);
            }
        }
        pclose(p); return;
    }
    if(ch=='3'){
        puts("\nAll tracked C/C++ files (may take time)\n");
        FILE *p = popen("git ls-files", "r"); if(!p){ puts("Failed to list files."); return; }
        char line[1024]; while(fgets(line,sizeof(line),p)){
            trim_newline(line); chomp(line);
            if(is_c_or_cpp(line)){
                char cmd[1400]; if(snprintf(cmd,sizeof(cmd),"clang-format -i '%s'", line)<(int)sizeof(cmd)) run_sys(cmd);
            }
        }
        pclose(p); return;
    }
    puts("Canceled.");
}

static void lint_cppcheck(void){
    if(!tool_exists("cppcheck")){ puts("cppcheck not found in PATH."); return; }
    puts("Running cppcheck on tracked sources (headers not excluded).\n");
    run_pipe("cppcheck --enable=warning,style,performance,portability --quiet --inline-suppr .");
}

int main(int argc, char **argv){
    if(argc>1 && (strcmp(argv[1],"-help")==0 || strcmp(argv[1],"--help")==0)){
        print_help();
        return 0;
    }
    if(!ensure_git_repo()) return 1;
    for(;;){
        show_header();
        menu();
        printf("\nYour choice: ");
        char line[32];
        if(!fgets(line,sizeof(line),stdin)) break;
        trim_newline(line);
        chomp(line);
        if(!*line) continue;
        int sel = atoi(line);
        switch(sel){
            case 1: list_branches(); break;
            case 2: switch_branch(); break;
            case 3: compare_branches(); break;
            case 4: drop_modified_files(); break;
            case 5: merge_branch(); break;
            case 6: show_log(); break;
            case 7: fetch_prune(); break;
            case 8: status_short(); break;
            case 9: stage_unstage(); break;
            case 10: commit_with_message(); break;
            case 11: pull_menu(); break;
            case 12: push_menu(); break;
            case 13: stash_menu(); break;
            case 14: create_branch(); break;
            case 15: delete_branch(); break;
            case 16: rebase_onto(); break;
            case 17: cherry_pick(); break;
            case 18: diff_file(); break;
            case 19: merge_preview_squash(); break;
            case 20: amend_last_commit(); break;
            case 21: revert_commit(); break;
            case 22: upstream_status(); break;
            case 23: clean_workspace(); break;
            case 24: interactive_rebase(); break;
            case 25: bisect_guided(); break;
            case 26: tags_menu(); break;
            case 27: submodules_menu(); break;
            case 28: worktrees_menu(); break;
            case 29: build_menu(); break;
            case 30: test_menu(); break;
            case 31: format_clang_format(); break;
            case 32: lint_cppcheck(); break;
            case 0: puts("Bye!"); return 0;
            default: puts("Unknown choice."); break;
        }
        press_enter();
    }
    return 0;
}
