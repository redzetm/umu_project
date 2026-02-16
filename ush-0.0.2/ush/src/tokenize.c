#include "ush_tokenize.h"

#include "ush_utils.h"

#include <string.h>

static int is_space(char c) {
  return c == ' ' || c == '\t';
}

static int is_op_char(char c) {
  return c == '|' || c == '<' || c == '>';
}

static int is_unsupported_char(char c) {
  switch (c) {
    case '"':
    case '\\':
    case '$':
    case '*':
    case '?':
    case '[':
    case ']':
    case ';':
    case '&':
      return 1;
    default:
      return 0;
  }
}

static parse_result_t push_tok(token_t out_tokens[USH_MAX_TOKENS], int *io_ntok, token_kind_t kind, const char *text) {
  if (*io_ntok >= USH_MAX_TOKENS) return PARSE_TOO_MANY_TOKENS;
  out_tokens[*io_ntok].kind = kind;
  out_tokens[*io_ntok].text = text;
  (*io_ntok)++;
  return PARSE_OK;
}

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
) {
  if (out_ntok == NULL || out_tokens == NULL || out_buf == NULL) return PARSE_UNSUPPORTED;
  *out_ntok = 0;
  out_buf[0] = '\0';

  if (line == NULL) return PARSE_EMPTY;
  if (ush_is_blank_line(line)) return PARSE_EMPTY;
  if (ush_is_comment_line(line)) return PARSE_EMPTY;

  const size_t buf_cap = (size_t)USH_MAX_LINE_LEN + 1;
  size_t bi = 0;
  size_t i = 0;

  while (line[i] != '\0') {
    while (is_space(line[i])) i++;
    if (line[i] == '\0') break;

    char c = line[i];

    if (c == '|') {
      if (line[i + 1] == '|') return PARSE_UNSUPPORTED;
      parse_result_t r = push_tok(out_tokens, out_ntok, TOK_PIPE, NULL);
      if (r != PARSE_OK) return r;
      i++;
      continue;
    }

    if (c == '<') {
      parse_result_t r = push_tok(out_tokens, out_ntok, TOK_REDIR_IN, NULL);
      if (r != PARSE_OK) return r;
      i++;
      continue;
    }

    if (c == '>') {
      if (line[i + 1] == '>') {
        parse_result_t r = push_tok(out_tokens, out_ntok, TOK_REDIR_APPEND, NULL);
        if (r != PARSE_OK) return r;
        i += 2;
      } else {
        parse_result_t r = push_tok(out_tokens, out_ntok, TOK_REDIR_OUT, NULL);
        if (r != PARSE_OK) return r;
        i++;
      }
      continue;
    }

    if (c == '\'') {
      i++; // skip opening
      size_t start = bi;
      size_t wlen = 0;
      while (line[i] != '\0' && line[i] != '\'') {
        if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
        if (wlen + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = line[i++];
        wlen++;
      }
      if (line[i] != '\'') return PARSE_UNSUPPORTED;
      i++; // closing
      if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
      out_buf[bi++] = '\0';
      parse_result_t r = push_tok(out_tokens, out_ntok, TOK_WORD, &out_buf[start]);
      if (r != PARSE_OK) return r;
      continue;
    }

    // word (unquoted)
    size_t start = bi;
    size_t wlen = 0;
    while (line[i] != '\0' && !is_space(line[i]) && !is_op_char(line[i]) && line[i] != '\'') {
      char ch = line[i];
      if (is_unsupported_char(ch)) return PARSE_UNSUPPORTED;
      if (ch == '|' && line[i + 1] == '|') return PARSE_UNSUPPORTED;
      if (ch == '&' && line[i + 1] == '&') return PARSE_UNSUPPORTED;

      if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
      if (wlen + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;

      out_buf[bi++] = ch;
      wlen++;
      i++;
    }

    if (wlen == 0) return PARSE_SYNTAX_ERROR;
    if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
    out_buf[bi++] = '\0';
    parse_result_t r = push_tok(out_tokens, out_ntok, TOK_WORD, &out_buf[start]);
    if (r != PARSE_OK) return r;
  }

  return (*out_ntok == 0) ? PARSE_EMPTY : PARSE_OK;
}
