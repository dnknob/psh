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

#ifndef READLINE
#include <readline/history.h>
#endif /* READLINE */

typedef struct {
    pid_t pid;
    char command[256];
    int is_stopped;
} Job;

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

int builtin_jobs(Job jobs[], int *job_count) {
    if (*job_count <= 0) {
        printf("No active background jobs.\n");
        return 0;
    }
    for (int i = 0; i < *job_count; i++) {
        printf("[%d] %-10s %s (PID: %d)\n", 
               i + 1, jobs[i].is_stopped ? "Stopped" : "Running", 
               jobs[i].command, jobs[i].pid);
    }
    return 0;
}

int builtin_fg(int argc, char *argv[], Job jobs[], int *job_count) {
    if (argc < 2) { printf("usage: fg <%%jobid>\n"); return 1; }
    int idx = atoi(&argv[1][1]) - 1;
    if (idx < 0 || idx >= *job_count) return 1;

    pid_t pid = jobs[idx].pid;
    int status;
    tcsetpgrp(STDIN_FILENO, pid);
    kill(pid, SIGCONT);
    waitpid(pid, &status, WUNTRACED);
    tcsetpgrp(STDIN_FILENO, getpgrp());

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        for (int j = idx; j < *job_count - 1; j++) jobs[j] = jobs[j + 1];
        (*job_count)--;
    } else if (WIFSTOPPED(status)) {
        jobs[idx].is_stopped = 1;
    }
    return 0;
}

int builtin_bg(int argc, char *argv[], Job jobs[], int job_count) {
    if (argc < 2) { printf("usage: bg <%%jobid>\n"); return 1; }
    int idx = atoi(&argv[1][1]) - 1;
    if (idx < 0 || idx >= job_count) return 1;

    jobs[idx].is_stopped = 0;
    kill(jobs[idx].pid, SIGCONT);
    printf("[%d] %s resumed in background\n", idx + 1, jobs[idx].command);
    return 0;
}

int builtin_kill(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: kill <PID or %%jobid>\n");
        return 1;
    }
    
    pid_t pid;
    if (argv[1][0] == '%') {
        return 0; 
    }
    
    pid = (pid_t)atoi(argv[1]);
    if (kill(pid, SIGKILL) == -1) {
        perror("kill");
        return 1;
    }
    printf("Process %d terminated.\n", pid);
    return 0;
}

int builtin_export(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s key=value command [args...]\n", argv[0]);
        return 1;
    }

    if (putenv(argv[1]) != 0) {
        perror("export: putenv failed");
        return 1;
    }

    char **new_argv = &argv[2];

    printf("Executing child with environment variable: %s\n", argv[1]);

    if (execvp(new_argv[0], new_argv) == -1) {
        perror("export: execution failed");
        exit(1);
    }

    return 0;
}

int builtin_set(int argc, char *argv[]) {
    char buffer[256];

    FILE *fp = popen("sh -c 'set'", "r");

    if (fp == NULL) {
        perror("popen failed");
        return 1;
    }

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        printf("%s", buffer);
    }
    pclose(fp);
    return 0;
}

int builtin_history(int argc, char *argv[]) {
  const char *history_path = ".psh_history";

  if (argc < 2) {
      printf("usage: %s [--list]\n", argv[0]);
      return 1;
  }

  if (strcmp(argv[1], "--list") == 0) {
      read_history(history_path);

      HIST_ENTRY **list = history_list();
      if (list) {
          printf("--- Past Command History ---\n");
          for (int i = 0; list[i]; i++) {
              printf("%d: %s\n", i + history_base, list[i]->line);
          }
      } else {
          printf("No history found\n");
      }
  } else {
      printf("history: Unknown argument: %s\n", argv[1]);
  }

  write_history(history_path);
  return 0;
}