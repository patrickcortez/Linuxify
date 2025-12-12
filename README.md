# Linuxify

A custom written shell called **Linuxify** in C++ for Users to directly interact with the
Windows kernel with linux syntax for its commands.

## Phase 1

- [X] Implement base of the shell
- [X] Implement basic file system commands: pwd, cd, ls, mkdir, rm, mv, cp and cat.
- [X] Run executables with `./` syntax
- [X] Shell session recognition (environment variables)
- [X] Console title set to "Linuxify Shell"

## Phase 2: Package Management, Registry and Utility Commands

- [X] `lin get <package>` - Install packages using winget
- [X] `lin remove <package>` - Uninstall packages
- [X] `lin update` - Check for updates
- [X] `lin upgrade` - Upgrade all packages
- [X] `lin search <query>` - Search packages (auto-syncs to aliases)
- [X] `lin list` - List installed packages
- [X] `lin info <package>` - Show package info
- [X] `lin alias` - Show package aliases
- [X] `lin add <name> <id>` - Add custom alias
- [X] Smart package discovery - aliases grow as you search
- [X] File system operations: touch, chmod, chown
- [X] External package registry (registry.cpp) - git, mysql, node, etc.
- [X] Utility commands: history, whoami, echo, env, printenv, export, which

## Phase 3: Process Management, Texts and Piping

- [X] Process Management: ps, kill, top, jobs, fg
- [X] Background processes: command &
- [X] Text commands: grep, head, tail, wc, sort, uniq, find
- [X] Output redirection: > (write), >> (append)
- [X] Piping: command | command


## Phase 4: System Information, Networking and Error handling

- [X] System Info: lsmem, lscpu, lshw, lsmount, lsof, lsblk, lsusb, lsnet
- [X] Networking: ip, ping, traceroute, nslookup, dig, curl, wget, netstat
- [X] Custom Networking: net show, net connect, net disconnect, net status
- [X] Error handling: Comprehensive try-catch with graceful error messages

# Phase 5: Interpreter and Shell Scripting

- [X] Interpreter: basically it will be called bash and it will run sh(shell) scripts natively
- [X] Shell Scripting: implement all shell syntaxes and features,
- [X] Implement source,test, function and alias.

## Phase 6: Bundled C++ Toolchain

- [X] Bundle MinGW-w64 GCC 15.x (UCRT, POSIX threads)
- [X] Compiler commands: gcc, g++, cc, c++
- [X] Build tools: make, ar, ld, as, ranlib
- [X] Debug tools: gdb, objdump, addr2line
- [X] Utility tools: strip, size, strings, nm, c++filt
- [X] Auto-configure PATH on install
- [X] Set CC/CXX environment variables for IDE auto-detection
