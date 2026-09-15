# mysh — A Mini Shell

`mysh` is a simple Unix shell implemented in C. It supports running external programs, a handful of built-in commands, I/O redirection, background job execution, and basic process tracking — with line editing and history powered by `linenoise`.

## Features

- **Command execution** — runs any external program found via `PATH` using `fork()` + `execvp()`.
- **Built-in commands**
  - `pwd` — print the current working directory.
  - `cd [dir]` — change directory (defaults to `$HOME` if no argument is given).
  - `ps` — list processes currently tracked by the shell (PID, background status, command name).
  - `kill <pid>` — send `SIGTERM` to a tracked process and reap it.
- **I/O redirection**
  - `< file` — redirect standard input from a file.
  - `> file` — redirect standard output to a file (truncate).
  - `>> file` — redirect standard output to a file (append).
- **Background execution** — append `&` after a command to run it in the background without blocking the shell. Multiple `&`-separated commands on one line are each launched independently.
- **Process tracking** — a fixed-size table (`MAX_PROCS = 128`) tracks running child PIDs, names, and whether they're background jobs. Finished background jobs are automatically reaped and reported before the next prompt.
- **Line editing & history** — powered by [`linenoise`](https://github.com/antirez/linenoise), giving you arrow-key history navigation and basic readline-style editing.

## Building

You'll need `linenoise.c`/`linenoise.h` available in your build. For example:

```sh
gcc -o mysh main.c linenoise.c
```

(Adjust filenames to match your project layout.)

## Running

```sh
./mysh
```

You'll get a `mysh$ ` prompt. Example session:

```
mysh$ pwd
/home/user/mysh
mysh$ ls > out.txt
mysh$ cat < out.txt
mysh$ sleep 10 &
[background] started sleep (pid 12345)
mysh$ ps
PID      BG       COMMAND
12345    yes      sleep
mysh$ kill 12345
mysh: killed process 12345
mysh$ exit    # Ctrl+D also works
```

## Known limitations

- No pipes (`|`) — only single commands per `&`-separated segment.
- No environment variable expansion or globbing.
- No quoting/escaping support in the tokenizer.
- `argv[64]` and `MAX_PROCS`/`MAX_SUBCOMMANDS` limits are fixed at compile time.
- `exit` is handled implicitly via EOF (Ctrl+D), not as a named builtin.

## Project structure

- `tokenize()` — parses a line into a `command_t` (argv, redirection targets).
- `run_command()` / `run_builtin()` — dispatches to `fork`+`exec` or built-in logic.
- `split_on_ampersand()` — splits a line into `&`-separated sub-commands.
- `add_process()` / `remove_process()` / `reap_finished()` — manage the process table.
