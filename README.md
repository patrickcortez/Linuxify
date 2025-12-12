# Linuxify

A custom-written userland distribution for Windows that brings the Linux experience to Windows users.

## Features

- Nano Text Editor with User-defined Syntax Highlighting
- Linux Shell with 60+ Linux commands
- Lin Package Manager (winget-powered)
- Registry for packages and commands
- Bundled C++ Toolchain (MinGW-w64 GCC 15.x)
- Lish shell script interpreter
- LVC version control system
- Shebang (`./`) execution support

## Components

---

### **Linuxify Shell**

The main shell executable (`linuxify.exe`) — a fully-featured command-line interface providing Linux-like commands on Windows. Written in C++ with 3300+ lines and 60+ commands.

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

**Shell Features:**
- Syntax highlighting for commands as you type
- Persistent command history
- Piping: `cmd1 | cmd2`
- Redirection: `>` (write), `>>` (append)
- Background processes: `command &`
- Environment variables: `$VAR`, `${VAR}`

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
# This script runs with lish
echo "Hello from lish!"
```

```python
#!python
# This script runs with python (must be in registry)
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

### **Nano Text Editor**

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
# plugins/cpp.nano

set `{}` = blue; #set color of special characters 

Section [.cpp]{ #make sure to add the file extension of the language inside []
    keyword: int, blue;
    keyword: if, magenta;
    keyword: return, magenta;
    preprocessor: include, green; #To set preprocessor keywords
}
```

Bundled plugins: `cpp.nano` (C/C++), `python.nano`

---

### **Lish (Shell Interpreter)**

A native shell script interpreter (`lish.exe`) that runs `.sh` scripts on Windows with bash-like syntax. Features a complete lexer, parser, and AST executor. 

Note:
You always need the shebang line for your .sh scripts to run, without it linuxify or your windows wont know what to run it with.

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

---

## Directory Structure

```
Linuxify/
├── linuxify.exe        # Main shell
├── nano.exe            # Text editor
├── cmds/               # Additional commands
│   ├── lish.exe        # Shell interpreter
│   └── lvc.exe         # Version control
├── linuxdb/            # Database files
│   ├── registry.lin    # Registered commands
│   ├── packages.lin    # Package aliases (128+)
│   └── history.lin     # Command history
├── plugins/            # Nano syntax plugins
│   ├── cpp.nano
│   └── python.nano
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

# Build nano
.\toolchain\compiler\mingw64\bin\g++ -std=c++17 -O2 -o nano.exe nano.cpp

# Build installer
.\build-installer.ps1
```

---

## License

Linuxify is open source. MinGW-w64 components are licensed under GPL/LGPL.
