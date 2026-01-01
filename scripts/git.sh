#!/bin/bash
# Git Power Tools - Advanced Git Workflow Automation
# Usage: ./git.sh [command] [args...]

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

VERSION="1.0.0"
SCRIPT_NAME="git.sh"

print_banner() {
    echo -e "${CYAN}"
    echo "  ╔═══════════════════════════════════════╗"
    echo "  ║       Git Power Tools v${VERSION}        ║"
    echo "  ║     Advanced Workflow Automation      ║"
    echo "  ╚═══════════════════════════════════════╝"
    echo -e "${NC}"
}

print_help() {
    print_banner
    echo -e "${BOLD}USAGE:${NC}"
    echo "  ./${SCRIPT_NAME} <command> [options]"
    echo ""
    echo -e "${BOLD}REPOSITORY COMMANDS:${NC}"
    echo -e "  ${GREEN}status${NC}, ${GREEN}s${NC}        Show detailed repository status"
    echo -e "  ${GREEN}info${NC}            Display repository information"
    echo -e "  ${GREEN}log${NC} [n]         Show last n commits (default: 10)"
    echo -e "  ${GREEN}graph${NC} [n]       Show commit graph (default: 15)"
    echo ""
    echo -e "${BOLD}COMMIT COMMANDS:${NC}"
    echo -e "  ${GREEN}save${NC} [msg]      Stage all and commit"
    echo -e "  ${GREEN}quick${NC}, ${GREEN}q${NC} [msg]  Stage, commit, and push"
    echo -e "  ${GREEN}amend${NC}           Amend last commit message"
    echo -e "  ${GREEN}fixup${NC}           Amend last commit with staged changes"
    echo -e "  ${GREEN}undo${NC}            Undo last commit (keep changes)"
    echo ""
    echo -e "${BOLD}BRANCH COMMANDS:${NC}"
    echo -e "  ${GREEN}branch${NC}, ${GREEN}b${NC}       List all branches"
    echo -e "  ${GREEN}new${NC} <name>      Create and switch to new branch"
    echo -e "  ${GREEN}switch${NC}, ${GREEN}sw${NC} <n> Switch to branch"
    echo -e "  ${GREEN}delete${NC} <name>   Delete a branch"
    echo -e "  ${GREEN}rename${NC} <new>    Rename current branch"
    echo ""
    echo -e "${BOLD}SYNC COMMANDS:${NC}"
    echo -e "  ${GREEN}push${NC}            Push to origin"
    echo -e "  ${GREEN}pull${NC}            Pull from origin"
    echo -e "  ${GREEN}sync${NC}            Pull then push"
    echo -e "  ${GREEN}fetch${NC}           Fetch all remotes"
    echo ""
    echo -e "${BOLD}STASH COMMANDS:${NC}"
    echo -e "  ${GREEN}stash${NC}           Stash current changes"
    echo -e "  ${GREEN}pop${NC}             Pop stashed changes"
    echo -e "  ${GREEN}stashlist${NC}       List all stashes"
    echo ""
    echo -e "${BOLD}DIFF COMMANDS:${NC}"
    echo -e "  ${GREEN}diff${NC}            Show unstaged changes"
    echo -e "  ${GREEN}staged${NC}          Show staged changes"
    echo -e "  ${GREEN}changes${NC}         Show all changes"
    echo ""
    echo -e "${BOLD}CLEANUP COMMANDS:${NC}"
    echo -e "  ${GREEN}clean${NC}           Remove untracked files (interactive)"
    echo -e "  ${GREEN}reset${NC}           Reset all changes (interactive)"
    echo -e "  ${GREEN}prune${NC}           Prune remote-tracking branches"
    echo ""
    echo -e "${BOLD}UTILITIES:${NC}"
    echo -e "  ${GREEN}alias${NC}           Show useful git aliases"
    echo -e "  ${GREEN}ignore${NC} <file>   Add file to .gitignore"
    echo -e "  ${GREEN}contributors${NC}    List contributors by commits"
    echo -e "  ${GREEN}stats${NC}           Show repository statistics"
    echo ""
    echo -e "${BOLD}EXAMPLES:${NC}"
    echo "  ./${SCRIPT_NAME} quick \"Added new feature\""
    echo "  ./${SCRIPT_NAME} log 20"
    echo "  ./${SCRIPT_NAME} new feature/login"
}

check_git_repo() {
    if ! git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
        echo -e "${RED}Error: Not inside a git repository${NC}"
        exit 1
    fi
}

get_branch() {
    git branch --show-current
}

get_remote_url() {
    git remote get-url origin 2>/dev/null || echo "No remote configured"
}

show_status() {
    check_git_repo
    local branch=$(get_branch)
    local remote=$(get_remote_url)
    
    echo -e "${BOLD}${BLUE}Repository Status${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}Branch:${NC} $branch"
    echo -e "${GREEN}Remote:${NC} $remote"
    echo ""
    
    local staged=$(git diff --cached --numstat 2>/dev/null | wc -l)
    local modified=$(git diff --numstat 2>/dev/null | wc -l)
    local untracked=$(git ls-files --others --exclude-standard 2>/dev/null | wc -l)
    
    echo -e "${YELLOW}Changes:${NC}"
    echo -e "  Staged:    ${GREEN}$staged${NC} files"
    echo -e "  Modified:  ${YELLOW}$modified${NC} files"
    echo -e "  Untracked: ${RED}$untracked${NC} files"
    echo ""
    
    if [ "$staged" -gt 0 ] || [ "$modified" -gt 0 ] || [ "$untracked" -gt 0 ]; then
        echo -e "${BOLD}File Status:${NC}"
        git status -s
    else
        echo -e "${GREEN}Working tree clean${NC}"
    fi
}

show_info() {
    check_git_repo
    
    echo -e "${BOLD}${BLUE}Repository Information${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    local root=$(git rev-parse --show-toplevel)
    echo -e "${GREEN}Root:${NC} $root"
    echo -e "${GREEN}Branch:${NC} $(get_branch)"
    echo -e "${GREEN}Remote:${NC} $(get_remote_url)"
    
    local commits=$(git rev-list --count HEAD 2>/dev/null || echo "0")
    echo -e "${GREEN}Total Commits:${NC} $commits"
    
    local first_commit=$(git log --reverse --format="%ar" 2>/dev/null | head -1)
    if [ -n "$first_commit" ]; then
        echo -e "${GREEN}First Commit:${NC} $first_commit"
    fi
    
    local last_commit=$(git log -1 --format="%ar" 2>/dev/null)
    if [ -n "$last_commit" ]; then
        echo -e "${GREEN}Last Commit:${NC} $last_commit"
    fi
    
    local branches=$(git branch | wc -l)
    echo -e "${GREEN}Local Branches:${NC} $branches"
    
    local tags=$(git tag | wc -l)
    echo -e "${GREEN}Tags:${NC} $tags"
}

show_log() {
    check_git_repo
    local count="${1:-10}"
    echo -e "${BOLD}${BLUE}Last $count Commits${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    git log --oneline --decorate -n "$count"
}

show_graph() {
    check_git_repo
    local count="${1:-15}"
    echo -e "${BOLD}${BLUE}Commit Graph (last $count)${NC}"
    git log --oneline --graph --decorate -n "$count" --all
}

save_changes() {
    check_git_repo
    local msg="${1:-Auto-save $(date '+%Y-%m-%d %H:%M')}"
    
    git add -A
    
    local count=$(git diff --cached --numstat | wc -l)
    if [ "$count" -eq 0 ]; then
        echo -e "${YELLOW}No changes to commit${NC}"
        return 0
    fi
    
    git commit -m "$msg"
    echo -e "${GREEN}Committed $count file(s):${NC} $msg"
}

quick_save() {
    check_git_repo
    local msg="${1:-Quick save $(date '+%Y-%m-%d %H:%M')}"
    local branch=$(get_branch)
    
    save_changes "$msg"
    
    echo -e "${YELLOW}Pushing to origin/$branch...${NC}"
    git push origin "$branch"
    echo -e "${GREEN}Saved and pushed!${NC}"
}

push_changes() {
    check_git_repo
    local branch=$(get_branch)
    
    echo -e "${YELLOW}Pushing to origin/$branch...${NC}"
    git push origin "$branch"
    echo -e "${GREEN}Pushed successfully${NC}"
}

pull_changes() {
    check_git_repo
    local branch=$(get_branch)
    
    echo -e "${YELLOW}Pulling from origin/$branch...${NC}"
    git pull origin "$branch"
    echo -e "${GREEN}Pulled successfully${NC}"
}

sync_changes() {
    check_git_repo
    
    echo -e "${YELLOW}Syncing with remote...${NC}"
    pull_changes
    push_changes
    echo -e "${GREEN}Sync complete!${NC}"
}

fetch_all() {
    check_git_repo
    
    echo -e "${YELLOW}Fetching all remotes...${NC}"
    git fetch --all --prune
    echo -e "${GREEN}Fetch complete${NC}"
}

list_branches() {
    check_git_repo
    local current=$(get_branch)
    
    echo -e "${BOLD}${BLUE}Branches${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    echo -e "${GREEN}Local:${NC}"
    git branch
    
    echo ""
    echo -e "${GREEN}Remote:${NC}"
    git branch -r
}

new_branch() {
    check_git_repo
    
    if [ -z "$1" ]; then
        echo -e "${RED}Usage: new <branch-name>${NC}"
        return 1
    fi
    
    local name="$1"
    
    git checkout -b "$name"
    echo -e "${GREEN}Created and switched to branch:${NC} $name"
}

switch_branch() {
    check_git_repo
    
    if [ -z "$1" ]; then
        echo -e "${RED}Usage: switch <branch-name>${NC}"
        return 1
    fi
    
    git checkout "$1"
    echo -e "${GREEN}Switched to:${NC} $1"
}

delete_branch() {
    check_git_repo
    
    if [ -z "$1" ]; then
        echo -e "${RED}Usage: delete <branch-name>${NC}"
        return 1
    fi
    
    local branch="$1"
    local current=$(get_branch)
    
    if [ "$branch" = "$current" ]; then
        echo -e "${RED}Cannot delete current branch${NC}"
        return 1
    fi
    
    read -p "Delete branch '$branch'? (y/n): " confirm
    if [ "$confirm" = "y" ]; then
        git branch -d "$branch"
        echo -e "${GREEN}Deleted branch:${NC} $branch"
    else
        echo -e "${YELLOW}Cancelled${NC}"
    fi
}

rename_branch() {
    check_git_repo
    
    if [ -z "$1" ]; then
        echo -e "${RED}Usage: rename <new-name>${NC}"
        return 1
    fi
    
    local old=$(get_branch)
    local new="$1"
    
    git branch -m "$new"
    echo -e "${GREEN}Renamed branch:${NC} $old -> $new"
}

undo_commit() {
    check_git_repo
    
    echo -e "${YELLOW}Undoing last commit (keeping changes)...${NC}"
    git reset --soft HEAD~1
    echo -e "${GREEN}Commit undone. Changes preserved in staging area.${NC}"
}

amend_commit() {
    check_git_repo
    
    echo -e "${YELLOW}Amending last commit...${NC}"
    git commit --amend
}

fixup_commit() {
    check_git_repo
    
    local staged=$(git diff --cached --numstat | wc -l)
    if [ "$staged" -eq 0 ]; then
        echo -e "${YELLOW}No staged changes to add${NC}"
        return 0
    fi
    
    echo -e "${YELLOW}Adding staged changes to last commit...${NC}"
    git commit --amend --no-edit
    echo -e "${GREEN}Last commit updated${NC}"
}

stash_changes() {
    check_git_repo
    
    local msg="${1:-Stash $(date '+%Y-%m-%d %H:%M')}"
    git stash push -m "$msg"
    echo -e "${GREEN}Changes stashed:${NC} $msg"
}

pop_stash() {
    check_git_repo
    
    git stash pop
    echo -e "${GREEN}Stash applied and removed${NC}"
}

list_stashes() {
    check_git_repo
    
    echo -e "${BOLD}${BLUE}Stash List${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    git stash list
}

show_diff() {
    check_git_repo
    git diff
}

show_staged() {
    check_git_repo
    
    echo -e "${BOLD}${BLUE}Staged Changes${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    git diff --cached
}

show_changes() {
    check_git_repo
    
    echo -e "${BOLD}${BLUE}All Changes${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    git diff HEAD
}

clean_repo() {
    check_git_repo
    
    echo -e "${YELLOW}Untracked files to remove:${NC}"
    git clean -n -d
    echo ""
    read -p "Remove these files? (y/n): " confirm
    if [ "$confirm" = "y" ]; then
        git clean -fd
        echo -e "${GREEN}Cleaned${NC}"
    else
        echo -e "${YELLOW}Cancelled${NC}"
    fi
}

reset_repo() {
    check_git_repo
    
    echo -e "${RED}WARNING: This will discard all uncommitted changes!${NC}"
    read -p "Are you sure? (yes/no): " confirm
    if [ "$confirm" = "yes" ]; then
        git reset --hard HEAD
        git clean -fd
        echo -e "${GREEN}Repository reset to HEAD${NC}"
    else
        echo -e "${YELLOW}Cancelled${NC}"
    fi
}

prune_branches() {
    check_git_repo
    
    echo -e "${YELLOW}Pruning remote-tracking branches...${NC}"
    git remote prune origin
    echo -e "${GREEN}Pruned${NC}"
}

show_aliases() {
    echo -e "${BOLD}${BLUE}Useful Git Aliases${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo "Add these to your .gitconfig [alias] section:"
    echo ""
    echo "  st = status -sb"
    echo "  co = checkout"
    echo "  br = branch"
    echo "  ci = commit"
    echo "  lg = log --oneline --graph --decorate"
    echo "  last = log -1 HEAD"
    echo "  unstage = reset HEAD --"
    echo "  amend = commit --amend --no-edit"
}

add_to_gitignore() {
    check_git_repo
    
    if [ -z "$1" ]; then
        echo -e "${RED}Usage: ignore <file-pattern>${NC}"
        return 1
    fi
    
    echo "$1" >> .gitignore
    echo -e "${GREEN}Added to .gitignore:${NC} $1"
}

show_contributors() {
    check_git_repo
    
    echo -e "${BOLD}${BLUE}Contributors (by commits)${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    git shortlog -sn --all
}

show_stats() {
    check_git_repo
    
    echo -e "${BOLD}${BLUE}Repository Statistics${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    local commits=$(git rev-list --count HEAD 2>/dev/null || echo "0")
    local files=$(git ls-files | wc -l)
    local branches=$(git branch | wc -l)
    local contributors=$(git shortlog -sn --all 2>/dev/null | wc -l)
    
    echo -e "${GREEN}Total Commits:${NC}     $commits"
    echo -e "${GREEN}Tracked Files:${NC}     $files"
    echo -e "${GREEN}Local Branches:${NC}    $branches"
    echo -e "${GREEN}Contributors:${NC}      $contributors"
    
    echo ""
    echo -e "${YELLOW}Lines of Code by Language:${NC}"
    git ls-files | while read f; do
        case "$f" in
            *.cpp|*.hpp|*.c|*.h) echo "C/C++";;
            *.py) echo "Python";;
            *.js|*.jsx) echo "JavaScript";;
            *.ts|*.tsx) echo "TypeScript";;
            *.sh) echo "Shell";;
            *.go) echo "Go";;
            *.rs) echo "Rust";;
            *.java) echo "Java";;
            *) echo "Other";;
        esac
    done 2>/dev/null | sort | uniq -c | sort -rn | head -5
}

case "${1:-help}" in
    status|s)       show_status ;;
    info)           show_info ;;
    log|l)          show_log "$2" ;;
    graph|g)        show_graph "$2" ;;
    save)           save_changes "$2" ;;
    quick|q)        quick_save "$2" ;;
    push)           push_changes ;;
    pull)           pull_changes ;;
    sync)           sync_changes ;;
    fetch)          fetch_all ;;
    branch|b)       list_branches ;;
    new)            new_branch "$2" ;;
    switch|sw)      switch_branch "$2" ;;
    delete|del)     delete_branch "$2" ;;
    rename)         rename_branch "$2" ;;
    undo)           undo_commit ;;
    amend)          amend_commit ;;
    fixup)          fixup_commit ;;
    stash)          stash_changes "$2" ;;
    pop)            pop_stash ;;
    stashlist|sl)   list_stashes ;;
    diff|d)         show_diff ;;
    staged)         show_staged ;;
    changes)        show_changes ;;
    clean)          clean_repo ;;
    reset)          reset_repo ;;
    prune)          prune_branches ;;
    alias)          show_aliases ;;
    ignore)         add_to_gitignore "$2" ;;
    contributors)   show_contributors ;;
    stats)          show_stats ;;
    help|--help|-h) print_help ;;
    version|--version|-v) echo "Git Power Tools v${VERSION}" ;;
    *)
        echo -e "${RED}Unknown command:${NC} $1"
        echo "Run './${SCRIPT_NAME} help' for usage"
        exit 1
        ;;
esac
