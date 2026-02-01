#pragma once

#include "ush.h"

/*
 * ush_builtins.h
 *
 * builtin（cd/exit/help）の判定と実行。
 */

int ush_is_builtin(const char *cmd);

/* 戻り値: 更新後の last_status */
int ush_run_builtin(ush_state_t *st, char *argv[]);
