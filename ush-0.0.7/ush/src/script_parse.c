#include "ush_script.h"

#include "ush_utils.h"

#include <string.h>

static int is_kw(const token_t *toks, int i, int ntok, const char *kw) {
  if (kw == NULL) return 0;
  if (toks == NULL) return 0;
  if (i < 0 || i >= ntok) return 0;
  if (toks[i].kind != TOK_WORD) return 0;
  if (toks[i].quote != QUOTE_NONE) return 0;
  return toks[i].text != NULL && strcmp(toks[i].text, kw) == 0;
}

static void skip_seps(const token_t *toks, int ntok, int *io_i) {
  int i = (io_i != NULL) ? *io_i : 0;
  while (i < ntok && toks[i].kind == TOK_SEMI) i++;
  if (io_i) *io_i = i;
}

static parse_result_t new_node(ush_script_t *sc, stmt_kind_t k, int *out_idx) {
  if (sc == NULL || out_idx == NULL) return PARSE_SYNTAX_ERROR;
  if (sc->n >= USH_MAX_STMTS) return PARSE_TOO_MANY_TOKENS;
  int idx = sc->n++;
  memset(&sc->nodes[idx], 0, sizeof(sc->nodes[idx]));
  sc->nodes[idx].kind = k;
  sc->nodes[idx].left = -1;
  sc->nodes[idx].right = -1;
  sc->nodes[idx].if_cond_root = -1;
  sc->nodes[idx].if_then_root = -1;
  sc->nodes[idx].if_else_root = -1;
  sc->nodes[idx].if_n_elif = 0;
  sc->nodes[idx].while_cond_root = -1;
  sc->nodes[idx].while_body_root = -1;
  sc->nodes[idx].for_name_tok = -1;
  sc->nodes[idx].for_words.start = 0;
  sc->nodes[idx].for_words.end = 0;
  sc->nodes[idx].for_body_root = -1;
  sc->nodes[idx].case_word_tok = -1;
  sc->nodes[idx].case_nitems = 0;
  *out_idx = idx;
  return PARSE_OK;
}

typedef struct {
  const char *w1;
  const char *w2;
  const char *w3;
  const char *w4;
} stop_words_t;

static int is_stop_kw(const token_t *toks, int i, int ntok, stop_words_t stop) {
  if (is_kw(toks, i, ntok, stop.w1)) return 1;
  if (is_kw(toks, i, ntok, stop.w2)) return 1;
  if (is_kw(toks, i, ntok, stop.w3)) return 1;
  if (is_kw(toks, i, ntok, stop.w4)) return 1;
  return 0;
}

static parse_result_t parse_stmt_list_until(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root,
  stop_words_t stop
);

static parse_result_t parse_stmt_list_until_dsemi(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
);

static parse_result_t parse_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
);

static parse_result_t parse_simple_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;

  // must not start with tokens that belong to compound syntax
  if (i < ntok && (toks[i].kind == TOK_DSEMI || toks[i].kind == TOK_RPAREN)) {
    return PARSE_SYNTAX_ERROR;
  }

  int start = i;
  while (i < ntok) {
    if (toks[i].kind == TOK_SEMI) break;
    if (toks[i].kind == TOK_DSEMI) break;
    if (toks[i].kind == TOK_RPAREN) return PARSE_SYNTAX_ERROR;
    i++;
  }
  int end = i;
  if (end <= start) return PARSE_SYNTAX_ERROR;

  int idx = -1;
  parse_result_t r = new_node(sc, ST_SIMPLE, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].simple.start = start;
  sc->nodes[idx].simple.end = end;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_if_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'if'
  i++;

  int cond_root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, sc, &cond_root, (stop_words_t){.w1 = "then"});
  if (r == PARSE_EMPTY) {
    // need at least one condition statement; if input ended, wait for more.
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "then")) return PARSE_SYNTAX_ERROR;
  i++;

  int then_root = -1;
  r = parse_stmt_list_until(toks, ntok, &i, sc, &then_root, (stop_words_t){.w1 = "elif", .w2 = "else", .w3 = "fi"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  int idx = -1;
  r = new_node(sc, ST_IF, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].if_cond_root = cond_root;
  sc->nodes[idx].if_then_root = then_root;
  sc->nodes[idx].if_else_root = -1;
  sc->nodes[idx].if_n_elif = 0;

  skip_seps(toks, ntok, &i);

  // elif*
  while (i < ntok && is_kw(toks, i, ntok, "elif")) {
    if (sc->nodes[idx].if_n_elif >= USH_MAX_ELIF) return PARSE_TOO_MANY_TOKENS;
    i++;

    int econd = -1;
    r = parse_stmt_list_until(toks, ntok, &i, sc, &econd, (stop_words_t){.w1 = "then"});
    if (r == PARSE_EMPTY) {
      return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
    }
    if (r != PARSE_OK) return r;

    skip_seps(toks, ntok, &i);
    if (i >= ntok) return PARSE_INCOMPLETE;
    if (!is_kw(toks, i, ntok, "then")) return PARSE_SYNTAX_ERROR;
    i++;

    int ethen = -1;
    r = parse_stmt_list_until(toks, ntok, &i, sc, &ethen, (stop_words_t){.w1 = "elif", .w2 = "else", .w3 = "fi"});
    if (r == PARSE_EMPTY) {
      return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
    }
    if (r != PARSE_OK) return r;

    int n = sc->nodes[idx].if_n_elif;
    sc->nodes[idx].if_elif_cond[n] = econd;
    sc->nodes[idx].if_elif_then[n] = ethen;
    sc->nodes[idx].if_n_elif++;

    skip_seps(toks, ntok, &i);
  }

  // else?
  if (i < ntok && is_kw(toks, i, ntok, "else")) {
    i++;
    int eroot = -1;
    r = parse_stmt_list_until(toks, ntok, &i, sc, &eroot, (stop_words_t){.w1 = "fi"});
    if (r == PARSE_EMPTY) {
      return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
    }
    if (r != PARSE_OK) return r;
    sc->nodes[idx].if_else_root = eroot;
    skip_seps(toks, ntok, &i);
  }

  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "fi")) return PARSE_SYNTAX_ERROR;
  i++;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_while_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'while'
  i++;

  int cond_root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, sc, &cond_root, (stop_words_t){.w1 = "do"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "do")) return PARSE_SYNTAX_ERROR;
  i++;

  int body_root = -1;
  r = parse_stmt_list_until(toks, ntok, &i, sc, &body_root, (stop_words_t){.w1 = "done"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "done")) return PARSE_SYNTAX_ERROR;
  i++;

  int idx = -1;
  r = new_node(sc, ST_WHILE, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].while_cond_root = cond_root;
  sc->nodes[idx].while_body_root = body_root;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_for_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'for'
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (toks[i].kind != TOK_WORD || toks[i].quote != QUOTE_NONE) return PARSE_SYNTAX_ERROR;
  if (!ush_is_valid_name(toks[i].text)) return PARSE_SYNTAX_ERROR;
  int name_tok = i;
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "in")) return PARSE_SYNTAX_ERROR;
  i++;

  // words until ';' (line break)
  skip_seps(toks, ntok, &i);
  int wstart = i;
  while (i < ntok && toks[i].kind == TOK_WORD) i++;
  int wend = i;

  // require separator before 'do'
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (toks[i].kind != TOK_SEMI) return PARSE_SYNTAX_ERROR;
  skip_seps(toks, ntok, &i);

  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "do")) return PARSE_SYNTAX_ERROR;
  i++;

  int body_root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, sc, &body_root, (stop_words_t){.w1 = "done"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "done")) return PARSE_SYNTAX_ERROR;
  i++;

  int idx = -1;
  r = new_node(sc, ST_FOR, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].for_name_tok = name_tok;
  sc->nodes[idx].for_words.start = wstart;
  sc->nodes[idx].for_words.end = wend;
  sc->nodes[idx].for_body_root = body_root;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_case_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'case'
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (toks[i].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
  int word_tok = i;
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "in")) return PARSE_SYNTAX_ERROR;
  i++;

  // allow separators after 'in'
  skip_seps(toks, ntok, &i);

  int idx = -1;
  parse_result_t r = new_node(sc, ST_CASE, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].case_word_tok = word_tok;
  sc->nodes[idx].case_nitems = 0;

  // case ... in <items> esac
  while (1) {
    skip_seps(toks, ntok, &i);
    if (i >= ntok) return PARSE_INCOMPLETE;
    if (is_kw(toks, i, ntok, "esac")) {
      i++;
      *out_root = idx;
      *io_i = i;
      return PARSE_OK;
    }

    if (sc->nodes[idx].case_nitems >= USH_MAX_CASE_ITEMS) return PARSE_TOO_MANY_TOKENS;

    ush_case_item_t *it = &sc->nodes[idx].case_items[sc->nodes[idx].case_nitems];
    memset(it, 0, sizeof(*it));
    it->npat = 0;
    it->body_root = -1;

    // pattern_list: WORD ( '|' WORD )* ')'
    while (1) {
      if (i >= ntok) return PARSE_INCOMPLETE;
      if (toks[i].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
      if (it->npat >= USH_MAX_CASE_PATS) return PARSE_TOO_MANY_TOKENS;
      it->pat_tok[it->npat++] = i;
      i++;

      if (i >= ntok) return PARSE_INCOMPLETE;
      if (toks[i].kind == TOK_PIPE) {
        i++;
        continue;
      }
      if (toks[i].kind == TOK_RPAREN) {
        i++;
        break;
      }
      return PARSE_SYNTAX_ERROR;
    }

    // body until ';;'
    int body_root = -1;
    r = parse_stmt_list_until_dsemi(toks, ntok, &i, sc, &body_root);
    if (r != PARSE_OK && r != PARSE_EMPTY) return r;

    skip_seps(toks, ntok, &i);
    if (i >= ntok) return PARSE_INCOMPLETE;
    if (toks[i].kind != TOK_DSEMI) return PARSE_SYNTAX_ERROR;
    i++;

    it->body_root = body_root;
    sc->nodes[idx].case_nitems++;
  }
}

static parse_result_t parse_stmt_list_until_dsemi(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  if (out_root == NULL) return PARSE_SYNTAX_ERROR;
  *out_root = -1;

  skip_seps(toks, ntok, io_i);
  int i = *io_i;

  if (i >= ntok) return PARSE_EMPTY;
  if (toks[i].kind == TOK_DSEMI) return PARSE_EMPTY;

  int left = -1;
  parse_result_t r = parse_stmt(toks, ntok, io_i, sc, &left);
  if (r != PARSE_OK) {
    if (r == PARSE_EMPTY) return PARSE_EMPTY;
    return r;
  }

  while (1) {
    int j = *io_i;
    skip_seps(toks, ntok, &j);
    if (j >= ntok) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }
    if (toks[j].kind == TOK_DSEMI) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }

    if (*io_i >= ntok || toks[*io_i].kind != TOK_SEMI) {
      *out_root = left;
      return PARSE_OK;
    }

    skip_seps(toks, ntok, io_i);
    int k = *io_i;
    if (k >= ntok || toks[k].kind == TOK_DSEMI) {
      *out_root = left;
      return PARSE_OK;
    }

    int right = -1;
    r = parse_stmt(toks, ntok, io_i, sc, &right);
    if (r == PARSE_EMPTY) continue;
    if (r != PARSE_OK) return r;

    int parent = -1;
    r = new_node(sc, ST_SEQ, &parent);
    if (r != PARSE_OK) return r;
    sc->nodes[parent].left = left;
    sc->nodes[parent].right = right;
    left = parent;
  }
}

static parse_result_t parse_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  skip_seps(toks, ntok, io_i);
  int i = *io_i;
  if (i >= ntok) return PARSE_EMPTY;

  if (toks[i].kind != TOK_WORD) {
    if (toks[i].kind == TOK_DSEMI || toks[i].kind == TOK_RPAREN) return PARSE_SYNTAX_ERROR;
    return PARSE_SYNTAX_ERROR;
  }

  if (is_kw(toks, i, ntok, "if")) {
    parse_result_t r = parse_if_stmt(toks, ntok, io_i, sc, out_root);
    return r;
  }
  if (is_kw(toks, i, ntok, "while")) {
    return parse_while_stmt(toks, ntok, io_i, sc, out_root);
  }
  if (is_kw(toks, i, ntok, "for")) {
    return parse_for_stmt(toks, ntok, io_i, sc, out_root);
  }
  if (is_kw(toks, i, ntok, "case")) {
    return parse_case_stmt(toks, ntok, io_i, sc, out_root);
  }

  // stray reserved words
  if (is_kw(toks, i, ntok, "then") || is_kw(toks, i, ntok, "elif") || is_kw(toks, i, ntok, "else") ||
      is_kw(toks, i, ntok, "fi") || is_kw(toks, i, ntok, "do") || is_kw(toks, i, ntok, "done") ||
      is_kw(toks, i, ntok, "in") || is_kw(toks, i, ntok, "esac")) {
    return PARSE_SYNTAX_ERROR;
  }

  return parse_simple_stmt(toks, ntok, io_i, sc, out_root);
}

static parse_result_t parse_stmt_list_until(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root,
  stop_words_t stop
) {
  if (out_root == NULL) return PARSE_SYNTAX_ERROR;
  *out_root = -1;

  skip_seps(toks, ntok, io_i);
  int i = *io_i;

  if (i >= ntok) return PARSE_EMPTY;
  if (is_stop_kw(toks, i, ntok, stop)) {
    return PARSE_EMPTY;
  }

  int left = -1;
  parse_result_t r = parse_stmt(toks, ntok, io_i, sc, &left);
  if (r != PARSE_OK) {
    if (r == PARSE_EMPTY) return PARSE_EMPTY;
    return r;
  }

  while (1) {
    int j = *io_i;
    skip_seps(toks, ntok, &j);
    if (j >= ntok) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }
    if (is_stop_kw(toks, j, ntok, stop)) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }

    // require at least one separator between statements
    if (*io_i >= ntok || toks[*io_i].kind != TOK_SEMI) {
      // no separator: this is a single simple statement, stop here
      *out_root = left;
      return PARSE_OK;
    }

    // consume separators
    skip_seps(toks, ntok, io_i);

    // if next is stop, allow trailing seps
    int k = *io_i;
    if (k >= ntok || is_stop_kw(toks, k, ntok, stop)) {
      *out_root = left;
      return PARSE_OK;
    }

    int right = -1;
    r = parse_stmt(toks, ntok, io_i, sc, &right);
    if (r == PARSE_EMPTY) {
      // tolerate empty statements due to multiple ';'
      continue;
    }
    if (r != PARSE_OK) return r;

    int parent = -1;
    r = new_node(sc, ST_SEQ, &parent);
    if (r != PARSE_OK) return r;
    sc->nodes[parent].left = left;
    sc->nodes[parent].right = right;
    left = parent;
  }
}

parse_result_t ush_parse_script(
  const token_t *toks,
  int ntok,
  ush_script_t *out,
  int *out_root
) {
  if (out == NULL || out_root == NULL) return PARSE_SYNTAX_ERROR;
  out->n = 0;
  *out_root = -1;

  if (toks == NULL || ntok <= 0) return PARSE_EMPTY;

  int i = 0;
  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_EMPTY;

  int root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, out, &root, (stop_words_t){});
  if (r == PARSE_EMPTY) return PARSE_EMPTY;
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i != ntok) {
    // leftover tokens (e.g. stray ';;' or ')')
    if (i < ntok && (toks[i].kind == TOK_DSEMI || toks[i].kind == TOK_RPAREN)) return PARSE_SYNTAX_ERROR;
    return PARSE_SYNTAX_ERROR;
  }

  *out_root = root;
  return PARSE_OK;
}
