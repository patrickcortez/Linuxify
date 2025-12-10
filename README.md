# C-Shell

A custom written shell called **Linuxify** in C++ for Users to directly interact with the
Windows kernel with linux syntax for its commands.

## Phase 1

- [X] Implement base of the shell
- [X] Implement basic file system commands: pwd, cd, ls, mkdir, rm, mv, cp and cat.
- [X] Run executables with `./` syntax
- [X] Shell session recognition (environment variables)
- [X] Console title set to "Linuxify Shell"

## Phase 2: Package Management

- [X] `lin get <package>` - Install packages using winget
- [X] `lin remove <package>` - Uninstall packages
- [X] `lin update` - Check for updates
- [X] `lin upgrade` - Upgrade all packages
- [X] `lin search <query>` - Search packages (auto-syncs to aliases)
- [X] `lin list` - List installed packages
- [X] `lin info <package>` - Show package info
- [X] `lin alias` - Show package aliases
- [X] `lin add <name> <id>` - Add custom alias
- [X] Smart package discovery - aliases grow as you search!
