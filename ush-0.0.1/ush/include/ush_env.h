#pragma once

/*
 * ush_env.h
 *
 * 環境変数関連。
 */

/* getenv("PATH") が NULL の場合は "/bin:/sbin" を返す。 */
const char *ush_get_path_or_default(void);
