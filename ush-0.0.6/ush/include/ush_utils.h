#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
void ush_perrorf(const char *context);  // ush: <context>: <strerror>

int ush_is_blank_line(const char *line);

int ush_is_space_ch(char c);

int ush_starts_with(const char *s, const char *prefix);

int ush_is_valid_name(const char *name);
int ush_is_assignment_word0(const char *s); // NAME=... の形式か（NAMEは正規）
