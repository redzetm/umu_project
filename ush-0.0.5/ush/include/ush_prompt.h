#pragma once
#include <stddef.h>

// out は NUL 終端される
int ush_prompt_render(char *out, size_t out_cap);
