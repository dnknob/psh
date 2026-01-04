#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <termios.h>

extern int sort(const struct dirent **a, const struct dirent **b);

int builtin_cd(int argc, char *argv[]) {
  const char *path;
    if (argc < 2) {
        path = getenv("HOME");
        if (!path) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
    } else {
        path = argv[1];
    }

    if (chdir(path) != 0) {
        perror("cd");
        return 1;
    }

    return 0;
}

int builtin_pwd() {
  char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
      printf("%s\n", cwd);
      return 0;
    } else {
      perror("pwd");
      return 1;
    }
}

int builtin_ls(int argc, char *argv[]) {
    struct dirent **namelist;
    const char *path = ".";

    if (argc > 1) {
        path = argv[1];
    }

    int n = scandir(path, &namelist, NULL, sort);

    if (n < 0) {
        perror("ls");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        printf("%s\n", namelist[i]->d_name);
        free(namelist[i]);
    }
    free(namelist);

    return 0;
}


int builtin_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (printf("%s%s", argv[i], (i < argc - 1) ? " " : "") < 0) {
            perror("echo");
            return 1;
        }
    }
    printf("\n");
    return 0;
}

int builtin_clear() {
  if (printf("\033[H\033[2J") < 0) {
    fprintf(stderr, "clear: Failed to clear screen\n");
    return 1;
  }
  return 0;
}

int builtin_cat(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "cat: Missing file operand\n");
        return 1;
    }

    FILE *fptr = fopen(argv[1], "r");

    if (fptr == NULL) {
        perror("cat"); 
        return 1;
    }

    int ch; 
    while ((ch = fgetc(fptr)) != EOF) {
        if (putchar(ch) == EOF) {
            perror("cat: Write error");
            fclose(fptr);
            return 1;
        }
    }

    if (ferror(fptr)) {
        fprintf(stderr, "cat: Error reading from %s\n", argv[1]);
    }

    fclose(fptr);
    return 0;
}

int builtin_whoami() {
  uid_t uid = geteuid();
  struct passwd *pw = getpwuid(uid);

  if (pw) {
    printf("%s\n", pw->pw_name);
  } else {
    perror("whoami: failed to grab user information");
  }
  return 0;
}

int builtin_jobs(pid_t job_pids[], int *job_count) {
    if (*job_count <= 0) {
        printf("No active background jobs.\n");
        return 0;
    }

    for (int i = 0; i < *job_count; i++) {
        int status;
        pid_t result = waitpid(job_pids[i], &status, WNOHANG | WUNTRACED);

        if (result == 0) {
            printf("[%d] Running    (PID: %d)\n", i + 1, job_pids[i]);
        } else {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                printf("[%d] Done       (PID: %d)\n", i + 1, job_pids[i]);

                for (int j = i; j < *job_count - 1; j++) {
                    job_pids[j] = job_pids[j + 1];
                }
                (*job_count)--;
                i--;
            } else if (WIFSTOPPED(status)) {
                printf("[%d] Stopped    (PID: %d)\n", i + 1, job_pids[i]);
            }
        }
    }
    return 0;
}

int builtin_fg(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: fg <PID>\n");
        return 1;
    }

    pid_t target_pid = (pid_t)atoi(argv[1]);
    int status;

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    if (kill(target_pid, SIGCONT) < 0) {
        perror("fg: kill (SIGCONT)");
        return 1;
    }

    if (tcsetpgrp(0, getpgid(target_pid)) < 0) {
        perror("fg: tcsetpgrp");
        return 1;
    }

    waitpid(target_pid, &status, WUNTRACED);

    tcsetpgrp(0, getpgrp());

    signal(SIGTTOU, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);

    if (WIFSTOPPED(status)) {
        printf("\n[%d] Stopped\n", target_pid);
    }

    return 0;
}
