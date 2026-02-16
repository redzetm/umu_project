#pragma once
#include <stddef.h>
#include "ush_limits.h"
#include "ush_err.h"

typedef enum {
  TOK_WORD = 0,
  TOK_PIPE,
  TOK_REDIR_IN,
  TOK_REDIR_OUT,
  TOK_REDIR_APPEND,
} token_kind_t;

typedef struct {
  token_kind_t kind;
  const char *text; // TOK_WORD のみ有効（トークン文字列）
} token_t;

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
);
