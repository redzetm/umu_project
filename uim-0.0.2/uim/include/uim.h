#ifndef UIM_H
#define UIM_H

#include "uim_buf.h"

#include <stddef.h>

typedef enum {
  UIM_MODE_NORMAL = 0,
  UIM_MODE_INSERT,
  UIM_MODE_COLON,
  UIM_MODE_SEARCH,
} uim_mode_t;

typedef struct {
  uim_mode_t mode;

  const char *path;
  uim_buf_t buf;

  // cursor in buffer
  size_t cur_row;
  size_t cur_col_byte; // UTF-8 boundary

  // scroll
  size_t row_off;
  int col_off; // display columns

  // yank buffer (one or more lines)
  char **yank_lines;
  size_t yank_n;

  // colon command input
  char colon[64];
  size_t colon_len;

  // search input (/) and last search pattern
  char search[64];
  size_t search_len;
  char last_search[64];
  size_t last_search_len;
  int has_last_search;

  // status message
  char status[128];
  int status_ttl; // frames

  // pending normal-mode command ('d', 'y', or 'g')
  int pending;

  // numeric prefix for NORMAL (e.g. 3dd, 10G)
  int num_prefix;

  // insert-mode UTF-8 assemble buffer
  unsigned char u8buf[8];
  int u8len;
  int u8need;

  int running;
} uim_t;

int uim_run_interactive(uim_t *u);
int uim_run_batch(uim_t *u, int in_fd);

#endif
