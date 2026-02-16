#pragma once
#include <stddef.h>
#include "ush_limits.h"

typedef struct {
  char items[USH_HISTORY_MAX][USH_MAX_LINE_LEN + 1];
  int count;
  int cursor;   // 履歴参照位置（0..count）
} ush_history_t;

int ush_lineedit_readline(
  const char *prompt,
  char *out_line,
  size_t out_cap,
  ush_history_t *hist,
  int last_status
);
