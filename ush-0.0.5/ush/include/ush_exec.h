#pragma once
#include <stddef.h>

#include "ush.h"
#include "ush_parse.h"

int ush_exec_ast(ush_state_t *st, const ush_ast_t *ast, int root);

// $(...) 用: cmdline を ush の 1行として子プロセスで評価し、stdout を out に格納する。
// out は NUL 終端される。
// - \r/\n はスペースに正規化し、末尾空白は削除してよい。
// - out_cap 超過は PARSE_TOO_LONG。
// - base_state は位置パラメータ等の参照に用いる（NULL可）。
parse_result_t ush_exec_capture_stdout(
	const ush_state_t *base_state,
	const char *cmdline,
	char *out,
	size_t out_cap
);
