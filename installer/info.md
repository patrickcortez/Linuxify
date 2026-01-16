# Linuxify

**A Native Unix-like Shell for Windows**

---

## What is Linuxify?

Linuxify is a Unix-style shell that runs natively on Windows. It is not an emulator, not a compatibility layer, and not a virtual machine. It is a standalone, self-contained shell environment built from the ground up in C++, designed for users who prefer the Unix command-line workflow.

**Core Philosophy:**
- **Native Execution**: Every command is a native Windows executable. There is no translation layer or runtime overhead.
- **Self-Contained**: The installer provides a complete, working shell environment with no external dependencies.
- **Secure by Design**: Features like the encrypted virtual filesystem keep your data protected.

---

## Features

| | |
|---|---|
| **60+ Built-in Commands** | `ls`, `grep`, `cat`, `find`, `ps`, `kill`, `curl`, `ping`, `whoami`, and more. |
| **Integrated Shell Interpreter** | Full `.sh` script support with variables, loops, functions, and pipes. |
| **Syntax Highlighting** | Real-time coloring of commands, arguments, and flags as you type. |
| **Auto-Suggestions** | Ghost text predictions from command history and the current directory. |
| **Piping and Redirection** | Standard `|`, `>`, `>>`, `<`, `&&`, `||` operators work as expected. |
| **Background Jobs** | Run processes in the background with `&` and manage them with `jobs` and `fg`. |
| **Shebang Execution** | Run scripts with `./script.sh`; the shell parses the `#!` line to find the right interpreter. |

---

## The Shell Architecture

Linuxify is built on a custom, stackless state machine architecture. This design eliminates recursion-related stack overflows and provides a clean separation between input, execution, and rendering.

### The Continuation Model

At its core, the shell is driven by a **trampoline-based state machine**. Instead of a traditional `while (true) { read(); execute(); }` loop that can lead to deep call stacks, the shell uses "Continuations."

A `Continuation` is an object representing a single unit of work. When executed, it performs its task and then returns the *next* `Continuation` to run. The main loop (the "trampoline") simply bounces between these returned states until one returns `nullptr`, signaling termination.

```
                   ┌─────────────────────────────────────────┐
                   │              ShellEngine                │
                   │        (The Trampoline Loop)            │
                   │                                         │
                   │  while (current != nullptr) {           │
                   │      current = current->run(context);   │
                   │  }                                      │
                   └──────────────────┬──────────────────────┘
                                      │
          ┌───────────────────────────┼───────────────────────────┐
          │                           │                           │
          ▼                           ▼                           ▼
   ┌─────────────┐             ┌─────────────┐             ┌─────────────┐
   │ StatePrompt │ ──────────► │StateReadInput│ ──────────► │ StateExecute│
   │             │             │   (Polls)    │             │             │
   │ Display PS1 │             │  Keystroke   │             │   Run Cmd   │
   └─────────────┘             └─────────────┘             └──────┬──────┘
          ▲                                                       │
          └───────────────────────────────────────────────────────┘
```

### ShellContext: The Persistent World

The `ShellContext` struct holds all state that must persist across state transitions:
- **Process State**: `running` flag, `lastExitCode`, `isAdmin`.
- **Environment**: `currentDir`, `commandHistory`.
- **Interpreter**: The Bash-like interpreter instance.
- **Variables**: Session environment variables and exported arrays.

This structure is passed by reference to every `Continuation`, allowing states to read and modify the shell's world.

### The Three Core States

1.  **StatePrompt**: Handles visual formatting and displays the shell prompt. It manages blank-line spacing to ensure output is visually clean.
2.  **StateReadInput**: Drives the `InputHandler` in a non-blocking poll loop. It captures keystrokes, handles arrow keys, and manages auto-suggestions. When the user presses Enter, it transitions to `StateExecute`.
3.  **StateExecute**: Takes the input line, passes it to the command execution logic, and then transitions back to `StatePrompt`.

---

## The Input Pipeline

The input system bypasses standard library I/O entirely. It uses the Windows Console API (`ReadConsoleInput`) to capture every keystroke, arrow key, and mouse event directly.

### InputHandler

The `InputHandler` class manages:
- **`buffer`**: The current command line string.
- **`cursorPos`**: The logical cursor position.
- **`historyIndex`**: For navigating through command history with the up/down arrows.
- **`ghostSuggestion`**: The currently displayed auto-suggestion.

On each input event, the handler determines if it is a printable character, a control sequence, or a navigation key. This allows:
1.  **Immediate Redraw**: Any change to the buffer triggers a re-render with updated syntax highlighting.
2.  **Prediction**: After a small debounce, the handler queries the `AutoSuggest` module to display ghost text.

### AutoSuggest

This module provides context-aware suggestions for Tab completion:
- **Command Context**: At the start of a line, it suggests from 60+ built-in commands.
- **Path Context**: If the current token resembles a path, it lists matching filesystem entries.

### AutoNav

A quality-of-life feature: if the user types a bare path like `..` or `~/projects`, and it resolves to an existing directory, the shell performs an implicit `cd`.

---

## The Execution Engine

When a command is submitted, it is processed by a multi-stage pipeline.

### Lexer (Tokenizer)

The input string is broken into distinct tokens: `WORD`, `PIPE`, `AMPERSAND`, `REDIRECT_OUT`, `AND_IF`, `OR_IF`, `SEMICOLON`, and more. Quote handling and escape sequences are resolved at this stage.

### Parser (AST Builder)

Tokens are parsed into an Abstract Syntax Tree (AST). The parser understands:
- `SimpleCommand`: A command and its arguments.
- `Pipeline`: `cmd1 | cmd2 | cmd3`.
- `List`: `cmd1 && cmd2 || cmd3; cmd4`.
- `IfStatement`, `ForLoop`, `WhileLoop`, `FunctionDefinition`.

### Executor (AST Walker)

The executor iterates through the AST to run commands. For pipelines, it creates native Windows pipes using `CreatePipe`. For logic operators, it checks exit codes to short-circuit execution.

---

## Process and Signal Management

### The "Raw Mode" Problem

The shell runs in a custom "Raw Mode" where character echoing and line buffering are disabled. This is essential for real-time syntax highlighting, but it would break child processes that expect standard terminal behavior.

### ChildHandler

`ChildHandler` is the solution. Before spawning a child process, it:
1.  Opens fresh, inheritable handles to `CONIN$` and `CONOUT$`.
2.  Configures "Cooked Mode" (`ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT`) on the child's input handle.
3.  Temporarily unregisters the shell's `Ctrl+C` handler.
4.  Launches the child using `CreateProcessA` with `STARTF_USESTDHANDLES`.
5.  After the child exits, restores Raw Mode and re-registers the shell's handlers.

### SignalHandler and InputDispatcher

The `InputDispatcher` is a singleton that manages a queue of `INPUT_RECORD` events. Its `poll()` method reads all available console input, checks for registered hotkeys, and buffers unhandled events for the `InputHandler`.

The `ConsoleCtrlHandler` function intercepts `CTRL_C_EVENT` and similar signals, routing them to the shell's interrupt logic instead of terminating the process.

---

## Crash Forensics

If the shell encounters a fatal error, the `Interrupt` module's Vectored Exception Handler (VEH) generates a detailed crash report.

**The report includes:**
- CPU register dump.
- x64 disassembly at the crash site.
- Full stack trace with symbol resolution (parsed directly from the PEB and PE headers).
- List of all loaded modules.
- Raw memory dump around the faulting address.

This is accomplished without any dependency on external debug symbol libraries like `DbgHelp.dll`.

---

## Installation

1.  Run `LinuxifyInstaller.exe`.
2.  Accept the license agreement.
3.  Choose installation options (Add to PATH, Context Menu integration, etc.).
4.  Click Install.

After installation, open any terminal and type `linuxify` to start the shell.

---

## License

Linuxify is proprietary, closed-source software. All rights reserved.
