#pragma once
#include "ush_err.h"
#include "ush_limits.h"
#include "ush_tokenize.h"

typedef struct {
  const char *argv_raw[USH_MAX_ARGS + 1];
  quote_kind_t argv_quote[USH_MAX_ARGS];
  int argc;

  const char *in_path_raw;   // < file
  quote_kind_t in_quote;

  const char *out_path_raw;  // > file, >> file
  quote_kind_t out_quote;
  int out_append;        // 0:>, 1:>>
} ush_cmd_t;

typedef struct {
  ush_cmd_t left;
  int has_right;
  ush_cmd_t right;
} ush_pipeline_t;

typedef enum {
  NODE_PIPELINE = 0,
  NODE_AND,
  NODE_OR,
  NODE_SEQ,
} node_kind_t;

typedef struct ush_node {
  node_kind_t kind;
  int left;   // index (NODE_AND/OR/SEQ)
  int right;  // index (NODE_AND/OR/SEQ)
  ush_pipeline_t pl; // NODE_PIPELINE
} ush_node_t;

typedef struct {
  ush_node_t nodes[USH_MAX_PIPES];
  int n;
} ush_ast_t;

parse_result_t ush_parse_line(
  const token_t *toks,
  int ntok,
  ush_ast_t *out_ast,
  int *out_root
);
