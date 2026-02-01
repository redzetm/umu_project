#pragma once

#include "ush_err.h"
#include "ush_limits.h"

/*
 * ush_parser.h
 *
 * 入力行を「空白区切り」で argv に変換する。
 * メモリ節約のため、line バッファを破壊的に加工して \0 を挿入する。
 */

parse_result_t ush_tokenize_inplace(
    char *line,                 /* 入力（破壊的に\0挿入） */
    char *argv[USH_MAX_ARGS+1], /* 出力 argv（NULL終端） */
    int *argc_out               /* 出力 argc */
);
