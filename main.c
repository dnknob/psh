#include <sys/types.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MAXJOBS 64

extern int builtin_cd(int argc, char *argv[]);
extern int builtin_pwd();
extern int builtin_ls(int argc, char *argv[]);
extern int builtin_echo(int argc, char *argv[]);
extern int builtin_clear();
extern int builtin_cat(int argc, char *argv[]);
extern int builtin_whoami();
extern int builtin_jobs(pid_t job_pids[], int *job_count);
extern int builtin_fg(int argc, char *argv[]);

char inb[1024];

void handle(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

void check(void) {
    if (geteuid() == 0) printf("# ");
    else printf("$ ");
    fflush(stdout);
}

int main(void) {
  pid_t job_pids[MAXJOBS];
  int job_count = 0;

    struct sigaction sa;
    sa.sa_handler = handle;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    while (1) {
        check();

        if (!fgets(inb, sizeof(inb), stdin)) {
            if (feof(stdin)) break;
            clearerr(stdin);
            continue;
        }

        inb[strcspn(inb, "\n")] = 0;
        if (strlen(inb) == 0) continue;

        int capacity = strlen(inb) / 2 + 2;
        char **argv = malloc(capacity * sizeof(char *));
        if (!argv) {
            perror("malloc");
            continue;
        }

        int argc = 0;
        char *tok = strtok(inb, " ");
        while (tok) {
            argv[argc++] = tok;
            tok = strtok(NULL, " ");
        }
        argv[argc] = NULL;

        if (argc == 0) {
            free(argv);
            continue;
        }

        int background = 0;
        if (strcmp(argv[argc-1], "&") == 0) {
          background = 1;
          argv[--argc] = NULL;
        }

        if (strcmp(argv[0], "exit") == 0) {
            free(argv);
            break;
        }

        if (strcmp(argv[0], "cd") == 0) {
            builtin_cd(argc, argv);
            free(argv);
            continue;
        }

        if (strcmp(argv[0], "pwd") == 0) {
            builtin_pwd();
            free(argv);
            continue;
        }

        if (strcmp(argv[0], "ls") == 0) {
            builtin_ls(argc, argv);
            free(argv);
            continue;
        }

        if (strcmp(argv[0], "echo") == 0) {
            builtin_echo(argc, argv);
            free(argv);
            continue;
        }

        if (strcmp(argv[0], "clear") == 0) {
            builtin_clear();
            free(argv);
            continue;
        }

        if (strcmp(argv[0], "cat") == 0) {
            builtin_cat(argc, argv);
            free(argv);
            continue;
        }

        if (strcmp(argv[0], "whoami") == 0) {
            builtin_whoami();
            free(argv);
            continue;
        }

        if (strcmp(argv[0], "jobs") == 0) {
            builtin_jobs(job_pids, &job_count);
            free(argv);
            continue;
        }
        if (strcmp(argv[0], "fg") == 0) {
            builtin_fg(argc, argv);
            free(argv);
            continue;
        }

        pid_t pid = fork();

        if (pid == 0) {
            if (execvp(argv[0], argv) == -1) {
                fprintf(stderr, "sh: %s: command not found\n", argv[0]);
            }
            free(argv);
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork");
        } else {
            if (background) {
                if (job_count < MAXJOBS) {
                    job_pids[job_count++] = pid;
                    printf("[%d] %d\n", job_count, pid);
                } else {
                    fprintf(stderr, "sh: job table full\n");
                }
            } else {
                int status;
                waitpid(pid, &status, WUNTRACED);
            }
        }
        free(argv);
    }
    return 0;
}
