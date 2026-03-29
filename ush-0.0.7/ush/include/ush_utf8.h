#pragma once

#include <stddef.h>

int ush_utf8_char_len(unsigned char b);
size_t ush_utf8_prev(const char *s, size_t i);
size_t ush_utf8_next(const char *s, size_t i);
int ush_utf8_width_at(const char *s, size_t i, size_t *out_len);
int ush_utf8_disp_width(const char *s, size_t len);
int ush_utf8_disp_width_range(const char *s, size_t start, size_t end);