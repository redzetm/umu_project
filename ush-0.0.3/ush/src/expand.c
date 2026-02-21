#include "ush_expand.h"

#include "ush_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int append_str(char *out, size_t cap, size_t *io_len, const char *s) {
  if (out == NULL || io_len == NULL || s == NULL) return 1;
  size_t sl = strlen(s);
  if (*io_len + sl + 1 > cap) return 1;
  memcpy(out + *io_len, s, sl);
  *io_len += sl;
  out[*io_len] = '\0';
  return 0;
}

static int append_ch(char *out, size_t cap, size_t *io_len, char c) {
  if (out == NULL || io_len == NULL) return 1;
  if (*io_len + 2 > cap) return 1;
  out[*io_len] = c;
  (*io_len)++;
  out[*io_len] = '\0';
  return 0;
}

static parse_result_t expand_var(const ush_expand_ctx_t *ctx, const char *p, size_t *io_i, char *out, size_t cap, size_t *io_len) {
  // p[*io_i] == '$'
  size_t i = *io_i;
  char n = p[i + 1];
  if (n == '\0') {
    if (append_ch(out, cap, io_len, '$')) return PARSE_TOO_LONG;
    *io_i = i + 1;
    return PARSE_OK;
  }

  if (n == '?') {
    char tmp[32];
    int st = (ctx != NULL) ? ctx->last_status : 0;
    snprintf(tmp, sizeof(tmp), "%d", st);
    if (append_str(out, cap, io_len, tmp)) return PARSE_TOO_LONG;
    *io_i = i + 2;
    return PARSE_OK;
  }

  if (n == '{' || n == '(' || (n >= '0' && n <= '9')) {
    return PARSE_UNSUPPORTED;
  }

  if (n == '`') {
    return PARSE_UNSUPPORTED;
  }

  if (isalpha((unsigned char)n) || n == '_') {
    size_t j = i + 1;
    char name[256];
    size_t ni = 0;
    while (p[j] != '\0' && (isalnum((unsigned char)p[j]) || p[j] == '_')) {
      if (ni + 1 < sizeof(name)) name[ni++] = p[j];
      j++;
    }
    name[ni] = '\0';

    const char *v = getenv(name);
    if (v == NULL) v = "";
    if (append_str(out, cap, io_len, v)) return PARSE_TOO_LONG;

    *io_i = j;
    return PARSE_OK;
  }

  // それ以外は '$' を文字として扱う
  if (append_ch(out, cap, io_len, '$')) return PARSE_TOO_LONG;
  *io_i = i + 1;
  return PARSE_OK;
}

parse_result_t ush_expand_word(
  const ush_expand_ctx_t *ctx,
  quote_kind_t quote,
  const char *in,
  char *out,
  size_t out_cap
) {
  if (out == NULL || out_cap == 0) return PARSE_TOO_LONG;
  out[0] = '\0';
  if (in == NULL) return PARSE_OK;

  // tilde expansion（未クォートのみ）
  // 変数展開と併用され得るため、ここでは一旦 in を差し替えて後段の変数展開ループへ流す。
  char tilde_buf[USH_MAX_TOKEN_LEN + 1];
  const char *src = in;

  if (quote == QUOTE_NONE && in[0] == '~') {
    if (in[1] == '\0' || in[1] == '/') {
      const char *home = getenv("HOME");
      if (home == NULL || home[0] == '\0') home = "/";

      size_t ti = 0;
      if (append_str(tilde_buf, sizeof(tilde_buf), &ti, home)) return PARSE_TOO_LONG;

      if (in[1] == '/') {
        // HOME が "/" のときに "//" を作らない
        const char *rest = in + 1;
        if (ti > 0 && tilde_buf[ti - 1] == '/' && rest[0] == '/') rest++;
        if (append_str(tilde_buf, sizeof(tilde_buf), &ti, rest)) return PARSE_TOO_LONG;
      }

      src = tilde_buf;
    } else {
      // ~user は未対応
      return PARSE_UNSUPPORTED;
    }
  }

  size_t olen = 0;

  // 変数展開（未クォート/ダブルクォート）
  if (quote == QUOTE_SINGLE) {
    if (append_str(out, out_cap, &olen, in)) return PARSE_TOO_LONG;
    return PARSE_OK;
  }

  for (size_t i = 0; src[i] != '\0';) {
    if (src[i] == '$') {
      parse_result_t r = expand_var(ctx, src, &i, out, out_cap, &olen);
      if (r != PARSE_OK) return r;
      continue;
    }

    if (src[i] == '`') {
      return PARSE_UNSUPPORTED;
    }

    if (append_ch(out, out_cap, &olen, src[i])) return PARSE_TOO_LONG;
    i++;
  }

  return PARSE_OK;
}
