#pragma once

/*
 * ush_err.h
 *
 * parser の戻り値定義。
 */

typedef enum {
    PARSE_OK = 0,
    PARSE_EMPTY,         /* 空行/空白のみ/コメントのみ */
    PARSE_TOO_LONG,      /* 行長 or トークン長の超過 */
    PARSE_TOO_MANY_ARGS, /* argv が上限超過 */
    PARSE_UNSUPPORTED,   /* 未対応記号を検出 */
} parse_result_t;
