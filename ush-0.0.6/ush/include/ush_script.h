#pragma once

#include "ush_err.h"
#include "ush_limits.h"
#include "ush_tokenize.h"
#include "ush.h"

// 文(Stmt)ベースのスクリプト

enum {
  USH_MAX_STMTS = 256,
  USH_MAX_ELIF  = 16,
  USH_MAX_CASE_ITEMS = 32,
  USH_MAX_CASE_PATS  = 16,
};

typedef enum {
  ST_SIMPLE = 0, // pipeline/&&/|| のみ（';' は外側で分割）
  ST_SEQ,
  ST_IF,
  ST_WHILE,
  ST_FOR,
  ST_CASE,
} stmt_kind_t;

typedef struct {
  int start; // inclusive token index
  int end;   // exclusive token index
} tok_range_t;

typedef struct {
  int pat_tok[USH_MAX_CASE_PATS];
  int npat;
  int body_root;
} ush_case_item_t;

typedef struct ush_stmt {
  stmt_kind_t kind;

  // ST_SEQ
  int left;
  int right;

  // ST_SIMPLE
  tok_range_t simple;

  // ST_IF
  int if_cond_root;
  int if_then_root;
  int if_else_root; // -1 if none
  int if_elif_cond[USH_MAX_ELIF];
  int if_elif_then[USH_MAX_ELIF];
  int if_n_elif;

  // ST_WHILE
  int while_cond_root;
  int while_body_root;

  // ST_FOR
  int for_name_tok;   // TOK_WORD (name)
  tok_range_t for_words; // TOK_WORD* (expanded at runtime)
  int for_body_root;

  // ST_CASE
  int case_word_tok;  // TOK_WORD
  ush_case_item_t case_items[USH_MAX_CASE_ITEMS];
  int case_nitems;
} ush_stmt_t;

typedef struct {
  ush_stmt_t nodes[USH_MAX_STMTS];
  int n;
} ush_script_t;

parse_result_t ush_parse_script(
  const token_t *toks,
  int ntok,
  ush_script_t *out,
  int *out_root
);

int ush_exec_script(
  ush_state_t *st,
  const token_t *toks,
  int ntok,
  const ush_script_t *sc,
  int root
);
