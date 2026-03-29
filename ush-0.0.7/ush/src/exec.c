#include "ush_exec.h"

#include "ush_builtins.h"
#include "ush_expand.h"
#include "ush_env.h"
#include "ush_parse.h"
#include "ush_script.h"
#include "ush_tokenize.h"
#include "ush_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

enum { USH_ESC = 1 };

static void set_child_sigint_default(void);

static void trim_cmdsub_output(char *s) {
  if (s == NULL) return;

  // trim trailing newlines (\r/\n)
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[--n] = '\0';
  }
}

parse_result_t ush_exec_capture_stdout(
  const ush_state_t *base_state,
  const char *cmdline,
  char *out,
  size_t out_cap
) {
  if (out == NULL || out_cap == 0) return PARSE_TOO_LONG;
  out[0] = '\0';
  if (cmdline == NULL) return PARSE_OK;

  int fds[2];
  if (pipe(fds) != 0) {
    return PARSE_SYNTAX_ERROR;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return PARSE_SYNTAX_ERROR;
  }

  if (pid == 0) {
    // child
    set_child_sigint_default();

    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);

    ush_state_t st;
    st.last_status = (base_state != NULL) ? base_state->last_status : 0;
    st.script_path = (base_state != NULL && base_state->script_path != NULL) ? base_state->script_path : "ush";
    st.pos_argc = (base_state != NULL) ? base_state->pos_argc : 0;
    st.pos_argv = (base_state != NULL) ? base_state->pos_argv : NULL;

    token_t toks[USH_MAX_TOKENS];
    int ntok = 0;
    char tokbuf[USH_MAX_LINE_LEN + 1];
    parse_result_t tr = ush_tokenize(cmdline, toks, &ntok, tokbuf);
    if (tr != PARSE_OK) _exit(2);

    ush_script_t sc;
    int root = -1;
    parse_result_t pr = ush_parse_script(toks, ntok, &sc, &root);
    if (pr != PARSE_OK) _exit(2);

    ush_exec_script(&st, toks, ntok, &sc, root);
    _exit(st.last_status & 255);
  }

  // parent
  close(fds[1]);
  size_t wi = 0;
  while (1) {
    if (wi + 1 >= out_cap) {
      close(fds[0]);
      // 子の回収だけはしておく
      int st = 0;
      waitpid(pid, &st, 0);
      return PARSE_TOO_LONG;
    }

    ssize_t r = read(fds[0], out + wi, out_cap - wi - 1);
    if (r < 0) {
      close(fds[0]);
      int st = 0;
      waitpid(pid, &st, 0);
      return PARSE_SYNTAX_ERROR;
    }
    if (r == 0) break;
    wi += (size_t)r;
  }
  out[wi] = '\0';
  close(fds[0]);

  int st = 0;
  waitpid(pid, &st, 0);

  trim_cmdsub_output(out);
  return PARSE_OK;
}

static void unmark_inplace(char *s) {
  if (s == NULL) return;
  size_t ri = 0;
  size_t wi = 0;
  while (s[ri] != '\0') {
    if ((unsigned char)s[ri] == (unsigned char)USH_ESC) {
      if (s[ri + 1] == '\0') break;
      s[wi++] = s[ri + 1];
      ri += 2;
      continue;
    }
    s[wi++] = s[ri++];
  }
  s[wi] = '\0';
}

static int marked_to_glob_pattern(const char *in, char *out, size_t cap) {
  if (out == NULL || cap == 0) return 1;
  out[0] = '\0';
  if (in == NULL) return 0;

  size_t wi = 0;
  for (size_t ri = 0; in[ri] != '\0';) {
    if ((unsigned char)in[ri] == (unsigned char)USH_ESC) {
      if (in[ri + 1] == '\0') return 1;
      if (wi + 3 > cap) return 1;
      out[wi++] = '\\';
      out[wi++] = in[ri + 1];
      out[wi] = '\0';
      ri += 2;
      continue;
    }
    if (wi + 2 > cap) return 1;
    out[wi++] = in[ri++];
    out[wi] = '\0';
  }
  return 0;
}

static int has_glob_meta_unescaped_marked(const char *s) {
  if (s == NULL) return 0;
  for (size_t i = 0; s[i] != '\0';) {
    if ((unsigned char)s[i] == (unsigned char)USH_ESC) {
      if (s[i + 1] == '\0') return 0;
      i += 2;
      continue;
    }
    if (s[i] == '*' || s[i] == '?') return 1;
    if (s[i] == '[') {
      // treat as meta only if there's a closing ']' later
      for (size_t j = i + 1; s[j] != '\0';) {
        if ((unsigned char)s[j] == (unsigned char)USH_ESC) {
          if (s[j + 1] == '\0') break;
          j += 2;
          continue;
        }
        if (s[j] == ']') return 1;
        j++;
      }
    }
    i++;
  }
  return 0;
}

static int has_unsupported_bracket_range(const char *pattern) {
  if (pattern == NULL) return 0;
  for (size_t i = 0; pattern[i] != '\0'; i++) {
    if (pattern[i] == '\\') {
      if (pattern[i + 1] != '\0') i++;
      continue;
    }
    if (pattern[i] != '[') continue;

    size_t j = i + 1;
    int first = 1;
    while (pattern[j] != '\0' && pattern[j] != ']') {
      if (pattern[j] == '\\') {
        if (pattern[j + 1] != '\0') {
          j += 2;
          first = 0;
          continue;
        }
        break;
      }
      if (pattern[j] == '-' && !first && pattern[j + 1] != '\0' && pattern[j + 1] != ']') {
        return 1; // [a-z] 形式
      }
      first = 0;
      j++;
    }
    if (pattern[j] == ']') {
      i = j;
    }
  }
  return 0;
}

static int scan_past_param_braced(const char *s, size_t *io_i) {
  size_t i = *io_i;
  if (s == NULL || s[i] != '$' || s[i + 1] != '{') return 1;

  size_t j = i + 2;
  while (s[j] != '\0' && s[j] != '}') j++;
  if (s[j] != '}') return 1;
  *io_i = j + 1;
  return 0;
}

static int scan_past_cmdsub_raw(const char *s, size_t *io_i) {
  size_t i = *io_i;
  if (s == NULL || s[i] != '$' || s[i + 1] != '(') return 1;

  size_t j = i + 2;
  quote_kind_t q = QUOTE_NONE;
  while (s[j] != '\0') {
    if ((unsigned char)s[j] == (unsigned char)USH_ESC) {
      if (s[j + 1] == '\0') return 1;
      j += 2;
      continue;
    }

    char c = s[j];
    if (q == QUOTE_NONE) {
      if (c == '\'') q = QUOTE_SINGLE;
      else if (c == '"') q = QUOTE_DOUBLE;
      else if (c == ')') {
        *io_i = j + 1;
        return 0;
      } else if (c == '$' && s[j + 1] == '(') {
        return 1;
      }
    } else if (q == QUOTE_SINGLE) {
      if (c == '\'') q = QUOTE_NONE;
    } else if (q == QUOTE_DOUBLE) {
      if (c == '"') q = QUOTE_NONE;
    }
    j++;
  }

  return 1;
}

static int brace_split_raw(
  quote_kind_t q,
  const char *raw,
  char out_raw[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  int *out_n
) {
  if (out_n == NULL) return 1;
  *out_n = 0;
  if (raw == NULL) return 0;

  if (q != QUOTE_NONE) {
    snprintf(out_raw[0], USH_MAX_TOKEN_LEN + 1, "%s", raw);
    *out_n = 1;
    return 0;
  }

  size_t len = strlen(raw);
  ssize_t open_idx = -1;
  ssize_t close_idx = -1;
  int comma_count = 0;
  int invalid = 0;

  for (size_t i = 0; i < len;) {
    if ((unsigned char)raw[i] == (unsigned char)USH_ESC) {
      if (raw[i + 1] == '\0') {
        invalid = 1;
        break;
      }
      i += 2;
      continue;
    }
    if (raw[i] == '$' && raw[i + 1] == '{') {
      if (scan_past_param_braced(raw, &i) != 0) {
        invalid = 1;
        break;
      }
      continue;
    }
    if (raw[i] == '$' && raw[i + 1] == '(') {
      if (scan_past_cmdsub_raw(raw, &i) != 0) {
        invalid = 1;
        break;
      }
      continue;
    }

    if (open_idx < 0) {
      if (raw[i] == '{') open_idx = (ssize_t)i;
      i++;
      continue;
    }

    if (raw[i] == '{') {
      invalid = 1;
      break;
    }
    if (raw[i] == '}') {
      close_idx = (ssize_t)i;
      break;
    }
    if (raw[i] == ',') comma_count++;
    i++;
  }

  if (invalid || open_idx < 0 || close_idx < 0 || comma_count <= 0) {
    snprintf(out_raw[0], USH_MAX_TOKEN_LEN + 1, "%s", raw);
    *out_n = 1;
    return 0;
  }

  for (size_t i = (size_t)close_idx + 1; i < len;) {
    if ((unsigned char)raw[i] == (unsigned char)USH_ESC) {
      if (raw[i + 1] == '\0') {
        invalid = 1;
        break;
      }
      i += 2;
      continue;
    }
    if (raw[i] == '$' && raw[i + 1] == '{') {
      if (scan_past_param_braced(raw, &i) != 0) {
        invalid = 1;
        break;
      }
      continue;
    }
    if (raw[i] == '$' && raw[i + 1] == '(') {
      if (scan_past_cmdsub_raw(raw, &i) != 0) {
        invalid = 1;
        break;
      }
      continue;
    }
    if (raw[i] == '{' || raw[i] == '}') {
      invalid = 1;
      break;
    }
    i++;
  }

  if (invalid) {
    snprintf(out_raw[0], USH_MAX_TOKEN_LEN + 1, "%s", raw);
    *out_n = 1;
    return 0;
  }

  size_t prefix_len = (size_t)open_idx;
  size_t suffix_len = len - (size_t)close_idx - 1;
  size_t item_start = (size_t)open_idx + 1;
  int outc = 0;

  while (item_start <= (size_t)close_idx) {
    size_t j = item_start;
    while (j < (size_t)close_idx) {
      if ((unsigned char)raw[j] == (unsigned char)USH_ESC) {
        if (raw[j + 1] == '\0') {
          invalid = 1;
          break;
        }
        j += 2;
        continue;
      }
      if (raw[j] == '$' && raw[j + 1] == '{') {
        if (scan_past_param_braced(raw, &j) != 0) {
          invalid = 1;
          break;
        }
        continue;
      }
      if (raw[j] == '$' && raw[j + 1] == '(') {
        if (scan_past_cmdsub_raw(raw, &j) != 0) {
          invalid = 1;
          break;
        }
        continue;
      }
      if (raw[j] == '{' || raw[j] == '}') {
        invalid = 1;
        break;
      }
      if (raw[j] == ',') break;
      j++;
    }

    if (invalid) break;

    size_t item_len = j - item_start;
    if (item_len == 0 || outc >= USH_MAX_ARGS) {
      invalid = 1;
      break;
    }
    if (prefix_len + item_len + suffix_len + 1 > USH_MAX_TOKEN_LEN + 1) return 1;

    size_t wi = 0;
    memcpy(out_raw[outc] + wi, raw, prefix_len);
    wi += prefix_len;
    memcpy(out_raw[outc] + wi, raw + item_start, item_len);
    wi += item_len;
    memcpy(out_raw[outc] + wi, raw + close_idx + 1, suffix_len);
    wi += suffix_len;
    out_raw[outc][wi] = '\0';
    outc++;

    if (j >= (size_t)close_idx) break;
    item_start = j + 1;
  }

  if (invalid || outc == 0) {
    snprintf(out_raw[0], USH_MAX_TOKEN_LEN + 1, "%s", raw);
    *out_n = 1;
    return 0;
  }

  *out_n = outc;
  return 0;
}

static int build_expand_ctx(const ush_state_t *st, ush_expand_ctx_t *out) {
  if (out == NULL) return 1;
  out->last_status = (st != NULL) ? st->last_status : 0;
  out->script_path = (st != NULL && st->script_path != NULL) ? st->script_path : "ush";
  out->pos_argc = (st != NULL) ? st->pos_argc : 0;
  out->pos_argv = (st != NULL) ? st->pos_argv : NULL;
  out->cmdsub_base = st;
  return 0;
}

static int append_expanded_word(
  const ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  char *out_argv[USH_MAX_ARGS + 1],
  int *io_outc
) {
  if (io_outc == NULL) return 2;

  ush_expand_ctx_t xctx;
  build_expand_ctx(st, &xctx);

  char raw_parts[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  int rawc = 0;
  if (brace_split_raw(q, raw, raw_parts, &rawc) != 0) {
    ush_eprintf("syntax error");
    return 2;
  }

  for (int ri = 0; ri < rawc; ri++) {
    int outc = *io_outc;
    if (outc >= USH_MAX_ARGS) {
      ush_eprintf("syntax error");
      return 2;
    }

    parse_result_t r = ush_expand_word(&xctx, q, raw_parts[ri], out_words[outc], sizeof(out_words[outc]));
    if (r == PARSE_UNSUPPORTED) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    if (r != PARSE_OK) {
      ush_eprintf("syntax error");
      return 2;
    }

    if (q == QUOTE_NONE && has_glob_meta_unescaped_marked(out_words[outc])) {
      char pattern[USH_MAX_TOKEN_LEN + 1];
      if (marked_to_glob_pattern(out_words[outc], pattern, sizeof(pattern)) != 0) {
        ush_eprintf("syntax error");
        return 2;
      }
      if (has_unsupported_bracket_range(pattern)) {
        ush_eprintf("unsupported syntax");
        return 2;
      }

      glob_t g;
      memset(&g, 0, sizeof(g));
      int gr = glob(pattern, GLOB_NOSORT, NULL, &g);
      if (gr == 0) {
        for (size_t k = 0; k < g.gl_pathc; k++) {
          if (*io_outc >= USH_MAX_ARGS) {
            globfree(&g);
            ush_eprintf("syntax error");
            return 2;
          }
          snprintf(out_words[*io_outc], USH_MAX_TOKEN_LEN + 1, "%s", g.gl_pathv[k]);
          out_argv[*io_outc] = out_words[*io_outc];
          (*io_outc)++;
        }
        globfree(&g);
        continue;
      }
      if (gr == GLOB_NOMATCH) {
        unmark_inplace(out_words[outc]);
        out_argv[outc] = out_words[outc];
        *io_outc = outc + 1;
        globfree(&g);
        continue;
      }

      globfree(&g);
      ush_eprintf("syntax error");
      return 2;
    }

    unmark_inplace(out_words[outc]);
    out_argv[outc] = out_words[outc];
    *io_outc = outc + 1;
  }

  return 0;
}

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

static int apply_assignment_words(char *argv[]) {
  if (argv == NULL || argv[0] == NULL) return 2;

  for (int i = 0; argv[i] != NULL; i++) {
    if (!ush_is_assignment_word0(argv[i])) {
      ush_eprintf("unsupported syntax");
      return 2;
    }

    char *eq = strchr(argv[i], '=');
    if (eq == NULL || eq == argv[i]) {
      ush_eprintf("syntax error");
      return 2;
    }

    char name[256];
    size_t name_len = (size_t)(eq - argv[i]);
    if (name_len >= sizeof(name)) {
      ush_eprintf("syntax error");
      return 2;
    }
    memcpy(name, argv[i], name_len);
    name[name_len] = '\0';

    if (setenv(name, eq + 1, 1) != 0) {
      ush_perrorf("setenv");
      return 1;
    }
  }

  return 0;
}

static int expand_argv(
  const ush_state_t *st,
  const ush_cmd_t *cmd,
  char out_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  char *out_argv[USH_MAX_ARGS + 1]
) {
  int outc = 0;

  for (int i = 0; i < cmd->argc; i++) {
    int er = append_expanded_word(st, cmd->argv_quote[i], cmd->argv_raw[i], out_words, out_argv, &outc);
    if (er != 0) return er;
  }

  out_argv[outc] = NULL;

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

  char words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *argv[USH_MAX_ARGS + 1];
  int argc = 0;
  int er = append_expanded_word(st, q, raw, words, argv, &argc);
  if (er != 0) return er;
  if (argc != 1 || argv[0] == NULL) {
    ush_eprintf("syntax error");
    return 2;
  }

  snprintf(out, USH_MAX_TOKEN_LEN + 1, "%s", argv[0]);
  return 0;
}

static int exec_command(ush_state_t *st, const ush_cmd_t *cmd) {
  char words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *argv[USH_MAX_ARGS + 1];

  int er = expand_argv(st, cmd, words, argv);
  if (er != 0) return er;
  if (argv[0] == NULL) return 2;

  if (ush_is_assignment_word0(argv[0])) {
    if (cmd->in_path_raw != NULL || cmd->out_path_raw != NULL) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    return apply_assignment_words(argv);
  }

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

  char l_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char r_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *l_argv[USH_MAX_ARGS + 1];
  char *r_argv[USH_MAX_ARGS + 1];

  int er;
  er = expand_argv(st, &pl->left, l_words, l_argv);
  if (er != 0) return er;
  er = expand_argv(st, &pl->right, r_words, r_argv);
  if (er != 0) return er;

  if (ush_is_builtin(l_argv[0]) || ush_is_builtin(r_argv[0])) {
    ush_eprintf("unsupported syntax");
    return 2;
  }

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

    char path[1024];
    int fail = 127;
    if (resolve_cmd(l_argv[0], path, &fail) != 0) _exit(fail);
    exec_with_sh_fallback(path, l_argv);
    _exit(126);
  }

  pid_t rp = fork();
  if (rp < 0) {
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
    case NODE_SEQ: {
      (void)eval_node(st, ast, n->left);
      return eval_node(st, ast, n->right);
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
