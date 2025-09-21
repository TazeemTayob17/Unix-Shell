# WitsShell - Unix-like Command Line Interpreter

**University of the Witwatersrand · School of Computer Science & Applied Mathematics**  
**Course:** COMS3010A Shell Project

A simple Unix-like command line interpreter that supports interactive and batch modes, I/O redirection, parallel command execution, and essential built-in commands.

## Features

- **Interactive & Batch Modes** - Run commands interactively or from script files
- **Process Management** - Uses `fork()`, `execv()`, and `wait()` for command execution
- **I/O Redirection** - Output redirection with `>` operator
- **Parallel Commands** - Execute multiple commands simultaneously with `&`
- **Built-in Commands** - `exit`, `cd`, and `path` commands
- **Path Resolution** - Configurable search paths for executable resolution

## Building and Running

### Build the Project

```bash
make
```

### Run the Shell

```bash
# Interactive mode
make run

# Or run directly after building
./witsshell

# Batch mode (run with a script file)
./witsshell <batchfile>
```

### Clean Build Files

```bash
make clean
```

## Usage Examples

```bash
# Basic command execution
witsshell> ls -la

# Change directory
witsshell> cd /home/user

# Output redirection
witsshell> ls -la /tmp > output.txt

# Parallel command execution
witsshell> ls & echo "hello" & pwd

# Set search path
witsshell> path /bin /usr/bin /usr/local/bin

# Exit shell
witsshell> exit
```

## Built-in Commands

| Command | Description | Arguments |
|---------|-------------|-----------|
| `exit` | Terminates the shell | None |
| `cd <dir>` | Changes current directory | Exactly one directory path |
| `path [dir1 dir2 ...]` | Sets executable search paths | Zero or more directory paths |

## Project Structure

The shell implements core Unix shell functionality:

- **Command Parsing** - Tokenizes input and handles whitespace
- **Process Creation** - Forks child processes for command execution
- **Path Resolution** - Searches configured directories for executables
- **I/O Redirection** - Redirects stdout and stderr to files using `>`
- **Parallel Execution** - Runs multiple commands concurrently with `&`
- **Error Handling** - Standardized error reporting for all failure cases

## License

This project is for educational purposes as part of COMS3010A coursework at the University of the Witwatersrand.
