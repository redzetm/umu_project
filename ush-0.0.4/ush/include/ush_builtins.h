#pragma once
#include "ush.h"

int ush_is_builtin(const char *cmd);
int ush_run_builtin(ush_state_t *st, char *argv[]);
