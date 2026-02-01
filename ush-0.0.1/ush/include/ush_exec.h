#pragma once

#include "ush.h"

/*
 * ush_exec.h
 *
 * 外部コマンドの実行。
 * - builtin ではない場合に呼び出す。
 * - fork/execve/waitpid を行い、st->last_status を更新して返す。
 */

int ush_exec_external(ush_state_t *st, char *argv[]);
