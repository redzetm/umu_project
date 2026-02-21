#include "ush_exec.h"

#include "ush_builtins.h"
#include "ush_expand.h"
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

static void set_child_sigint_default(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
}

static int open_in(const char *path) {
  if (path == NULL) return -1;
  return open(path, O_RDONLY);
}

static int open_out(const char *path, int append) {
  if (path == NULL) return -1;
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  return open(path, flags, 0644);
}

static int resolve_cmd(const char *cmd, char out[1024], int *out_fail_status) {
  if (out_fail_status) *out_fail_status = 127;
  if (cmd == NULL || cmd[0] == '\0') {
    if (out_fail_status) *out_fail_status = 127;
    return 1;
  }

  if (strchr(cmd, '/') != NULL) {
    snprintf(out, 1024, "%s", cmd);
    if (access(out, X_OK) == 0) return 0;
    if (out_fail_status) *out_fail_status = (errno == EACCES) ? 126 : 127;
    return 1;
  }

  const char *path = ush_get_path_or_default();
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", path);

  int saw_eacces = 0;

  for (char *save = NULL, *p = strtok_r(tmp, ":", &save); p != NULL; p = strtok_r(NULL, ":", &save)) {
    if (p[0] == '\0') continue;
    char cand[1024];
    snprintf(cand, sizeof(cand), "%s/%s", p, cmd);

    if (access(cand, X_OK) == 0) {
      snprintf(out, 1024, "%s", cand);
      return 0;
    }

    if (errno == EACCES) saw_eacces = 1;
  }

  if (out_fail_status) *out_fail_status = saw_eacces ? 126 : 127;
  return 1;
}

static void exec_with_sh_fallback(char *path, char *argv[]) {
  execve(path, argv, environ);
  if (errno == ENOEXEC) {
    // /bin/sh path args...
    // argv[0] を /bin/sh に差し替えた配列を作る
    int argc = 0;
    while (argv[argc] != NULL) argc++;

    char **nargv = (char **)calloc((size_t)argc + 2, sizeof(char *));
    if (nargv == NULL) _exit(126);

    static char sh0[] = "/bin/sh";
    nargv[0] = sh0;
    nargv[1] = path;
    for (int i = 1; i < argc; i++) nargv[i + 1] = argv[i];
    nargv[argc + 1] = NULL;

    execve("/bin/sh", nargv, environ);
  }
  _exit(126);
}

static int exec_external_cmd(char *argv[], int in_fd, int out_fd) {
  char path[1024];
  int fail = 127;
  if (resolve_cmd(argv[0], path, &fail) != 0) {
    return fail;
  }

  pid_t pid = fork();
  if (pid < 0) {
    ush_perrorf("fork");
    return 1;
  }

  if (pid == 0) {
    set_child_sigint_default();

    if (in_fd >= 0) {
      dup2(in_fd, STDIN_FILENO);
    }
    if (out_fd >= 0) {
      dup2(out_fd, STDOUT_FILENO);
    }

    // close inherited
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    exec_with_sh_fallback(path, argv);
    _exit(126);
  }

  int st = 0;
  if (waitpid(pid, &st, 0) < 0) {
    ush_perrorf("waitpid");
    return 1;
  }

  if (WIFEXITED(st)) return WEXITSTATUS(st);
  if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
  return 1;
}

static int expand_argv(
  const ush_state_t *st,
  const ush_cmd_t *cmd,
  char out_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  char *out_argv[USH_MAX_ARGS + 1]
) {
  ush_expand_ctx_t xctx;
  xctx.last_status = (st != NULL) ? st->last_status : 0;

  for (int i = 0; i < cmd->argc; i++) {
    parse_result_t r = ush_expand_word(&xctx, cmd->argv_quote[i], cmd->argv_raw[i], out_words[i], sizeof(out_words[i]));
    if (r == PARSE_UNSUPPORTED) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    if (r != PARSE_OK) {
      ush_eprintf("syntax error");
      return 2;
    }
    out_argv[i] = out_words[i];
  }
  out_argv[cmd->argc] = NULL;

  if (cmd->argc >= 1 && ush_is_assignment_word0(out_argv[0])) {
    ush_eprintf("unsupported syntax");
    return 2;
  }

  return 0;
}

static int expand_redir_path(
  const ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out[USH_MAX_TOKEN_LEN + 1]
) {
  if (raw == NULL) {
    out[0] = '\0';
    return 0;
  }

  ush_expand_ctx_t xctx;
  xctx.last_status = (st != NULL) ? st->last_status : 0;

  parse_result_t r = ush_expand_word(&xctx, q, raw, out, USH_MAX_TOKEN_LEN + 1);
  if (r == PARSE_UNSUPPORTED) {
    ush_eprintf("unsupported syntax");
    return 2;
  }
  if (r != PARSE_OK) {
    ush_eprintf("syntax error");
    return 2;
  }
  return 0;
}

static int exec_command(ush_state_t *st, const ush_cmd_t *cmd) {
  char words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *argv[USH_MAX_ARGS + 1];

  int er = expand_argv(st, cmd, words, argv);
  if (er != 0) return er;
  if (argv[0] == NULL) return 2;

  // builtins: パイプなし/リダイレクトなしのみ
  if (ush_is_builtin(argv[0])) {
    if (cmd->in_path_raw != NULL || cmd->out_path_raw != NULL) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    return ush_run_builtin(st, argv);
  }

  char in_path[USH_MAX_TOKEN_LEN + 1];
  char out_path[USH_MAX_TOKEN_LEN + 1];
  int xr;

  xr = expand_redir_path(st, cmd->in_quote, cmd->in_path_raw, in_path);
  if (xr != 0) return xr;
  xr = expand_redir_path(st, cmd->out_quote, cmd->out_path_raw, out_path);
  if (xr != 0) return xr;

  // redirect pre-open in parent
  int in_fd = -1;
  int out_fd = -1;

  if (cmd->in_path_raw != NULL) {
    in_fd = open_in(in_path);
    if (in_fd < 0) {
      ush_perrorf("open");
      return 1;
    }
  }

  if (cmd->out_path_raw != NULL) {
    out_fd = open_out(out_path, cmd->out_append);
    if (out_fd < 0) {
      if (in_fd >= 0) close(in_fd);
      ush_perrorf("open");
      return 1;
    }
  }

  int r = exec_external_cmd(argv, in_fd, out_fd);

  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  return r;
}

static int exec_pipeline(ush_state_t *st, const ush_pipeline_t *pl) {
  if (!pl->has_right) {
    return exec_command(st, &pl->left);
  }

  // Expand argv first (and detect builtins / env assignment) before any open/fork
  char l_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char r_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *l_argv[USH_MAX_ARGS + 1];
  char *r_argv[USH_MAX_ARGS + 1];

  int er;
  er = expand_argv(st, &pl->left, l_words, l_argv);
  if (er != 0) return er;
  er = expand_argv(st, &pl->right, r_words, r_argv);
  if (er != 0) return er;

  // builtins in pipe are unsupported
  if (ush_is_builtin(l_argv[0]) || ush_is_builtin(r_argv[0])) {
    ush_eprintf("unsupported syntax");
    return 2;
  }

  // redirect pre-open in parent (failure => no fork)
  int in_fd = -1;
  int out_fd = -1;

  char in_path[USH_MAX_TOKEN_LEN + 1];
  char out_path[USH_MAX_TOKEN_LEN + 1];
  int xr;

  xr = expand_redir_path(st, pl->left.in_quote, pl->left.in_path_raw, in_path);
  if (xr != 0) return xr;
  xr = expand_redir_path(st, pl->right.out_quote, pl->right.out_path_raw, out_path);
  if (xr != 0) return xr;

  if (pl->left.in_path_raw != NULL) {
    in_fd = open_in(in_path);
    if (in_fd < 0) {
      ush_perrorf("open");
      return 1;
    }
  }

  if (pl->right.out_path_raw != NULL) {
    out_fd = open_out(out_path, pl->right.out_append);
    if (out_fd < 0) {
      if (in_fd >= 0) close(in_fd);
      ush_perrorf("open");
      return 1;
    }
  }

  int pfd[2];
  if (pipe(pfd) != 0) {
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("pipe");
    return 1;
  }

  pid_t lp = fork();
  if (lp < 0) {
    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("fork");
    return 1;
  }

  if (lp == 0) {
    set_child_sigint_default();

    if (in_fd >= 0) dup2(in_fd, STDIN_FILENO);
    dup2(pfd[1], STDOUT_FILENO);

    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    // exec left
    char path[1024];
    int fail = 127;
    if (resolve_cmd(l_argv[0], path, &fail) != 0) _exit(fail);
    exec_with_sh_fallback(path, l_argv);
    _exit(126);
  }

  pid_t rp = fork();
  if (rp < 0) {
    // kill left? 0.0.3では簡易化して待って返す
    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("fork");
    return 1;
  }

  if (rp == 0) {
    set_child_sigint_default();

    dup2(pfd[0], STDIN_FILENO);
    if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);

    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    char path[1024];
    int fail = 127;
    if (resolve_cmd(r_argv[0], path, &fail) != 0) _exit(fail);
    exec_with_sh_fallback(path, r_argv);
    _exit(126);
  }

  // parent
  close(pfd[0]);
  close(pfd[1]);
  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  int st_l = 0;
  int st_r = 0;
  waitpid(lp, &st_l, 0);
  waitpid(rp, &st_r, 0);

  if (WIFEXITED(st_r)) return WEXITSTATUS(st_r);
  if (WIFSIGNALED(st_r)) return 128 + WTERMSIG(st_r);
  return 1;
}

static int eval_node(ush_state_t *st, const ush_ast_t *ast, int idx) {
  const ush_node_t *n = &ast->nodes[idx];

  switch (n->kind) {
    case NODE_PIPELINE:
      st->last_status = exec_pipeline(st, &n->pl);
      return st->last_status;
    case NODE_AND: {
      int ls = eval_node(st, ast, n->left);
      // 右側の $?: 左側の結果が見える必要がある
      st->last_status = ls;
      if (ls == 0) return eval_node(st, ast, n->right);
      return ls;
    }
    case NODE_OR: {
      int ls = eval_node(st, ast, n->left);
      st->last_status = ls;
      if (ls != 0) return eval_node(st, ast, n->right);
      return ls;
    }
  }

  return 1;
}

int ush_exec_ast(ush_state_t *st, const ush_ast_t *ast, int root) {
  if (st == NULL || ast == NULL || root < 0) return 1;
  int r = eval_node(st, ast, root);
  st->last_status = r;
  return r;
}
