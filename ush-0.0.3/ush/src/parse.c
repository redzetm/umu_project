#include "ush_parse.h"
#include "ush_utils.h"

#include <stddef.h>
#include <string.h>

static void init_cmd(ush_cmd_t *c) {
  memset(c, 0, sizeof(*c));
  c->argc = 0;
  c->argv_raw[0] = NULL;
  c->in_path_raw = NULL;
  c->out_path_raw = NULL;
  c->in_quote = QUOTE_NONE;
  c->out_quote = QUOTE_NONE;
  c->out_append = 0;
}

static parse_result_t add_arg(ush_cmd_t *c, const char *s, quote_kind_t q) {
  if (c->argc >= USH_MAX_ARGS) return PARSE_TOO_MANY_ARGS;
  c->argv_raw[c->argc] = s;
  c->argv_quote[c->argc] = q;
  c->argc++;
  c->argv_raw[c->argc] = NULL;
  return PARSE_OK;
}

static int new_node(ush_ast_t *ast, node_kind_t k) {
  if (ast->n >= USH_MAX_PIPES) return -1;
  int idx = ast->n++;
  memset(&ast->nodes[idx], 0, sizeof(ast->nodes[idx]));
  ast->nodes[idx].kind = k;
  ast->nodes[idx].left = -1;
  ast->nodes[idx].right = -1;
  return idx;
}

static parse_result_t parse_command(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_cmd_t *out_cmd,
  int allow_redirects
) {
  init_cmd(out_cmd);

  int i = *io_i;
  if (i >= ntok) return PARSE_SYNTAX_ERROR;

  // words
  while (i < ntok && toks[i].kind == TOK_WORD) {
    parse_result_t ar = add_arg(out_cmd, toks[i].text, toks[i].quote);
    if (ar != PARSE_OK) return ar;
    i++;
  }

  if (out_cmd->argc == 0) return PARSE_SYNTAX_ERROR;

  if (!allow_redirects) {
    *io_i = i;
    return PARSE_OK;
  }

  // redirects? (must be at end)
  int seen_in = 0;
  int seen_out = 0;

  while (i < ntok) {
    token_kind_t k = toks[i].kind;

    if (k != TOK_REDIR_IN && k != TOK_REDIR_OUT && k != TOK_REDIR_APPEND) break;

    if (i + 1 >= ntok || toks[i + 1].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;

    const char *path = toks[i + 1].text;
    quote_kind_t pq = toks[i + 1].quote;

    if (k == TOK_REDIR_IN) {
      if (seen_in) return PARSE_SYNTAX_ERROR;
      if (seen_out) return PARSE_SYNTAX_ERROR; // < は > より前のみ（仕様簡易文法）
      out_cmd->in_path_raw = path;
      out_cmd->in_quote = pq;
      seen_in = 1;
    } else {
      if (seen_out) return PARSE_SYNTAX_ERROR;
      out_cmd->out_path_raw = path;
      out_cmd->out_quote = pq;
      out_cmd->out_append = (k == TOK_REDIR_APPEND);
      seen_out = 1;
    }

    i += 2;

    // redirects must be last elements of command
    if (i < ntok && toks[i].kind == TOK_WORD) return PARSE_SYNTAX_ERROR;
  }

  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_pipeline(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_pipeline_t *out_pl,
  int allow_redirects
) {
  out_pl->has_right = 0;
  init_cmd(&out_pl->left);
  init_cmd(&out_pl->right);

  parse_result_t r = parse_command(toks, ntok, io_i, &out_pl->left, allow_redirects);
  if (r != PARSE_OK) return r;

  if (*io_i < ntok && toks[*io_i].kind == TOK_PIPE) {
    (*io_i)++;
    out_pl->has_right = 1;

    r = parse_command(toks, ntok, io_i, &out_pl->right, allow_redirects);
    if (r != PARSE_OK) return r;

    // 1段パイプのみ
    if (*io_i < ntok && toks[*io_i].kind == TOK_PIPE) return PARSE_SYNTAX_ERROR;

    // 制約: < は左のみ、> は右のみ
    if (out_pl->right.in_path_raw != NULL) return PARSE_SYNTAX_ERROR;
    if (out_pl->left.out_path_raw != NULL) return PARSE_SYNTAX_ERROR;
  }

  return PARSE_OK;
}

parse_result_t ush_parse_line(
  const token_t *toks,
  int ntok,
  ush_ast_t *out_ast,
  int *out_root
) {
  if (out_ast == NULL || out_root == NULL) return PARSE_SYNTAX_ERROR;
  out_ast->n = 0;
  *out_root = -1;

  if (ntok <= 0) return PARSE_EMPTY;

  int i = 0;

  // 1つ目 pipeline
  ush_pipeline_t pl;

  parse_result_t r = parse_pipeline(toks, ntok, &i, &pl, 1);
  if (r != PARSE_OK) return r;

  int left_idx = new_node(out_ast, NODE_PIPELINE);
  if (left_idx < 0) return PARSE_TOO_MANY_TOKENS;
  out_ast->nodes[left_idx].pl = pl;

  while (i < ntok) {
    token_kind_t op = toks[i].kind;
    if (op != TOK_AND && op != TOK_OR) return PARSE_SYNTAX_ERROR;
    i++;

    r = parse_pipeline(toks, ntok, &i, &pl, 1);
    if (r != PARSE_OK) return r;

    int right_idx = new_node(out_ast, NODE_PIPELINE);
    if (right_idx < 0) return PARSE_TOO_MANY_TOKENS;
    out_ast->nodes[right_idx].pl = pl;

    int parent = new_node(out_ast, (op == TOK_AND) ? NODE_AND : NODE_OR);
    if (parent < 0) return PARSE_TOO_MANY_TOKENS;

    out_ast->nodes[parent].left = left_idx;
    out_ast->nodes[parent].right = right_idx;

    left_idx = parent;
  }

  *out_root = left_idx;
  return PARSE_OK;
}
