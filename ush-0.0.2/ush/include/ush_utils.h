#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
void ush_perrorf(const char *context);  // ush: <context>: <strerror>

int ush_is_blank_line(const char *line);
int ush_is_comment_line(const char *line);

int ush_starts_with(const char *s, const char *prefix);
