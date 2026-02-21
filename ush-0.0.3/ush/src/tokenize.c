#include "ush_tokenize.h"

#include "ush_utils.h"

#include <string.h>

static int is_glob_char(char c) {
  return c == '*' || c == '?' || c == '[' || c == ']';
}

static int push_tok(token_t out[], int *io_n, token_kind_t k, quote_kind_t q, const char *t) {
  if (*io_n >= USH_MAX_TOKENS) return 1;
  out[*io_n].kind = k;
  out[*io_n].quote = q;
  out[*io_n].text = t;
  (*io_n)++;
  return 0;
}

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
) {
  if (out_ntok) *out_ntok = 0;
  if (line == NULL) return PARSE_EMPTY;

  size_t len = strlen(line);
  if (len > USH_MAX_LINE_LEN) return PARSE_TOO_LONG;

  int ntok = 0;
  size_t bi = 0;

  size_t i = 0;
  while (i < len) {
    // skip spaces
    while (i < len && ush_is_space_ch(line[i])) i++;
    if (i >= len) break;

    // token-start comment
    if (line[i] == '#') {
      break;
    }

    // operators
    if (line[i] == '&') {
      if (i + 1 < len && line[i + 1] == '&') {
        if (push_tok(out_tokens, &ntok, TOK_AND, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      return PARSE_UNSUPPORTED;
    }

    if (line[i] == '|') {
      if (i + 1 < len && line[i + 1] == '|') {
        if (push_tok(out_tokens, &ntok, TOK_OR, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      if (push_tok(out_tokens, &ntok, TOK_PIPE, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    if (line[i] == '<') {
      // ヒアドキュメント（<< / <<<）は未対応
      if (i + 1 < len && line[i + 1] == '<') {
        return PARSE_UNSUPPORTED;
      }
      if (push_tok(out_tokens, &ntok, TOK_REDIR_IN, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    if (line[i] == '>') {
      if (i + 1 < len && line[i + 1] == '>') {
        if (push_tok(out_tokens, &ntok, TOK_REDIR_APPEND, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      if (push_tok(out_tokens, &ntok, TOK_REDIR_OUT, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    // unsupported single-char tokens (always)
    if (line[i] == ';' || line[i] == '(' || line[i] == ')' || line[i] == '{' || line[i] == '}') {
      return PARSE_UNSUPPORTED;
    }

    // WORD
    quote_kind_t q = QUOTE_NONE;

    if (line[i] == '\'') {
      q = QUOTE_SINGLE;
      i++;
      size_t start = bi;
      while (i < len && line[i] != '\'') {
        if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = line[i++];
      }
      if (i >= len) return PARSE_SYNTAX_ERROR;
      // consume closing '
      i++;

      // token must end here
      if (i < len && !ush_is_space_ch(line[i]) && line[i] != '#' &&
          line[i] != '|' && line[i] != '&' && line[i] != '<' && line[i] != '>') {
        return PARSE_SYNTAX_ERROR;
      }

      out_buf[bi++] = '\0';
      if (push_tok(out_tokens, &ntok, TOK_WORD, q, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;
      continue;
    }

    if (line[i] == '"') {
      q = QUOTE_DOUBLE;
      i++;
      size_t start = bi;
      while (i < len && line[i] != '"') {
        if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = line[i++];
      }
      if (i >= len) return PARSE_SYNTAX_ERROR;
      i++;

      if (i < len && !ush_is_space_ch(line[i]) && line[i] != '#' &&
          line[i] != '|' && line[i] != '&' && line[i] != '<' && line[i] != '>') {
        return PARSE_SYNTAX_ERROR;
      }

      out_buf[bi++] = '\0';
      if (push_tok(out_tokens, &ntok, TOK_WORD, q, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;
      continue;
    }

    // unquoted word: read until space/operator/comment-start
    size_t start = bi;
    while (i < len) {
      char c = line[i];
      if (ush_is_space_ch(c)) break;
      // トークン途中の '#' は文字として扱う（コメント開始は「トークン先頭の #」のみ）

      // stop before operator
      if (c == '|' || c == '&' || c == '<' || c == '>') break;

      if (c == '\'' || c == '"') return PARSE_SYNTAX_ERROR;
      if (c == ';' || c == '(' || c == ')' || c == '{' || c == '}') return PARSE_UNSUPPORTED;
      if (is_glob_char(c)) {
        if (!(c == '?' && i > 0 && line[i - 1] == '$')) return PARSE_UNSUPPORTED;
      }

      if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
      if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
      out_buf[bi++] = c;
      i++;
    }

    if (bi == start) {
      return PARSE_SYNTAX_ERROR;
    }

    out_buf[bi++] = '\0';
    if (push_tok(out_tokens, &ntok, TOK_WORD, QUOTE_NONE, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;

    // token-start comment: if next is '#', stop
    while (i < len && ush_is_space_ch(line[i])) i++;
    if (i < len && line[i] == '#') break;
  }

  if (out_ntok) *out_ntok = ntok;
  return (ntok == 0) ? PARSE_EMPTY : PARSE_OK;
}
