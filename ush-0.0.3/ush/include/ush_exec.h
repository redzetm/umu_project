#pragma once
#include "ush.h"
#include "ush_parse.h"

int ush_exec_ast(ush_state_t *st, const ush_ast_t *ast, int root);
