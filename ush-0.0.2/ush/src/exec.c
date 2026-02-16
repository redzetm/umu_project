#include "ush_exec.h"

#include "ush_env.h"
#include "ush_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int has_slash(const char *s) {
  for (; *s; s++) {
    if (*s == '/') return 1;
  }
  return 0;
}

static void child_exec_fallback_sh(const char *path, char *const argv[]) {
  // argv_sh = {"/bin/sh", path, argv[1..], NULL}
  char *argv_sh[USH_MAX_ARGS + 3];
  int ai = 0;
  argv_sh[ai++] = (char *)"/bin/sh";
  argv_sh[ai++] = (char *)path;
  for (int i = 1; argv[i] != NULL && ai < (int)(USH_MAX_ARGS + 2); i++) {
    argv_sh[ai++] = argv[i];
  }
  argv_sh[ai] = NULL;
  execve("/bin/sh", argv_sh, environ);
}

static void child_exec_path_or_die(const char *cmd, char *const argv[]) {
  if (has_slash(cmd)) {
    execve(cmd, argv, environ);
    if (errno == ENOEXEC) {
      child_exec_fallback_sh(cmd, argv);
    }
    ush_perrorf(cmd);
    _exit(126);
  }

  const char *path = ush_get_path_or_default();
  int saw_eacces = 0;

  const char *seg = path;
  while (1) {
    const char *colon = strchr(seg, ':');
    size_t seg_len = colon ? (size_t)(colon - seg) : strlen(seg);

    char dir[4096];
    if (seg_len == 0) {
      strcpy(dir, ".");
    } else {
      if (seg_len >= sizeof(dir)) seg_len = sizeof(dir) - 1;
      memcpy(dir, seg, seg_len);
      dir[seg_len] = '\0';
    }

    char full[8192];
    snprintf(full, sizeof(full), "%s/%s", dir, cmd);

    execve(full, argv, environ);
    if (errno == ENOEXEC) {
      child_exec_fallback_sh(full, argv);
    }

    if (errno == EACCES) saw_eacces = 1;

    if (!colon) break;
    seg = colon + 1;
  }

  if (saw_eacces) {
    ush_eprintf("ush: permission denied: %s\n", cmd);
    _exit(126);
  }

  ush_eprintf("ush: command not found: %s\n", cmd);
  _exit(127);
}

int ush_exec_pipeline(ush_state_t *st, const ush_pipeline_t *pl) {
  if (st == NULL || pl == NULL) return 1;

  int in_fd = -1;
  int out_fd = -1;

  if (pl->in_path != NULL) {
    in_fd = open(pl->in_path, O_RDONLY);
    if (in_fd < 0) {
      ush_perrorf("<");
      st->last_status = 1;
      return st->last_status;
    }
  }

  if (pl->out_path != NULL) {
    int flags = O_WRONLY | O_CREAT;
    flags |= pl->out_append ? O_APPEND : O_TRUNC;
    out_fd = open(pl->out_path, flags, 0644);
    if (out_fd < 0) {
      ush_perrorf(pl->out_append ? ">>" : ">");
      if (in_fd >= 0) close(in_fd);
      st->last_status = 1;
      return st->last_status;
    }
  }

  int pipes[USH_MAX_CMDS - 1][2];
  for (int i = 0; i < pl->ncmd - 1; i++) {
    if (pipe(pipes[i]) != 0) {
      ush_perrorf("pipe");
      for (int j = 0; j < i; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      if (in_fd >= 0) close(in_fd);
      if (out_fd >= 0) close(out_fd);
      st->last_status = 1;
      return st->last_status;
    }
  }

  pid_t pids[USH_MAX_CMDS];
  for (int i = 0; i < pl->ncmd; i++) pids[i] = -1;

  for (int i = 0; i < pl->ncmd; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      ush_perrorf("fork");
      st->last_status = 1;
      // cleanup
      for (int j = 0; j < pl->ncmd - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      if (in_fd >= 0) close(in_fd);
      if (out_fd >= 0) close(out_fd);
      return st->last_status;
    }

    if (pid == 0) {
      signal(SIGINT, SIG_DFL);

      if (i == 0) {
        if (in_fd >= 0) {
          dup2(in_fd, STDIN_FILENO);
        }
      } else {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }

      if (i == pl->ncmd - 1) {
        if (out_fd >= 0) {
          dup2(out_fd, STDOUT_FILENO);
        }
      } else {
        dup2(pipes[i][1], STDOUT_FILENO);
      }

      for (int j = 0; j < pl->ncmd - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      if (in_fd >= 0) close(in_fd);
      if (out_fd >= 0) close(out_fd);

      char *cmd = pl->cmds[i].argv[0];
      if (cmd == NULL) _exit(127);
      child_exec_path_or_die(cmd, pl->cmds[i].argv);
      _exit(127);
    }

    pids[i] = pid;
  }

  for (int j = 0; j < pl->ncmd - 1; j++) {
    close(pipes[j][0]);
    close(pipes[j][1]);
  }
  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  int last_status = 0;
  pid_t last_pid = pids[pl->ncmd - 1];

  for (int i = 0; i < pl->ncmd; i++) {
    int stw = 0;
    if (waitpid(pids[i], &stw, 0) < 0) {
      ush_perrorf("waitpid");
      continue;
    }
    if (pids[i] == last_pid) {
      if (WIFEXITED(stw)) {
        last_status = WEXITSTATUS(stw);
      } else if (WIFSIGNALED(stw)) {
        last_status = 128 + WTERMSIG(stw);
      } else {
        last_status = 1;
      }
    }
  }

  st->last_status = last_status;
  return st->last_status;
}
