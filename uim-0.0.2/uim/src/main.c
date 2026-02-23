#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "uim.h"

#include "uim_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void uim_init(uim_t *u, const char *path) {
  memset(u, 0, sizeof(*u));
  u->mode = UIM_MODE_NORMAL;
  u->path = path;
  uim_buf_init(&u->buf);
  u->cur_row = 0;
  u->cur_col_byte = 0;
  u->row_off = 0;
  u->col_off = 0;
  u->yank_lines = NULL;
  u->yank_n = 0;
  u->colon[0] = '\0';
  u->colon_len = 0;
  u->search[0] = '\0';
  u->search_len = 0;
  u->last_search[0] = '\0';
  u->last_search_len = 0;
  u->has_last_search = 0;
  u->pending = 0;
  u->num_prefix = 0;
  u->u8len = 0;
  u->u8need = 0;
  u->running = 1;
}

static void uim_destroy(uim_t *u) {
  if (!u) return;
  if (u->yank_lines) {
    for (size_t i = 0; i < u->yank_n; i++) free(u->yank_lines[i]);
    free(u->yank_lines);
  }
  u->yank_lines = NULL;
  u->yank_n = 0;
  uim_buf_free(&u->buf);
}

int main(int argc, char **argv) {
  int batch = 0;
  const char *path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--batch") == 0) {
      batch = 1;
      continue;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      fprintf(stderr, "usage: uim [--batch] <file>\n");
      return 2;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "uim: unknown option\n");
      return 2;
    }
    path = argv[i];
  }

  if (path == NULL) {
    fprintf(stderr, "usage: uim [--batch] <file>\n");
    return 2;
  }

  uim_t u;
  uim_init(&u, path);

  if (uim_buf_load(&u.buf, path) != 0) {
    fprintf(stderr, "uim: open failed\n");
    uim_destroy(&u);
    return 1;
  }

  int r = batch ? uim_run_batch(&u, STDIN_FILENO) : uim_run_interactive(&u);
  uim_destroy(&u);
  return r;
}
