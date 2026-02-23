#ifndef UIM_UTF8_H
#define UIM_UTF8_H

#include <stddef.h>

int uim_utf8_char_len(unsigned char b);

// Returns index of previous UTF-8 character start (or 0).
size_t uim_utf8_prev(const char *s, size_t i);

// Returns index of next UTF-8 character start (or strlen(s)).
size_t uim_utf8_next(const char *s, size_t i);

// Returns display width at s[i] (1 or 2) and sets *out_len to byte length.
// Tabs are treated as UIM_TABSTOP spaces (width depends on current column; handled elsewhere).
int uim_utf8_width_at(const char *s, size_t i, size_t *out_len);

// Rough display width: ASCII=1, non-ASCII=2, '\t'=tabstop alignment.
int uim_disp_width(const char *s);

// Display column (0-based) for byte index i (clamped to end).
int uim_disp_col_for_byte_index(const char *s, size_t i);

// Byte index for desired display column (0-based). Returns a valid UTF-8 boundary.
size_t uim_byte_index_for_disp_col(const char *s, int target_col);

#endif
