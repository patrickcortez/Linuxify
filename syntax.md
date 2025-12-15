# Linuxify Shell Interpreter Syntax Reference

## Shebang Lines

```bash
#!default          # Use internal Linuxify interpreter
#!lish             # Use internal Linuxify interpreter (alias)
#!bash             # Use internal Linuxify interpreter (alias)
#!sh               # Use internal Linuxify interpreter (alias)
#!/path/to/interp  # Use external interpreter
```

---

## Variables

```bash
# Assignment
NAME="value"
COUNT=42

# Usage
echo $NAME
echo ${NAME}
echo "Hello $NAME"

# Special variables
$?                 # Exit code of last command
```

---

## Command Substitution

```bash
# Using $() - preferred
FILES=$(ls)
COUNT=$(wc -l < file.txt)
echo "Today is $(date)"

# Using backticks - legacy
FILES=`ls`
CURRENT=`pwd`

# In conditions
if [ -n "$(git status)" ]; then
    echo "Git status available"
fi

# Nested commands
echo "Files: $(echo $(ls) | wc -w)"
```

---

## Comments

```bash
# This is a comment
echo "hello"  # Inline comment
```

---

## Control Flow

### If Statement

```bash
if [ condition ]; then
    commands
fi

if [ condition ]; then
    commands
else
    other_commands
fi

if [ condition ]; then
    commands
elif [ other_condition ]; then
    other_commands
else
    fallback_commands
fi
```

### For Loop

```bash
for var in item1 item2 item3; do
    echo $var
done

for i in 1 2 3 4 5; do
    echo "Number: $i"
done
```

### While Loop

```bash
while [ condition ]; do
    commands
done
```

### Functions

```bash
# Define a function
greet() {
    echo "Hello, $1!"
    echo "All args: $@"
    echo "Arg count: $#"
}

# Alternative syntax
function say_hello {
    echo "Hi there, $1!"
}

# Call functions with arguments
greet "World"
greet "Alice" "Bob" "Charlie"

# Positional parameters:
# $0 - function name
# $1-$9 - arguments
# $# - number of arguments
# $@ - all arguments
```

### Case Statement (Switch)

```bash
case $variable in
    pattern1)
        commands
        ;;
    pattern2 | pattern3)
        commands
        ;;
    prefix*)
        echo "Starts with prefix"
        ;;
    *suffix)
        echo "Ends with suffix"
        ;;
    *)
        echo "Default case"
        ;;
esac

# Example
check_os() {
    case $1 in
        windows | win)
            echo "Windows detected"
            ;;
        linux | ubuntu)
            echo "Linux detected"
            ;;
        *)
            echo "Unknown OS"
            ;;
    esac
}
```

---

## Test Conditions `[ ... ]`

### File Tests

| Operator | Description           |
|----------|-----------------------|
| `-f FILE` | File exists          |
| `-d FILE` | Directory exists     |
| `-e FILE` | Path exists          |

### String Tests

| Operator  | Description           |
|-----------|-----------------------|
| `-z STR`  | String is empty       |
| `-n STR`  | String is not empty   |
| `a = b`   | Strings are equal     |
| `a == b`  | Strings are equal     |
| `a != b`  | Strings are not equal |

### Numeric Tests

| Operator   | Description              |
|------------|--------------------------|
| `a -eq b`  | Equal                    |
| `a -ne b`  | Not equal                |
| `a -lt b`  | Less than                |
| `a -le b`  | Less than or equal       |
| `a -gt b`  | Greater than             |
| `a -ge b`  | Greater than or equal    |

---

## Operators

### Pipes

```bash
cmd1 | cmd2              # Pipe output of cmd1 to cmd2
```

### Redirection

```bash
cmd > file               # Redirect stdout to file (overwrite)
cmd >> file              # Redirect stdout to file (append)
cmd < file               # Redirect stdin from file
```

### Command Chaining

```bash
cmd1 && cmd2             # Run cmd2 only if cmd1 succeeds
cmd1 || cmd2             # Run cmd2 only if cmd1 fails
cmd1; cmd2               # Run commands sequentially
cmd &                    # Run command in background
```

---

## Available Commands

### Interpreter Built-ins

| Command           | Description                          |
|-------------------|--------------------------------------|
| `echo <args>`     | Print arguments to stdout            |
| `cd <dir>`        | Change current directory             |
| `pwd`             | Print working directory              |
| `export VAR=val`  | Set environment variable             |
| `set`             | Display all variables                |
| `exit [code]`     | Exit the shell                       |
| `test <expr>`     | Evaluate conditional expression      |
| `[ expr ]`        | Same as test                         |
| `true`            | Return exit code 0                   |
| `false`           | Return exit code 1                   |
| `help`            | Display help information             |

### All Linuxify Commands (Fallback)

When running scripts with `#!default`, ALL Linuxify shell commands are available:

```bash
ls, cat, mkdir, rm, mv, cp, touch, chmod, chown, head, tail, grep, find,
wc, sort, uniq, diff, tar, Lino, whoami, hostname, date, uptime, df, ps,
kill, jobs, bg, fg, history, alias, unalias, source, man, clear, wget,
curl, ssh, scp, ping, netstat, ifconfig, setup, registry, sudo, ...
```

### External Commands (Registry & PATH)

Any command registered in the Linuxify registry or available in PATH works:

```bash
git, python, node, npm, code, docker, kubectl, sql, gcc, make, ...
```

**Example using external commands in a script:**
```bash
#!default

git status
python --version
node -e "console.log('Hello from Node!')"
```

---

## Quoting

```bash
"double quotes"          # Variables expanded, special chars preserved
'single quotes'          # Literal string, no expansion
```

---

## Example Script

```bash
#!default

NAME="World"
COUNT=3

echo "Hello $NAME!"

for i in 1 2 3; do
    if [ $i -eq 2 ]; then
        echo "Found two!"
    else
        echo "Number: $i"
    fi
done

if [ -f "myfile.txt" ]; then
    echo "File exists"
else
    echo "File not found"
fi
```
