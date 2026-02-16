#include "ush.h"

#include "ush_builtins.h"
#include "ush_err.h"
#include "ush_exec.h"
#include "ush_lineedit.h"
#include "ush_parse.h"
#include "ush_prompt.h"
#include "ush_tokenize.h"
#include "ush_utils.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void print_parse_error(parse_result_t r) {
  switch (r) {
    case PARSE_EMPTY:
      return;
    case PARSE_TOO_LONG:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_TOO_MANY_TOKENS:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_TOO_MANY_ARGS:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_UNSUPPORTED:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_SYNTAX_ERROR:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_OK:
    default:
      return;
  }
}

static int any_builtin_in_pipeline(const ush_pipeline_t *pl) {
  for (int i = 0; i < pl->ncmd; i++) {
    const char *cmd = pl->cmds[i].argv[0];
    if (cmd != NULL && ush_is_builtin(cmd)) return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  ush_state_t st = {.last_status = 0};
  signal(SIGINT, SIG_IGN);
  ush_history_t hist;
  hist.count = 0;
  hist.cursor = 0;

  for (;;) {
    char prompt[256];
    (void)ush_prompt_render(prompt, sizeof(prompt));

    char line[USH_MAX_LINE_LEN + 1];
    int rr = ush_lineedit_readline(prompt, line, sizeof(line), &hist, st.last_status);
    if (rr == 1) exit(st.last_status);

    if (ush_is_blank_line(line) || ush_is_comment_line(line)) continue;

    token_t toks[USH_MAX_TOKENS];
    int ntok = 0;
    char tokbuf[USH_MAX_LINE_LEN + 1];

    parse_result_t tr = ush_tokenize(line, toks, &ntok, tokbuf);
    if (tr != PARSE_OK) {
      if (tr == PARSE_EMPTY) continue;
      print_parse_error(tr);
      st.last_status = 2;
      continue;
    }

    ush_pipeline_t pl;
    parse_result_t pr = ush_parse_pipeline(toks, ntok, &pl);
    if (pr != PARSE_OK) {
      print_parse_error(pr);
      st.last_status = 2;
      continue;
    }

    // builtins
    if (pl.ncmd == 1 && pl.in_path == NULL && pl.out_path == NULL &&
        pl.cmds[0].argv[0] != NULL && ush_is_builtin(pl.cmds[0].argv[0])) {
      st.last_status = ush_run_builtin(&st, pl.cmds[0].argv);
      continue;
    }

    // builtins with pipe/redir are unsupported
    if (any_builtin_in_pipeline(&pl)) {
      ush_eprintf("ush: unsupported syntax\n");
      st.last_status = 2;
      continue;
    }

    st.last_status = ush_exec_pipeline(&st, &pl);
  }
}
