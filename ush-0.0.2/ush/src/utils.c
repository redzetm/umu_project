#include "ush_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

void ush_eprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

void ush_perrorf(const char *context) {
  int saved = errno;
  ush_eprintf("ush: %s: %s\n", context, strerror(saved));
}

int ush_is_blank_line(const char *line) {
  if (line == NULL) return 1;
  for (const char *p = line; *p; p++) {
    if (*p != ' ' && *p != '\t') return 0;
  }
  return 1;
}

int ush_is_comment_line(const char *line) {
  if (line == NULL) return 0;
  const char *p = line;
  while (*p == ' ' || *p == '\t') p++;
  return *p == '#';
}

int ush_starts_with(const char *s, const char *prefix) {
  if (s == NULL || prefix == NULL) return 0;
  while (*prefix) {
    if (*s != *prefix) return 0;
    s++;
    prefix++;
  }
  return 1;
}
