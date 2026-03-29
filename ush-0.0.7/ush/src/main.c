#include "ush.h"

#include "ush_err.h"
#include "ush_exec.h"
#include "ush_lineedit.h"
#include "ush_prompt.h"
#include "ush_tokenize.h"
#include "ush_parse.h"
#include "ush_script.h"
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
    case PARSE_INCOMPLETE:
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

static parse_result_t eval_text(ush_state_t *st, const char *text) {
  token_t toks[USH_MAX_TOKENS];
  int ntok = 0;
  char tokbuf[USH_MAX_LINE_LEN + 1];

  parse_result_t tr = ush_tokenize(text, toks, &ntok, tokbuf);
  if (tr != PARSE_OK) return tr;

  ush_script_t sc;
  int root = -1;
  parse_result_t pr = ush_parse_script(toks, ntok, &sc, &root);
  if (pr != PARSE_OK) return pr;

  ush_exec_script(st, toks, ntok, &sc, root);
  return PARSE_OK;
}

static int run_interactive(ush_state_t *st) {
  ush_history_t hist;
  memset(&hist, 0, sizeof(hist));

  char buf[USH_MAX_LINE_LEN + 1];
  size_t blen = 0;
  buf[0] = '\0';

  for (;;) {
    char prompt[256];
    ush_prompt_render(prompt, sizeof(prompt));

    char line[USH_MAX_LINE_LEN + 1];
    int r = ush_lineedit_readline(prompt, line, sizeof(line), &hist);
    if (r == 1) {
      exit(st->last_status & 255);
    }

    if (ush_is_blank_line(line)) {
      if (blen == 0) continue;
      // blank line inside a compound: treat as separator
      line[0] = '\0';
    }

    size_t ln = strlen(line);
    // append: <line> ;
    if (blen + ln + 2 > sizeof(buf)) {
      ush_eprintf("syntax error");
      st->last_status = 2;
      blen = 0;
      buf[0] = '\0';
      continue;
    }

    if (ln > 0) {
      memcpy(buf + blen, line, ln);
      blen += ln;
    }
    buf[blen++] = ';';
    buf[blen] = '\0';

    parse_result_t pr = eval_text(st, buf);
    if (pr == PARSE_INCOMPLETE) {
      continue;
    }
    if (pr != PARSE_OK) {
      handle_parse_error(st, pr);
    }
    // ok or error: reset buffer
    blen = 0;
    buf[0] = '\0';
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
  char buf[USH_MAX_LINE_LEN + 1];
  size_t blen = 0;
  buf[0] = '\0';
  int lineno = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    lineno++;

    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

    if (lineno == 1 && ush_starts_with(line, "#!")) {
      continue;
    }

    if (ush_is_blank_line(line)) continue;

    size_t ln = strlen(line);
    if (blen + ln + 2 > sizeof(buf)) {
      ush_eprintf("syntax error");
      st->last_status = 2;
      fclose(fp);
      return st->last_status;
    }

    memcpy(buf + blen, line, ln);
    blen += ln;
    buf[blen++] = ';';
    buf[blen] = '\0';

    parse_result_t pr = eval_text(st, buf);
    if (pr == PARSE_INCOMPLETE) {
      continue;
    }
    if (pr != PARSE_OK) {
      handle_parse_error(st, pr);
      // on error, clear buffer and continue
    }
    blen = 0;
    buf[0] = '\0';
  }

  if (blen > 0) {
    parse_result_t pr = eval_text(st, buf);
    if (pr == PARSE_INCOMPLETE) {
      ush_eprintf("syntax error");
      st->last_status = 2;
    } else if (pr != PARSE_OK) {
      handle_parse_error(st, pr);
    }
  }

  fclose(fp);
  return st->last_status;
}

int main(int argc, char **argv) {
  if (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "--version") == 0) {
    puts(USH_VERSION);
    return 0;
  }

  ush_state_t st;
  st.last_status = 0;
  st.script_path = "ush";
  st.pos_argc = 0;
  st.pos_argv = NULL;

  set_parent_sigint_ignore();

  if (argc == 1) {
    return run_interactive(&st);
  }

  if (argc >= 2) {
    st.script_path = argv[1];
    st.pos_argc = (argc >= 3) ? (argc - 2) : 0;
    st.pos_argv = (argc >= 3) ? &argv[2] : NULL;
    return run_script(&st, argv[1]);
  }

  ush_eprintf("syntax error");
  return 2;
}

