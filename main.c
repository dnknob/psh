#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <libgen.h>
#include <unistd.h>
#include <pwd.h>
#include <termios.h>

#ifndef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define MAXJOBS 64
#define MAX_INPUT_SIZE 1024
#define MAX_PATH_SIZE 1024

extern int builtin_cd(int argc, char *argv[]);
extern int builtin_pwd();
extern int builtin_ls(int argc, char *argv[]);
extern int builtin_echo(int argc, char *argv[]);
extern int builtin_clear();
extern int builtin_cat(int argc, char *argv[]);
extern int builtin_whoami();
extern int builtin_jobs(pid_t job_pids[], int *job_count);
extern int builtin_fg(int argc, char *argv[], pid_t job_pids[], int *job_count);
extern int builtin_bg(int argc, char *argv[], pid_t job_pids[], int job_count);
extern int builtin_kill(int argc, char *argv[]);
extern int builtin_history(int argc, char *argv[]);
extern int builtin_export(int argc, char *argv[]);
extern int builtin_set(int argc, char *argv[]);

void handle_sigint(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

void display_prompt(void) {
    char hostname[256];
    char path[MAX_PATH_SIZE];
    char path_copy[MAX_PATH_SIZE];
    char *folder;
    const char *prompt_symbol;
    char *username = NULL;
    struct passwd *pw;

    username = getlogin();
    if (username == NULL) {
        pw = getpwuid(getuid());
        username = (pw != NULL) ? pw->pw_name : "unknown";
    }

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        snprintf(hostname, sizeof(hostname), "unknown-host");
    }
    hostname[sizeof(hostname) - 1] = '\0';

    if (getcwd(path, sizeof(path)) != NULL) {
        snprintf(path_copy, sizeof(path_copy), "%s", path);
        folder = basename(path_copy);
    } else {
        folder = "?";
    }

    prompt_symbol = (geteuid() == 0) ? "#" : "$";

    printf("%s@%s:%s %s ", username, hostname, folder, prompt_symbol);
    fflush(stdout);
}

int parse_command(char *input, char ***argv_ptr) {
    int argc = 0;
    char **argv = malloc((strlen(input) / 2 + 2) * sizeof(char *));
    
    if (argv == NULL) {
        perror("malloc");
        return -1;
    }

    char *token = strtok(input, " ");
    while (token != NULL) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    argv[argc] = NULL;

    *argv_ptr = argv;
    return argc;
}

int check_background(int *argc, char **argv) {
    if (*argc > 0 && strcmp(argv[*argc - 1], "&") == 0) {
        argv[--(*argc)] = NULL;
        return 1;
    }
    return 0;
}

int execute_builtin(int argc, char **argv, pid_t job_pids[], int *job_count) {
    if (strcmp(argv[0], "exit") == 0) return -1;
    if (strcmp(argv[0], "cd") == 0) { builtin_cd(argc, argv); return 1; }
    if (strcmp(argv[0], "pwd") == 0) { builtin_pwd(); return 1; }
    if (strcmp(argv[0], "ls") == 0) { builtin_ls(argc, argv); return 1; }
    if (strcmp(argv[0], "echo") == 0) { builtin_echo(argc, argv); return 1; }
    if (strcmp(argv[0], "clear") == 0) { builtin_clear(); return 1; }
    if (strcmp(argv[0], "cat") == 0) { builtin_cat(argc, argv); return 1; }
    if (strcmp(argv[0], "whoami") == 0) { builtin_whoami(); return 1; }
    if (strcmp(argv[0], "jobs") == 0) { builtin_jobs(job_pids, job_count); return 1; }
    if (strcmp(argv[0], "fg") == 0) { builtin_fg(argc, argv, job_pids, job_count); return 1; }
    if (strcmp(argv[0], "bg") == 0) { builtin_bg(argc, argv, job_pids, *job_count); return 1; }
    if (strcmp(argv[0], "kill") == 0) { builtin_kill(argc, argv); return 1; }
    if (strcmp(argv[0], "export") == 0) { builtin_export(argc, argv); return 1; }
    if (strcmp(argv[0], "set") == 0) { builtin_set(argc, argv); return 1; }
    if (strcmp(argv[0], "history") == 0) { builtin_history(argc, argv); return 1; }
    
    return 0;
}

void reap_background_jobs(pid_t job_pids[], int *job_count) {
    for (int i = 0; i < *job_count; i++) {
        int status;
        pid_t result = waitpid(job_pids[i], &status, WNOHANG | WUNTRACED);
        
        if (result > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
            for (int j = i; j < *job_count - 1; j++) {
                job_pids[j] = job_pids[j + 1];
            }
            (*job_count)--;
            i--;
        }
    }
}

void execute_external(char **argv, int background, pid_t job_pids[], int *job_count, pid_t shell_pgid) {
    pid_t pid = fork();
    
    if (pid == 0) {
        setpgid(0, 0);
        if (!background) {
            tcsetpgrp(STDIN_FILENO, getpid());
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        if (execvp(argv[0], argv) == -1) {
            fprintf(stderr, "psh: %s: command not found\n", argv[0]);
        }
        exit(EXIT_FAILURE);
        
    } else if (pid > 0) {
        setpgid(pid, pid);
        
        if (background) {
            if (*job_count < MAXJOBS) {
                job_pids[(*job_count)++] = pid;
                printf("[%d] %d\n", *job_count, pid);
            }
        } else {
            tcsetpgrp(STDIN_FILENO, pid);
            int status;
            waitpid(pid, &status, WUNTRACED);
            tcsetpgrp(STDIN_FILENO, shell_pgid);

            if (WIFSTOPPED(status)) {
                if (*job_count < MAXJOBS) {
                    job_pids[(*job_count)++] = pid;
                    printf("\n[%d] Stopped %d\n", *job_count, pid);
                }
            }
        }
    } else {
        perror("fork");
    }
}

void setup_signals(void) {
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

int main(void) {
    pid_t job_pids[MAXJOBS];
    int job_count = 0;
    char input_buffer[MAX_INPUT_SIZE];

    setup_signals();
    pid_t shell_pgid = getpgrp();

    while (1) {
        reap_background_jobs(job_pids, &job_count);

        display_prompt();

        if (!fgets(input_buffer, sizeof(input_buffer), stdin)) {
            if (feof(stdin)) {
                break;
            }
            clearerr(stdin);
            continue;
        }

        input_buffer[strcspn(input_buffer, "\n")] = '\0';

	#ifndef READLINE
        if (strlen(input_buffer) > 0) {
            add_history(input_buffer);
        }
	#endif

        if (strlen(input_buffer) == 0) {
            continue;
        }

        char **argv;
        int argc = parse_command(input_buffer, &argv);
        if (argc <= 0) {
            free(argv);
            continue;
        }

        int background = check_background(&argc, argv);

        int builtin_result = execute_builtin(argc, argv, job_pids, &job_count);
        if (builtin_result == -1) {
            free(argv);
            break;
        } else if (builtin_result == 1) {
            free(argv);
            continue;
        }

        execute_external(argv, background, job_pids, &job_count, shell_pgid);

        free(argv);
    }

    return 0;
}
