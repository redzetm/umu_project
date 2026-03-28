# ush-0.0.7-詳細設計書.md
UmuOS User Shell (ush) — 詳細設計書（0.0.7 / UTF-8 対話入力対応）  
Target OS: UmuOS 系（少なくとも 0.1.7-base-stable 系で成立することを狙う）

本書は ush-0.0.7 の参照実装を規定する詳細設計書であり、実装意図、責務分割、必要ファイル、貼り付け可能コード、受け入れ確認までを 1 つにまとめる。

このリポジトリ運用では、本書を 0.0.7 の唯一の正とする。矛盾がある場合は本書を優先する。

- 0.0.7 の主目的は、ash に頼らず、ush 自身の対話コマンドラインで UTF-8 日本語を壊さず扱えるようにすることである。
- 0.0.6 までの制御構文、複数行ブロック、展開、glob、外部コマンド実行の仕様は原則維持する。
- 0.0.7 では加えて、`cp {aaa,bbb,ccc,ddd} /umu_bin/` のような最小限の brace expansion を扱えるようにする。
- 本書は差分メモではなく、0.0.7 単体で読んで実装できる粒度を目指す。
- ビルドは静的ビルドとし、UmuOSには、バイナリで持ち込む。

---

# 0. この文書の読み方（コピペ区分）

- 本書中のコードブロックは、直前のラベルで用途を区分する。
	- 【実装用（貼り付け可）: <貼り付け先パス>】: そのまま貼り付けてビルドできることを想定する。
	- 【参考（擬似コード）】: 実装の流れを掴むための説明用。
	- 【説明】: 背景、目的、設計判断、制約。
- パス表記は ush ディレクトリからの相対パスを貼り付け先として示す。
- 0.0.7 では特に lineedit と UTF-8 補助モジュールが重要である。ここを最優先で読む。

---

# 1. 前提・設計原則

- 対話用シェルであり、POSIX sh / bash 互換の追求は目的にしない。
- 単一静的バイナリ /umu_bin/ush を成果物とする。
- /bin/sh（BusyBox ash）は残すが、0.0.7 の目的は「日本語を含む対話操作を ush 側で成立させる」ことである。
- 日本語対応の目標は「UTF-8 を壊さず入力、移動、削除、表示する」であり、厳密な Unicode 全面対応は目標外とする。
- 文字処理は locale 依存ではなく、UTF-8 のバイト境界と簡易表示幅計算で行う。
- 対話入力では raw mode を用いるが、入力確定後は必ず元の tty 状態へ戻す。
- 未対応構文は検出してエラーにする。黙って誤動作しないことを優先する。
- brace expansion は bash 完全互換を目指さず、日常操作に必要な最小サブセットだけを実装する。

0.0.7 で新たに保証したいこと:

- 日本語を含むコマンドラインを貼り付けや逐次入力で受け付けられる。
- 左右カーソル移動が UTF-8 文字境界を壊さない。
- Backspace / Delete が UTF-8 1 文字単位で動く。
- redraw 時に日本語が混在してもカーソル位置が大きく崩れない。
- tokenize / expand / exec まで日本語を含むトークンがそのまま通る。
- unquoted な `{a,b,c}` を含む語が複数語へ展開される。

0.0.7 でも目標外のもの:

- IME 自体の提供
- 厳密な East Asian Width 判定
- 結合文字列、異体字セレクタ、絵文字クラスタの完全対応
- ジョブ制御、関数、算術式、配列、高度な FD 操作

---

# 2. 実装の前提（1から実装できる粒度）

## 2.1 ソース構成

【説明】

0.0.7 は 0.0.6 の構成を土台にしつつ、UTF-8 補助モジュールを追加する。

```text
ush/
	include/
		ush.h
		ush_limits.h
		ush_err.h
		ush_utils.h
		ush_env.h
		ush_prompt.h
		ush_lineedit.h
		ush_utf8.h
		ush_tokenize.h
		ush_expand.h
		ush_parse.h
		ush_exec.h
		ush_builtins.h
		ush_script.h
	src/
		main.c
		utils.c
		env.c
		prompt.c
		lineedit.c
		utf8.c
		tokenize.c
		expand.c
		parse.c
		exec.c
		script_parse.c
		script_exec.c
		builtins.c
	tests/
		smoke_ush.sh
```

## 2.2 必要ファイル一覧

- include/ush.h
- include/ush_limits.h
- include/ush_err.h
- include/ush_utils.h
- include/ush_env.h
- include/ush_prompt.h
- include/ush_lineedit.h
- include/ush_utf8.h
- include/ush_tokenize.h
- include/ush_expand.h
- include/ush_parse.h
- include/ush_exec.h
- include/ush_builtins.h
- include/ush_script.h
- src/main.c
- src/utils.c
- src/env.c
- src/prompt.c
- src/lineedit.c
- src/utf8.c
- src/tokenize.c
- src/expand.c
- src/parse.c
- src/exec.c
- src/script_parse.c
- src/script_exec.c
- src/builtins.c
- tests/smoke_ush.sh

## 2.3 依存/前提（ホスト側ビルド環境）

- libc は musl を前提とする。
- ビルドは musl-gcc -static を基本とする。
- glob() を使うため glob.h を提供する libc が必要である。

## 2.4 互換性の考え方

0.0.7 は 0.0.6 の次を維持する。

- tokenize の意味論
- script_parse / script_exec の意味論
- expand の位置パラメータとコマンド置換
- exec の外部コマンド実行と /bin/sh フォールバック
- builtins の最小集合

一方で 0.0.7 では lineedit の扱いを byte 中心から UTF-8 文字境界中心へ切り替える。
さらに brace expansion のために tokenize と exec/script_exec へ最小限の拡張を入れる。

---

# 3. 作業領域の作り方（1から作る場合）

【実装用（貼り付け可）】

```sh
cd /home/tama/umu_project/

rm -rf ./ush-0.0.7/ush
mkdir -p ./ush-0.0.7/ush/include ./ush-0.0.7/ush/src ./ush-0.0.7/ush/tests

touch \
	./ush-0.0.7/ush/include/ush.h \
	./ush-0.0.7/ush/include/ush_limits.h \
	./ush-0.0.7/ush/include/ush_err.h \
	./ush-0.0.7/ush/include/ush_utils.h \
	./ush-0.0.7/ush/include/ush_env.h \
	./ush-0.0.7/ush/include/ush_prompt.h \
	./ush-0.0.7/ush/include/ush_lineedit.h \
	./ush-0.0.7/ush/include/ush_utf8.h \
	./ush-0.0.7/ush/include/ush_tokenize.h \
	./ush-0.0.7/ush/include/ush_expand.h \
	./ush-0.0.7/ush/include/ush_parse.h \
	./ush-0.0.7/ush/include/ush_exec.h \
	./ush-0.0.7/ush/include/ush_builtins.h \
	./ush-0.0.7/ush/include/ush_script.h \
	./ush-0.0.7/ush/src/main.c \
	./ush-0.0.7/ush/src/utils.c \
	./ush-0.0.7/ush/src/env.c \
	./ush-0.0.7/ush/src/prompt.c \
	./ush-0.0.7/ush/src/lineedit.c \
	./ush-0.0.7/ush/src/utf8.c \
	./ush-0.0.7/ush/src/tokenize.c \
	./ush-0.0.7/ush/src/expand.c \
	./ush-0.0.7/ush/src/parse.c \
	./ush-0.0.7/ush/src/exec.c \
	./ush-0.0.7/ush/src/script_parse.c \
	./ush-0.0.7/ush/src/script_exec.c \
	./ush-0.0.7/ush/src/builtins.c \
	./ush-0.0.7/ush/tests/smoke_ush.sh
```

---

# 4. ビルド手順（開発ホスト）

【実装用（貼り付け可）】

```sh
cd /home/tama/umu_project/ush-0.0.7/ush

musl-gcc -static -O2 -Wall -Wextra -Wshadow -Wpointer-arith -Wwrite-strings \
	-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	-Iinclude \
	-o ush \
	src/main.c src/utils.c src/env.c src/prompt.c src/lineedit.c src/utf8.c \
	src/tokenize.c src/expand.c src/parse.c src/exec.c \
	src/script_parse.c src/script_exec.c src/builtins.c
```

---

# 5. 0.0.7 の要点

0.0.7 の本質は次の 5 点である。

1. UTF-8 文字境界を理解する補助モジュールを導入する。
2. lineedit のカーソル位置は byte index のまま保持しつつ、移動と削除を UTF-8 1 文字単位にする。
3. redraw のカーソル戻し量を byte 差ではなく表示幅差で計算する。
4. tokenize / expand / exec には極力手を入れず、日本語を含む語をそのまま運べるようにする。
5. unquoted な brace expansion を最小限だけ導入し、`cp {aaa,bbb} /umu_bin/` のような日常操作を ush 側で扱えるようにする。

0.0.6 までの問題:

- ch >= 0x20 && ch <= 0x7e の ASCII 限定挿入
- 左右移動が cursor++ / cursor-- の byte 単位
- Backspace / Delete が 1 byte 削除
- redraw の tail = len - cursor が表示幅を無視

0.0.7 ではこれを解消する。

---

# 6. tokenize / parse / expand / exec の基本方針

## 6.1 tokenize

- 空白と演算子でトークンを切る。
- 非 ASCII バイトは特別扱いせず、空白や演算子でない限り WORD の一部として取り込む。
- つまり日本語を含むファイル名や引数は、そのまま 1 つの WORD に入る。
- 0.0.7 では unquoted WORD の先頭に `{` が来ても即エラーにせず、brace expansion 候補として WORD に取り込めるようにする。
- `${VAR}` のための `{` と brace expansion の `{` は tokenize では区別しない。どちらも WORD に入れ、expand 側または exec 側で解釈する。

## 6.2 parse

- 0.0.6 と同じく、制御構文、パイプ、短絡演算、リダイレクトを扱う。
- 日本語対応のために parse の構文規則自体は変えない。

## 6.3 expand

- $VAR, ${VAR}, $?, $0..$9, $#, ~, $(...) を扱う。
- 文字列処理は NUL 終端バイト列として扱い、UTF-8 の意味解釈までは行わない。
- 日本語を含む展開結果も、そのまま byte 列として次段へ渡す。
- 0.0.7 では unquoted WORD に対して brace expansion を追加する。
- 実行順序は「brace expansion」→「通常の word 展開」→「glob」の順にする。

### 6.3.1 brace expansion の対象と制限

0.0.7 で対応するのは、次の最小サブセットだけである。

- `cp {aaa,bbb,ccc,ddd} /umu_bin/`
- `cp foo{1,2}.txt /tmp/`
- `echo /home/{tama,root}/tmp`

対応方針:

- 対象は unquoted WORD のみ。
- 1 個の WORD から複数の WORD を生成する。
- 1 つの WORD の中で扱う brace pair は 1 組だけに限定する。
- prefix と suffix は許可する。つまり `pre{a,b}post` を許可する。
- 各要素はカンマ区切りとする。
- 空要素は許可しない。

0.0.7 で未対応とするもの:

- nested brace expansion: `a{b,{c,d}}e`
- range 形式: `{1..9}`, `{a..z}`
- quoted brace expansion: `"{a,b}"`, `'{a,b}'`
- 1 語内で複数組の brace pair を直積展開すること

展開ルール:

- quote が QUOTE_NONE のときだけ有効にする。
- `{` と `}` と `,` が top-level で 1 組だけ現れる場合に brace expansion とみなす。
- 成立しない場合は、単なる通常文字列として扱う。
- brace expansion は raw の unquoted WORD に対して先に適用し、その後で各生成語に対して $VAR, ${VAR}, $(...) などの通常展開を行う。
- これにより、変数展開の結果として現れた `{` `}` `,` は brace expansion の再解釈対象にしない。
- brace expansion の結果に glob メタ文字が含まれる場合、その後の glob 展開へ渡す。

実装上の置き場所:

- `ush_expand_word()` 自体は「1 語を 1 語へ」展開する API のまま維持する。
- そのため brace expansion は `exec.c` の argv 展開と、`script_exec.c` の for/in や case パターン展開で、`ush_expand_word()` を呼ぶ前に行う。
- 具体的には、raw WORD を brace expansion で 0 個以上の語へ分け、その各要素に対して通常の word 展開を行い、最後に glob を適用する。

## 6.4 exec

- 外部コマンドは PATH 探索のうえ execve する。
- ENOEXEC のときのみ /bin/sh フォールバックする。
- 日本語を含む argv は、そのまま execve に渡す。
- brace expansion は exec の argv 構築時点で反映済みとする。

---

# 7. UTF-8 補助モジュール（include/ush_utf8.h / src/utf8.c）

【説明】

0.0.7 では lineedit のために、最低限の UTF-8 補助関数を持つ。

- 先頭バイトから UTF-8 文字長を判定する
- 前の文字開始位置へ戻る
- 次の文字開始位置へ進む
- 指定 byte 位置までの表示幅を計算する
- 非 ASCII の表示幅は簡易に 2 とする

このモジュールは locale に依存しない。

## 7.1 UTF-8 ヘッダ

【実装用（貼り付け可）: include/ush_utf8.h】

```c
#pragma once

#include <stddef.h>

int ush_utf8_char_len(unsigned char b);
size_t ush_utf8_prev(const char *s, size_t i);
size_t ush_utf8_next(const char *s, size_t i);
int ush_utf8_width_at(const char *s, size_t i, size_t *out_len);
int ush_utf8_disp_width(const char *s, size_t len);
int ush_utf8_disp_width_range(const char *s, size_t start, size_t end);
```

## 7.2 UTF-8 実装

【実装用（貼り付け可）: src/utf8.c】

```c
#include "ush_utf8.h"

#include <string.h>

int ush_utf8_char_len(unsigned char b) {
	if (b < 0x80) return 1;
	if ((b & 0xE0) == 0xC0) return 2;
	if ((b & 0xF0) == 0xE0) return 3;
	if ((b & 0xF8) == 0xF0) return 4;
	return 1;
}

static int is_cont(unsigned char b) {
	return (b & 0xC0) == 0x80;
}

size_t ush_utf8_prev(const char *s, size_t i) {
	if (s == NULL || i == 0) return 0;
	size_t n = strlen(s);
	if (i > n) i = n;

	size_t j = i - 1;
	int lim = 0;
	while (j > 0 && is_cont((unsigned char)s[j]) && lim < 4) {
		j--;
		lim++;
	}
	return j;
}

size_t ush_utf8_next(const char *s, size_t i) {
	if (s == NULL) return 0;
	size_t n = strlen(s);
	if (i >= n) return n;

	int l = ush_utf8_char_len((unsigned char)s[i]);
	size_t j = i + (size_t)l;
	if (j > n) j = n;
	while (j < n && is_cont((unsigned char)s[j])) j++;
	return j;
}

int ush_utf8_width_at(const char *s, size_t i, size_t *out_len) {
	if (out_len) *out_len = 0;
	if (s == NULL) return 1;
	unsigned char b = (unsigned char)s[i];
	if (b == '\0') return 1;

	int l = ush_utf8_char_len(b);
	if (out_len) *out_len = (size_t)l;

	if (b < 0x80) {
		if (b == '\t') return 4;
		return 1;
	}

	return 2;
}

int ush_utf8_disp_width_range(const char *s, size_t start, size_t end) {
	if (s == NULL) return 0;
	size_t n = strlen(s);
	if (start > n) start = n;
	if (end > n) end = n;
	if (end < start) end = start;

	int col = 0;
	for (size_t i = start; i < end && s[i] != '\0';) {
		size_t bl = 0;
		int w = ush_utf8_width_at(s, i, &bl);
		col += w;
		if (bl == 0) bl = 1;
		i += bl;
	}
	return col;
}

int ush_utf8_disp_width(const char *s, size_t len) {
	if (s == NULL) return 0;
	size_t n = strlen(s);
	if (len > n) len = n;
	return ush_utf8_disp_width_range(s, 0, len);
}
```

---

# 8. lineedit（src/lineedit.c）

【説明】

0.0.7 の中心である。設計要点は次の通り。

- バッファ buf は従来通り NUL 終端 byte 列で持つ。
- cursor も従来通り byte index とする。
- ただし cursor の更新は ush_utf8_prev / ush_utf8_next を使う。
- Backspace は cursor の直前 1 文字を削除する。
- Delete は cursor 位置の 1 文字を削除する。
- redraw は prompt + buf を表示した後、末尾から cursor までの表示幅分だけ左へ戻す。
- 入力受理は ASCII のみではなく、0x20 以上の通常バイトを受け入れる。
- Tab 補完は複雑ケースを避ける方針を維持する。

## 8.1 lineedit の補助関数

- hist_push / hist_set は従来通り。
- current_token_range は byte index のままでよい。
- 補完対象の切り出しも byte 列として扱う。
- ファイル名に日本語が含まれていても、補完候補は byte 列比較で成立する。

## 8.2 redraw の考え方

旧実装:

- tail = len - cursor
- その分だけ ESC [ D で戻す

新実装:

- tail_w = ush_utf8_disp_width_range(buf, cursor, len)
- その表示幅ぶんだけ ESC [ D で戻す

これにより、日本語が混ざってもカーソル戻し量が大きく崩れにくくなる。

## 8.3 文字入力の考え方

- read(2) で 1 byte ずつ受ける方針は維持する。
- 0x20..0x7e だけでなく、0x80 以上も通常入力として受け入れる。
- 制御文字や ESC シーケンスだけは特別扱いする。
- UTF-8 妥当性の厳密検証はこの版では必須にしない。壊れた byte 列も「1 byte 文字」として扱って先へ進める。

## 8.4 lineedit 実装

【実装用（貼り付け可）: src/lineedit.c】

```c
#include "ush_lineedit.h"

#include "ush_env.h"
#include "ush_utf8.h"
#include "ush_utils.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
	int enabled;
	struct termios orig;
} raw_state_t;

static raw_state_t g_raw;
static int g_raw_atexit_registered;

static void restore_raw(void) {
	if (g_raw.enabled) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_raw.orig);
		g_raw.enabled = 0;
	}
}

static int enable_raw(void) {
	if (!isatty(STDIN_FILENO)) return 0;

	struct termios t;
	if (tcgetattr(STDIN_FILENO, &t) != 0) return 1;
	g_raw.orig = t;

	t.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN);
	t.c_iflag &= (tcflag_t)~(IXON | ICRNL);
	t.c_oflag |= (tcflag_t)(OPOST | ONLCR);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) return 1;
	g_raw.enabled = 1;
	if (!g_raw_atexit_registered) {
		atexit(restore_raw);
		g_raw_atexit_registered = 1;
	}
	return 0;
}

static void redraw(const char *prompt, const char *buf, size_t len, size_t cursor) {
	fputs("\r", stdout);
	fputs(prompt, stdout);
	fwrite(buf, 1, len, stdout);
	fputs("\x1b[K", stdout);

	int tail_w = ush_utf8_disp_width_range(buf, cursor, len);
	if (tail_w > 0) {
		fprintf(stdout, "\x1b[%dD", tail_w);
	}
	fflush(stdout);
}

static int hist_push(ush_history_t *hist, const char *line) {
	if (hist == NULL || line == NULL || line[0] == '\0') return 0;
	if (hist->count < USH_HISTORY_MAX) {
		snprintf(hist->items[hist->count], sizeof(hist->items[hist->count]), "%s", line);
		hist->count++;
	} else {
		for (int i = 1; i < USH_HISTORY_MAX; i++) {
			memcpy(hist->items[i - 1], hist->items[i], sizeof(hist->items[i - 1]));
		}
		snprintf(hist->items[USH_HISTORY_MAX - 1], sizeof(hist->items[USH_HISTORY_MAX - 1]), "%s", line);
	}
	hist->cursor = hist->count;
	return 0;
}

static int hist_set(ush_history_t *hist, int idx, char *buf, size_t cap, size_t *io_len, size_t *io_cursor) {
	if (hist == NULL || buf == NULL || cap == 0 || io_len == NULL || io_cursor == NULL) return 1;
	if (idx < 0) idx = 0;
	if (idx > hist->count) idx = hist->count;
	hist->cursor = idx;

	if (idx == hist->count) {
		buf[0] = '\0';
		*io_len = 0;
		*io_cursor = 0;
		return 0;
	}

	snprintf(buf, cap, "%s", hist->items[idx]);
	*io_len = strlen(buf);
	*io_cursor = *io_len;
	return 0;
}

static int is_cmd_char(int c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

static int has_complex_for_completion(const char *buf, size_t len) {
	if (buf == NULL) return 1;
	for (size_t i = 0; i < len; i++) {
		if (buf[i] == '\\' || buf[i] == '\'' || buf[i] == '"') return 1;
	}
	return 0;
}

static void current_token_range(const char *buf, size_t len, size_t cursor, size_t *out_start, size_t *out_end) {
	if (out_start) *out_start = 0;
	if (out_end) *out_end = 0;
	if (buf == NULL) return;
	if (cursor > len) cursor = len;

	size_t start = cursor;
	while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t') {
		start--;
	}

	if (out_start) *out_start = start;
	if (out_end) *out_end = cursor;
}

static int list_dir_matches(const char *dir, const char *prefix, char out[256][USH_MAX_TOKEN_LEN + 1], int *io_n) {
	if (dir == NULL || prefix == NULL || out == NULL || io_n == NULL) return 1;

	DIR *dp = opendir(dir);
	if (dp == NULL) return 1;

	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		const char *name = de->d_name;
		if (name == NULL || name[0] == '\0') continue;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
		if (name[0] == '.' && prefix[0] != '.') continue;
		if (!ush_starts_with(name, prefix)) continue;

		if (*io_n < 256) {
			snprintf(out[*io_n], USH_MAX_TOKEN_LEN + 1, "%s", name);
			(*io_n)++;
		}
	}

	closedir(dp);
	return 0;
}

static void common_prefix(char *io_prefix, size_t cap, char matches[256][USH_MAX_TOKEN_LEN + 1], int n) {
	if (io_prefix == NULL || cap == 0 || n <= 0) return;

	size_t base = strlen(matches[0]);
	for (int i = 1; i < n; i++) {
		size_t k = 0;
		while (k < base && matches[i][k] != '\0' && matches[i][k] == matches[0][k]) k++;
		base = k;
	}

	if (base + 1 > cap) base = cap - 1;
	memcpy(io_prefix, matches[0], base);
	io_prefix[base] = '\0';
}

static int do_tab_complete(char *buf, size_t cap, size_t *io_len, size_t *io_cursor) {
	if (buf == NULL || io_len == NULL || io_cursor == NULL) return 1;
	if (has_complex_for_completion(buf, *io_len)) return 0;

	size_t start = 0;
	size_t end = 0;
	current_token_range(buf, *io_len, *io_cursor, &start, &end);
	if (end < start) return 0;

	char tok[USH_MAX_TOKEN_LEN + 1];
	size_t tlen = end - start;
	if (tlen == 0) return 0;
	if (tlen >= sizeof(tok)) tlen = sizeof(tok) - 1;
	memcpy(tok, buf + start, tlen);
	tok[tlen] = '\0';

	size_t fn = 0;
	while (fn < *io_len && (buf[fn] == ' ' || buf[fn] == '\t')) fn++;
	int is_first = (start == fn);

	char matches[256][USH_MAX_TOKEN_LEN + 1];
	int n = 0;

	if (is_first && strchr(tok, '/') == NULL) {
		int can_cmd_complete = 1;
		for (size_t k = 0; tok[k] != '\0'; k++) {
			if ((unsigned char)tok[k] >= 0x80) {
				can_cmd_complete = 0;
				break;
			}
			if (!is_cmd_char((unsigned char)tok[k])) {
				can_cmd_complete = 0;
				break;
			}
		}

		if (can_cmd_complete) {
			const char *builtins[] = {"cd", "pwd", "export", "test", "[", "exit", "help"};
			for (size_t bi = 0; bi < sizeof(builtins) / sizeof(builtins[0]); bi++) {
				if (ush_starts_with(builtins[bi], tok) && n < 256) {
					snprintf(matches[n], USH_MAX_TOKEN_LEN + 1, "%s", builtins[bi]);
					n++;
				}
			}

			const char *path = ush_get_path_or_default();
			char tmp[4096];
			snprintf(tmp, sizeof(tmp), "%s", path);
			for (char *save = NULL, *p = strtok_r(tmp, ":", &save); p != NULL; p = strtok_r(NULL, ":", &save)) {
				list_dir_matches(p, tok, matches, &n);
				if (n >= 256) break;
			}
		}
	}

	if (n == 0) return 0;
	if (n == 1) {
		const char *m = matches[0];
		size_t mlen = strlen(m);
		if (start + mlen + (*io_len - end) + 1 > cap) return 0;
		memmove(buf + start + mlen, buf + end, (*io_len - end) + 1);
		memcpy(buf + start, m, mlen);
		*io_len = start + mlen + (*io_len - end);
		*io_cursor = start + mlen;
		return 0;
	}

	char cp[USH_MAX_TOKEN_LEN + 1];
	cp[0] = '\0';
	common_prefix(cp, sizeof(cp), matches, n);
	if (cp[0] != '\0' && strlen(cp) > strlen(tok)) {
		size_t cplen = strlen(cp);
		if (start + cplen + (*io_len - end) + 1 > cap) return 0;
		memmove(buf + start + cplen, buf + end, (*io_len - end) + 1);
		memcpy(buf + start, cp, cplen);
		*io_len = start + cplen + (*io_len - end);
		*io_cursor = start + cplen;
		return 0;
	}

	fputc('\n', stdout);
	for (int k = 0; k < n; k++) {
		fputs(matches[k], stdout);
		fputc((k == n - 1) ? '\n' : ' ', stdout);
	}
	return 0;
}

int ush_lineedit_readline(const char *prompt, char *out_line, size_t out_cap, ush_history_t *hist) {
	if (out_line == NULL || out_cap == 0) return 1;
	out_line[0] = '\0';

	if (!isatty(STDIN_FILENO)) {
		if (fgets(out_line, (int)out_cap, stdin) == NULL) return 1;
		size_t n = strlen(out_line);
		while (n > 0 && (out_line[n - 1] == '\n' || out_line[n - 1] == '\r')) out_line[--n] = '\0';
		return 0;
	}

	if (enable_raw() != 0) return 1;

	int rc = 1;
	char buf[USH_MAX_LINE_LEN + 1];
	size_t len = 0;
	size_t cursor = 0;
	char saved[USH_MAX_LINE_LEN + 1];
	int saved_valid = 0;
	buf[0] = '\0';

	if (hist != NULL) hist->cursor = hist->count;
	redraw(prompt, buf, len, cursor);

	for (;;) {
		unsigned char ch;
		ssize_t r = read(STDIN_FILENO, &ch, 1);
		if (r <= 0) {
			rc = 1;
			goto out;
		}

		if (ch == '\r' || ch == '\n') {
			fputc('\n', stdout);
			buf[len] = '\0';
			snprintf(out_line, out_cap, "%s", buf);
			if (hist != NULL && out_line[0] != '\0') hist_push(hist, out_line);
			rc = 0;
			goto out;
		}

		if (ch == 0x04) {
			if (len == 0) {
				fputc('\n', stdout);
				rc = 1;
				goto out;
			}
			continue;
		}

		if (ch == 0x08 || ch == 0x7f) {
			if (cursor > 0) {
				size_t prev = ush_utf8_prev(buf, cursor);
				memmove(buf + prev, buf + cursor, len - cursor + 1);
				len -= (cursor - prev);
				cursor = prev;
				redraw(prompt, buf, len, cursor);
			}
			continue;
		}

		if (ch == '\t') {
			do_tab_complete(buf, sizeof(buf), &len, &cursor);
			redraw(prompt, buf, len, cursor);
			continue;
		}

		if (ch == 0x1b) {
			unsigned char seq[4] = {0};
			if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
			if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

			if (seq[0] == '[') {
				if (seq[1] == 'C') {
					if (cursor < len) {
						cursor = ush_utf8_next(buf, cursor);
						redraw(prompt, buf, len, cursor);
					}
					continue;
				}
				if (seq[1] == 'D') {
					if (cursor > 0) {
						cursor = ush_utf8_prev(buf, cursor);
						redraw(prompt, buf, len, cursor);
					}
					continue;
				}
				if (seq[1] == 'A') {
					if (hist != NULL && hist->count > 0) {
						if (hist->cursor == hist->count && !saved_valid) {
							snprintf(saved, sizeof(saved), "%s", buf);
							saved_valid = 1;
						}
						if (hist->cursor > 0) {
							hist_set(hist, hist->cursor - 1, buf, sizeof(buf), &len, &cursor);
							redraw(prompt, buf, len, cursor);
						}
					}
					continue;
				}
				if (seq[1] == 'B') {
					if (hist != NULL && hist->count > 0) {
						if (hist->cursor < hist->count) {
							hist_set(hist, hist->cursor + 1, buf, sizeof(buf), &len, &cursor);
							if (hist->cursor == hist->count && saved_valid) {
								snprintf(buf, sizeof(buf), "%s", saved);
								len = strlen(buf);
								cursor = len;
							}
							redraw(prompt, buf, len, cursor);
						}
					}
					continue;
				}
				if (seq[1] == '3') {
					if (read(STDIN_FILENO, &seq[2], 1) <= 0) continue;
					if (seq[2] == '~' && cursor < len) {
						size_t next = ush_utf8_next(buf, cursor);
						memmove(buf + cursor, buf + next, len - next + 1);
						len -= (next - cursor);
						redraw(prompt, buf, len, cursor);
					}
					continue;
				}
			}
			continue;
		}

		if (ch >= 0x20 || ch >= 0x80) {
			if (len + 1 >= sizeof(buf)) continue;
			if (len + 1 >= out_cap) continue;

			memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
			buf[cursor] = (char)ch;
			len++;
			cursor++;
			redraw(prompt, buf, len, cursor);
			continue;
		}
	}

out:
	restore_raw();
	return rc;
}
```

---

# 9. tokenize / parse / expand / exec の実装上の注意

【説明】

0.0.7 の日本語対応で重要なのは、これらの層を無理に Unicode 対応にしないことである。

- tokenize は非 ASCII を拒否しない
- tokenize は brace expansion 候補となる `{...}` を WORD として保持する
- parse は token kind だけを見る
- expand は NUL 終端 byte 列として扱う
- exec は argv をそのまま渡す

brace expansion については、次の分担にする。

- tokenize: `{` で始まる unquoted 語を落とさない
- exec/script_exec: raw の unquoted WORD に対して最初に brace expansion を適用し、必要なら複数語へ増やす
- expand: brace expansion 後の各語に対して変数展開やコマンド置換で 1 語を完成させる
- glob: 通常展開後の各語に対して個別に適用する

この順序にしておくと、`${VAR}` 自体は従来通り使え、さらに変数値の中に `{A,B}` のような文字列が入っていても、それを brace expansion として誤展開しない。

つまり、日本語対応のために必要なのは「対話入力で壊さない」ことであり、「内部表現を wide char 化する」ことではない。

---

# 10. 受け入れテスト（tests/smoke_ush.sh）

【説明】

0.0.7 では従来のスモークに加えて、最低でも次を追加確認する。

- 日本語を含む非対話スクリプトが実行できる
- 日本語を含む引数が外部コマンドへ渡る
- 日本語を含むファイル名へリダイレクトできる
- brace expansion で 1 語が複数語へ展開される
- ${VAR} と brace expansion が同居しても、変数値由来の波括弧を誤って brace expansion しない

対話の左右移動や Backspace そのものは自動化が難しいので、まずは byte 列が壊れないことをバッチ実行で担保し、そのうえで手動確認項目を設ける。

【実装用（貼り付け可）: tests/smoke_ush.sh】

```sh
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-/tmp/ush_smoke_ush_bin}"

build_ush() {
	local out="$1"
	musl-gcc -std=c11 -O2 -g -static \
		-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
		-I"$ROOT_DIR/include" \
		"$ROOT_DIR"/src/*.c \
		-o "$out"
}

assert_eq() {
	local name="$1"
	local got="$2"
	local exp="$3"
	if [[ "$got" != "$exp" ]]; then
		echo "[FAIL] $name" >&2
		echo "  expected: [$exp]" >&2
		echo "  got:      [$got]" >&2
		exit 1
	fi
	echo "[OK]   $name"
}

echo "[INFO] build: $BIN"
build_ush "$BIN"

out="$($BIN --version | tr -d '\r')"
assert_eq version "$out" "ush-0.0.7"

JP1=/tmp/ush_jp_echo.ush
cat >"$JP1" <<'EOF'
printf '%s\n' '日本語'
EOF
out="$($BIN "$JP1" | tr -d '\r')"
assert_eq japanese-literal "$out" "日本語"

JP2=/tmp/ush_jp_arg.ush
cat >"$JP2" <<'EOF'
printf '%s\n' 日本語abc
EOF
out="$($BIN "$JP2" | tr -d '\r')"
assert_eq japanese-unquoted-word "$out" "日本語abc"

JPFILE=/tmp/日本語_ush_test.txt
rm -f "$JPFILE"
JP3=/tmp/ush_jp_redir.ush
cat >"$JP3" <<EOF
printf '%s\n' '中身' > "$JPFILE"
cat < "$JPFILE"
EOF
out="$($BIN "$JP3" | tr -d '\r')"
assert_eq japanese-redirection "$out" "中身"

BR1=/tmp/ush_brace_1.ush
cat >"$BR1" <<'EOF'
printf '%s\n' {aaa,bbb,ccc,ddd}
EOF
out="$($BIN "$BR1" | tr -d '\r')"
assert_eq brace-basic "$out" $'aaa\nbbb\nccc\nddd'

BR2=/tmp/ush_brace_2.ush
cat >"$BR2" <<'EOF'
printf '%s\n' pre{A,B}post
EOF
out="$($BIN "$BR2" | tr -d '\r')"
assert_eq brace-prefix-suffix "$out" $'preApost\npreBpost'

BR3=/tmp/ush_brace_3.ush
cat >"$BR3" <<'EOF'
X='{A,B}'
printf '%s\n' "${X}"
EOF
out="$($BIN "$BR3" | tr -d '\r')"
assert_eq brace-var-literal-preserved "$out" '{A,B}'

BR4=/tmp/ush_brace_4.ush
cat >"$BR4" <<'EOF'
X=Z
printf '%s\n' pre{A,${X}}post
EOF
out="$($BIN "$BR4" | tr -d '\r')"
assert_eq brace-before-var "$out" $'preApost\npreZpost'

echo "ALL OK"
```

## 10.1 手動確認項目

手動では次を確認する。

1. ush を起動する
2. 日本語を貼り付け入力する
3. 左右キーで 1 文字ずつ動くことを確認する
4. Backspace で日本語 1 文字が壊れず消えることを確認する
5. Delete でカーソル位置の日本語 1 文字が壊れず消えることを確認する
6. printf '%s\n' 日本語 と打って期待通り表示されることを確認する

---

# 11. 仕様と設計の整合チェック

- 0.0.7 は ash ではなく ush 自身の対話コマンドラインで日本語を扱うことを目的にしている
- そのため、中心改修は lineedit と UTF-8 補助モジュールである
- tokenize / parse / expand / exec の意味論は大きく崩さないが、brace expansion のために tokenize と exec/script_exec に最小限の拡張を入れる
- 受け入れ条件は「日本語を壊さない」であり、「完全な Unicode 編集器にする」ではない
- brace expansion は bash 完全互換ではなく、日常操作向けの最小サブセットでよい

---

# 12. 1から実装するための共通ファイル（貼り付け可）

この章は、0.0.7 のうち変更が小さい共通ファイルをまとめる。

## 12.1 制限値（include/ush_limits.h）

【実装用（貼り付け可）: include/ush_limits.h】

```c
#pragma once

enum {
	USH_MAX_LINE_LEN  = 8192,
	USH_MAX_ARGS      = 128,
	USH_MAX_TOKEN_LEN = 1024,
	USH_MAX_CMDS      = 2,
	USH_MAX_PIPES     = 64,
	USH_MAX_TOKENS    = 256,
	USH_HISTORY_MAX   = 32,
};
```

## 12.2 エラー/戻り値（include/ush_err.h）

【実装用（貼り付け可）: include/ush_err.h】

```c
#pragma once

typedef enum {
	PARSE_OK = 0,
	PARSE_EMPTY,
	PARSE_INCOMPLETE,
	PARSE_TOO_LONG,
	PARSE_TOO_MANY_TOKENS,
	PARSE_TOO_MANY_ARGS,
	PARSE_UNSUPPORTED,
	PARSE_SYNTAX_ERROR,
} parse_result_t;
```

## 12.3 グローバル状態（include/ush.h）

【実装用（貼り付け可）: include/ush.h】

```c
#pragma once

#define USH_VERSION "ush-0.0.7"

typedef struct {
	int last_status;
	const char *script_path;
	int pos_argc;
	char **pos_argv;
} ush_state_t;
```

## 12.4 lineedit ヘッダ（include/ush_lineedit.h）

【実装用（貼り付け可）: include/ush_lineedit.h】

```c
#pragma once

#include <stddef.h>
#include "ush_limits.h"

typedef struct {
	char items[USH_HISTORY_MAX][USH_MAX_LINE_LEN + 1];
	int count;
	int cursor;
} ush_history_t;

int ush_lineedit_readline(
	const char *prompt,
	char *out_line,
	size_t out_cap,
	ush_history_t *hist
);
```

---

# 13. 実装のまとめ

0.0.7 の実装判断を短くまとめると、次の通りである。

- 日本語対応は ash ではなく ush でやる
- 文字列の内部表現は byte 列のまま維持する
- lineedit だけ UTF-8 文字境界を理解する
- redraw は表示幅を考慮する
- tokenize / parse / expand / exec は必要最小限の変更に留める
- brace expansion は unquoted な最小サブセットに限定して導入する

この方針であれば、UmuOS の最小シェルとしての軽さを保ちながら、実用上の日本語コマンドライン入力を成立させやすい。
