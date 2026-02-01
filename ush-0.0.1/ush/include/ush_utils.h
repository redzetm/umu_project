#pragma once

#include <stddef.h>

/*
 * ush_utils.h
 *
 * 雑多な小物関数。
 */

/* stderr に printf 形式で出力する（内部で vfprintf を使う）。 */
void ush_eprintf(const char *fmt, ...);

/*
 * 1行入力を取得する。
 * 戻り値: 0=成功, 1=EOF
 *
 * 注意: getline() 由来のため、line_buf/cap は呼び出し側で保持して再利用する。
 */
int ush_read_line(char **line_buf, size_t *cap);

/*
 * 典型的なエラー出力の形をそろえる。
 * 例: ush: cd: <strerror>
 */
void ush_perrorf(const char *context);

/* スペース/タブ/改行だけの行なら真。 */
int ush_is_blank_line(const char *line);

/* 最初の非空白文字が # なら真（コメント行）。 */
int ush_is_comment_line(const char *line);

/* 文字がスペース/タブのどちらか。 */
int ush_is_space_tab(char c);
