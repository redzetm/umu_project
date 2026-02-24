#include "ush_script.h"

#include "ush_exec.h"
#include "ush_expand.h"
#include "ush_parse.h"
#include "ush_utils.h"

#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { USH_ESC = 1 };

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
        return 1;
      }
      first = 0;
      j++;
    }
    if (pattern[j] == ']') i = j;
  }
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

static int exec_simple_range(ush_state_t *st, const token_t *toks, tok_range_t r) {
  if (st == NULL || toks == NULL) return 1;
  int n = r.end - r.start;
  if (n <= 0) return 0;

  ush_ast_t ast;
  int root = -1;
  parse_result_t pr = ush_parse_line(&toks[r.start], n, &ast, &root);
  if (pr != PARSE_OK) {
    if (pr == PARSE_UNSUPPORTED) {
      ush_eprintf("unsupported syntax");
      st->last_status = 2;
      return 2;
    }
    ush_eprintf("syntax error");
    st->last_status = 2;
    return 2;
  }

  return ush_exec_ast(st, &ast, root);
}

static int eval_stmt(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx);

static int eval_seq(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  (void)eval_stmt(st, toks, ntok, sc, n->left);
  return eval_stmt(st, toks, ntok, sc, n->right);
}

static int expand_one_word(
  ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out[USH_MAX_TOKEN_LEN + 1]
) {
  ush_expand_ctx_t x;
  build_expand_ctx(st, &x);

  parse_result_t r = ush_expand_word(&x, q, raw, out, USH_MAX_TOKEN_LEN + 1);
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

static int expand_word_to_list(
  ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  int *io_n
) {
  if (io_n == NULL) return 2;
  int n = *io_n;
  if (n >= USH_MAX_ARGS) return 2;

  char tmp[USH_MAX_TOKEN_LEN + 1];
  int er = expand_one_word(st, q, raw, tmp);
  if (er != 0) return er;

  if (q == QUOTE_NONE && has_glob_meta_unescaped_marked(tmp)) {
    char pattern[USH_MAX_TOKEN_LEN + 1];
    if (marked_to_glob_pattern(tmp, pattern, sizeof(pattern)) != 0) {
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
        if (n >= USH_MAX_ARGS) {
          globfree(&g);
          ush_eprintf("syntax error");
          return 2;
        }
        snprintf(out_words[n], USH_MAX_TOKEN_LEN + 1, "%s", g.gl_pathv[k]);
        n++;
      }
      globfree(&g);
      *io_n = n;
      return 0;
    }
    if (gr == GLOB_NOMATCH) {
      unmark_inplace(tmp);
      snprintf(out_words[n], USH_MAX_TOKEN_LEN + 1, "%s", tmp);
      n++;
      globfree(&g);
      *io_n = n;
      return 0;
    }

    globfree(&g);
    ush_eprintf("syntax error");
    return 2;
  }

  unmark_inplace(tmp);
  snprintf(out_words[n], USH_MAX_TOKEN_LEN + 1, "%s", tmp);
  n++;
  *io_n = n;
  return 0;
}

static int eval_if(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  int r = eval_stmt(st, toks, ntok, sc, n->if_cond_root);
  if (r == 0) return eval_stmt(st, toks, ntok, sc, n->if_then_root);

  for (int i = 0; i < n->if_n_elif; i++) {
    int cr = eval_stmt(st, toks, ntok, sc, n->if_elif_cond[i]);
    if (cr == 0) return eval_stmt(st, toks, ntok, sc, n->if_elif_then[i]);
  }

  if (n->if_else_root >= 0) return eval_stmt(st, toks, ntok, sc, n->if_else_root);
  return r;
}

static int eval_while(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  int last = 0;
  int ran = 0;
  while (1) {
    int cr = eval_stmt(st, toks, ntok, sc, n->while_cond_root);
    if (cr != 0) break;
    ran = 1;
    last = eval_stmt(st, toks, ntok, sc, n->while_body_root);
  }
  if (!ran) return 0;
  return last;
}

static int eval_for(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  if (n->for_name_tok < 0 || n->for_name_tok >= ntok) return 2;

  const char *name = toks[n->for_name_tok].text;
  if (!ush_is_valid_name(name)) {
    ush_eprintf("syntax error");
    return 2;
  }

  char vals[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  int nval = 0;

  for (int ti = n->for_words.start; ti < n->for_words.end; ti++) {
    if (ti < 0 || ti >= ntok) return 2;
    if (toks[ti].kind != TOK_WORD) return 2;

    int er = expand_word_to_list(st, toks[ti].quote, toks[ti].text, vals, &nval);
    if (er != 0) return er;
  }

  int last = 0;
  for (int vi = 0; vi < nval; vi++) {
    if (setenv(name, vals[vi], 1) != 0) {
      ush_perrorf("setenv");
      return 1;
    }
    last = eval_stmt(st, toks, ntok, sc, n->for_body_root);
  }

  return (nval == 0) ? 0 : last;
}

static int case_pat_matches(
  ush_state_t *st,
  const token_t *toks,
  int ntok,
  int pat_tok,
  const char *subject
) {
  if (pat_tok < 0 || pat_tok >= ntok) return 0;
  if (toks[pat_tok].kind != TOK_WORD) return 0;

  char expanded[USH_MAX_TOKEN_LEN + 1];
  int er = expand_one_word(st, toks[pat_tok].quote, toks[pat_tok].text, expanded);
  if (er != 0) return -1;

  if (toks[pat_tok].quote != QUOTE_NONE) {
    unmark_inplace(expanded);
    return (strcmp(expanded, subject) == 0) ? 1 : 0;
  }

  char pattern[USH_MAX_TOKEN_LEN + 1];
  if (marked_to_glob_pattern(expanded, pattern, sizeof(pattern)) != 0) {
    ush_eprintf("syntax error");
    return -1;
  }
  if (has_unsupported_bracket_range(pattern)) {
    ush_eprintf("unsupported syntax");
    return -1;
  }

  int fr = fnmatch(pattern, subject, 0);
  return (fr == 0) ? 1 : 0;
}

static int eval_case(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  if (n->case_word_tok < 0 || n->case_word_tok >= ntok) return 2;
  if (toks[n->case_word_tok].kind != TOK_WORD) return 2;

  char subject[USH_MAX_TOKEN_LEN + 1];
  int er = expand_one_word(st, toks[n->case_word_tok].quote, toks[n->case_word_tok].text, subject);
  if (er != 0) return er;
  unmark_inplace(subject);

  for (int ii = 0; ii < n->case_nitems; ii++) {
    const ush_case_item_t *it = &n->case_items[ii];

    for (int pi = 0; pi < it->npat; pi++) {
      int mr = case_pat_matches(st, toks, ntok, it->pat_tok[pi], subject);
      if (mr < 0) return 2;
      if (mr == 1) {
        if (it->body_root < 0) {
          st->last_status = 0;
          return 0;
        }
        return eval_stmt(st, toks, ntok, sc, it->body_root);
      }
    }
  }

  st->last_status = 0;
  return 0;
}

static int eval_stmt(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  if (st == NULL || toks == NULL || sc == NULL) return 1;
  if (idx < 0 || idx >= sc->n) return 1;

  const ush_stmt_t *n = &sc->nodes[idx];

  int r = 1;
  switch (n->kind) {
    case ST_SIMPLE:
      r = exec_simple_range(st, toks, n->simple);
      break;
    case ST_SEQ:
      r = eval_seq(st, toks, ntok, sc, idx);
      break;
    case ST_IF:
      r = eval_if(st, toks, ntok, sc, idx);
      break;
    case ST_WHILE:
      r = eval_while(st, toks, ntok, sc, idx);
      break;
    case ST_FOR:
      r = eval_for(st, toks, ntok, sc, idx);
      break;
    case ST_CASE:
      r = eval_case(st, toks, ntok, sc, idx);
      break;
  }

  st->last_status = r;
  return r;
}

int ush_exec_script(
  ush_state_t *st,
  const token_t *toks,
  int ntok,
  const ush_script_t *sc,
  int root
) {
  (void)ntok;
  if (st == NULL || toks == NULL || sc == NULL || root < 0) return 1;
  return eval_stmt(st, toks, ntok, sc, root);
}
