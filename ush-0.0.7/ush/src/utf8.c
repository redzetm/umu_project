#include "ush_utf8.h"

#include <string.h>

int ush_utf8_char_len(unsigned char b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 1;
}

static int is_cont(unsigned char b) {
  return (b & 0xC0) == 0x80;
}

size_t ush_utf8_prev(const char *s, size_t i) {
  if (s == NULL || i == 0) return 0;
  size_t n = strlen(s);
  if (i > n) i = n;

  size_t j = i - 1;
  int lim = 0;
  while (j > 0 && is_cont((unsigned char)s[j]) && lim < 4) {
    j--;
    lim++;
  }
  return j;
}

size_t ush_utf8_next(const char *s, size_t i) {
  if (s == NULL) return 0;
  size_t n = strlen(s);
  if (i >= n) return n;

  int l = ush_utf8_char_len((unsigned char)s[i]);
  size_t j = i + (size_t)l;
  if (j > n) j = n;
  while (j < n && is_cont((unsigned char)s[j])) j++;
  return j;
}

int ush_utf8_width_at(const char *s, size_t i, size_t *out_len) {
  if (out_len) *out_len = 0;
  if (s == NULL) return 1;

  unsigned char b = (unsigned char)s[i];
  if (b == '\0') return 1;

  int l = ush_utf8_char_len(b);
  if (out_len) *out_len = (size_t)l;

  if (b < 0x80) {
    if (b == '\t') return 4;
    return 1;
  }

  return 2;
}

int ush_utf8_disp_width_range(const char *s, size_t start, size_t end) {
  if (s == NULL) return 0;
  size_t n = strlen(s);
  if (start > n) start = n;
  if (end > n) end = n;
  if (end < start) end = start;

  int col = 0;
  for (size_t i = start; i < end && s[i] != '\0';) {
    size_t bl = 0;
    int w = ush_utf8_width_at(s, i, &bl);
    col += w;
    if (bl == 0) bl = 1;
    i += bl;
  }
  return col;
}

int ush_utf8_disp_width(const char *s, size_t len) {
  if (s == NULL) return 0;
  size_t n = strlen(s);
  if (len > n) len = n;
  return ush_utf8_disp_width_range(s, 0, len);
}