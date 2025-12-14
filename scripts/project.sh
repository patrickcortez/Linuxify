#!default

log() {
    echo "[$(date)] $1"
}

show_help() {
    echo "Git Project Manager"
    echo "Usage: project.sh <command> [args]"
    echo ""
    echo "Commands:"
    echo "  status    Show status of all git repos in current directory"
    echo "  pull      Pull updates for all repos"
    echo "  clean     Clean build artifacts (node_modules, __pycache__, etc)"
    echo "  backup    Create timestamped backup of current directory"
    echo "  info      Show system information"
}

get_repo_status() {
    cd $1
    STATUS=$(git status --porcelain 2>/dev/null)
    if [ -n "$STATUS" ]; then
        echo "  [MODIFIED] $1"
    else
        echo "  [CLEAN] $1"
    fi
    cd ..
}

pull_repo() {
    log "Pulling $1..."
    cd $1
    git pull
    cd ..
}

clean_project() {
    log "Cleaning build artifacts..."
    
    for dir in node_modules __pycache__ .cache dist build bin obj; do
        if [ -d "$dir" ]; then
            log "Removing $dir"
            rm -rf $dir
        fi
    done
    
    log "Cleanup complete!"
}

backup_dir() {
    BACKUP_NAME="backup_$(date +%Y%m%d_%H%M%S).tar"
    log "Creating backup: $BACKUP_NAME"
    tar -cf $BACKUP_NAME .
    log "Backup complete!"
}

show_info() {
    echo "=== System Information ==="
    echo ""
    echo "User: $(whoami)"
    echo "Host: $(hostname)"
    echo "Directory: $(pwd)"
    echo ""
    echo "=== Disk Usage ==="
    df -h
}

case $1 in
    status)
        echo "=== Repository Status ==="
        for dir in */; do
            if [ -d "$dir/.git" ]; then
                get_repo_status $dir
            fi
        done
        ;;
    pull)
        echo "=== Pulling All Repos ==="
        for dir in */; do
            if [ -d "$dir/.git" ]; then
                pull_repo $dir
            fi
        done
        ;;
    clean)
        clean_project
        ;;
    backup)
        backup_dir
        ;;
    info)
        show_info
        ;;
    *)
        show_help
        ;;
esac
