#include "uim_utf8.h"

#include "uim_limits.h"

#include <string.h>

int uim_utf8_char_len(unsigned char b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 1; // invalid lead; treat as single byte
}

static int is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

size_t uim_utf8_prev(const char *s, size_t i) {
  if (s == NULL || i == 0) return 0;
  size_t n = strlen(s);
  if (i > n) i = n;

  size_t j = i - 1;
  // move to lead byte
  int lim = 0;
  while (j > 0 && is_cont((unsigned char)s[j]) && lim < 4) {
    j--;
    lim++;
  }
  return j;
}

size_t uim_utf8_next(const char *s, size_t i) {
  if (s == NULL) return 0;
  size_t n = strlen(s);
  if (i >= n) return n;

  int l = uim_utf8_char_len((unsigned char)s[i]);
  size_t j = i + (size_t)l;
  if (j > n) j = n;
  // If we landed mid-continuation due to invalid bytes, skip continuations.
  while (j < n && is_cont((unsigned char)s[j])) j++;
  return j;
}

int uim_utf8_width_at(const char *s, size_t i, size_t *out_len) {
  if (out_len) *out_len = 0;
  if (s == NULL) return 1;
  unsigned char b = (unsigned char)s[i];
  if (b == '\0') return 1;

  int l = uim_utf8_char_len(b);
  if (out_len) *out_len = (size_t)l;

  if (b < 0x80) {
    if (b == '\t') return UIM_TABSTOP;
    return 1;
  }

  // Rough: treat any non-ASCII as width 2.
  return 2;
}

int uim_disp_width(const char *s) {
  if (s == NULL) return 0;
  int col = 0;
  for (size_t i = 0; s[i] != '\0';) {
    size_t bl = 0;
    int w = uim_utf8_width_at(s, i, &bl);
    if ((unsigned char)s[i] == '\t') {
      int next = ((col / UIM_TABSTOP) + 1) * UIM_TABSTOP;
      col = next;
    } else {
      col += w;
    }
    if (bl == 0) bl = 1;
    i += bl;
  }
  return col;
}

int uim_disp_col_for_byte_index(const char *s, size_t i) {
  if (s == NULL) return 0;
  size_t n = strlen(s);
  if (i > n) i = n;

  int col = 0;
  for (size_t j = 0; j < i && s[j] != '\0';) {
    size_t bl = 0;
    int w = uim_utf8_width_at(s, j, &bl);
    if ((unsigned char)s[j] == '\t') {
      int next = ((col / UIM_TABSTOP) + 1) * UIM_TABSTOP;
      col = next;
    } else {
      col += w;
    }
    if (bl == 0) bl = 1;
    j += bl;
  }
  return col;
}

size_t uim_byte_index_for_disp_col(const char *s, int target_col) {
  if (s == NULL) return 0;
  if (target_col <= 0) return 0;

  int col = 0;
  size_t i = 0;
  while (s[i] != '\0') {
    size_t bl = 0;
    int w = uim_utf8_width_at(s, i, &bl);

    int next_col;
    if ((unsigned char)s[i] == '\t') {
      next_col = ((col / UIM_TABSTOP) + 1) * UIM_TABSTOP;
    } else {
      next_col = col + w;
    }

    if (next_col > target_col) break;

    col = next_col;
    if (bl == 0) bl = 1;
    i += bl;
  }
  return i;
}
