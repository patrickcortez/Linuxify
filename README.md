# Linuxify

A native Unix-like shell and development platform for Windows — built on a continuation-passing style (CPS) state machine engine that provides stackless execution, preventing stack overflow regardless of session length. Features an integrated Bash interpreter, GUI terminal, version control, virtual file system, and task scheduler. Built entirely from scratch in C++.

## Features

- **Linuxify Shell** with 60+ built-in Linux commands
- **Lino Text Editor** with syntax highlighting plugins
- **Lin Package Manager** (winget-powered) with 180+ package aliases
- **Integrated Bash interpreter** with full lexer, parser, and AST executor
- **LVC version control** system with SHA-256 content addressing
- **Node graph file system** with full disk encryption
- **Nexplore** - GUI file explorer for Node images
- **Windux** - GUI terminal with tabs and ConPTY
- **Crond daemon** for scheduled tasks
- **LinMake build system** with incremental builds
- Shebang (`./`) execution support
- Real-time syntax highlighting as you type
- Frequency-based auto-suggestions
- Auto-navigation (type a directory path to navigate directly)
- Signal and crash handlers

## Components

---

### **Linuxify Shell**

The main shell executable (`linuxify.exe`) — a complete Unix-like shell environment for Windows powered by a **trampoline-based state machine engine**. Unlike traditional shells that use recursive call stacks, Linuxify uses a continuation-passing architecture where each shell state (`StatePrompt`, `StateReadInput`, `StateExecute`) is an independent node that returns the next state to execute. The `ShellEngine` dispatcher iteratively bounces between states, eliminating stack growth and preventing overflow regardless of session length.

Comparable to bash or zsh, it features an integrated Bash interpreter, native script execution, control flow, variable expansion, and 60+ built-in commands. Written in C++ with 8900+ lines.

**Supported Commands:**

| Category | Commands |
|----------|----------|
| **File System** | `pwd`, `cd`, `ls`, `mkdir`, `rm`, `rmdir`, `mv`, `cp`, `cat`, `touch`, `chmod`, `chown`, `find` |
| **Text** | `grep`, `head`, `tail`, `wc`, `sort`, `uniq`, `cut`, `tr`, `echo` |
| **Process** | `ps`, `kill`, `top`, `htop`, `jobs`, `fg`, `&` (background) |
| **System Info** | `lsmem`, `free`, `lscpu`, `lshw`, `sysinfo`, `lsmount`, `lsblk`, `df`, `lsusb`, `lsnet`, `lsof` |
| **Networking** | `ip`, `ping`, `traceroute`, `nslookup`, `dig`, `curl`, `wget`, `netstat`, `ifconfig` |
| **Shell** | `history`, `whoami`, `env`, `export`, `which`, `clear`, `exit`, `source`, `alias`, `declare` |
| **Tools** | `make`, `gdb`, `ar`, `ld`, `objdump`, `nm`, `strip` |
| **Scheduling** | `crontab`, `crond` |
| **System** | `nuke`, `unnuke`, `setup` |

**Shell Features:**
- Syntax highlighting for commands as you type
- Frequency-based auto-suggestions (ghost text)
- Persistent command history with `history clear` support
- Piping: `cmd1 | cmd2`
- Redirection: `>` (write), `>>` (append), `2>` (stderr), `&>` (both)
- Input redirection: `<`, `<<` (heredoc), `<<<` (here-string)
- Background processes: `command &`
- Environment variables: `$VAR`, `${VAR}`
- Command substitution: `$(command)`
- Arithmetic evaluation
- Auto-pairing for quotes and brackets
- Text selection with copy/cut/paste (Ctrl+C/X/V)
- Integrated Bash interpreter for `.sh` scripts

---

### **Shell Architecture**

The shell is built on a modular architecture with specialized subsystems working together to provide a complete Unix-like experience on Windows.

#### Engine Core (`engine/`)

The engine provides the foundational state management for the shell:

| File | Purpose |
|------|---------|
| `shell_context.hpp` | Central shell state container holding current directory, environment variables, shell variables, last exit code, and execution flags |
| `continuation.hpp` | Detects incomplete input (unclosed quotes, brackets, heredocs) and handles multi-line continuation prompts (`> `) |
| `states.hpp` | Shell execution state machine managing interactive mode, script execution, and pipeline states |

#### Input System

The input subsystem provides a sophisticated terminal experience:

**`input_handler.hpp`** (824 lines) - The main input loop:
- Real-time syntax highlighting as you type (commands, arguments, flags, strings)
- Cursor movement with proper multi-line wrapping
- History navigation with Up/Down arrows
- Tab completion with filesystem and command suggestions
- Bracket and quote auto-pairing with smart wrap-selection
- Text selection with Shift+Arrow keys
- Clipboard support (Ctrl+C/X/V when text selected)
- Ghost text auto-suggestions based on command frequency

**`io_handler.hpp`** - Low-level console I/O:
- Direct Win32 Console API (`WriteConsoleA`, `SetConsoleCursorPosition`)
- Color management with predefined shell colors (commands, args, flags, strings)
- Screen buffer operations (clear screen, clear area, clear from cursor)
- Cursor position tracking and manipulation

#### Output System

**`shell_streams.hpp`** (295 lines) - Custom I/O streams:
- `ShellIO::sout` - Thread-safe stdout with color support
- `ShellIO::serr` - Thread-safe stderr 
- `ShellIO::sin` - Buffered stdin with tokenization
- Automatic prompt redrawing when background output interrupts input
- ANSI color codes via `ShellIO::Color` enum
- Console vs pipe detection for proper output handling

#### Signal & Interrupt Handling

**`signal_handler.hpp`** (196 lines) - Unified event handling:
- `InputDispatcher` singleton managing raw console input
- Key binding registration for custom shortcuts
- Console control handler for CTRL_C, CTRL_BREAK, CTRL_CLOSE
- Standard signal handlers (SIGINT, SIGTERM, SIGABRT)
- Signal blocking during critical sections
- Graceful shutdown with cleanup callbacks

**`interrupt.hpp`** (705 lines) - Deep interrupt diagnostics:
- `PEResolver` - PE header parsing and symbol resolution
- `UnwindMachine` - x64 stack unwinding using RUNTIME_FUNCTION tables
- Memory dump utilities for crash analysis
- Module enumeration via PEB/LDR structures
- Disassembly around crash addresses
- Thread enumeration and hung thread detection

**`crash_handler.hpp`** (148 lines) - Crash recovery:
- Windows SEH (Structured Exception Handling) filter
- C++ `std::terminate` handler
- Formatted crash reports with:
  - Exception name and code
  - Crash address with module+offset
  - Full stack trace with symbol resolution
- Console mode restoration before crash output

#### Process Management

**`child_handler.hpp`** (178 lines) - Child process spawning:
- Console handle duplication for proper I/O inheritance
- Console mode configuration (cooked mode for children, VT processing)
- Signal handler suspension during child execution
- UAC elevation support via `ShellExecuteEx` with "runas" verb
- Foreground process tracking for job control
- Automatic shell state restoration after child exits

**`process_manager.hpp`** - Background job management:
- Job list tracking (PID, command, status)
- Foreground/background process switching
- Process termination and cleanup

#### Shell Features

**`auto-suggest.hpp`** (300+ lines) - Intelligent suggestions:
- Frequency index built from command history
- Binary search for prefix matching
- Recency as tie-breaker for equal frequencies
- Filesystem path completion fallback
- Case-insensitive matching

**`auto-nav.hpp`** (128 lines) - Auto-navigation:
- Detects path-like inputs (starting with `/`, `./`, `..`, `~`, or drive letter)
- Path validation and resolution
- Automatic `cd` when typing a valid directory path

**`arith.hpp`** (322 lines) - Arithmetic evaluator:
- Full expression parser (Tokenizer → Parser → Evaluator)
- Operators: `+`, `-`, `*`, `/`, `()`
- Proper operator precedence
- Floating-point support with integer output when possible
- Used for `$(( expression ))` evaluation

**`fuzzy.hpp`** - Fuzzy matching:
- Levenshtein distance for typo correction
- Command name suggestions for "did you mean?" prompts

---


### **Registry**

The *Registry* is a component of Linuxify that manages external packages and integrates custom command aliases. It is also the central source for Linuxify's Shebang line, which is used to run executables with `./`.

**Commands:**

| Command | Description |
|---------|-------------|
| `registry add <cmd> <path>` | Add a command to the registry |
| `registry remove <cmd>` | Remove a command from the registry |
| `registry list` | List all registered commands |
| `registry refresh` | Scan filesystem for packages/binaries |

**How it works:**
1. Scans common directories (Program Files, AppData, etc.)
2. Searches system PATH for executables
3. Stores commands in `linuxdb/registry.lin`
4. Enables `./script.sh` execution via shebang

---

### **Shebang Execution (`./`)**

The shebang line (`#!`) is **required** for running scripts with `./` in Linuxify. It tells the shell which interpreter to use.

**How it works:**
1. When you run `./script.sh`, Linuxify reads the first line
2. Parses the shebang (e.g., `#!lish` or `#!/usr/bin/env python`)
3. Looks up the interpreter in the **Registry**
4. Executes the script with that interpreter

**Example Scripts:**
```bash
#!/bin/lish
echo "Hello from lish!"
```

```python
#!python
print("Hello from Python!")
```

**Supported Shebang Formats:**
- `#!lish` - Registry name (simplest)
- `#!python` - Any registered interpreter
- `#!/usr/bin/env lish` - Unix-style (env is stripped)
- `#!C:/path/to/interpreter.exe` - Absolute path

---

### **System Integration**

Linuxify provides deep system integration capabilities:

**Commands:**

| Command | Description |
|---------|-------------|
| `nuke` | Replace cmd.exe and powershell.exe with Linuxify (requires admin) |
| `unnuke` | Restore original Windows shells |
| `setup install` | Register .sh files with Windows for Explorer/cmd/PowerShell execution |
| `setup uninstall` | Remove .sh file association |
| `setup status` | Check current file association |

**Warning:** The `nuke` command uses IFEO (Image File Execution Options) injection to redirect shell binaries. Use with caution.

---

### **Lin Package Manager**

A winget-powered package manager with Linux-style syntax. Uses `linuxdb/packages.lin` for 180+ package aliases.

**Commands:**

| Command | Description |
|---------|-------------|
| `lin get <package>` | Install package (e.g., `lin get git`) |
| `lin remove <package>` | Uninstall package |
| `lin update` | Check for updates |
| `lin upgrade` | Upgrade all packages |
| `lin search <query>` | Search packages |
| `lin list` | List installed packages |
| `lin info <package>` | Show package details |
| `lin alias` | Show package mappings |

**Package Aliases Include:**
- Languages: `python`, `nodejs`, `rust`, `go`, `java`, `ruby`, `php`, `lua`, `zig`, `nim`
- Tools: `git`, `docker`, `cmake`, `curl`, `wget`, `jq`, `fzf`, `ripgrep`, `fd`, `bat`
- Editors: `code`, `vim`, `neovim`, `notepad++`, `sublime`, `helix`
- Browsers: `chrome`, `firefox`, `edge`, `brave`, `opera`, `vivaldi`
- Databases: `sqlite`, `mysql`, `postgres`, `mongodb`, `redis`
- Cloud: `azure-cli`, `aws-cli`, `gcloud`, `docker`, `kubectl`, `terraform`

---

### **Lino Text Editor**

A terminal-based text editor with fast rendering and plugin-based syntax highlighting.

**Features:**
- Screen buffer rendering (instant display)
- Input buffering for fast paste
- Auto line-wrap at screen edge
- Plugin-based syntax highlighting

**Keyboard Shortcuts:**

| Shortcut | Action |
|----------|--------|
| `Ctrl+X` | Exit |
| `Ctrl+O` | Save |
| `Ctrl+K` | Cut line |
| `Ctrl+U` | Paste line |
| `Ctrl+W` | Search |
| `Ctrl+G` | Help |

**Syntax Highlighting Plugins:**

Create `.nano` files in `plugins/` folder:
```
Section [.cpp]{
    keyword: int, blue;
    keyword: if, magenta;
    keyword: return, magenta;
    preprocessor: include, green;
}
```

Bundled plugins: `cpp.nano` (C/C++), `python.nano`

---

### **Lish (Shell Interpreter)**

A native shell script interpreter (`lish.exe`) that runs `.sh` scripts on Windows with bash-like syntax. Features a complete lexer, parser, and AST executor.

**Usage:**
```bash
lish script.sh         # Run a script
lish -c "echo hello"   # Run inline command
lish                   # Interactive mode
```

**Supported Syntax:**
- Variables: `NAME="value"`, `$NAME`, `${NAME}`
- Control flow: `if/elif/else/fi`, `for/do/done`, `while/do/done`
- Test operators: `[ -f file ]`, `[ $a = $b ]`, `[ $a -eq 1 ]`
- Pipes and redirects: `|`, `>`, `>>`
- Functions: `function name() { ... }`
- Command substitution: `$(command)`

---

### **Integrated Bash Interpreter**

The shell includes a fully integrated Bash interpreter (`interpreter.hpp`) with 2700+ lines of code, providing:

**Components:**
- **Lexer**: Tokenizes shell script input with support for all bash tokens
- **Parser**: Builds an Abstract Syntax Tree (AST) for script execution
- **Executor**: Walks the AST and executes commands with proper control flow

**Supported Constructs:**
- Simple commands and pipelines
- Compound commands (`&&`, `||`)
- If/elif/else conditionals
- For and while loops
- Case statements
- Function definitions and calls
- Variable expansion and command substitution
- Redirections and background execution
- Break/continue/return statements

---

### **LVC (Version Control)**

A git-like version control system built into Linuxify with sophisticated algorithms.

**Features:**
- SHA-256 content-addressable object storage
- Myers diff algorithm (O(N+M)D optimal)
- Rolling hash delta compression (rsync-style)
- Colorized diff output

**Commands:**

| Command | Description |
|---------|-------------|
| `lvc init` | Initialize repository |
| `lvc add <file>` | Stage files |
| `lvc commit -m "msg"` | Commit changes |
| `lvc log` | View history |
| `lvc status` | Show working tree status |
| `lvc diff` | Show changes |
| `lvc versions <file>` | List file versions |
| `lvc show <commit>` | Show commit details |

---

### **LinMake (Build System)**

A CMake-like build system (`linmake.exe`) native to Linuxify with simple config files and auto-detection.

**Commands:**

| Command | Description |
|---------|-------------|
| `linmake init` | Create LMake config template |
| `linmake build` | Compile project (incremental) |
| `linmake build --release` | Optimized build (-O2) |
| `linmake build --debug` | Debug build with symbols |
| `linmake run` | Build and execute |
| `linmake clean` | Remove build artifacts |

**Features:**
- Auto-detects `.c`/`.cpp` source files
- Incremental builds (only recompiles changed files)
- Resolves bundled libraries automatically (z, ssl, curl, png, sqlite3, curses)
- Colored output with progress indicators

**LMake Config:**
```ini
project = myapp
type = executable
version = 1.0.0

[sources]
src/*.cpp
main.c

[libraries]
curl
sqlite3
z

[flags]
std = c++17
optimize = 2
static = true
```

---

### **Node (Graph File System)**

A fully functional graph-based virtual file system tool (`node.exe`). It creates and manages virtual disk images with a complete directory structure and interactive shell.

**Features:**
- Graph-based inode structure
- Virtual disk images stored in `linuxdb/nodes/`
- **Password protection support (Full Disk Encryption)**
- SHA-256 key derivation with PBKDF2-style iterations
- Interactive shell with colored output
- Full file persistence
- Block-based storage allocation

**Commands:**

| Command | Description |
|---------|-------------|
| `node init <name>` | Create new image in `linuxdb/nodes/` |
| `node init --password <name>` | Create password-protected image |
| `node mount <name>` | Mount image from `linuxdb/nodes/` |
| `node list` | List available file systems |
| `ls`, `cd`, `pwd` | Directory navigation |
| `mkdir`, `rmdir` | Directory management |
| `touch`, `rm`, `cat` | File operations |
| `Lino`, `echo` | File editing |
| `exit` | Unmount and exit |

---

### **Nexplore (GUI File Explorer)**

A Windows GUI application (`nexplore.exe`) for browsing `.node` file system images with an Explorer-style interface.

**Features:**
- Dark-themed modern UI with DWM integration
- Folder and file icons with visual selection
- Navigation with back button and breadcrumb path
- Mouse wheel scrolling support
- Double-click to open folders or view file contents
- **Encrypted `.node` file support** with password prompt
- SHA-256 key derivation for decryption

**Usage:**
- Launch via `nexplore` command or double-click
- Use File → Open to browse for `.node` images
- Navigate folders by double-clicking
- Click Back button or use history to return

---

### **GUI Terminal (Windux)**

A modern Windows GUI terminal emulator (`windux.exe`) with tabbed interface and ConPTY support.

**Features:**
- **Tabbed interface** - Multiple terminal sessions in one window
- **ConPTY integration** - Full pseudo-console support for TUI apps
- **Pixel-perfect font rendering** with custom bitmap font
- **ANSI/VT100 escape sequence processing**
- **TUI Support** - Alternate screen buffer for apps like Lino, htop
- **Text selection** with mouse drag and copy/paste
- **Scrollback buffer** with mouse wheel scrolling
- **Visual scrollbar** for navigation
- **Keyboard shortcuts**: Ctrl+T (new tab), Ctrl+W (close tab), Ctrl+Tab (switch)

**Usage:**
```bash
windux              # Launch GUI terminal
```

---

### **Crond (Cron Daemon)**

A Unix-style cron daemon (`crond.exe`) for scheduling recurring tasks on Windows.

**Features:**
- Full crontab syntax support (minute, hour, day, month, weekday)
- Special schedules: `@reboot`, `@daily`, `@hourly`, etc.
- Range and step expressions: `1-5`, `*/15`, `1,3,5`
- Script interpreter resolution via Registry
- IPC communication with the shell
- Windows startup integration
- Detailed logging to `linuxdb/crond.log`

**Commands:**

| Command | Description |
|---------|-------------|
| `crontab -e` | Edit crontab file |
| `crontab -l` | List scheduled jobs |
| `crond start` | Start daemon in background |
| `crond stop` | Stop running daemon |
| `crond status` | Check daemon status |
| `crond install` | Install to Windows startup |
| `crond uninstall` | Remove from Windows startup |

**Crontab Format:**
```
# ┌───────────── minute (0 - 59)
# │ ┌───────────── hour (0 - 23)
# │ │ ┌───────────── day of month (1 - 31)
# │ │ │ ┌───────────── month (1 - 12)
# │ │ │ │ ┌───────────── day of week (0 - 6) (Sunday = 0)
# │ │ │ │ │
# * * * * * command
0 9 * * * echo "Good morning!"
@daily ./backup.sh
```

---

## Directory Structure

```
Linuxify/
├── linuxify.exe        # Main shell (8900+ lines)
├── Lino.exe            # Text editor
├── cmds/               # Additional commands
│   ├── lish.exe        # Shell interpreter
│   ├── lvc.exe         # Version control
│   ├── node.exe        # Graph file system
│   ├── nexplore.exe    # GUI file explorer
│   ├── windux.exe      # GUI terminal
│   ├── crond.exe       # Cron daemon
│   ├── curl.exe        # HTTP client
│   ├── grep.exe        # Pattern search
│   ├── linmake.exe     # Build system
│   └── ...             # 20+ additional tools
├── cmds-src/           # Source files
│   ├── interpreter.hpp # Bash interpreter (2700+ lines)
│   ├── auto-suggest.hpp # Auto-suggestion system
│   ├── child_handler.hpp # Process management
│   ├── system_integrator.hpp # Nuke/unnuke functionality
│   ├── lish.cpp        # Lish interpreter
│   ├── lvc.hpp         # LVC implementation
│   ├── node.hpp        # Node FS implementation
│   ├── nexplore.cpp    # Nexplore GUI
│   ├── gui_terminal.cpp # Terminal GUI
│   └── crond.cpp       # Cron daemon
├── linuxdb/            # Database files
│   ├── registry.lin    # Registered commands
│   ├── packages.lin    # Package aliases (180+)
│   ├── history.lin     # Command history
│   ├── var.lin         # Persistent variables
│   ├── crontab         # Scheduled tasks
│   ├── crond.log       # Daemon log
│   └── nodes/          # Node FS images
├── plugins/            # Syntax highlighting plugins
│   ├── cpp.nano
│   └── python.nano
├── input_handler.hpp   # Input handling with auto-suggestions
├── signal_handler.hpp  # Signal handling
├── crash_handler.hpp   # Crash recovery
└── toolchain/          # Bundled MinGW-w64
    └── compiler/
        └── mingw64/
```

---

## Installation

Run the installer, which will:
1. Install Linuxify to `C:\Program Files\Linuxify`
2. Add to system PATH
3. Configure `CC` and `CXX` environment variables
4. Create Start Menu shortcuts

---

## Building from Source

```bash
# Build main shell
g++ -std=c++17 -static -o linuxify.exe main.cpp registry.cpp -lpsapi -lws2_32 -liphlpapi -lwininet -lwlanapi

# Build Lino
g++ -std=c++17 -O2 -o Lino.exe Lino.cpp

# Build lish
g++ -std=c++17 -static -o cmds/lish.exe cmds-src/lish.cpp

# Build lvc
g++ -std=c++17 -static -o cmds/lvc.exe cmds-src/lvc.cpp

# Build node
g++ -std=c++17 -static -o cmds/node.exe cmds-src/node.cpp

# Build nexplore (GUI)
g++ -std=c++17 -static -mwindows -o cmds/nexplore.exe cmds-src/nexplore.cpp -lgdi32 -luser32 -lcomdlg32 -ldwmapi -lshell32

# Build GUI terminal (windux)
g++ -std=c++17 -static -mwindows -o cmds/windux.exe cmds-src/gui_terminal.cpp cmds-src/terminal.res -lgdi32 -luser32 -ldwmapi

# Build crond
g++ -std=c++17 -static -o cmds/crond.exe cmds-src/crond.cpp -lws2_32

# Build installer
.\build-installer.ps1
```

---

## License

Linuxify is licensed under the GNU General Public License v3.0 (GPLv3). See the LICENSE file for details.

MinGW-w64 components are licensed under GPL/LGPL.
