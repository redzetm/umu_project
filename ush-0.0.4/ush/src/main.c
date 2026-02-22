#include "ush.h"

#include "ush_err.h"
#include "ush_exec.h"
#include "ush_lineedit.h"
#include "ush_prompt.h"
#include "ush_tokenize.h"
#include "ush_parse.h"
#include "ush_utils.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_parent_sigint_ignore(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
}

static void handle_parse_error(ush_state_t *st, parse_result_t r) {
  if (st == NULL) return;

  switch (r) {
    case PARSE_EMPTY:
      return;
    case PARSE_UNSUPPORTED:
      ush_eprintf("unsupported syntax");
      st->last_status = 2;
      return;
    case PARSE_SYNTAX_ERROR:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_TOO_LONG:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_TOO_MANY_TOKENS:
    case PARSE_TOO_MANY_ARGS:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_OK:
      return;
  }
}

static void eval_line(ush_state_t *st, const char *line) {
  token_t toks[USH_MAX_TOKENS];
  int ntok = 0;
  char tokbuf[USH_MAX_LINE_LEN + 1];

  parse_result_t tr = ush_tokenize(line, toks, &ntok, tokbuf);
  if (tr != PARSE_OK) {
    handle_parse_error(st, tr);
    return;
  }

  ush_ast_t ast;
  int root = -1;
  parse_result_t pr = ush_parse_line(toks, ntok, &ast, &root);
  if (pr != PARSE_OK) {
    handle_parse_error(st, pr);
    return;
  }

  ush_exec_ast(st, &ast, root);
}

static int run_interactive(ush_state_t *st) {
  ush_history_t hist;
  memset(&hist, 0, sizeof(hist));

  for (;;) {
    char prompt[256];
    ush_prompt_render(prompt, sizeof(prompt));

    char line[USH_MAX_LINE_LEN + 1];
    int r = ush_lineedit_readline(prompt, line, sizeof(line), &hist);
    if (r == 1) {
      exit(st->last_status & 255);
    }

    if (ush_is_blank_line(line)) continue;

    eval_line(st, line);
  }
}

static int run_script(ush_state_t *st, const char *path) {
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    ush_perrorf("open");
    st->last_status = 1;
    return 1;
  }

  char line[USH_MAX_LINE_LEN + 2];
  int lineno = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    lineno++;

    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

    if (lineno == 1 && ush_starts_with(line, "#!")) {
      continue;
    }

    if (ush_is_blank_line(line)) continue;

    eval_line(st, line);
  }

  fclose(fp);
  return st->last_status;
}

int main(int argc, char **argv) {
  ush_state_t st;
  st.last_status = 0;

  set_parent_sigint_ignore();

  if (argc == 1) {
    return run_interactive(&st);
  }

  if (argc == 2) {
    return run_script(&st, argv[1]);
  }

  ush_eprintf("syntax error");
  return 2;
}

