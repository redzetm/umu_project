#ifndef UIM_BUF_H
#define UIM_BUF_H

#include <stddef.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} uim_line_t;

typedef struct {
  uim_line_t *lines;
  size_t n;
  size_t cap;
  int modified;
} uim_buf_t;

void uim_buf_init(uim_buf_t *b);
void uim_buf_free(uim_buf_t *b);

int uim_buf_load(uim_buf_t *b, const char *path);
int uim_buf_save(const uim_buf_t *b, const char *path);

// Ensure there is at least one line.
int uim_buf_ensure_nonempty(uim_buf_t *b);

// Delete/yank/paste whole line.
int uim_buf_delete_line(uim_buf_t *b, size_t row);
int uim_buf_insert_line_after(uim_buf_t *b, size_t row, const char *s, size_t slen);

// Edit within a line at byte index (must be UTF-8 boundary).
int uim_buf_insert_bytes(uim_buf_t *b, size_t row, size_t col_byte, const char *bytes, size_t nbytes);
int uim_buf_delete_prev_char(uim_buf_t *b, size_t row, size_t *io_col_byte);
int uim_buf_split_line(uim_buf_t *b, size_t row, size_t col_byte);

#endif
