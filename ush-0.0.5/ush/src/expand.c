#include "ush_expand.h"

#include "ush_exec.h"
#include "ush_parse.h"
#include "ush_tokenize.h"

#include "ush_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { USH_ESC = 1 };

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

static parse_result_t expand_var(
  const ush_expand_ctx_t *ctx,
  const char *p,
  size_t *io_i,
  char *out,
  size_t cap,
  size_t *io_len
) {
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

  if (n == '#') {
    char tmp[32];
    int c = (ctx != NULL) ? ctx->pos_argc : 0;
    snprintf(tmp, sizeof(tmp), "%d", c);
    if (append_str(out, cap, io_len, tmp)) return PARSE_TOO_LONG;
    *io_i = i + 2;
    return PARSE_OK;
  }

  if (n == '@' || n == '*') {
    return PARSE_UNSUPPORTED;
  }

  if (n == '{') {
    // ${NAME}
    size_t j = i + 2;
    if (p[j] == '\0') return PARSE_SYNTAX_ERROR;

    // find closing }
    size_t close = j;
    while (p[close] != '\0' && p[close] != '}') close++;
    if (p[close] != '}') return PARSE_SYNTAX_ERROR;

    if (close == j) return PARSE_UNSUPPORTED;

    char name[256];
    size_t ni = 0;
    for (size_t k = j; k < close; k++) {
      if (ni + 1 < sizeof(name)) name[ni++] = p[k];
    }
    name[ni] = '\0';

    if (!ush_is_valid_name(name)) return PARSE_UNSUPPORTED;

    const char *v = getenv(name);
    if (v == NULL) v = "";
    if (append_str(out, cap, io_len, v)) return PARSE_TOO_LONG;

    *io_i = close + 1;
    return PARSE_OK;
  }

  if (n >= '0' && n <= '9') {
    // $0..$9 のみ対応（$10 以降は未対応）
    if (p[i + 2] != '\0' && p[i + 2] >= '0' && p[i + 2] <= '9') {
      return PARSE_UNSUPPORTED;
    }

    int idx = (int)(n - '0');
    if (idx == 0) {
      const char *s = (ctx != NULL && ctx->script_path != NULL) ? ctx->script_path : "ush";
      if (append_str(out, cap, io_len, s)) return PARSE_TOO_LONG;
      *io_i = i + 2;
      return PARSE_OK;
    }

    const char *s = "";
    if (ctx != NULL && ctx->pos_argv != NULL && idx <= ctx->pos_argc) {
      s = ctx->pos_argv[idx - 1];
      if (s == NULL) s = "";
    }

    if (append_str(out, cap, io_len, s)) return PARSE_TOO_LONG;
    *io_i = i + 2;
    return PARSE_OK;
  }

  if (n == '(') {
    // $(cmdline)
    size_t j = i + 2;
    quote_kind_t q = QUOTE_NONE;
    int closed = 0;

    char cmdline[USH_MAX_LINE_LEN + 1];
    size_t clen = 0;
    cmdline[0] = '\0';

    while (p[j] != '\0') {
      if ((unsigned char)p[j] == (unsigned char)USH_ESC) {
        if (p[j + 1] == '\0') return PARSE_SYNTAX_ERROR;
        // 内側のtokenizeが理解できるように、マーカーはバックスラッシュへ戻す
        if (clen + 2 >= sizeof(cmdline)) return PARSE_TOO_LONG;
        cmdline[clen++] = '\\';
        cmdline[clen++] = p[j + 1];
        cmdline[clen] = '\0';
        j += 2;
        continue;
      }

      char c = p[j];

      if (q == QUOTE_NONE) {
        if (c == '\'') q = QUOTE_SINGLE;
        else if (c == '"') q = QUOTE_DOUBLE;

        if (c == ')') {
          closed = 1;
          j++;
          break;
        }

        if (c == '$' && p[j + 1] == '(') {
          // 入れ子は未対応
          return PARSE_UNSUPPORTED;
        }
      } else if (q == QUOTE_SINGLE) {
        if (c == '\'') q = QUOTE_NONE;
      } else if (q == QUOTE_DOUBLE) {
        if (c == '"') q = QUOTE_NONE;
      }

      if (clen + 1 >= sizeof(cmdline)) return PARSE_TOO_LONG;
      cmdline[clen++] = c;
      cmdline[clen] = '\0';
      j++;
    }

    if (!closed) return PARSE_SYNTAX_ERROR;
    if (ush_is_blank_line(cmdline)) return PARSE_SYNTAX_ERROR;

    // まず解析だけして、構文/未対応を確実に検出する
    token_t toks[USH_MAX_TOKENS];
    int ntok = 0;
    char tokbuf[USH_MAX_LINE_LEN + 1];
    parse_result_t tr = ush_tokenize(cmdline, toks, &ntok, tokbuf);
    if (tr != PARSE_OK) return tr;

    ush_ast_t ast;
    int root = -1;
    parse_result_t pr = ush_parse_line(toks, ntok, &ast, &root);
    if (pr != PARSE_OK) return pr;

    // 実行して stdout を捕捉
    ush_state_t base;
    base.last_status = (ctx != NULL) ? ctx->last_status : 0;
    base.script_path = (ctx != NULL && ctx->script_path != NULL) ? ctx->script_path : "ush";
    base.pos_argc = (ctx != NULL) ? ctx->pos_argc : 0;
    base.pos_argv = (ctx != NULL) ? ctx->pos_argv : NULL;

    const ush_state_t *bs = (ctx != NULL && ctx->cmdsub_base != NULL) ? ctx->cmdsub_base : &base;

    char sub[USH_MAX_LINE_LEN + 1];
    parse_result_t er = ush_exec_capture_stdout(bs, cmdline, sub, sizeof(sub));
    if (er != PARSE_OK) return er;

    if (append_str(out, cap, io_len, sub)) return PARSE_TOO_LONG;
    *io_i = j;
    return PARSE_OK;
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
  char tilde_buf[USH_MAX_TOKEN_LEN + 1];
  const char *src = in;

  if (quote == QUOTE_NONE && in[0] == '~') {
    if (in[1] == '\0' || in[1] == '/') {
      const char *home = getenv("HOME");
      if (home == NULL || home[0] == '\0') home = "/";

      size_t ti = 0;
      if (append_str(tilde_buf, sizeof(tilde_buf), &ti, home)) return PARSE_TOO_LONG;

      if (in[1] == '/') {
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
    if ((unsigned char)src[i] == (unsigned char)USH_ESC) {
      if (src[i + 1] == '\0') return PARSE_SYNTAX_ERROR;
      if (append_ch(out, out_cap, &olen, (char)USH_ESC)) return PARSE_TOO_LONG;
      if (append_ch(out, out_cap, &olen, src[i + 1])) return PARSE_TOO_LONG;
      i += 2;
      continue;
    }

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
