#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>      // PATH_MAX
#include <signal.h> 
#include "linenoise.h"

#define MAX_PROCS 128

typedef struct {
    pid_t pid;
    char name[256];
    int background;
    int in_use;
} proc_entry_t;

proc_entry_t table[MAX_PROCS];

int add_process(pid_t pid, const char *name, int background) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (!table[i].in_use) {
            table[i].pid = pid;
            table[i].background = background;
            table[i].in_use = 1;
            strncpy(table[i].name, name, sizeof(table[i].name) - 1);
            table[i].name[sizeof(table[i].name) - 1] = '\0';
            return 0;
        }
    }
    fprintf(stderr, "mysh: process table full\n");
    return -1;
}

void remove_process(pid_t pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (table[i].in_use && table[i].pid == pid) {
            table[i].in_use = 0;
            return;
        }
    }
}

#define MAX_ARGS 64

typedef struct {
    char *argv[MAX_ARGS + 1];
    int argc;
    char *infile;     // NULL for now — used in Stage 6
    char *outfile;    // NULL for now — used in Stage 6
    int append;       // unused for now — used in Stage 6
    int background;   // unused for now — used in Stage 7
} command_t;

void tokenize(char *line, command_t *cmd) {
    memset(cmd, 0, sizeof(*cmd));

    char *p = line;
    while (*p != '\0') {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;

        if (*p == '>' || *p == '<') {
            int is_append = 0;
            char op = *p;
            p++;
            if (op == '>' && *p == '>') {
                is_append = 1;
                p++;
            }

            while (isspace((unsigned char)*p)) p++;
            if (*p == '\0') {
                fprintf(stderr, "mysh: syntax error: expected filename after '%s'\n",
                        op == '<' ? "<" : (is_append ? ">>" : ">"));
                cmd->argc = 0;   // treat as no-op
                return;
            }

            char *fname_start = p;
            while (*p != '\0' && !isspace((unsigned char)*p) && *p != '>' && *p != '<') {
                p++;
            }
            int had_more = (*p != '\0');
            if (had_more) *p = '\0';

            if (op == '<') {
                cmd->infile = fname_start;
            } else {
                cmd->outfile = fname_start;
                cmd->append = is_append;
            }

            if (had_more) p++;
            continue;
        }

        char *word_start = p;
        while (*p != '\0' && !isspace((unsigned char)*p) && *p != '>' && *p != '<') {
            p++;
        }
        int had_more = (*p != '\0');
        if (had_more) *p = '\0';

        if (cmd->argc < MAX_ARGS) {
            cmd->argv[cmd->argc++] = word_start;
        }

        if (had_more) p++;
    }

    cmd->argv[cmd->argc] = NULL;
}

int is_builtin(const char *name) {
    return strcmp(name, "pwd") == 0 || strcmp(name, "cd") == 0 || 
           strcmp(name, "ps") == 0 || strcmp(name, "kill") == 0;
}

void builtin_ps(void) {
    printf("%-8s %-8s %s\n", "PID", "BG", "COMMAND");
    for (int i = 0; i < MAX_PROCS; i++) {
        if (table[i].in_use) {
            printf("%-8d %-8s %s\n", table[i].pid,
                   table[i].background ? "yes" : "no", table[i].name);
        }
    }
}

void builtin_kill(command_t *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "mysh: kill: usage: kill <pid>\n");
        return;
    }

    char *endptr;
    long pid_l = strtol(cmd->argv[1], &endptr, 10);
    if (*endptr != '\0' || pid_l <= 0) {
        fprintf(stderr, "mysh: kill: invalid pid '%s'\n", cmd->argv[1]);
        return;
    }
    pid_t pid = (pid_t)pid_l;

    int found = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (table[i].in_use && table[i].pid == pid) { found = 1; break; }
    }
    if (!found) {
        fprintf(stderr, "mysh: kill: pid %d is not a process mysh is tracking\n", pid);
        return;
    }

    if (kill(pid, SIGTERM) != 0) {
        perror("mysh: kill");
        return;
    }

    waitpid(pid, NULL, 0);   // reap it so it doesn't linger as a zombie
    remove_process(pid);
    printf("mysh: killed process %d\n", pid);
}

void run_builtin(command_t *cmd) {
    if (strcmp(cmd->argv[0], "pwd") == 0) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("mysh: pwd");
            return;
        }
        printf("%s\n", cwd);
    } else if (strcmp(cmd->argv[0], "cd") == 0) {
        const char *target = cmd->argc >= 2 ? cmd->argv[1] : getenv("HOME");
        if (target == NULL) {
            fprintf(stderr, "mysh: cd: HOME not set\n");
            return;
        }
        if (chdir(target) != 0) {
            fprintf(stderr, "mysh: cd: %s: %s\n", target, strerror(errno));
        }
    } else if (strcmp(cmd->argv[0], "ps") == 0) {
        builtin_ps();
    } else if (strcmp(cmd->argv[0], "kill") == 0) {
        builtin_kill(cmd);
    }
}

void run_command(command_t *cmd) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        if (cmd->infile != NULL) {
            int fd = open(cmd->infile, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "mysh: %s: %s\n", cmd->infile, strerror(errno));
                _exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (cmd->outfile != NULL) {
            int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
            int fd = open(cmd->outfile, flags, 0644);
            if (fd < 0) {
                fprintf(stderr, "mysh: %s: %s\n", cmd->outfile, strerror(errno));
                _exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(cmd->argv[0], cmd->argv);
        fprintf(stderr, "mysh: %s: %s\n", cmd->argv[0], strerror(errno));
        _exit(127);
    }

        if (cmd->background) {
        add_process(pid, cmd->argv[0], 1);
        printf("[background] started %s (pid %d)\n", cmd->argv[0], pid);
        // no waitpid here — that's the whole point
    } else {
        add_process(pid, cmd->argv[0], 0);
        int status;
        waitpid(pid, &status, 0);
        remove_process(pid);
    }
}

#define MAX_SUBCOMMANDS 32

int split_on_ampersand(char *line, char *pieces[], int backgrounds[]) {
    int count = 0;
    char *start = line;
    char *amp;

    while ((amp = strchr(start, '&')) != NULL) {
        *amp = '\0';
        pieces[count] = start;
        backgrounds[count] = 1;
        count++;
        start = amp + 1;
    }

    // whatever's left after the last '&' (or the whole line, if no '&' at all)
    pieces[count] = start;
    backgrounds[count] = 0;
    count++;

    return count;
}

void reap_finished(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (!table[i].in_use) continue;

        int status;
        pid_t result = waitpid(table[i].pid, &status, WNOHANG);

        if (result == table[i].pid) {
            printf("[background] pid %d (%s) finished\n", table[i].pid, table[i].name);
            table[i].in_use = 0;
        }
    }
}

int main(void) {
    char *line;
    while (1) {
        reap_finished();
        line = linenoise("mysh$ ");
        if (line == NULL) break;
        
        char *pieces[MAX_SUBCOMMANDS];
        int backgrounds[MAX_SUBCOMMANDS];
        int num_pieces = split_on_ampersand(line, pieces, backgrounds);

        for (int i = 0; i < num_pieces; i++) {
            command_t cmd;
            tokenize(pieces[i], &cmd);
            cmd.background = backgrounds[i];   // tokenize zeroes this, so set it after

            if (cmd.argc > 0) {
                if (is_builtin(cmd.argv[0])) {
                    run_builtin(&cmd);
                } else {
                    run_command(&cmd);
                }
            }
        }

        linenoiseHistoryAdd(line);
        free(line);
    }
    return 0;
}