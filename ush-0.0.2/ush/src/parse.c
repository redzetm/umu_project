#include "ush_parse.h"

#include <string.h>

static void init_pipeline(ush_pipeline_t *pl) {
  memset(pl, 0, sizeof(*pl));
  pl->ncmd = 1;
  for (int i = 0; i < USH_MAX_CMDS; i++) {
    pl->cmds[i].argc = 0;
    pl->cmds[i].argv[0] = NULL;
  }
}

parse_result_t ush_parse_pipeline(
  const token_t *toks,
  int ntok,
  ush_pipeline_t *out_pl
) {
  if (toks == NULL || out_pl == NULL) return PARSE_UNSUPPORTED;
  if (ntok <= 0) return PARSE_EMPTY;

  init_pipeline(out_pl);

  int cmd_i = 0;

  for (int i = 0; i < ntok; i++) {
    token_kind_t k = toks[i].kind;

    if (k == TOK_WORD) {
      ush_cmd_t *cmd = &out_pl->cmds[cmd_i];
      if (cmd->argc >= USH_MAX_ARGS) return PARSE_TOO_MANY_ARGS;
      cmd->argv[cmd->argc++] = (char *)toks[i].text;
      continue;
    }

    if (k == TOK_PIPE) {
      if (out_pl->out_path != NULL) return PARSE_SYNTAX_ERROR;
      if (out_pl->cmds[cmd_i].argc == 0) return PARSE_SYNTAX_ERROR;
      if (cmd_i + 1 >= USH_MAX_CMDS) return PARSE_SYNTAX_ERROR;
      cmd_i++;
      out_pl->ncmd = cmd_i + 1;
      continue;
    }

    if (k == TOK_REDIR_IN) {
      if (out_pl->in_path != NULL) return PARSE_SYNTAX_ERROR;
      if (cmd_i != 0) return PARSE_SYNTAX_ERROR;
      if (i + 1 >= ntok) return PARSE_SYNTAX_ERROR;
      if (toks[i + 1].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
      out_pl->in_path = toks[i + 1].text;
      i++;
      continue;
    }

    if (k == TOK_REDIR_OUT || k == TOK_REDIR_APPEND) {
      if (out_pl->out_path != NULL) return PARSE_SYNTAX_ERROR;
      if (i + 1 >= ntok) return PARSE_SYNTAX_ERROR;
      if (toks[i + 1].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
      out_pl->out_path = toks[i + 1].text;
      out_pl->out_append = (k == TOK_REDIR_APPEND) ? 1 : 0;
      i++;
      continue;
    }

    return PARSE_SYNTAX_ERROR;
  }

  if (out_pl->cmds[cmd_i].argc == 0) return PARSE_SYNTAX_ERROR;

  for (int c = 0; c < out_pl->ncmd; c++) {
    out_pl->cmds[c].argv[out_pl->cmds[c].argc] = NULL;
  }

  return PARSE_OK;
}
