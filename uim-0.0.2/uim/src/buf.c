#include "uim_buf.h"

#include "uim_limits.h"
#include "uim_utf8.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void line_free(uim_line_t *l) {
  if (!l) return;
  free(l->data);
  l->data = NULL;
  l->len = 0;
  l->cap = 0;
}

static int line_reserve(uim_line_t *l, size_t need) {
  if (!l) return 1;
  if (need + 1 <= l->cap) return 0;
  size_t nc = (l->cap == 0) ? 64 : l->cap;
  while (nc < need + 1) nc *= 2;
  if (nc > (size_t)UIM_MAX_LINE_BYTES + 1) nc = (size_t)UIM_MAX_LINE_BYTES + 1;
  if (need + 1 > nc) return 1;
  char *p = (char *)realloc(l->data, nc);
  if (!p) return 1;
  l->data = p;
  l->cap = nc;
  return 0;
}

static int line_set(uim_line_t *l, const char *s, size_t slen) {
  if (!l) return 1;
  if (slen > (size_t)UIM_MAX_LINE_BYTES) return 1;
  if (line_reserve(l, slen)) return 1;
  memcpy(l->data, s, slen);
  l->data[slen] = '\0';
  l->len = slen;
  return 0;
}

void uim_buf_init(uim_buf_t *b) {
  if (!b) return;
  b->lines = NULL;
  b->n = 0;
  b->cap = 0;
  b->modified = 0;
}

void uim_buf_free(uim_buf_t *b) {
  if (!b) return;
  for (size_t i = 0; i < b->n; i++) line_free(&b->lines[i]);
  free(b->lines);
  b->lines = NULL;
  b->n = 0;
  b->cap = 0;
  b->modified = 0;
}

static int buf_reserve(uim_buf_t *b, size_t need) {
  if (!b) return 1;
  if (need <= b->cap) return 0;
  size_t nc = (b->cap == 0) ? 32 : b->cap;
  while (nc < need) nc *= 2;
  uim_line_t *p = (uim_line_t *)realloc(b->lines, nc * sizeof(uim_line_t));
  if (!p) return 1;
  for (size_t i = b->cap; i < nc; i++) {
    p[i].data = NULL;
    p[i].len = 0;
    p[i].cap = 0;
  }
  b->lines = p;
  b->cap = nc;
  return 0;
}

int uim_buf_ensure_nonempty(uim_buf_t *b) {
  if (!b) return 1;
  if (b->n > 0) return 0;
  if (buf_reserve(b, 1)) return 1;
  if (line_set(&b->lines[0], "", 0)) return 1;
  b->n = 1;
  return 0;
}

int uim_buf_load(uim_buf_t *b, const char *path) {
  if (!b) return 1;
  uim_buf_free(b);
  uim_buf_init(b);

  if (path == NULL) return 1;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    // Treat missing file as empty buffer.
    if (errno == ENOENT) {
      if (uim_buf_ensure_nonempty(b)) return 1;
      b->modified = 0;
      return 0;
    }
    return 1;
  }

  char tmp[UIM_MAX_LINE_BYTES + 2];
  while (fgets(tmp, sizeof(tmp), fp) != NULL) {
    size_t n = strlen(tmp);
    while (n > 0 && (tmp[n - 1] == '\n' || tmp[n - 1] == '\r')) tmp[--n] = '\0';

    if (buf_reserve(b, b->n + 1)) {
      fclose(fp);
      return 1;
    }

    if (line_set(&b->lines[b->n], tmp, n)) {
      fclose(fp);
      return 1;
    }
    b->n++;
  }
  fclose(fp);

  if (uim_buf_ensure_nonempty(b)) return 1;
  b->modified = 0;
  return 0;
}

int uim_buf_save(const uim_buf_t *b, const char *path) {
  if (!b || !path) return 1;
  FILE *fp = fopen(path, "wb");
  if (!fp) return 1;

  for (size_t i = 0; i < b->n; i++) {
    const char *s = b->lines[i].data ? b->lines[i].data : "";
    size_t sl = b->lines[i].len;
    if (fwrite(s, 1, sl, fp) != sl) {
      fclose(fp);
      return 1;
    }
    if (fwrite("\n", 1, 1, fp) != 1) {
      fclose(fp);
      return 1;
    }
  }

  fclose(fp);
  return 0;
}

int uim_buf_delete_line(uim_buf_t *b, size_t row) {
  if (!b) return 1;
  if (b->n == 0) return 1;
  if (row >= b->n) row = b->n - 1;

  line_free(&b->lines[row]);

  for (size_t i = row; i + 1 < b->n; i++) {
    b->lines[i] = b->lines[i + 1];
  }

  // clear last slot
  b->lines[b->n - 1].data = NULL;
  b->lines[b->n - 1].len = 0;
  b->lines[b->n - 1].cap = 0;

  b->n--;
  if (b->n == 0) {
    if (uim_buf_ensure_nonempty(b)) return 1;
  }

  b->modified = 1;
  return 0;
}

int uim_buf_insert_line_after(uim_buf_t *b, size_t row, const char *s, size_t slen) {
  if (!b) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  if (buf_reserve(b, b->n + 1)) return 1;
  for (size_t i = b->n; i > row + 1; i--) {
    b->lines[i] = b->lines[i - 1];
  }
  b->lines[row + 1].data = NULL;
  b->lines[row + 1].len = 0;
  b->lines[row + 1].cap = 0;

  if (line_set(&b->lines[row + 1], s ? s : "", slen)) return 1;
  b->n++;
  b->modified = 1;
  return 0;
}

int uim_buf_insert_bytes(uim_buf_t *b, size_t row, size_t col_byte, const char *bytes, size_t nbytes) {
  if (!b || !bytes) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  uim_line_t *l = &b->lines[row];
  if (!l->data) {
    if (line_set(l, "", 0)) return 1;
  }

  if (col_byte > l->len) col_byte = l->len;

  if (l->len + nbytes > (size_t)UIM_MAX_LINE_BYTES) return 1;
  if (line_reserve(l, l->len + nbytes)) return 1;

  memmove(l->data + col_byte + nbytes, l->data + col_byte, l->len - col_byte + 1);
  memcpy(l->data + col_byte, bytes, nbytes);
  l->len += nbytes;

  b->modified = 1;
  return 0;
}

int uim_buf_delete_prev_char(uim_buf_t *b, size_t row, size_t *io_col_byte) {
  if (!b || !io_col_byte) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  uim_line_t *l = &b->lines[row];
  if (!l->data) return 0;

  size_t col = *io_col_byte;
  if (col == 0 || col > l->len) return 0;

  size_t prev = uim_utf8_prev(l->data, col);
  if (prev >= col) return 0;
  size_t del = col - prev;

  memmove(l->data + prev, l->data + col, l->len - col + 1);
  l->len -= del;
  *io_col_byte = prev;

  b->modified = 1;
  return 0;
}

int uim_buf_split_line(uim_buf_t *b, size_t row, size_t col_byte) {
  if (!b) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  uim_line_t *l = &b->lines[row];
  if (!l->data) {
    if (line_set(l, "", 0)) return 1;
  }

  if (col_byte > l->len) col_byte = l->len;

  const char *right = l->data + col_byte;
  size_t rlen = l->len - col_byte;

  // shrink current line
  l->data[col_byte] = '\0';
  l->len = col_byte;

  if (uim_buf_insert_line_after(b, row, right, rlen)) return 1;
  b->modified = 1;
  return 0;
}
