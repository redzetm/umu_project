#include "ush_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ush_eprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("ush: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ush_perrorf(const char *context) {
  int e = errno;
  if (context == NULL) context = "error";
  fprintf(stderr, "ush: %s: %s\n", context, strerror(e));
}

int ush_is_space_ch(char c) {
  return c == ' ' || c == '\t';
}

int ush_is_blank_line(const char *line) {
  if (line == NULL) return 1;
  for (const char *p = line; *p; p++) {
    if (!ush_is_space_ch(*p)) return 0;
  }
  return 1;
}

int ush_starts_with(const char *s, const char *prefix) {
  if (s == NULL || prefix == NULL) return 0;
  size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

int ush_is_valid_name(const char *name) {
  if (name == NULL || name[0] == '\0') return 0;
  if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return 0;
  for (const char *p = name + 1; *p; p++) {
    if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
  }
  return 1;
}

int ush_is_assignment_word0(const char *s) {
  if (s == NULL) return 0;
  const char *eq = strchr(s, '=');
  if (eq == NULL) return 0;

  // NAME= の NAME 部分が正規
  if (eq == s) return 0;

  char name[256];
  size_t n = (size_t)(eq - s);
  if (n >= sizeof(name)) return 0;
  memcpy(name, s, n);
  name[n] = '\0';

  return ush_is_valid_name(name);
}
