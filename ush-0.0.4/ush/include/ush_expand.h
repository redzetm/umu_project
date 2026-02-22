#pragma once
#include <stddef.h>

#include "ush_err.h"
#include "ush_tokenize.h"

typedef struct {
  int last_status;
} ush_expand_ctx_t;

// out は NUL 終端される。
// 失敗時: PARSE_UNSUPPORTED または PARSE_TOO_LONG を返す。
parse_result_t ush_expand_word(
  const ush_expand_ctx_t *ctx,
  quote_kind_t quote,
  const char *in,
  char *out,
  size_t out_cap
);
