# Linuxify

A native Unix-like shell and development platform for Windows — featuring an integrated Bash interpreter, GUI terminal, version control, virtual file system, task scheduler, and bundled toolchain. Built entirely from scratch in C++.

## Features

- Lino Text Editor with User-defined Syntax Highlighting
- Linux Shell with 60+ Linux commands
- Lin Package Manager (winget-powered)
- Registry for packages and commands
- Bundled C++ Toolchain (MinGW-w64 GCC 15.x) with dev libraries
- Integrated Bash shell script interpreter
- LVC version control system
- Node graph file system with encryption support
- Nexplore - GUI file explorer for Node images
- GUI Terminal (windux) with tabs and ConPTY
- Crond daemon for scheduled tasks
- LinMake build system
- Shebang (`./`) execution support

## Components

---

### **Linuxify Shell**

The main shell executable (`linuxify.exe`) — a complete Unix-like shell environment for Windows with an integrated Bash interpreter. Comparable to bash or zsh, it features native script execution, control flow, variable expansion, and 60+ built-in commands. Written in C++ with 5500+ lines.

**Supported Commands:**

| Category | Commands |
|----------|----------|
| **File System** | `pwd`, `cd`, `ls`, `mkdir`, `rm`, `rmdir`, `mv`, `cp`, `cat`, `touch`, `chmod`, `chown`, `find` |
| **Text** | `grep`, `head`, `tail`, `wc`, `sort`, `uniq`, `echo` |
| **Process** | `ps`, `kill`, `top`, `htop`, `jobs`, `fg`, `&` (background) |
| **System Info** | `lsmem`, `free`, `lscpu`, `lshw`, `sysinfo`, `lsmount`, `lsblk`, `df`, `lsusb`, `lsnet`, `lsof` |
| **Networking** | `ip`, `ping`, `traceroute`, `nslookup`, `dig`, `curl`, `wget`, `netstat`, `ifconfig` |
| **Shell** | `history`, `whoami`, `env`, `export`, `which`, `clear`, `exit` |
| **Toolchain** | `gcc`, `g++`, `make`, `gdb`, `ar`, `ld`, `objdump`, `nm`, `strip` |
| **Scheduling** | `crontab`, `crond` |

**Shell Features:**
- Syntax highlighting for commands as you type
- Persistent command history
- Piping: `cmd1 | cmd2`
- Redirection: `>` (write), `>>` (append)
- Background processes: `command &`
- Environment variables: `$VAR`, `${VAR}`
- Integrated Bash interpreter for `.sh` scripts

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

**Important:** Without a shebang, `./script.sh` will fail with:
```
Script missing shebang line: script.sh
Add a shebang: #!<interpreter> (registry name or absolute path)
```

---

### **Setup Command**

The `setup` command registers .sh files with Windows, allowing you to run scripts from Explorer, cmd, and PowerShell.

**Commands:**

| Command | Description |
|---------|-------------|
| `setup install` | Register .sh files with Windows (requires admin) |
| `setup uninstall` | Remove .sh file association |
| `setup status` | Check current file association |

**What `setup install` does:**
1. Creates `LishScript` file type pointing to `lish.exe`
2. Associates `.sh` extension with `LishScript`
3. Adds `.SH` to `PATHEXT` for PowerShell compatibility
4. Requires Administrator privileges (will prompt for elevation)

**After installation, you can run .sh scripts:**
- From cmd: `script.sh` or `.\script.sh`
- From PowerShell: `lish script.sh` or `cmd /c .\script.sh`
- Double-click in Explorer
- From Linuxify: `./script.sh`

---

### **Lin Package Manager**

A winget-powered package manager with Linux-style syntax. Uses `linuxdb/packages.lin` for 128+ package aliases.

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
- Languages: `python`, `nodejs`, `rust`, `go`, `java`, `ruby`, `php`
- Tools: `git`, `docker`, `cmake`, `curl`, `wget`, `jq`, `fzf`
- Editors: `code`, `vim`, `neovim`, `notepad++`, `sublime`
- Browsers: `chrome`, `firefox`, `edge`, `brave`

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

Create `.Lino` files in `plugins/` folder:
```
Section [.cpp]{
    keyword: int, blue;
    keyword: if, magenta;
    keyword: return, magenta;
    preprocessor: include, green;
}
```

Bundled plugins: `cpp.Lino` (C/C++), `python.Lino`

---

### **Lish (Shell Interpreter)**

A native shell script interpreter (`lish.exe`) that runs `.sh` scripts on Windows with bash-like syntax. Features a complete lexer, parser, and AST executor.

**Usage:**
```bash
#!lish
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

The shell now includes a fully integrated Bash interpreter (`interpreter.hpp`) with 1600+ lines of code, providing:

**Components:**
- **Lexer**: Tokenizes shell script input with support for all bash tokens
- **Parser**: Builds an Abstract Syntax Tree (AST) for script execution
- **Executor**: Walks the AST and executes commands with proper control flow

**Supported Constructs:**
- Simple commands and pipelines
- Compound commands (`&&`, `||`)
- If/elif/else conditionals
- For and while loops
- Function definitions and calls
- Variable expansion and command substitution
- Redirections and background execution

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

### **GUI Terminal (windux)**

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

### **Bundled C++ Toolchain**

Linuxify bundles MinGW-w64 GCC 15.x providing a complete C++ development environment.

**Included Tools:**
- Compilers: `gcc`, `g++`, `cc`, `c++`
- Build tools: `make`, `ar`, `ld`, `as`, `ranlib`
- Debug tools: `gdb`, `objdump`, `addr2line`
- Utilities: `strip`, `size`, `strings`, `nm`, `c++filt`

**IDE Integration:**
After installation:
- `PATH` includes toolchain binaries
- `CC`or `g++` → `gcc.exe`
- `CXX`or `gcc` → `g++.exe`

IDEs like VS Code and CLion auto-detect the compiler.

**Bundled Development Libraries:**

The toolchain includes pre-compiled libraries for common development needs:

| Library | Version | Include | Link Flags | Purpose |
|---------|---------|---------|------------|---------|
| zlib | 1.3.1 | `<zlib.h>` | `-lz` | Compression |
| OpenSSL | 3.4.0 | `<openssl/ssl.h>` | `-lssl -lcrypto` | TLS/crypto |
| PDCurses | 4.5.3 | `<curses.h>` | `-lpdcurses` | Terminal UI |
| libpng | 1.6.53 | `<png.h>` | `-lpng -lz` | PNG images |
| libcurl | 8.11.1 | `<curl/curl.h>` | `-lcurl` | HTTP client |
| SQLite3 | 3.51.1 | `<sqlite3.h>` | `-lsqlite3` | Embedded database |

**Example:**
```bash
gcc myapp.c -lz -lssl -lcrypto -lcurl -lsqlite3 -o myapp.exe
```

---

## Directory Structure

```
Linuxify/
├── linuxify.exe        # Main shell (5500+ lines)
├── Lino.exe            # Text editor
├── cmds/               # Additional commands
│   ├── lish.exe        # Shell interpreter
│   ├── lvc.exe         # Version control
│   ├── node.exe        # Graph file system
│   ├── nexplore.exe    # GUI file explorer
│   ├── windux.exe      # GUI terminal
│   └── crond.exe       # Cron daemon
├── cmds-src/           # Source files
│   ├── interpreter.hpp # Bash interpreter (1600+ lines)
│   ├── lish.hpp        # Lish interpreter
│   ├── lvc.hpp         # LVC implementation
│   ├── node.hpp        # Node FS implementation
│   ├── nexplore.cpp    # Nexplore GUI
│   ├── gui_terminal.cpp # Terminal GUI (850+ lines)
│   └── crond.cpp       # Cron daemon
├── linuxdb/            # Database files
│   ├── registry.lin    # Registered commands
│   ├── packages.lin    # Package aliases (128+)
│   ├── history.lin     # Command history
│   ├── crontab         # Scheduled tasks
│   ├── crond.log       # Daemon log
│   └── nodes/          # Node FS images
├── plugins/            # Lino syntax plugins
│   ├── cpp.Lino
│   └── python.Lino
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
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -static -o linuxify.exe main.cpp registry.cpp

# Build Lino
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -O2 -o Lino.exe Lino.cpp

# Build lish
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -static -o cmds\lish.exe cmds-src\lish.cpp

# Build lvc
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -static -o cmds\lvc.exe cmds-src\lvc.cpp

# Build node
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -static -o cmds\node.exe cmds-src\node.cpp

# Build nexplore (GUI)
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -static -mwindows -o cmds\nexplore.exe cmds-src\nexplore.cpp -lgdi32 -luser32 -lcomdlg32 -ldwmapi -lshell32

# Build GUI terminal (windux)
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -static -mwindows -o cmds\windux.exe cmds-src\gui_terminal.cpp cmds-src\terminal.res -lgdi32 -luser32 -ldwmapi

# Build crond
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -static -o cmds\crond.exe cmds-src\crond.cpp -lws2_32

# Build installer
.\build-installer.ps1
```

---

## License

Linuxify is licensed under the GNU General Public License v3.0 (GPLv3). See the LICENSE file for details.

MinGW-w64 components are licensed under GPL/LGPL.
