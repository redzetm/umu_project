#pragma once
#include "ush_limits.h"
#include "ush_err.h"
#include "ush_tokenize.h"

typedef struct {
  char *argv[USH_MAX_ARGS + 1];
  int argc;
} ush_cmd_t;

typedef struct {
  ush_cmd_t cmds[USH_MAX_CMDS];
  int ncmd;

  const char *in_path;   // < file
  const char *out_path;  // > file, >> file
  int out_append;        // 0:>, 1:>>
} ush_pipeline_t;

parse_result_t ush_parse_pipeline(
  const token_t *toks,
  int ntok,
  ush_pipeline_t *out_pl
);
