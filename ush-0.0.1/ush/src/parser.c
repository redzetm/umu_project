#include "ush_parser.h"

#include "ush_utils.h"

#include <string.h>

/*
 * 未対応記号検出（方針B）
 * 仕様上、見つけた時点で "この行は実行しない"。
 */
static int ush_is_unsupported_char(char c)
{
    switch (c) {
    case '\'':
    case '"':
    case '\\':
    case '$':
    case '*':
    case '?':
    case '[':
    case ']':
    case '|':
    case '<':
    case '>':
    case ';':
    case '&':
        return 1;
    default:
        return 0;
    }
}

parse_result_t ush_tokenize_inplace(
    char *line,
    char *argv[USH_MAX_ARGS + 1],
    int *argc_out)
{
    if (argc_out != NULL) {
        *argc_out = 0;
    }
    if (line == NULL || argv == NULL || argc_out == NULL) {
        return PARSE_EMPTY;
    }

    if (ush_is_blank_line(line) || ush_is_comment_line(line)) {
        return PARSE_EMPTY;
    }

    int argc = 0;
    char *p = line;

    while (*p != '\0') {
        /* まず、未対応記号を検出（空白かどうかに関係なく検出する） */
        if (ush_is_unsupported_char(*p)) {
            return PARSE_UNSUPPORTED;
        }

        /* 区切り（スペース/タブ/改行）をスキップ */
        while (*p != '\0' && (ush_is_space_tab(*p) || *p == '\n')) {
            *p = '\0';
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* トークン開始 */
        if (argc >= USH_MAX_ARGS) {
            return PARSE_TOO_MANY_ARGS;
        }
        argv[argc++] = p;

        /* トークン終端まで進める（同時に長さチェック） */
        size_t tok_len = 0;
        while (*p != '\0' && !ush_is_space_tab(*p) && *p != '\n') {
            if (ush_is_unsupported_char(*p)) {
                return PARSE_UNSUPPORTED;
            }

            tok_len++;
            if (tok_len > (size_t)USH_MAX_TOKEN_LEN) {
                return PARSE_TOO_LONG;
            }
            p++;
        }

        /* 次のループで区切りを \0 にするので、ここではそのまま */
    }

    argv[argc] = NULL;
    *argc_out = argc;

    if (argc == 0) {
        return PARSE_EMPTY;
    }

    return PARSE_OK;
}
