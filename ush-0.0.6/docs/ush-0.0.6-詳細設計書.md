# ush-0.0.6-詳細設計書.md
UmuOS User Shell (ush) — 詳細設計書（0.0.6 / 拡張）  
Target OS: UmuOS-0.1.7-base-stable（想定）

本書は ush-0.0.6 の参照実装を規定する詳細設計書であり、実装手順・関数分割・データ構造・貼り付け可能コードを提示する。

このリポジトリ運用では **本書を唯一の正** とし、矛盾がある場合は本書を優先する。
- 仕様の要約: [ush-0.0.6-仕様書.md](ush-0.0.6-仕様書.md)
- 基本設計の要約: [ush-0.0.6-基本設計書.md](ush-0.0.6-基本設計書.md)

注意（このリポジトリ運用）:
- 本書は「設計書」であり、実装手順・関数分割・データ構造・貼り付け可能コードを規定する。
- 現在のリポジトリには、本書の内容に基づく参照実装として `ush-0.0.6/ush` が存在する。
- ただし「誰がやっても同一成果物になる」ことを満たすため、貼り付け可能コードを併記する。
- **コピペ バージョンにしているが、構造理解をしないと意味がないので、できるだけコードを参照して理解を深めることをお勧めします。**

---

# 0. この文書の読み方（コピペ区分）
- 本書中のコードブロックは、直前のラベルで用途を区分する。
  - 【実装用（貼り付け可）: <貼り付け先パス>】: そのまま貼り付けてビルドできることを想定する。
  - 【参考（擬似コード）】: 実装の流れ説明用。
  - 【説明】: 背景・意図・補足。
- パス表記は「`ush/` ディレクトリからの相対パス」を貼り付け先として示す。

---

# 1. 前提・設計原則
- 対話用シェル（POSIX/b*sh 互換は追わない）
- `/bin/sh`（BusyBox ash）は残し、`execve()` が `ENOEXEC` のときのみ `/bin/sh` にフォールバック
- 静的リンク（musl）前提で、単一バイナリ `/umu_bin/ush` を成果物とする
- 未対応構文は「検出してエラー」で誤動作を避ける
- リダイレクト `open()` 失敗時は **行を丸ごと不実行（1つもforkしない）**

---

# 2. 実装の前提（1から実装できる粒度）

本書は、0.0.4 が手元に無い状態でも **1から実装できる** ように、必要な `include/` と `src/` の全ファイルについて「貼り付け可能コード」を提示する。

注意:
- 0.0.4 のソースを流用することは可能だが、必須ではない（時短オプション）。

## 2.1 ソース構成（0.0.5踏襲）

【説明】

0.0.6 は 0.0.5 と同じソース構成（`ush/include` と `ush/src`）を踏襲しつつ、制御構文のためにスクリプト用のモジュール（`ush_script.h` / `script_parse.c` / `script_exec.c`）を追加する。

```
ush/
  include/
    ush.h
    ush_limits.h
    ush_err.h
    ush_utils.h
    ush_env.h
    ush_prompt.h
    ush_lineedit.h
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
    tokenize.c
    expand.c
    parse.c
    exec.c
    script_parse.c
    script_exec.c
    builtins.c
```

## 2.2 必要ファイル一覧（1から実装する場合）

【説明】

本書の手順通りにコピペしていく場合、最終的に以下のファイルが揃う。

- `include/ush.h`
- `include/ush_limits.h`
- `include/ush_err.h`
- `include/ush_utils.h`
- `include/ush_env.h`
- `include/ush_prompt.h`
- `include/ush_lineedit.h`
- `include/ush_tokenize.h`
- `include/ush_expand.h`
- `include/ush_parse.h`
- `include/ush_exec.h`
- `include/ush_builtins.h`
- `include/ush_script.h`
- `src/main.c`
- `src/utils.c`
- `src/env.c`
- `src/prompt.c`
- `src/lineedit.c`
- `src/tokenize.c`
- `src/expand.c`
- `src/parse.c`
- `src/exec.c`
- `src/script_parse.c`
- `src/script_exec.c`
- `src/builtins.c`

## 2.3 依存/前提（ホスト側ビルド環境）

【説明】

- `src/exec.c` は `glob()` を用いるため、`glob.h` を提供する libc（musl など）が必要。
- ビルドは 0.0.4 と同様に `musl-gcc -static` を想定する。

## 2.4 互換性チェック（0.0.4を流用する場合）

【説明】

0.0.4 のヘッダ群をそのまま流用するため、差し替えファイルは以下の条件を満たすこと。

- `ush_tokenize()` の関数シグネチャは 0.0.4 と同一である。
- `ush_parse_line()` の関数シグネチャは 0.0.4 と同一である。
- `ush_expand_word()` の関数シグネチャは 0.0.4 と同一である。
- `ush_exec_ast()` の関数シグネチャは 0.0.4 と同一である。
- `ush_lineedit_readline()` の関数シグネチャは 0.0.4 と同一である。

ただし 0.0.5/0.0.6 では以下が拡張される。
- `ush_state_t` に位置パラメータ（`$0` `$1..$9` `$#`）の保持を追加する。
- `ush_expand_ctx_t` に位置パラメータと `$(...)` のためのコンテキストを追加する。
- `include/ush_exec.h` に `ush_exec_capture_stdout()` を追加する。

さらに 0.0.6 では以下を追加する。
- `include/ush_err.h` に `PARSE_INCOMPLETE` を追加する（複数行ブロックの継続入力用）。
- `include/ush_tokenize.h` に `TOK_DSEMI`/`TOK_RPAREN` を追加する（case 用）。
- `include/ush_script.h` / `src/script_parse.c` / `src/script_exec.c` を追加する（if/while/for/case を扱う文AST）。
- `src/main.c` は「1行ごと評価」から「複数行ブロック評価（内部的に ';' 連結）」へ変更する。

## 2.5 作業領域の作り方（推奨）

### 2.5.1 1から作る（推奨: 正規手順）

【実装用（貼り付け可）】

```sh
cd /home/tama/umu_project/

rm -rf ./ush-0.0.6/ush
mkdir -p ./ush-0.0.6/ush/include ./ush-0.0.6/ush/src

touch \
  ./ush-0.0.6/ush/include/ush.h \
  ./ush-0.0.6/ush/include/ush_limits.h \
  ./ush-0.0.6/ush/include/ush_err.h \
  ./ush-0.0.6/ush/include/ush_utils.h \
  ./ush-0.0.6/ush/include/ush_env.h \
  ./ush-0.0.6/ush/include/ush_prompt.h \
  ./ush-0.0.6/ush/include/ush_lineedit.h \
  ./ush-0.0.6/ush/include/ush_tokenize.h \
  ./ush-0.0.6/ush/include/ush_expand.h \
  ./ush-0.0.6/ush/include/ush_parse.h \
  ./ush-0.0.6/ush/include/ush_exec.h \
  ./ush-0.0.6/ush/include/ush_builtins.h \
  ./ush-0.0.6/ush/include/ush_script.h \
  ./ush-0.0.6/ush/src/main.c \
  ./ush-0.0.6/ush/src/utils.c \
  ./ush-0.0.6/ush/src/env.c \
  ./ush-0.0.6/ush/src/prompt.c \
  ./ush-0.0.6/ush/src/lineedit.c \
  ./ush-0.0.6/ush/src/tokenize.c \
  ./ush-0.0.6/ush/src/expand.c \
  ./ush-0.0.6/ush/src/parse.c \
  ./ush-0.0.6/ush/src/exec.c \
  ./ush-0.0.6/ush/src/script_parse.c \
  ./ush-0.0.6/ush/src/script_exec.c \
  ./ush-0.0.6/ush/src/builtins.c
```

この後、各ファイルの中身を本書の該当セクションからコピペして埋める。

### 2.6 コピペ順序（ファイル → 該当セクション）

【説明】

貼り付けは順不同でもよいが、迷う場合は上から順に進める。
（VS Code の検索で `【実装用（貼り付け可）: include/ush_...】` を探すと早い）

- `include/ush_limits.h` → 14.1
- `include/ush_err.h` → 14.2
- `include/ush.h` → 14.3
- `include/ush_utils.h` / `src/utils.c` → 14.4
- `include/ush_env.h` / `src/env.c` → 14.5
- `include/ush_prompt.h` / `src/prompt.c` → 14.6
- `include/ush_lineedit.h` → 14.7
- `include/ush_tokenize.h` → 5
- `src/tokenize.c` → 7
- `include/ush_expand.h` → 14.8
- `src/expand.c` → 9
- `include/ush_parse.h` → 6
- `src/parse.c` → 8
- `include/ush_exec.h` → 14.9
- `src/exec.c` → 10
- `include/ush_script.h` → 14.12
- `src/script_parse.c` → 14.13
- `src/script_exec.c` → 14.14
- `include/ush_builtins.h` / `src/builtins.c` → 14.10
- `src/lineedit.c` → 11
- `src/main.c` → 14.11

### 2.5.2 0.0.4からコピーして時短する（任意）

【実装用（貼り付け可）】

```sh
cd /home/tama/umu_project/

# 0.0.5 のソースツリーを 0.0.6 の作業領域へコピーする（例）
# ※コマンドは例示。実際の運用に合わせて読み替える。
cp -a ush-0.0.5/ush ./ush-0.0.6/ush

# 以降、この文書の「貼り付け可」ブロックで指定されるファイルを置換する。
```

設計の狙い:
- 0.0.6 で変わる箇所（tokenize/exec/main/builtins/lineedit とヘッダ + 新規 script_*）だけを差分として固定し、手戻りを減らす。

---

# 3. ビルド手順（開発ホスト）

0.0.5 と同様に `musl-gcc -static` で静的リンクする。

【実装用（貼り付け可）】

```sh
cd /home/tama/umu_project/ush-0.0.6/ush

musl-gcc -static -O2 -Wall -Wextra -Wshadow -Wpointer-arith -Wwrite-strings \
  -Iinclude \
  -o ush \
  src/main.c src/utils.c src/env.c src/prompt.c src/lineedit.c \
  src/tokenize.c src/expand.c src/parse.c src/exec.c \
  src/script_parse.c src/script_exec.c src/builtins.c
```

---

# 4. 差分の一覧（0.0.5 → 0.0.6）

0.0.6 追加仕様（制御構文/複数行ブロック/test/[ ]）により、以下が差分対象となる。

- `include/ush_err.h`
  - `PARSE_INCOMPLETE` を追加
- `include/ush_tokenize.h` / `src/tokenize.c`
  - `TOK_DSEMI`（`;;`）と `TOK_RPAREN`（`)`）を追加（case 用）
  - `bar)` のような入力を `WORD` + `TOK_RPAREN` に分割できるようにする
- `include/ush_script.h`（新規）
  - 文AST（ST_IF/ST_WHILE/ST_FOR/ST_CASE 等）を追加
- `src/script_parse.c`（新規）
  - if/while/for/case の構文解析と `PARSE_INCOMPLETE` を実装
- `src/script_exec.c`（新規）
  - 文ASTの実行（for/case 用の expand/glob/fnmatch を内蔵）
- `src/main.c`
  - 行を `;` 連結して「ブロック」として評価（未完了なら追加入力）
- `src/exec.c`
  - `$(...)` の評価を script_parse/script_exec 経由に切替
- `src/builtins.c`
  - builtin `test` と `[` を追加
  - help 表示を 0.0.6 向けに更新
- `src/lineedit.c`
  - 補完候補に `test` と `[` を追加

---

# 5. トークン定義（include/ush_tokenize.h）

【実装用（貼り付け可）: include/ush_tokenize.h】

```c
#pragma once
#include <stddef.h>
#include "ush_err.h"
#include "ush_limits.h"

typedef enum {
  TOK_WORD = 0,
  TOK_PIPE,
  TOK_AND,
  TOK_OR,
  TOK_SEMI,
  TOK_DSEMI, // ';;' (case item terminator)
  TOK_RPAREN, // ')'
  TOK_REDIR_IN,
  TOK_REDIR_OUT,
  TOK_REDIR_APPEND,
} token_kind_t;

typedef enum {
  QUOTE_NONE = 0,
  QUOTE_SINGLE,
  QUOTE_DOUBLE,
} quote_kind_t;

typedef struct {
  token_kind_t kind;
  quote_kind_t quote; // TOK_WORD のみ
  const char *text;   // TOK_WORD のみ有効（トークン文字列）
} token_t;

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
);
```

---

# 6. AST 定義（include/ush_parse.h）

【実装用（貼り付け可）: include/ush_parse.h】

```c
#pragma once
#include "ush_err.h"
#include "ush_limits.h"
#include "ush_tokenize.h"

typedef struct {
  const char *argv_raw[USH_MAX_ARGS + 1];
  quote_kind_t argv_quote[USH_MAX_ARGS];
  int argc;

  const char *in_path_raw;   // < file
  quote_kind_t in_quote;

  const char *out_path_raw;  // > file, >> file
  quote_kind_t out_quote;
  int out_append;        // 0:>, 1:>>
} ush_cmd_t;

typedef struct {
  ush_cmd_t left;
  int has_right;
  ush_cmd_t right;
} ush_pipeline_t;

typedef enum {
  NODE_PIPELINE = 0,
  NODE_AND,
  NODE_OR,
  NODE_SEQ,
} node_kind_t;

typedef struct ush_node {
  node_kind_t kind;
  int left;   // index (NODE_AND/OR/SEQ)
  int right;  // index (NODE_AND/OR/SEQ)
  ush_pipeline_t pl; // NODE_PIPELINE
} ush_node_t;

typedef struct {
  ush_node_t nodes[USH_MAX_PIPES];
  int n;
} ush_ast_t;

parse_result_t ush_parse_line(
  const token_t *toks,
  int ntok,
  ush_ast_t *out_ast,
  int *out_root
);
```

---

# 7. tokenize（src/tokenize.c）

設計要点:
- `;` を演算子として独立トークン化する（仕様書 6.3/7.2）。
- `case` のために `;;` と `)` も演算子として独立トークン化する（`TOK_DSEMI` / `TOK_RPAREN`）。
- `bar)` のように WORD 末尾に `)` が連結された入力を、`TOK_WORD("bar")` + `TOK_RPAREN` に分割できるようにする。
- グロブ文字（`* ? [ ]`）は未クォート WORD でも許可し、未対応検出にしない（仕様書 12）。
- 最小エスケープを導入し、エスケープされた 1 文字は内部マーカー（制御文字）で保持する（仕様書 10）。
- 未クォート WORD 内で `$(...)` を 1 WORD として保持できるようにする（入れ子は未対応）。

内部マーカー:
- `USH_ESC` を 1（`0x01`）とし、WORD バッファ内で「`USH_ESC` + 実文字」を 1 文字分のリテラルとして扱う。

【実装用（貼り付け可）: src/tokenize.c】

```c
#include "ush_tokenize.h"

#include "ush_utils.h"

#include <string.h>

enum { USH_ESC = 1 };

static void normalize_cmdsub_output_unquoted(char *s) {
  if (s == NULL) return;

  // \r/\n -> space
  for (size_t i = 0; s[i] != '\0'; i++) {
    if (s[i] == '\r' || s[i] == '\n') s[i] = ' ';
  }

  // trim trailing spaces/tabs
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
    s[--n] = '\0';
  }
}

static int is_escape_target_unquoted(char c) {
  return ush_is_space_ch(c) || c == '\\' || c == '\'' || c == '"' || c == '$' ||
         c == ';' || c == '|' || c == '&' || c == '<' || c == '>' ||
         c == '*' || c == '?' || c == '[' || c == ']';
}

static int is_escape_target_dquote(char c) {
  return c == '"' || c == '\\' || c == '$';
}

static int push_tok(token_t out[], int *io_n, token_kind_t k, quote_kind_t q, const char *t) {
  if (*io_n >= USH_MAX_TOKENS) return 1;
  out[*io_n].kind = k;
  out[*io_n].quote = q;
  out[*io_n].text = t;
  (*io_n)++;
  return 0;
}

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
) {
  if (out_ntok) *out_ntok = 0;
  if (line == NULL) return PARSE_EMPTY;

  size_t len = strlen(line);
  if (len > USH_MAX_LINE_LEN) return PARSE_TOO_LONG;

  int ntok = 0;
  size_t bi = 0;

  size_t i = 0;
  while (i < len) {
    // skip spaces
    while (i < len && ush_is_space_ch(line[i])) i++;
    if (i >= len) break;

    // token-start comment
    if (line[i] == '#') {
      break;
    }

    // operators
    if (line[i] == '&') {
      if (i + 1 < len && line[i + 1] == '&') {
        if (push_tok(out_tokens, &ntok, TOK_AND, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      return PARSE_UNSUPPORTED;
    }

    if (line[i] == '|') {
      if (i + 1 < len && line[i + 1] == '|') {
        if (push_tok(out_tokens, &ntok, TOK_OR, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      if (push_tok(out_tokens, &ntok, TOK_PIPE, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    if (line[i] == ';') {
      if (i + 1 < len && line[i + 1] == ';') {
        if (push_tok(out_tokens, &ntok, TOK_DSEMI, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      if (push_tok(out_tokens, &ntok, TOK_SEMI, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    if (line[i] == '<') {
      // ヒアドキュメント（<< / <<<）は未対応
      if (i + 1 < len && line[i + 1] == '<') {
        return PARSE_UNSUPPORTED;
      }
      if (push_tok(out_tokens, &ntok, TOK_REDIR_IN, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    if (line[i] == '>') {
      if (i + 1 < len && line[i + 1] == '>') {
        if (push_tok(out_tokens, &ntok, TOK_REDIR_APPEND, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      if (push_tok(out_tokens, &ntok, TOK_REDIR_OUT, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    // operators for compound syntax
    if (line[i] == ')') {
      if (push_tok(out_tokens, &ntok, TOK_RPAREN, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    // unsupported single-char tokens (always)
    if (line[i] == '(' || line[i] == '{' || line[i] == '}') {
      return PARSE_UNSUPPORTED;
    }

    // WORD
    quote_kind_t q = QUOTE_NONE;

    if (line[i] == '\'') {
      q = QUOTE_SINGLE;
      i++;
      size_t start = bi;
      while (i < len && line[i] != '\'') {
        if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = line[i++];
      }
      if (i >= len) return PARSE_SYNTAX_ERROR;
      // consume closing '
      i++;

      // token must end here
      if (i < len && !ush_is_space_ch(line[i]) && line[i] != '#' &&
          line[i] != '|' && line[i] != '&' && line[i] != '<' && line[i] != '>' && line[i] != ';' && line[i] != ')') {
        return PARSE_SYNTAX_ERROR;
      }

      out_buf[bi++] = '\0';
      if (push_tok(out_tokens, &ntok, TOK_WORD, q, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;
      continue;
    }

    if (line[i] == '"') {
      q = QUOTE_DOUBLE;
      i++;
      size_t start = bi;
      while (i < len && line[i] != '"') {
        if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;

        if (line[i] == '\\' && i + 1 < len && is_escape_target_dquote(line[i + 1])) {
          if (bi + 2 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
          if ((bi - start) + 2 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
          out_buf[bi++] = (char)USH_ESC;
          out_buf[bi++] = line[i + 1];
          i += 2;
          continue;
        }

        out_buf[bi++] = line[i++];
      }
      if (i >= len) return PARSE_SYNTAX_ERROR;
      i++;

      if (i < len && !ush_is_space_ch(line[i]) && line[i] != '#' &&
          line[i] != '|' && line[i] != '&' && line[i] != '<' && line[i] != '>' && line[i] != ';' && line[i] != ')') {
        return PARSE_SYNTAX_ERROR;
      }

      out_buf[bi++] = '\0';
      if (push_tok(out_tokens, &ntok, TOK_WORD, q, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;
      continue;
    }

    // unquoted word: read until space/operator/comment-start
    size_t start = bi;
    int in_cmdsub = 0;
    quote_kind_t cmdsub_q = QUOTE_NONE;
    while (i < len) {
      char c = line[i];
      if (!in_cmdsub && ush_is_space_ch(c)) break;
      // トークン途中の '#' は文字として扱う（コメント開始は「トークン先頭の #」のみ）

      // stop before operator
      if (!in_cmdsub && (c == '|' || c == '&' || c == '<' || c == '>' || c == ';' || c == ')')) break;

      // start command substitution: $(...)
      if (!in_cmdsub && c == '$' && i + 1 < len && line[i + 1] == '(') {
        if (bi + 2 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 2 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = '$';
        out_buf[bi++] = '(';
        i += 2;
        in_cmdsub = 1;
        cmdsub_q = QUOTE_NONE;
        continue;
      }

      // disallow nesting: $( $(...) )
      if (in_cmdsub && cmdsub_q == QUOTE_NONE && c == '$' && i + 1 < len && line[i + 1] == '(') {
        return PARSE_UNSUPPORTED;
      }

      if (in_cmdsub && cmdsub_q == QUOTE_NONE) {
        if (c == '\'') {
          cmdsub_q = QUOTE_SINGLE;
        } else if (c == '"') {
          cmdsub_q = QUOTE_DOUBLE;
        } else if (c == ')') {
          if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
          if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
          out_buf[bi++] = c;
          i++;
          in_cmdsub = 0;
          continue;
        } else if (c == '(') {
          // bare '(' is unsupported (outside of "$((" nesting which is already rejected)
          return PARSE_UNSUPPORTED;
        }
      } else if (in_cmdsub && cmdsub_q == QUOTE_SINGLE) {
        if (c == '\'') cmdsub_q = QUOTE_NONE;
      } else if (in_cmdsub && cmdsub_q == QUOTE_DOUBLE) {
        if (c == '"') cmdsub_q = QUOTE_NONE;
      }

      if (c == '\\') {
        if (i + 1 >= len) {
          // 行継続は未対応だが、末尾 '\\' は通常文字として取り込む
          if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
          if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
          out_buf[bi++] = c;
          i++;
          continue;
        }
        char n = line[i + 1];
        if (is_escape_target_unquoted(n)) {
          if (bi + 2 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
          if ((bi - start) + 2 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
          out_buf[bi++] = (char)USH_ESC;
          out_buf[bi++] = n;
          i += 2;
          continue;
        }
        // 対象外は '\\' を通常文字として取り込む
        if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = c;
        i++;
        continue;
      }

      if (!in_cmdsub && (c == '\'' || c == '"')) return PARSE_SYNTAX_ERROR;

      // '(' は $(...) 以外は未対応
      // '{' '}' は ${VAR} のため WORD 内では許可する
      if (!in_cmdsub && c == '(') return PARSE_UNSUPPORTED;

      if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
      if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
      out_buf[bi++] = c;
      i++;
    }

    if (in_cmdsub) {
      // 未閉鎖 $(...)
      return PARSE_SYNTAX_ERROR;
    }

    if (bi == start) {
      return PARSE_SYNTAX_ERROR;
    }

    out_buf[bi++] = '\0';
    if (push_tok(out_tokens, &ntok, TOK_WORD, QUOTE_NONE, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;

    // token-start comment: if next is '#', stop
    while (i < len && ush_is_space_ch(line[i])) i++;
    if (i < len && line[i] == '#') break;
  }

  if (out_ntok) *out_ntok = ntok;
  return (ntok == 0) ? PARSE_EMPTY : PARSE_OK;
}
```

---

# 8. parse（src/parse.c）

設計要点:
- `src/parse.c` は「単純文（simple command）」用のパーサとして、パイプライン/リダイレクト/`&&`/`||` を AST（`NODE_PIPELINE`/`NODE_AND`/`NODE_OR`）に構築する。
- 0.0.6 のトップレベル構文（if/while/for/case と文区切り `;`）は `src/script_parse.c` 側で処理し、単純文に落とせる部分のみを `ush_parse_line()` に渡す。
- `;` による文の並びは基本的に script 層で `ST_SEQ` として表現される（従来の `NODE_SEQ` は互換性のために残しているが、0.0.6 の通常経路では現れにくい）。

【実装用（貼り付け可）: src/parse.c】

```c
#include "ush_parse.h"
#include "ush_utils.h"

#include <stddef.h>
#include <string.h>

static void init_cmd(ush_cmd_t *c) {
  memset(c, 0, sizeof(*c));
  c->argc = 0;
  c->argv_raw[0] = NULL;
  c->in_path_raw = NULL;
  c->out_path_raw = NULL;
  c->in_quote = QUOTE_NONE;
  c->out_quote = QUOTE_NONE;
  c->out_append = 0;
}

static parse_result_t add_arg(ush_cmd_t *c, const char *s, quote_kind_t q) {
  if (c->argc >= USH_MAX_ARGS) return PARSE_TOO_MANY_ARGS;
  c->argv_raw[c->argc] = s;
  c->argv_quote[c->argc] = q;
  c->argc++;
  c->argv_raw[c->argc] = NULL;
  return PARSE_OK;
}

static int new_node(ush_ast_t *ast, node_kind_t k) {
  if (ast->n >= USH_MAX_PIPES) return -1;
  int idx = ast->n++;
  memset(&ast->nodes[idx], 0, sizeof(ast->nodes[idx]));
  ast->nodes[idx].kind = k;
  ast->nodes[idx].left = -1;
  ast->nodes[idx].right = -1;
  return idx;
}

static parse_result_t parse_command(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_cmd_t *out_cmd,
  int allow_redirects
) {
  init_cmd(out_cmd);

  int i = *io_i;
  if (i >= ntok) return PARSE_SYNTAX_ERROR;

  // words
  while (i < ntok && toks[i].kind == TOK_WORD) {
    parse_result_t ar = add_arg(out_cmd, toks[i].text, toks[i].quote);
    if (ar != PARSE_OK) return ar;
    i++;
  }

  if (out_cmd->argc == 0) return PARSE_SYNTAX_ERROR;

  if (!allow_redirects) {
    *io_i = i;
    return PARSE_OK;
  }

  // redirects? (must be at end)
  int seen_in = 0;
  int seen_out = 0;

  while (i < ntok) {
    token_kind_t k = toks[i].kind;

    if (k != TOK_REDIR_IN && k != TOK_REDIR_OUT && k != TOK_REDIR_APPEND) break;

    if (i + 1 >= ntok || toks[i + 1].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;

    const char *path = toks[i + 1].text;
    quote_kind_t pq = toks[i + 1].quote;

    if (k == TOK_REDIR_IN) {
      if (seen_in) return PARSE_SYNTAX_ERROR;
      if (seen_out) return PARSE_SYNTAX_ERROR; // < は > より前のみ（仕様簡易文法）
      out_cmd->in_path_raw = path;
      out_cmd->in_quote = pq;
      seen_in = 1;
    } else {
      if (seen_out) return PARSE_SYNTAX_ERROR;
      out_cmd->out_path_raw = path;
      out_cmd->out_quote = pq;
      out_cmd->out_append = (k == TOK_REDIR_APPEND);
      seen_out = 1;
    }

    i += 2;

    // redirects must be last elements of command
    if (i < ntok && toks[i].kind == TOK_WORD) return PARSE_SYNTAX_ERROR;
  }

  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_pipeline(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_pipeline_t *out_pl,
  int allow_redirects
) {
  out_pl->has_right = 0;
  init_cmd(&out_pl->left);
  init_cmd(&out_pl->right);

  parse_result_t r = parse_command(toks, ntok, io_i, &out_pl->left, allow_redirects);
  if (r != PARSE_OK) return r;

  if (*io_i < ntok && toks[*io_i].kind == TOK_PIPE) {
    (*io_i)++;
    out_pl->has_right = 1;

    r = parse_command(toks, ntok, io_i, &out_pl->right, allow_redirects);
    if (r != PARSE_OK) return r;

    // 1段パイプのみ
    if (*io_i < ntok && toks[*io_i].kind == TOK_PIPE) return PARSE_SYNTAX_ERROR;

    // 制約: < は左のみ、> は右のみ
    if (out_pl->right.in_path_raw != NULL) return PARSE_SYNTAX_ERROR;
    if (out_pl->left.out_path_raw != NULL) return PARSE_SYNTAX_ERROR;
  }

  return PARSE_OK;
}

static parse_result_t parse_list(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_ast_t *out_ast,
  int *out_root
) {
  if (out_root == NULL) return PARSE_SYNTAX_ERROR;
  *out_root = -1;

  // 1つ目 pipeline
  ush_pipeline_t pl;
  parse_result_t r = parse_pipeline(toks, ntok, io_i, &pl, 1);
  if (r != PARSE_OK) return r;

  int left_idx = new_node(out_ast, NODE_PIPELINE);
  if (left_idx < 0) return PARSE_TOO_MANY_TOKENS;
  out_ast->nodes[left_idx].pl = pl;

  while (*io_i < ntok) {
    token_kind_t op = toks[*io_i].kind;
    if (op != TOK_AND && op != TOK_OR) break;
    (*io_i)++;

    r = parse_pipeline(toks, ntok, io_i, &pl, 1);
    if (r != PARSE_OK) return r;

    int right_idx = new_node(out_ast, NODE_PIPELINE);
    if (right_idx < 0) return PARSE_TOO_MANY_TOKENS;
    out_ast->nodes[right_idx].pl = pl;

    int parent = new_node(out_ast, (op == TOK_AND) ? NODE_AND : NODE_OR);
    if (parent < 0) return PARSE_TOO_MANY_TOKENS;

    out_ast->nodes[parent].left = left_idx;
    out_ast->nodes[parent].right = right_idx;
    left_idx = parent;
  }

  *out_root = left_idx;
  return PARSE_OK;
}

static parse_result_t parse_seq(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_ast_t *out_ast,
  int *out_root
) {
  if (out_root == NULL) return PARSE_SYNTAX_ERROR;
  *out_root = -1;

  int left_root = -1;
  parse_result_t r = parse_list(toks, ntok, io_i, out_ast, &left_root);
  if (r != PARSE_OK) return r;

  while (*io_i < ntok) {
    if (toks[*io_i].kind != TOK_SEMI) return PARSE_SYNTAX_ERROR;
    (*io_i)++;

    // 空要素は許さない（末尾 ; も不可）
    if (*io_i >= ntok) return PARSE_SYNTAX_ERROR;

    int right_root = -1;
    r = parse_list(toks, ntok, io_i, out_ast, &right_root);
    if (r != PARSE_OK) return r;

    int parent = new_node(out_ast, NODE_SEQ);
    if (parent < 0) return PARSE_TOO_MANY_TOKENS;
    out_ast->nodes[parent].left = left_root;
    out_ast->nodes[parent].right = right_root;
    left_root = parent;
  }

  *out_root = left_root;
  return PARSE_OK;
}

parse_result_t ush_parse_line(
  const token_t *toks,
  int ntok,
  ush_ast_t *out_ast,
  int *out_root
) {
  if (out_ast == NULL || out_root == NULL) return PARSE_SYNTAX_ERROR;
  out_ast->n = 0;
  *out_root = -1;

  if (ntok <= 0) return PARSE_EMPTY;

  int i = 0;

  parse_result_t r = parse_seq(toks, ntok, &i, out_ast, out_root);
  if (r != PARSE_OK) return r;
  if (i != ntok) return PARSE_SYNTAX_ERROR;
  return PARSE_OK;
}
```

---

# 9. expand（src/expand.c）

設計要点:
- `${NAME}` を追加する（仕様書 11）。
- エスケープマーカー（`USH_ESC`）を保持し、`\$` 等で変数展開を抑止できるようにする。
- 位置パラメータ（`$0..$9` / `$#`）とコマンド置換（`$(...)`）を展開できるようにする（入れ子は未対応）。

【実装用（貼り付け可）: src/expand.c】

```c
#include "ush_expand.h"

#include "ush_exec.h"
#include "ush_parse.h"
#include "ush_tokenize.h"

#include "ush_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { USH_ESC = 1 };

static int append_str(char *out, size_t cap, size_t *io_len, const char *s) {
  if (out == NULL || io_len == NULL || s == NULL) return 1;
  size_t sl = strlen(s);
  if (*io_len + sl + 1 > cap) return 1;
  memcpy(out + *io_len, s, sl);
  *io_len += sl;
  out[*io_len] = '\0';
  return 0;
}

static int append_ch(char *out, size_t cap, size_t *io_len, char c) {
  if (out == NULL || io_len == NULL) return 1;
  if (*io_len + 2 > cap) return 1;
  out[*io_len] = c;
  (*io_len)++;
  out[*io_len] = '\0';
  return 0;
}

static parse_result_t expand_var(
  const ush_expand_ctx_t *ctx,
  quote_kind_t outer_quote,
  const char *p,
  size_t *io_i,
  char *out,
  size_t cap,
  size_t *io_len
) {
  // p[*io_i] == '$'
  size_t i = *io_i;
  char n = p[i + 1];
  if (n == '\0') {
    if (append_ch(out, cap, io_len, '$')) return PARSE_TOO_LONG;
    *io_i = i + 1;
    return PARSE_OK;
  }

  if (n == '?') {
    char tmp[32];
    int st = (ctx != NULL) ? ctx->last_status : 0;
    snprintf(tmp, sizeof(tmp), "%d", st);
    if (append_str(out, cap, io_len, tmp)) return PARSE_TOO_LONG;
    *io_i = i + 2;
    return PARSE_OK;
  }

  if (n == '#') {
    char tmp[32];
    int c = (ctx != NULL) ? ctx->pos_argc : 0;
    snprintf(tmp, sizeof(tmp), "%d", c);
    if (append_str(out, cap, io_len, tmp)) return PARSE_TOO_LONG;
    *io_i = i + 2;
    return PARSE_OK;
  }

  if (n == '@' || n == '*') {
    return PARSE_UNSUPPORTED;
  }

  if (n == '{') {
    // ${NAME}
    size_t j = i + 2;
    if (p[j] == '\0') return PARSE_SYNTAX_ERROR;

    // find closing }
    size_t close = j;
    while (p[close] != '\0' && p[close] != '}') close++;
    if (p[close] != '}') return PARSE_SYNTAX_ERROR;

    if (close == j) return PARSE_UNSUPPORTED;

    char name[256];
    size_t ni = 0;
    for (size_t k = j; k < close; k++) {
      if (ni + 1 < sizeof(name)) name[ni++] = p[k];
    }
    name[ni] = '\0';

    if (!ush_is_valid_name(name)) return PARSE_UNSUPPORTED;

    const char *v = getenv(name);
    if (v == NULL) v = "";
    if (append_str(out, cap, io_len, v)) return PARSE_TOO_LONG;

    *io_i = close + 1;
    return PARSE_OK;
  }

  if (n >= '0' && n <= '9') {
    // $0..$9 のみ対応（$10 以降は未対応）
    if (p[i + 2] != '\0' && p[i + 2] >= '0' && p[i + 2] <= '9') {
      return PARSE_UNSUPPORTED;
    }

    int idx = (int)(n - '0');
    if (idx == 0) {
      const char *s = (ctx != NULL && ctx->script_path != NULL) ? ctx->script_path : "ush";
      if (append_str(out, cap, io_len, s)) return PARSE_TOO_LONG;
      *io_i = i + 2;
      return PARSE_OK;
    }

    const char *s = "";
    if (ctx != NULL && ctx->pos_argv != NULL && idx <= ctx->pos_argc) {
      s = ctx->pos_argv[idx - 1];
      if (s == NULL) s = "";
    }

    if (append_str(out, cap, io_len, s)) return PARSE_TOO_LONG;
    *io_i = i + 2;
    return PARSE_OK;
  }

  if (n == '(') {
    // $(cmdline)
    size_t j = i + 2;
    quote_kind_t q = QUOTE_NONE;
    int closed = 0;

    char cmdline[USH_MAX_LINE_LEN + 1];
    size_t clen = 0;
    cmdline[0] = '\0';

    while (p[j] != '\0') {
      if ((unsigned char)p[j] == (unsigned char)USH_ESC) {
        if (p[j + 1] == '\0') return PARSE_SYNTAX_ERROR;
        // 内側のtokenizeが理解できるように、マーカーはバックスラッシュへ戻す
        if (clen + 2 >= sizeof(cmdline)) return PARSE_TOO_LONG;
        cmdline[clen++] = '\\';
        cmdline[clen++] = p[j + 1];
        cmdline[clen] = '\0';
        j += 2;
        continue;
      }

      char c = p[j];

      if (q == QUOTE_NONE) {
        if (c == '\'') q = QUOTE_SINGLE;
        else if (c == '"') q = QUOTE_DOUBLE;

        if (c == ')') {
          closed = 1;
          j++;
          break;
        }

        if (c == '$' && p[j + 1] == '(') {
          // 入れ子は未対応
          return PARSE_UNSUPPORTED;
        }
      } else if (q == QUOTE_SINGLE) {
        if (c == '\'') q = QUOTE_NONE;
      } else if (q == QUOTE_DOUBLE) {
        if (c == '"') q = QUOTE_NONE;
      }

      if (clen + 1 >= sizeof(cmdline)) return PARSE_TOO_LONG;
      cmdline[clen++] = c;
      cmdline[clen] = '\0';
      j++;
    }

    if (!closed) return PARSE_SYNTAX_ERROR;
    if (ush_is_blank_line(cmdline)) return PARSE_SYNTAX_ERROR;

    // まず解析だけして、構文/未対応を確実に検出する
    token_t toks[USH_MAX_TOKENS];
    int ntok = 0;
    char tokbuf[USH_MAX_LINE_LEN + 1];
    parse_result_t tr = ush_tokenize(cmdline, toks, &ntok, tokbuf);
    if (tr != PARSE_OK) return tr;

    ush_ast_t ast;
    int root = -1;
    parse_result_t pr = ush_parse_line(toks, ntok, &ast, &root);
    if (pr != PARSE_OK) return pr;

    // 実行して stdout を捕捉
    ush_state_t base;
    base.last_status = (ctx != NULL) ? ctx->last_status : 0;
    base.script_path = (ctx != NULL && ctx->script_path != NULL) ? ctx->script_path : "ush";
    base.pos_argc = (ctx != NULL) ? ctx->pos_argc : 0;
    base.pos_argv = (ctx != NULL) ? ctx->pos_argv : NULL;

    const ush_state_t *bs = (ctx != NULL && ctx->cmdsub_base != NULL) ? ctx->cmdsub_base : &base;

    char sub[USH_MAX_LINE_LEN + 1];
    parse_result_t er = ush_exec_capture_stdout(bs, cmdline, sub, sizeof(sub));
    if (er != PARSE_OK) return er;

    // - quoted: preserve internal newlines
    // - unquoted: normalize newlines to spaces
    if (outer_quote == QUOTE_NONE) {
      normalize_cmdsub_output_unquoted(sub);
    }

    if (append_str(out, cap, io_len, sub)) return PARSE_TOO_LONG;
    *io_i = j;
    return PARSE_OK;
  }

  if (n == '`') {
    return PARSE_UNSUPPORTED;
  }

  if (isalpha((unsigned char)n) || n == '_') {
    size_t j = i + 1;
    char name[256];
    size_t ni = 0;
    while (p[j] != '\0' && (isalnum((unsigned char)p[j]) || p[j] == '_')) {
      if (ni + 1 < sizeof(name)) name[ni++] = p[j];
      j++;
    }
    name[ni] = '\0';

    const char *v = getenv(name);
    if (v == NULL) v = "";
    if (append_str(out, cap, io_len, v)) return PARSE_TOO_LONG;

    *io_i = j;
    return PARSE_OK;
  }

  // それ以外は '$' を文字として扱う
  if (append_ch(out, cap, io_len, '$')) return PARSE_TOO_LONG;
  *io_i = i + 1;
  return PARSE_OK;
}

parse_result_t ush_expand_word(
  const ush_expand_ctx_t *ctx,
  quote_kind_t quote,
  const char *in,
  char *out,
  size_t out_cap
) {
  if (out == NULL || out_cap == 0) return PARSE_TOO_LONG;
  out[0] = '\0';
  if (in == NULL) return PARSE_OK;

  // tilde expansion（未クォートのみ）
  char tilde_buf[USH_MAX_TOKEN_LEN + 1];
  const char *src = in;

  if (quote == QUOTE_NONE && in[0] == '~') {
    if (in[1] == '\0' || in[1] == '/') {
      const char *home = getenv("HOME");
      if (home == NULL || home[0] == '\0') home = "/";

      size_t ti = 0;
      if (append_str(tilde_buf, sizeof(tilde_buf), &ti, home)) return PARSE_TOO_LONG;

      if (in[1] == '/') {
        const char *rest = in + 1;
        if (ti > 0 && tilde_buf[ti - 1] == '/' && rest[0] == '/') rest++;
        if (append_str(tilde_buf, sizeof(tilde_buf), &ti, rest)) return PARSE_TOO_LONG;
      }

      src = tilde_buf;
    } else {
      // ~user は未対応
      return PARSE_UNSUPPORTED;
    }
  }

  size_t olen = 0;

  // 変数展開（未クォート/ダブルクォート）
  if (quote == QUOTE_SINGLE) {
    if (append_str(out, out_cap, &olen, in)) return PARSE_TOO_LONG;
    return PARSE_OK;
  }

  for (size_t i = 0; src[i] != '\0';) {
    if ((unsigned char)src[i] == (unsigned char)USH_ESC) {
      if (src[i + 1] == '\0') return PARSE_SYNTAX_ERROR;
      if (append_ch(out, out_cap, &olen, (char)USH_ESC)) return PARSE_TOO_LONG;
      if (append_ch(out, out_cap, &olen, src[i + 1])) return PARSE_TOO_LONG;
      i += 2;
      continue;
    }

    if (src[i] == '$') {
      parse_result_t r = expand_var(ctx, quote, src, &i, out, out_cap, &olen);
      if (r != PARSE_OK) return r;
      continue;
    }

    if (src[i] == '`') {
      return PARSE_UNSUPPORTED;
    }

    if (append_ch(out, out_cap, &olen, src[i])) return PARSE_TOO_LONG;
    i++;
  }

  return PARSE_OK;
}
```

---

# 10. exec（src/exec.c）

設計要点（0.0.6）:
- argv 展開後、未クォートのみグロブ展開を実施する（仕様書 12）。
- `[a-z]` 等の範囲指定は未対応として検出し `unsupported syntax`。
- エスケープマーカーを考慮し、エスケープされた `*` 等はメタ文字として扱わない。
- 0.0.6 では「文の並び」や制御構文の評価は `src/script_exec.c` が担当し、`src/exec.c` は単純文 AST の評価（外部コマンド/パイプ/`&&`/`||`/リダイレクト）に集中する。
- `$(...)` の評価用に `ush_exec_capture_stdout()` を提供し、stdout を捕捉する（末尾改行は削除）。未クォートのときのみ改行をスペースに正規化し、`"$(...)"` では内部の改行を保持する。子プロセス側は `ush_parse_script()` / `ush_exec_script()` 経由で評価し、コマンド置換内でも制御構文を解釈できるようにする。

【実装用（貼り付け可）: src/exec.c】

```c
#include "ush_exec.h"

#include "ush_builtins.h"
#include "ush_expand.h"
#include "ush_env.h"
#include "ush_parse.h"
#include "ush_script.h"
#include "ush_tokenize.h"
#include "ush_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

enum { USH_ESC = 1 };

static void set_child_sigint_default(void);

static void trim_cmdsub_output(char *s) {
  if (s == NULL) return;

  // trim trailing newlines (\r/\n)
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[--n] = '\0';
  }
}

parse_result_t ush_exec_capture_stdout(
  const ush_state_t *base_state,
  const char *cmdline,
  char *out,
  size_t out_cap
) {
  if (out == NULL || out_cap == 0) return PARSE_TOO_LONG;
  out[0] = '\0';
  if (cmdline == NULL) return PARSE_OK;

  int fds[2];
  if (pipe(fds) != 0) {
    return PARSE_SYNTAX_ERROR;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return PARSE_SYNTAX_ERROR;
  }

  if (pid == 0) {
    // child
    set_child_sigint_default();

    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);

    ush_state_t st;
    st.last_status = (base_state != NULL) ? base_state->last_status : 0;
    st.script_path = (base_state != NULL && base_state->script_path != NULL) ? base_state->script_path : "ush";
    st.pos_argc = (base_state != NULL) ? base_state->pos_argc : 0;
    st.pos_argv = (base_state != NULL) ? base_state->pos_argv : NULL;

    token_t toks[USH_MAX_TOKENS];
    int ntok = 0;
    char tokbuf[USH_MAX_LINE_LEN + 1];
    parse_result_t tr = ush_tokenize(cmdline, toks, &ntok, tokbuf);
    if (tr != PARSE_OK) _exit(2);

    ush_script_t sc;
    int root = -1;
    parse_result_t pr = ush_parse_script(toks, ntok, &sc, &root);
    if (pr != PARSE_OK) _exit(2);

    ush_exec_script(&st, toks, ntok, &sc, root);
    _exit(st.last_status & 255);
  }

  // parent
  close(fds[1]);
  size_t wi = 0;
  while (1) {
    if (wi + 1 >= out_cap) {
      close(fds[0]);
      // 子の回収だけはしておく
      int st = 0;
      waitpid(pid, &st, 0);
      return PARSE_TOO_LONG;
    }

    ssize_t r = read(fds[0], out + wi, out_cap - wi - 1);
    if (r < 0) {
      close(fds[0]);
      int st = 0;
      waitpid(pid, &st, 0);
      return PARSE_SYNTAX_ERROR;
    }
    if (r == 0) break;
    wi += (size_t)r;
  }
  out[wi] = '\0';
  close(fds[0]);

  int st = 0;
  waitpid(pid, &st, 0);

  trim_cmdsub_output(out);
  return PARSE_OK;
}

static void unmark_inplace(char *s) {
  if (s == NULL) return;
  size_t ri = 0;
  size_t wi = 0;
  while (s[ri] != '\0') {
    if ((unsigned char)s[ri] == (unsigned char)USH_ESC) {
      if (s[ri + 1] == '\0') break;
      s[wi++] = s[ri + 1];
      ri += 2;
      continue;
    }
    s[wi++] = s[ri++];
  }
  s[wi] = '\0';
}

static int marked_to_glob_pattern(const char *in, char *out, size_t cap) {
  if (out == NULL || cap == 0) return 1;
  out[0] = '\0';
  if (in == NULL) return 0;

  size_t wi = 0;
  for (size_t ri = 0; in[ri] != '\0';) {
    if ((unsigned char)in[ri] == (unsigned char)USH_ESC) {
      if (in[ri + 1] == '\0') return 1;
      if (wi + 3 > cap) return 1;
      out[wi++] = '\\';
      out[wi++] = in[ri + 1];
      out[wi] = '\0';
      ri += 2;
      continue;
    }
    if (wi + 2 > cap) return 1;
    out[wi++] = in[ri++];
    out[wi] = '\0';
  }
  return 0;
}

static int has_glob_meta_unescaped_marked(const char *s) {
  if (s == NULL) return 0;
  for (size_t i = 0; s[i] != '\0';) {
    if ((unsigned char)s[i] == (unsigned char)USH_ESC) {
      if (s[i + 1] == '\0') return 0;
      i += 2;
      continue;
    }
    if (s[i] == '*' || s[i] == '?') return 1;
    if (s[i] == '[') {
      // treat as meta only if there's a closing ']' later
      for (size_t j = i + 1; s[j] != '\0';) {
        if ((unsigned char)s[j] == (unsigned char)USH_ESC) {
          if (s[j + 1] == '\0') break;
          j += 2;
          continue;
        }
        if (s[j] == ']') return 1;
        j++;
      }
    }
    i++;
  }
  return 0;
}

static int has_unsupported_bracket_range(const char *pattern) {
  if (pattern == NULL) return 0;
  for (size_t i = 0; pattern[i] != '\0'; i++) {
    if (pattern[i] == '\\') {
      if (pattern[i + 1] != '\0') i++;
      continue;
    }
    if (pattern[i] != '[') continue;

    size_t j = i + 1;
    int first = 1;
    while (pattern[j] != '\0' && pattern[j] != ']') {
      if (pattern[j] == '\\') {
        if (pattern[j + 1] != '\0') {
          j += 2;
          first = 0;
          continue;
        }
        break;
      }
      if (pattern[j] == '-' && !first && pattern[j + 1] != '\0' && pattern[j + 1] != ']') {
        return 1; // [a-z] 形式
      }
      first = 0;
      j++;
    }
    if (pattern[j] == ']') {
      i = j;
    }
  }
  return 0;
}

static void set_child_sigint_default(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
}

static int open_in(const char *path) {
  if (path == NULL) return -1;
  return open(path, O_RDONLY);
}

static int open_out(const char *path, int append) {
  if (path == NULL) return -1;
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  return open(path, flags, 0644);
}

static int resolve_cmd(const char *cmd, char out[1024], int *out_fail_status) {
  if (out_fail_status) *out_fail_status = 127;
  if (cmd == NULL || cmd[0] == '\0') {
    if (out_fail_status) *out_fail_status = 127;
    return 1;
  }

  if (strchr(cmd, '/') != NULL) {
    snprintf(out, 1024, "%s", cmd);
    if (access(out, X_OK) == 0) return 0;
    if (out_fail_status) *out_fail_status = (errno == EACCES) ? 126 : 127;
    return 1;
  }

  const char *path = ush_get_path_or_default();
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", path);

  int saw_eacces = 0;

  for (char *save = NULL, *p = strtok_r(tmp, ":", &save); p != NULL; p = strtok_r(NULL, ":", &save)) {
    if (p[0] == '\0') continue;
    char cand[1024];
    snprintf(cand, sizeof(cand), "%s/%s", p, cmd);

    if (access(cand, X_OK) == 0) {
      snprintf(out, 1024, "%s", cand);
      return 0;
    }

    if (errno == EACCES) saw_eacces = 1;
  }

  if (out_fail_status) *out_fail_status = saw_eacces ? 126 : 127;
  return 1;
}

static void exec_with_sh_fallback(char *path, char *argv[]) {
  execve(path, argv, environ);
  if (errno == ENOEXEC) {
    int argc = 0;
    while (argv[argc] != NULL) argc++;

    char **nargv = (char **)calloc((size_t)argc + 2, sizeof(char *));
    if (nargv == NULL) _exit(126);

    static char sh0[] = "/bin/sh";
    nargv[0] = sh0;
    nargv[1] = path;
    for (int i = 1; i < argc; i++) nargv[i + 1] = argv[i];
    nargv[argc + 1] = NULL;

    execve("/bin/sh", nargv, environ);
  }
  _exit(126);
}

static int exec_external_cmd(char *argv[], int in_fd, int out_fd) {
  char path[1024];
  int fail = 127;
  if (resolve_cmd(argv[0], path, &fail) != 0) {
    return fail;
  }

  pid_t pid = fork();
  if (pid < 0) {
    ush_perrorf("fork");
    return 1;
  }

  if (pid == 0) {
    set_child_sigint_default();

    if (in_fd >= 0) {
      dup2(in_fd, STDIN_FILENO);
    }
    if (out_fd >= 0) {
      dup2(out_fd, STDOUT_FILENO);
    }

    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    exec_with_sh_fallback(path, argv);
    _exit(126);
  }

  int st = 0;
  if (waitpid(pid, &st, 0) < 0) {
    ush_perrorf("waitpid");
    return 1;
  }

  if (WIFEXITED(st)) return WEXITSTATUS(st);
  if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
  return 1;
}

static int expand_argv(
  const ush_state_t *st,
  const ush_cmd_t *cmd,
  char out_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  char *out_argv[USH_MAX_ARGS + 1]
) {
  ush_expand_ctx_t xctx;
  xctx.last_status = (st != NULL) ? st->last_status : 0;
  xctx.script_path = (st != NULL && st->script_path != NULL) ? st->script_path : "ush";
  xctx.pos_argc = (st != NULL) ? st->pos_argc : 0;
  xctx.pos_argv = (st != NULL) ? st->pos_argv : NULL;
  xctx.cmdsub_base = st;

  int outc = 0;

  for (int i = 0; i < cmd->argc; i++) {
    if (outc >= USH_MAX_ARGS) {
      ush_eprintf("syntax error");
      return 2;
    }

    parse_result_t r = ush_expand_word(&xctx, cmd->argv_quote[i], cmd->argv_raw[i], out_words[outc], sizeof(out_words[outc]));
    if (r == PARSE_UNSUPPORTED) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    if (r != PARSE_OK) {
      ush_eprintf("syntax error");
      return 2;
    }

    if (cmd->argv_quote[i] == QUOTE_NONE && has_glob_meta_unescaped_marked(out_words[outc])) {
      char pattern[USH_MAX_TOKEN_LEN + 1];
      if (marked_to_glob_pattern(out_words[outc], pattern, sizeof(pattern)) != 0) {
        ush_eprintf("syntax error");
        return 2;
      }
      if (has_unsupported_bracket_range(pattern)) {
        ush_eprintf("unsupported syntax");
        return 2;
      }

      glob_t g;
      memset(&g, 0, sizeof(g));
      int gr = glob(pattern, GLOB_NOSORT, NULL, &g);
      if (gr == 0) {
        for (size_t k = 0; k < g.gl_pathc; k++) {
          if (outc >= USH_MAX_ARGS) {
            globfree(&g);
            ush_eprintf("syntax error");
            return 2;
          }
          snprintf(out_words[outc], USH_MAX_TOKEN_LEN + 1, "%s", g.gl_pathv[k]);
          out_argv[outc] = out_words[outc];
          outc++;
        }
        globfree(&g);
        continue;
      }
      if (gr == GLOB_NOMATCH) {
        // 0件: そのまま（マーカーだけ外す）
        unmark_inplace(out_words[outc]);
        out_argv[outc] = out_words[outc];
        outc++;
        globfree(&g);
        continue;
      }

      globfree(&g);
      ush_eprintf("syntax error");
      return 2;
    }

    unmark_inplace(out_words[outc]);
    out_argv[outc] = out_words[outc];
    outc++;
  }

  out_argv[outc] = NULL;

  if (outc >= 1 && ush_is_assignment_word0(out_argv[0])) {
    ush_eprintf("unsupported syntax");
    return 2;
  }

  return 0;
}

static int expand_redir_path(
  const ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out[USH_MAX_TOKEN_LEN + 1]
) {
  if (raw == NULL) {
    out[0] = '\0';
    return 0;
  }

  ush_expand_ctx_t xctx;
  xctx.last_status = (st != NULL) ? st->last_status : 0;
  xctx.script_path = (st != NULL && st->script_path != NULL) ? st->script_path : "ush";
  xctx.pos_argc = (st != NULL) ? st->pos_argc : 0;
  xctx.pos_argv = (st != NULL) ? st->pos_argv : NULL;
  xctx.cmdsub_base = st;

  char tmp[USH_MAX_TOKEN_LEN + 1];
  parse_result_t r = ush_expand_word(&xctx, q, raw, tmp, sizeof(tmp));
  if (r == PARSE_UNSUPPORTED) {
    ush_eprintf("unsupported syntax");
    return 2;
  }
  if (r != PARSE_OK) {
    ush_eprintf("syntax error");
    return 2;
  }

  if (q == QUOTE_NONE && has_glob_meta_unescaped_marked(tmp)) {
    char pattern[USH_MAX_TOKEN_LEN + 1];
    if (marked_to_glob_pattern(tmp, pattern, sizeof(pattern)) != 0) {
      ush_eprintf("syntax error");
      return 2;
    }
    if (has_unsupported_bracket_range(pattern)) {
      ush_eprintf("unsupported syntax");
      return 2;
    }

    glob_t g;
    memset(&g, 0, sizeof(g));
    int gr = glob(pattern, GLOB_NOSORT, NULL, &g);
    if (gr == 0) {
      if (g.gl_pathc != 1) {
        globfree(&g);
        ush_eprintf("syntax error");
        return 2;
      }
      snprintf(out, USH_MAX_TOKEN_LEN + 1, "%s", g.gl_pathv[0]);
      globfree(&g);
      return 0;
    }
    if (gr == GLOB_NOMATCH) {
      unmark_inplace(tmp);
      snprintf(out, USH_MAX_TOKEN_LEN + 1, "%s", tmp);
      globfree(&g);
      return 0;
    }

    globfree(&g);
    ush_eprintf("syntax error");
    return 2;
  }

  unmark_inplace(tmp);
  snprintf(out, USH_MAX_TOKEN_LEN + 1, "%s", tmp);
  return 0;
}

static int exec_command(ush_state_t *st, const ush_cmd_t *cmd) {
  char words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *argv[USH_MAX_ARGS + 1];

  int er = expand_argv(st, cmd, words, argv);
  if (er != 0) return er;
  if (argv[0] == NULL) return 2;

  if (ush_is_builtin(argv[0])) {
    if (cmd->in_path_raw != NULL || cmd->out_path_raw != NULL) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    return ush_run_builtin(st, argv);
  }

  char in_path[USH_MAX_TOKEN_LEN + 1];
  char out_path[USH_MAX_TOKEN_LEN + 1];
  int xr;

  xr = expand_redir_path(st, cmd->in_quote, cmd->in_path_raw, in_path);
  if (xr != 0) return xr;
  xr = expand_redir_path(st, cmd->out_quote, cmd->out_path_raw, out_path);
  if (xr != 0) return xr;

  int in_fd = -1;
  int out_fd = -1;

  if (cmd->in_path_raw != NULL) {
    in_fd = open_in(in_path);
    if (in_fd < 0) {
      ush_perrorf("open");
      return 1;
    }
  }

  if (cmd->out_path_raw != NULL) {
    out_fd = open_out(out_path, cmd->out_append);
    if (out_fd < 0) {
      if (in_fd >= 0) close(in_fd);
      ush_perrorf("open");
      return 1;
    }
  }

  int r = exec_external_cmd(argv, in_fd, out_fd);

  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  return r;
}

static int exec_pipeline(ush_state_t *st, const ush_pipeline_t *pl) {
  if (!pl->has_right) {
    return exec_command(st, &pl->left);
  }

  char l_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char r_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *l_argv[USH_MAX_ARGS + 1];
  char *r_argv[USH_MAX_ARGS + 1];

  int er;
  er = expand_argv(st, &pl->left, l_words, l_argv);
  if (er != 0) return er;
  er = expand_argv(st, &pl->right, r_words, r_argv);
  if (er != 0) return er;

  if (ush_is_builtin(l_argv[0]) || ush_is_builtin(r_argv[0])) {
    ush_eprintf("unsupported syntax");
    return 2;
  }

  int in_fd = -1;
  int out_fd = -1;

  char in_path[USH_MAX_TOKEN_LEN + 1];
  char out_path[USH_MAX_TOKEN_LEN + 1];
  int xr;

  xr = expand_redir_path(st, pl->left.in_quote, pl->left.in_path_raw, in_path);
  if (xr != 0) return xr;
  xr = expand_redir_path(st, pl->right.out_quote, pl->right.out_path_raw, out_path);
  if (xr != 0) return xr;

  if (pl->left.in_path_raw != NULL) {
    in_fd = open_in(in_path);
    if (in_fd < 0) {
      ush_perrorf("open");
      return 1;
    }
  }

  if (pl->right.out_path_raw != NULL) {
    out_fd = open_out(out_path, pl->right.out_append);
    if (out_fd < 0) {
      if (in_fd >= 0) close(in_fd);
      ush_perrorf("open");
      return 1;
    }
  }

  int pfd[2];
  if (pipe(pfd) != 0) {
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("pipe");
    return 1;
  }

  pid_t lp = fork();
  if (lp < 0) {
    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("fork");
    return 1;
  }

  if (lp == 0) {
    set_child_sigint_default();

    if (in_fd >= 0) dup2(in_fd, STDIN_FILENO);
    dup2(pfd[1], STDOUT_FILENO);

    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    char path[1024];
    int fail = 127;
    if (resolve_cmd(l_argv[0], path, &fail) != 0) _exit(fail);
    exec_with_sh_fallback(path, l_argv);
    _exit(126);
  }

  pid_t rp = fork();
  if (rp < 0) {
    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("fork");
    return 1;
  }

  if (rp == 0) {
    set_child_sigint_default();

    dup2(pfd[0], STDIN_FILENO);
    if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);

    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    char path[1024];
    int fail = 127;
    if (resolve_cmd(r_argv[0], path, &fail) != 0) _exit(fail);
    exec_with_sh_fallback(path, r_argv);
    _exit(126);
  }

  close(pfd[0]);
  close(pfd[1]);
  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  int st_l = 0;
  int st_r = 0;
  waitpid(lp, &st_l, 0);
  waitpid(rp, &st_r, 0);

  if (WIFEXITED(st_r)) return WEXITSTATUS(st_r);
  if (WIFSIGNALED(st_r)) return 128 + WTERMSIG(st_r);
  return 1;
}

static int eval_node(ush_state_t *st, const ush_ast_t *ast, int idx) {
  const ush_node_t *n = &ast->nodes[idx];

  switch (n->kind) {
    case NODE_PIPELINE:
      st->last_status = exec_pipeline(st, &n->pl);
      return st->last_status;
    case NODE_AND: {
      int ls = eval_node(st, ast, n->left);
      st->last_status = ls;
      if (ls == 0) return eval_node(st, ast, n->right);
      return ls;
    }
    case NODE_OR: {
      int ls = eval_node(st, ast, n->left);
      st->last_status = ls;
      if (ls != 0) return eval_node(st, ast, n->right);
      return ls;
    }
    case NODE_SEQ: {
      (void)eval_node(st, ast, n->left);
      return eval_node(st, ast, n->right);
    }
  }

  return 1;
}

int ush_exec_ast(ush_state_t *st, const ush_ast_t *ast, int root) {
  if (st == NULL || ast == NULL || root < 0) return 1;
  int r = eval_node(st, ast, root);
  st->last_status = r;
  return r;
}
```

---

# 11. lineedit（src/lineedit.c）

設計要点（0.0.4追加）:
- Tab 補完を「現在トークン」に対して実施する（仕様書 15）。
- 複雑ケース（`\\`/`'`/`"` を含む）は補完しない。
- 先頭トークンはコマンド補完、それ以外はファイル/パス補完。
- 対話入力のため raw mode を使うが、入力確定後は raw を解除して外部コマンドが通常のTTY状態で動けるようにする。

【実装用（貼り付け可）: src/lineedit.c】

```c
#include "ush_lineedit.h"

#include "ush_env.h"
#include "ush_utils.h"

#include <dirent.h>
#include <errno.h>
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
  if (!isatty(STDIN_FILENO)) return 0; // 非TTYはそのまま（読み取りのみ）

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

  size_t tail = len - cursor;
  if (tail > 0) {
    fprintf(stdout, "\x1b[%zuD", tail);
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

static int is_dir_path(const char *path) {
  if (path == NULL) return 0;
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode);
}

static int list_path_matches(
  const char *dir_to_scan,
  const char *dir_prefix,
  const char *base_prefix,
  char out[256][USH_MAX_TOKEN_LEN + 1],
  int *io_n
) {
  if (dir_to_scan == NULL || dir_prefix == NULL || base_prefix == NULL || out == NULL || io_n == NULL) return 1;

  DIR *dp = opendir(dir_to_scan);
  if (dp == NULL) return 1;

  struct dirent *de;
  while ((de = readdir(dp)) != NULL) {
    const char *name = de->d_name;
    if (name == NULL || name[0] == '\0') continue;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

    if (name[0] == '.' && base_prefix[0] != '.') continue;
    if (!ush_starts_with(name, base_prefix)) continue;

    if (*io_n >= 256) break;

    char cand[USH_MAX_TOKEN_LEN + 1];
    snprintf(cand, sizeof(cand), "%s%s", dir_prefix, name);

    char full[USH_MAX_TOKEN_LEN + 1];
    if (dir_prefix[0] == '\0') {
      snprintf(full, sizeof(full), "%s", name);
    } else {
      snprintf(full, sizeof(full), "%s%s", dir_prefix, name);
    }

    if (is_dir_path(full)) {
      snprintf(out[*io_n], USH_MAX_TOKEN_LEN + 1, "%s/", cand);
    } else {
      snprintf(out[*io_n], USH_MAX_TOKEN_LEN + 1, "%s", cand);
    }

    (*io_n)++;
  }

  closedir(dp);
  return 0;
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

  // 先頭トークンか？（先頭の非空白位置と一致）
  size_t fn = 0;
  while (fn < *io_len && (buf[fn] == ' ' || buf[fn] == '\t')) fn++;
  int is_first = (start == fn);

  int can_cmd_complete = 0;
  if (is_first && strchr(tok, '/') == NULL) {
    can_cmd_complete = 1;
    for (size_t k = 0; tok[k] != '\0'; k++) {
      if (!is_cmd_char((unsigned char)tok[k])) {
        can_cmd_complete = 0;
        break;
      }
    }
  }

  char matches[256][USH_MAX_TOKEN_LEN + 1];
  int n = 0;

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
  } else {
    const char *slash = strrchr(tok, '/');
    char dir_prefix[USH_MAX_TOKEN_LEN + 1];
    char base_prefix[USH_MAX_TOKEN_LEN + 1];
    char dir_to_scan[USH_MAX_TOKEN_LEN + 1];

    if (slash != NULL) {
      size_t dlen = (size_t)(slash - tok) + 1; // include '/'
      if (dlen >= sizeof(dir_prefix)) dlen = sizeof(dir_prefix) - 1;
      memcpy(dir_prefix, tok, dlen);
      dir_prefix[dlen] = '\0';
      snprintf(base_prefix, sizeof(base_prefix), "%s", slash + 1);

      size_t scan_len = strlen(dir_prefix);
      if (scan_len == 0) {
        snprintf(dir_to_scan, sizeof(dir_to_scan), ".");
      } else if (scan_len == 1 && dir_prefix[0] == '/') {
        snprintf(dir_to_scan, sizeof(dir_to_scan), "/");
      } else {
        if (scan_len >= sizeof(dir_to_scan)) scan_len = sizeof(dir_to_scan) - 1;
        memcpy(dir_to_scan, dir_prefix, scan_len);
        dir_to_scan[scan_len - 1] = '\0';
      }
    } else {
      dir_prefix[0] = '\0';
      snprintf(base_prefix, sizeof(base_prefix), "%s", tok);
      snprintf(dir_to_scan, sizeof(dir_to_scan), ".");
    }

    list_path_matches(dir_to_scan, dir_prefix, base_prefix, matches, &n);
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
    while (n > 0 && (out_line[n - 1] == '\n' || out_line[n - 1] == '\r')) {
      out_line[--n] = '\0';
    }
    return 0;
  }

  if (enable_raw() != 0) return 1;

  int rc = 1;

  char buf[USH_MAX_LINE_LEN + 1];
  size_t len = 0;
  size_t cursor = 0;
  buf[0] = '\0';

  char saved[USH_MAX_LINE_LEN + 1];
  int saved_valid = 0;

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

    if (ch == 0x04) { // Ctrl-D
      if (len == 0) {
        fputc('\n', stdout);
        rc = 1;
        goto out;
      }
      continue;
    }

    if (ch == 0x08 || ch == 0x7f) { // BS/DEL
      if (cursor > 0) {
        memmove(buf + cursor - 1, buf + cursor, len - cursor);
        len--;
        cursor--;
        buf[len] = '\0';
        redraw(prompt, buf, len, cursor);
      }
      continue;
    }

    if (ch == '\t') { // Tab
      do_tab_complete(buf, sizeof(buf), &len, &cursor);
      redraw(prompt, buf, len, cursor);
      continue;
    }

    if (ch == 0x1b) { // ESC
      unsigned char seq[4] = {0};
      if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
      if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

      if (seq[0] == '[') {
        if (seq[1] == 'C') { // right
          if (cursor < len) {
            cursor++;
            redraw(prompt, buf, len, cursor);
          }
          continue;
        }
        if (seq[1] == 'D') { // left
          if (cursor > 0) {
            cursor--;
            redraw(prompt, buf, len, cursor);
          }
          continue;
        }
        if (seq[1] == 'A') { // up
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
        if (seq[1] == 'B') { // down
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
        if (seq[1] == '3') { // Delete: ESC [ 3 ~
          if (read(STDIN_FILENO, &seq[2], 1) <= 0) continue;
          if (seq[2] == '~') {
            if (cursor < len) {
              memmove(buf + cursor, buf + cursor + 1, len - cursor - 1);
              len--;
              buf[len] = '\0';
              redraw(prompt, buf, len, cursor);
            }
          }
          continue;
        }
      }

      continue;
    }

    if (ch >= 0x20 && ch <= 0x7e) {
      if (len + 1 >= sizeof(buf)) continue;
      if (len + 1 >= out_cap) continue;

      memmove(buf + cursor + 1, buf + cursor, len - cursor);
      buf[cursor] = (char)ch;
      len++;
      cursor++;
      buf[len] = '\0';
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

# 12. 簡易スモークテスト（受け入れ手順の例）

【実装用（貼り付け可）】

```sh
cat > /tmp/ush004_smoke.sh <<'EOF'
echo A ; echo B

export FOO=bar
echo ${FOO}

echo /no/such/*
echo /tmp/[a-z]
EOF

./ush /tmp/ush004_smoke.sh </dev/null ; echo EXIT:$?
```

---

# 13. 仕様と設計の整合チェック
- `;` の空要素は parse で `syntax error` になる（仕様書 7.2.2）。
- `${NAME}` の派生は expand で `unsupported syntax` になる（仕様書 11.2）。
- グロブは未クォートのみで、0件はそのまま残る（仕様書 12.1/12.4）。
- `[a-z]` は exec のグロブ処理で `unsupported syntax` になる（仕様書 12.2）。
- Tab 補完は「現在トークン」に適用され、複雑ケースは補完しない（仕様書 15.2）。

---

# 14. 1から実装するための共通ファイル（貼り付け可）

この章は「0.0.3 を流用しない」場合でも 1から実装できるよう、0.0.4で不変の共通ファイルをまとめて提示する。

## 14.1 制限値（include/ush_limits.h）

【実装用（貼り付け可）: include/ush_limits.h】

```c
#pragma once

enum {
  USH_MAX_LINE_LEN  = 8192,
  USH_MAX_ARGS      = 128,
  USH_MAX_TOKEN_LEN = 1024,

  // パイプは 1 段まで（最大 2 コマンド）
  USH_MAX_CMDS      = 2,

  // list := pipeline ( (&&||) pipeline )*
  // 1行内に許す pipeline 数の上限（実装簡易化）
  USH_MAX_PIPES     = 64,

  // トークン配列上限（経験則）
  USH_MAX_TOKENS    = 256,

  // 簡易履歴
  USH_HISTORY_MAX   = 32,
};
```

## 14.2 エラー/戻り値（include/ush_err.h）

【実装用（貼り付け可）: include/ush_err.h】

```c
#pragma once

typedef enum {
  PARSE_OK = 0,
  PARSE_EMPTY,          // 空行/空白のみ/コメント行
  PARSE_INCOMPLETE,     // 入力が途中（複数行ブロックの続きが必要）
  PARSE_TOO_LONG,       // 行長 or トークン長超過
  PARSE_TOO_MANY_TOKENS,
  PARSE_TOO_MANY_ARGS,  // argvが上限超過

  PARSE_UNSUPPORTED,    // 未対応構文を検出（"unsupported syntax"）
  PARSE_SYNTAX_ERROR,   // 構文エラー（"syntax error"）
} parse_result_t;
```

## 14.3 グローバル状態（include/ush.h）

【実装用（貼り付け可）: include/ush.h】

```c
#pragma once

typedef struct {
  int last_status;  // 初期値0

  // 位置パラメータ（$0 $1..$9 $#）
  // 対話モード: script_path="ush", pos_argc=0
  // スクリプトモード: script_path=scriptのパス文字列, pos_argv=argv[2..]
  const char *script_path;
  int pos_argc;
  char **pos_argv;
} ush_state_t;
```

## 14.4 utils（include/ush_utils.h / src/utils.c）

【実装用（貼り付け可）: include/ush_utils.h】

```c
#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
void ush_perrorf(const char *context);  // ush: <context>: <strerror>

int ush_is_blank_line(const char *line);

int ush_is_space_ch(char c);

int ush_starts_with(const char *s, const char *prefix);

int ush_is_valid_name(const char *name);
int ush_is_assignment_word0(const char *s); // NAME=... の形式か（NAMEは正規）
```

【実装用（貼り付け可）: src/utils.c】

```c
#include "ush_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ush_eprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("ush: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ush_perrorf(const char *context) {
  int e = errno;
  if (context == NULL) context = "error";
  fprintf(stderr, "ush: %s: %s\n", context, strerror(e));
}

int ush_is_space_ch(char c) {
  return c == ' ' || c == '\t';
}

int ush_is_blank_line(const char *line) {
  if (line == NULL) return 1;
  for (const char *p = line; *p; p++) {
    if (!ush_is_space_ch(*p)) return 0;
  }
  return 1;
}

int ush_starts_with(const char *s, const char *prefix) {
  if (s == NULL || prefix == NULL) return 0;
  size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

int ush_is_valid_name(const char *name) {
  if (name == NULL || name[0] == '\0') return 0;
  if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return 0;
  for (const char *p = name + 1; *p; p++) {
    if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
  }
  return 1;
}

int ush_is_assignment_word0(const char *s) {
  if (s == NULL) return 0;
  const char *eq = strchr(s, '=');
  if (eq == NULL) return 0;

  // NAME= の NAME 部分が正規
  if (eq == s) return 0;

  char name[256];
  size_t n = (size_t)(eq - s);
  if (n >= sizeof(name)) return 0;
  memcpy(name, s, n);
  name[n] = '\0';

  return ush_is_valid_name(name);
}
```

## 14.5 env（include/ush_env.h / src/env.c）

【実装用（貼り付け可）: include/ush_env.h】

```c
#pragma once

const char *ush_get_path_or_default(void);
```

【実装用（貼り付け可）: src/env.c】

```c
#include "ush_env.h"

#include <stdlib.h>

const char *ush_get_path_or_default(void) {
  const char *p = getenv("PATH");
  if (p == NULL || p[0] == '\0') return "/umu_bin:/sbin:/bin";
  return p;
}
```

## 14.6 prompt（include/ush_prompt.h / src/prompt.c）

【実装用（貼り付け可）: include/ush_prompt.h】

```c
#pragma once
#include <stddef.h>

// out は NUL 終端される
int ush_prompt_render(char *out, size_t out_cap);
```

【実装用（貼り付け可）: src/prompt.c】

```c
#include "ush_prompt.h"

#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static const char *get_ps1_raw(void) {
  const char *p = getenv("USH_PS1");
  if (p != NULL && p[0] != '\0') return p;
  p = getenv("PS1");
  if (p != NULL && p[0] != '\0') return p;
  return "\\u@UmuOS:ush:\\w\\$ ";
}

static const char *get_username(void) {
  const char *u = getenv("USER");
  if (u != NULL && u[0] != '\0') return u;
  struct passwd *pw = getpwuid(getuid());
  if (pw != NULL && pw->pw_name != NULL) return pw->pw_name;
  return "user";
}

static int render_cwd(char *out, size_t cap) {
  if (out == NULL || cap == 0) return 1;
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    snprintf(out, cap, "?");
    return 1;
  }

  const char *home = getenv("HOME");
  if (home != NULL && home[0] != '\0') {
    size_t home_len = strlen(home);
    if (strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
      // HOME配下を ~ 省略
      if (cwd[home_len] == '\0') {
        snprintf(out, cap, "~");
      } else {
        snprintf(out, cap, "~%s", cwd + home_len);
      }
      return 0;
    }
  }

  snprintf(out, cap, "%s", cwd);
  return 0;
}

int ush_prompt_render(char *out, size_t out_cap) {
  if (out == NULL || out_cap == 0) return 1;

  const char *in = get_ps1_raw();
  size_t oi = 0;

  for (size_t i = 0; in[i] != '\0'; i++) {
    if (oi + 1 >= out_cap) break;

    if (in[i] != '\\') {
      out[oi++] = in[i];
      continue;
    }

    char n = in[i + 1];
    if (n == '\0') {
      out[oi++] = '\\';
      break;
    }

    if (n == 'u') {
      const char *u = get_username();
      size_t len = strlen(u);
      if (oi + len >= out_cap) len = out_cap - oi - 1;
      memcpy(out + oi, u, len);
      oi += len;
      i++;
      continue;
    }

    if (n == 'w') {
      char w[1024];
      render_cwd(w, sizeof(w));
      size_t len = strlen(w);
      if (oi + len >= out_cap) len = out_cap - oi - 1;
      memcpy(out + oi, w, len);
      oi += len;
      i++;
      continue;
    }

    if (n == '$') {
      out[oi++] = (geteuid() == 0) ? '#' : '$';
      i++;
      continue;
    }

    if (n == '\\') {
      out[oi++] = '\\';
      i++;
      continue;
    }

    // 未対応はそのまま
    out[oi++] = '\\';
  }

  out[oi] = '\0';
  return 0;
}
```

## 14.7 lineedit ヘッダ（include/ush_lineedit.h）

【実装用（貼り付け可）: include/ush_lineedit.h】

```c
#pragma once
#include <stddef.h>
#include "ush_limits.h"

typedef struct {
  char items[USH_HISTORY_MAX][USH_MAX_LINE_LEN + 1];
  int count;
  int cursor;   // 履歴参照位置（0..count）
} ush_history_t;

int ush_lineedit_readline(
  const char *prompt,
  char *out_line,
  size_t out_cap,
  ush_history_t *hist
);
```

## 14.8 expand ヘッダ（include/ush_expand.h）

【実装用（貼り付け可）: include/ush_expand.h】

```c
#pragma once
#include <stddef.h>

#include "ush_err.h"
#include "ush.h"
#include "ush_tokenize.h"

typedef struct {
  int last_status;

  // 位置パラメータ
  const char *script_path;
  int pos_argc;
  char **pos_argv;

  // コマンド置換の評価に使う状態（NULL可）
  const ush_state_t *cmdsub_base;
} ush_expand_ctx_t;

// out は NUL 終端される。
// 失敗時: PARSE_UNSUPPORTED または PARSE_TOO_LONG を返す。
parse_result_t ush_expand_word(
  const ush_expand_ctx_t *ctx,
  quote_kind_t quote,
  const char *in,
  char *out,
  size_t out_cap
);
```

## 14.9 exec ヘッダ（include/ush_exec.h）

【実装用（貼り付け可）: include/ush_exec.h】

```c
#pragma once
#include <stddef.h>

#include "ush.h"
#include "ush_parse.h"

int ush_exec_ast(ush_state_t *st, const ush_ast_t *ast, int root);

// $(...) 用: cmdline を ush の 1行として子プロセスで評価し、stdout を out に格納する。
// out は NUL 終端される。
// - 末尾の改行（\r/\n）は削除する（内部の改行の扱いは呼び出し側で決める）。
// - out_cap 超過は PARSE_TOO_LONG。
// - base_state は位置パラメータ等の参照に用いる（NULL可）。
parse_result_t ush_exec_capture_stdout(
  const ush_state_t *base_state,
  const char *cmdline,
  char *out,
  size_t out_cap
);
```

## 14.10 builtins（include/ush_builtins.h / src/builtins.c）

【実装用（貼り付け可）: include/ush_builtins.h】

```c
#pragma once
#include "ush.h"

int ush_is_builtin(const char *cmd);
int ush_run_builtin(ush_state_t *st, char *argv[]);
```

【実装用（貼り付け可）: src/builtins.c】

```c
#include "ush_builtins.h"

#include "ush_utils.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_unary(const char *op, const char *arg) {
  if (op == NULL) return 2;
  if (strcmp(op, "-n") == 0) {
    return (arg != NULL && arg[0] != '\0') ? 0 : 1;
  }
  if (strcmp(op, "-z") == 0) {
    return (arg == NULL || arg[0] == '\0') ? 0 : 1;
  }
  if (strcmp(op, "-e") == 0 || strcmp(op, "-f") == 0 || strcmp(op, "-d") == 0) {
    if (arg == NULL) return 2;
    struct stat st;
    if (stat(arg, &st) != 0) return 1;
    if (strcmp(op, "-e") == 0) return 0;
    if (strcmp(op, "-f") == 0) return S_ISREG(st.st_mode) ? 0 : 1;
    if (strcmp(op, "-d") == 0) return S_ISDIR(st.st_mode) ? 0 : 1;
  }
  return 2;
}

static int test_binary(const char *a, const char *op, const char *b) {
  if (a == NULL || op == NULL || b == NULL) return 2;
  if (strcmp(op, "=") == 0) return (strcmp(a, b) == 0) ? 0 : 1;
  if (strcmp(op, "!=") == 0) return (strcmp(a, b) != 0) ? 0 : 1;
  return 2;
}

// argv points to a test-like expression, NOT including the command name.
static int builtin_testlike(char *argv[]) {
  if (argv == NULL) return 2;

  // empty: false
  if (argv[0] == NULL) return 1;

  // unary '!'
  if (strcmp(argv[0], "!") == 0) {
    int r = builtin_testlike(&argv[1]);
    if (r == 0) return 1;
    if (r == 1) return 0;
    return r;
  }

  // single arg: true if non-empty
  if (argv[1] == NULL) {
    return (argv[0][0] != '\0') ? 0 : 1;
  }

  // unary op
  if (argv[2] == NULL) {
    int r = test_unary(argv[0], argv[1]);
    return r;
  }

  // binary op
  if (argv[3] == NULL) {
    int r = test_binary(argv[0], argv[1], argv[2]);
    return r;
  }

  return 2;
}

static int builtin_cd(char *argv[]) {
  const char *dir = argv[1];
  if (dir == NULL) {
    dir = getenv("HOME");
    if (dir == NULL || dir[0] == '\0') dir = "/";
  }
  if (argv[2] != NULL) return 2;
  if (chdir(dir) != 0) {
    ush_perrorf("cd");
    return 1;
  }
  return 0;
}

static int builtin_pwd(char *argv[]) {
  if (argv[1] != NULL) return 2;
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    ush_perrorf("pwd");
    return 1;
  }
  puts(cwd);
  return 0;
}

static int builtin_export(char *argv[]) {
  // 仕様: export NAME=VALUE / export NAME
  if (argv[1] == NULL) return 2;
  if (argv[2] != NULL) return 2;

  const char *arg = argv[1];
  const char *eq = strchr(arg, '=');
  if (eq != NULL) {
    size_t n = (size_t)(eq - arg);
    char name[256];
    if (n == 0 || n >= sizeof(name)) return 2;
    memcpy(name, arg, n);
    name[n] = '\0';
    if (!ush_is_valid_name(name)) return 2;
    const char *val = eq + 1;
    if (setenv(name, val, 1) != 0) {
      ush_perrorf("export");
      return 1;
    }
    return 0;
  }

  if (!ush_is_valid_name(arg)) return 2;
  // NAME のみ: 未定義なら空文字で作成、定義済みなら維持
  const char *v = getenv(arg);
  if (v == NULL) {
    if (setenv(arg, "", 1) != 0) {
      ush_perrorf("export");
      return 1;
    }
  }
  return 0;
}

static int builtin_exit(ush_state_t *st, char *argv[]) {
  if (argv[1] == NULL) exit(st->last_status & 255);
  if (argv[2] != NULL) return 2;
  char *end = NULL;
  long v = strtol(argv[1], &end, 10);
  if (end == NULL || *end != '\0') return 2;
  exit((int)(v & 255));
}

static int builtin_help(void) {
  puts("ush 0.0.6 builtins:");
  puts("  cd [DIR]");
  puts("  pwd");
  puts("  export NAME=VALUE | export NAME");
  puts("  test EXPR");
  puts("  [ EXPR ]");
  puts("  exit [N]");
  puts("  help");
  puts("");
  puts("operators: | (1 stage), &&, ||, ;, <, >, >>");
  puts("control: if/then/elif/else/fi, while/do/done, for/in/do/done, case/in/esac");
  puts("expansion: $VAR, $?, ${VAR}, ~");
  puts("glob: *, ?, [], (no [a-z] range)");
  puts("notes: no break/continue, no functions, no arithmetic");
  return 0;
}

int ush_is_builtin(const char *cmd) {
  if (cmd == NULL) return 0;
  return strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 || strcmp(cmd, "export") == 0 ||
         strcmp(cmd, "test") == 0 || strcmp(cmd, "[") == 0 ||
         strcmp(cmd, "exit") == 0 || strcmp(cmd, "help") == 0;
}

int ush_run_builtin(ush_state_t *st, char *argv[]) {
  if (argv == NULL || argv[0] == NULL) return 2;

  if (strcmp(argv[0], "cd") == 0) return builtin_cd(argv);
  if (strcmp(argv[0], "pwd") == 0) return builtin_pwd(argv);
  if (strcmp(argv[0], "export") == 0) return builtin_export(argv);
  if (strcmp(argv[0], "test") == 0) return builtin_testlike(&argv[1]);
  if (strcmp(argv[0], "[") == 0) {
    // require closing ']'
    int n = 0;
    while (argv[n] != NULL) n++;
    if (n < 2) return 2;
    if (strcmp(argv[n - 1], "]") != 0) return 2;
    argv[n - 1] = NULL;
    int r = builtin_testlike(&argv[1]);
    argv[n - 1] = (char *)"]";
    return r;
  }
  if (strcmp(argv[0], "exit") == 0) return builtin_exit(st, argv);
  if (strcmp(argv[0], "help") == 0) return builtin_help();

  return 2;
}
```

## 14.11 main（src/main.c）

【実装用（貼り付け可）: src/main.c】

```c
#include "ush.h"

#include "ush_err.h"
#include "ush_exec.h"
#include "ush_lineedit.h"
#include "ush_prompt.h"
#include "ush_tokenize.h"
#include "ush_parse.h"
#include "ush_script.h"
#include "ush_utils.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_parent_sigint_ignore(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
}

static void handle_parse_error(ush_state_t *st, parse_result_t r) {
  if (st == NULL) return;

  switch (r) {
    case PARSE_EMPTY:
      return;
    case PARSE_INCOMPLETE:
      return;
    case PARSE_UNSUPPORTED:
      ush_eprintf("unsupported syntax");
      st->last_status = 2;
      return;
    case PARSE_SYNTAX_ERROR:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_TOO_LONG:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_TOO_MANY_TOKENS:
    case PARSE_TOO_MANY_ARGS:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_OK:
      return;
  }
}

static parse_result_t eval_text(ush_state_t *st, const char *text) {
  token_t toks[USH_MAX_TOKENS];
  int ntok = 0;
  char tokbuf[USH_MAX_LINE_LEN + 1];

  parse_result_t tr = ush_tokenize(text, toks, &ntok, tokbuf);
  if (tr != PARSE_OK) return tr;

  ush_script_t sc;
  int root = -1;
  parse_result_t pr = ush_parse_script(toks, ntok, &sc, &root);
  if (pr != PARSE_OK) return pr;

  ush_exec_script(st, toks, ntok, &sc, root);
  return PARSE_OK;
}

static int run_interactive(ush_state_t *st) {
  ush_history_t hist;
  memset(&hist, 0, sizeof(hist));

  char buf[USH_MAX_LINE_LEN + 1];
  size_t blen = 0;
  buf[0] = '\0';

  for (;;) {
    char prompt[256];
    ush_prompt_render(prompt, sizeof(prompt));

    char line[USH_MAX_LINE_LEN + 1];
    int r = ush_lineedit_readline(prompt, line, sizeof(line), &hist);
    if (r == 1) {
      exit(st->last_status & 255);
    }

    if (ush_is_blank_line(line)) {
      if (blen == 0) continue;
      // blank line inside a compound: treat as separator
      line[0] = '\0';
    }

    size_t ln = strlen(line);
    // append: <line> ;
    if (blen + ln + 2 > sizeof(buf)) {
      ush_eprintf("syntax error");
      st->last_status = 2;
      blen = 0;
      buf[0] = '\0';
      continue;
    }

    if (ln > 0) {
      memcpy(buf + blen, line, ln);
      blen += ln;
    }
    buf[blen++] = ';';
    buf[blen] = '\0';

    parse_result_t pr = eval_text(st, buf);
    if (pr == PARSE_INCOMPLETE) {
      continue;
    }
    if (pr != PARSE_OK) {
      handle_parse_error(st, pr);
    }
    // ok or error: reset buffer
    blen = 0;
    buf[0] = '\0';
  }
}

static int run_script(ush_state_t *st, const char *path) {
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    ush_perrorf("open");
    st->last_status = 1;
    return 1;
  }

  char line[USH_MAX_LINE_LEN + 2];
  char buf[USH_MAX_LINE_LEN + 1];
  size_t blen = 0;
  buf[0] = '\0';
  int lineno = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    lineno++;

    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

    if (lineno == 1 && ush_starts_with(line, "#!")) {
      continue;
    }

    if (ush_is_blank_line(line)) continue;

    size_t ln = strlen(line);
    if (blen + ln + 2 > sizeof(buf)) {
      ush_eprintf("syntax error");
      st->last_status = 2;
      fclose(fp);
      return st->last_status;
    }

    memcpy(buf + blen, line, ln);
    blen += ln;
    buf[blen++] = ';';
    buf[blen] = '\0';

    parse_result_t pr = eval_text(st, buf);
    if (pr == PARSE_INCOMPLETE) {
      continue;
    }
    if (pr != PARSE_OK) {
      handle_parse_error(st, pr);
      // on error, clear buffer and continue
    }
    blen = 0;
    buf[0] = '\0';
  }

  if (blen > 0) {
    parse_result_t pr = eval_text(st, buf);
    if (pr == PARSE_INCOMPLETE) {
      ush_eprintf("syntax error");
      st->last_status = 2;
    } else if (pr != PARSE_OK) {
      handle_parse_error(st, pr);
    }
  }

  fclose(fp);
  return st->last_status;
}

int main(int argc, char **argv) {
  ush_state_t st;
  st.last_status = 0;
  st.script_path = "ush";
  st.pos_argc = 0;
  st.pos_argv = NULL;

  set_parent_sigint_ignore();

  if (argc == 1) {
    return run_interactive(&st);
  }

  if (argc >= 2) {
    st.script_path = argv[1];
    st.pos_argc = (argc >= 3) ? (argc - 2) : 0;
    st.pos_argv = (argc >= 3) ? &argv[2] : NULL;
    return run_script(&st, argv[1]);
  }

  ush_eprintf("syntax error");
  return 2;
}

```

## 14.12 script ヘッダ（include/ush_script.h）

【実装用（貼り付け可）: include/ush_script.h】

```c
#pragma once

#include "ush_err.h"
#include "ush_limits.h"
#include "ush_tokenize.h"
#include "ush.h"

// 文(Stmt)ベースのスクリプト

enum {
  USH_MAX_STMTS = 256,
  USH_MAX_ELIF  = 16,
  USH_MAX_CASE_ITEMS = 32,
  USH_MAX_CASE_PATS  = 16,
};

typedef enum {
  ST_SIMPLE = 0, // pipeline/&&/|| のみ（';' は外側で分割）
  ST_SEQ,
  ST_IF,
  ST_WHILE,
  ST_FOR,
  ST_CASE,
} stmt_kind_t;

typedef struct {
  int start; // inclusive token index
  int end;   // exclusive token index
} tok_range_t;

typedef struct {
  int pat_tok[USH_MAX_CASE_PATS];
  int npat;
  int body_root;
} ush_case_item_t;

typedef struct ush_stmt {
  stmt_kind_t kind;

  // ST_SEQ
  int left;
  int right;

  // ST_SIMPLE
  tok_range_t simple;

  // ST_IF
  int if_cond_root;
  int if_then_root;
  int if_else_root; // -1 if none
  int if_elif_cond[USH_MAX_ELIF];
  int if_elif_then[USH_MAX_ELIF];
  int if_n_elif;

  // ST_WHILE
  int while_cond_root;
  int while_body_root;

  // ST_FOR
  int for_name_tok;   // TOK_WORD (name)
  tok_range_t for_words; // TOK_WORD* (expanded at runtime)
  int for_body_root;

  // ST_CASE
  int case_word_tok;  // TOK_WORD
  ush_case_item_t case_items[USH_MAX_CASE_ITEMS];
  int case_nitems;
} ush_stmt_t;

typedef struct {
  ush_stmt_t nodes[USH_MAX_STMTS];
  int n;
} ush_script_t;

parse_result_t ush_parse_script(
  const token_t *toks,
  int ntok,
  ush_script_t *out,
  int *out_root
);

int ush_exec_script(
  ush_state_t *st,
  const token_t *toks,
  int ntok,
  const ush_script_t *sc,
  int root
);

```

## 14.13 script_parse（src/script_parse.c）

【実装用（貼り付け可）: src/script_parse.c】

```c
#include "ush_script.h"

#include "ush_utils.h"

#include <string.h>

static int is_kw(const token_t *toks, int i, int ntok, const char *kw) {
  if (kw == NULL) return 0;
  if (toks == NULL) return 0;
  if (i < 0 || i >= ntok) return 0;
  if (toks[i].kind != TOK_WORD) return 0;
  if (toks[i].quote != QUOTE_NONE) return 0;
  return toks[i].text != NULL && strcmp(toks[i].text, kw) == 0;
}

static void skip_seps(const token_t *toks, int ntok, int *io_i) {
  int i = (io_i != NULL) ? *io_i : 0;
  while (i < ntok && toks[i].kind == TOK_SEMI) i++;
  if (io_i) *io_i = i;
}

static parse_result_t new_node(ush_script_t *sc, stmt_kind_t k, int *out_idx) {
  if (sc == NULL || out_idx == NULL) return PARSE_SYNTAX_ERROR;
  if (sc->n >= USH_MAX_STMTS) return PARSE_TOO_MANY_TOKENS;
  int idx = sc->n++;
  memset(&sc->nodes[idx], 0, sizeof(sc->nodes[idx]));
  sc->nodes[idx].kind = k;
  sc->nodes[idx].left = -1;
  sc->nodes[idx].right = -1;
  sc->nodes[idx].if_cond_root = -1;
  sc->nodes[idx].if_then_root = -1;
  sc->nodes[idx].if_else_root = -1;
  sc->nodes[idx].if_n_elif = 0;
  sc->nodes[idx].while_cond_root = -1;
  sc->nodes[idx].while_body_root = -1;
  sc->nodes[idx].for_name_tok = -1;
  sc->nodes[idx].for_words.start = 0;
  sc->nodes[idx].for_words.end = 0;
  sc->nodes[idx].for_body_root = -1;
  sc->nodes[idx].case_word_tok = -1;
  sc->nodes[idx].case_nitems = 0;
  *out_idx = idx;
  return PARSE_OK;
}

typedef struct {
  const char *w1;
  const char *w2;
  const char *w3;
  const char *w4;
} stop_words_t;

static int is_stop_kw(const token_t *toks, int i, int ntok, stop_words_t stop) {
  if (is_kw(toks, i, ntok, stop.w1)) return 1;
  if (is_kw(toks, i, ntok, stop.w2)) return 1;
  if (is_kw(toks, i, ntok, stop.w3)) return 1;
  if (is_kw(toks, i, ntok, stop.w4)) return 1;
  return 0;
}

static parse_result_t parse_stmt_list_until(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root,
  stop_words_t stop
);

static parse_result_t parse_stmt_list_until_dsemi(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
);

static parse_result_t parse_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
);

static parse_result_t parse_simple_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;

  // must not start with tokens that belong to compound syntax
  if (i < ntok && (toks[i].kind == TOK_DSEMI || toks[i].kind == TOK_RPAREN)) {
    return PARSE_SYNTAX_ERROR;
  }

  int start = i;
  while (i < ntok) {
    if (toks[i].kind == TOK_SEMI) break;
    if (toks[i].kind == TOK_DSEMI) break;
    if (toks[i].kind == TOK_RPAREN) return PARSE_SYNTAX_ERROR;
    i++;
  }
  int end = i;
  if (end <= start) return PARSE_SYNTAX_ERROR;

  int idx = -1;
  parse_result_t r = new_node(sc, ST_SIMPLE, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].simple.start = start;
  sc->nodes[idx].simple.end = end;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_if_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'if'
  i++;

  int cond_root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, sc, &cond_root, (stop_words_t){.w1 = "then"});
  if (r == PARSE_EMPTY) {
    // need at least one condition statement; if input ended, wait for more.
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "then")) return PARSE_SYNTAX_ERROR;
  i++;

  int then_root = -1;
  r = parse_stmt_list_until(toks, ntok, &i, sc, &then_root, (stop_words_t){.w1 = "elif", .w2 = "else", .w3 = "fi"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  int idx = -1;
  r = new_node(sc, ST_IF, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].if_cond_root = cond_root;
  sc->nodes[idx].if_then_root = then_root;
  sc->nodes[idx].if_else_root = -1;
  sc->nodes[idx].if_n_elif = 0;

  skip_seps(toks, ntok, &i);

  // elif*
  while (i < ntok && is_kw(toks, i, ntok, "elif")) {
    if (sc->nodes[idx].if_n_elif >= USH_MAX_ELIF) return PARSE_TOO_MANY_TOKENS;
    i++;

    int econd = -1;
    r = parse_stmt_list_until(toks, ntok, &i, sc, &econd, (stop_words_t){.w1 = "then"});
    if (r == PARSE_EMPTY) {
      return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
    }
    if (r != PARSE_OK) return r;

    skip_seps(toks, ntok, &i);
    if (i >= ntok) return PARSE_INCOMPLETE;
    if (!is_kw(toks, i, ntok, "then")) return PARSE_SYNTAX_ERROR;
    i++;

    int ethen = -1;
    r = parse_stmt_list_until(toks, ntok, &i, sc, &ethen, (stop_words_t){.w1 = "elif", .w2 = "else", .w3 = "fi"});
    if (r == PARSE_EMPTY) {
      return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
    }
    if (r != PARSE_OK) return r;

    int n = sc->nodes[idx].if_n_elif;
    sc->nodes[idx].if_elif_cond[n] = econd;
    sc->nodes[idx].if_elif_then[n] = ethen;
    sc->nodes[idx].if_n_elif++;

    skip_seps(toks, ntok, &i);
  }

  // else?
  if (i < ntok && is_kw(toks, i, ntok, "else")) {
    i++;
    int eroot = -1;
    r = parse_stmt_list_until(toks, ntok, &i, sc, &eroot, (stop_words_t){.w1 = "fi"});
    if (r == PARSE_EMPTY) {
      return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
    }
    if (r != PARSE_OK) return r;
    sc->nodes[idx].if_else_root = eroot;
    skip_seps(toks, ntok, &i);
  }

  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "fi")) return PARSE_SYNTAX_ERROR;
  i++;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_while_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'while'
  i++;

  int cond_root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, sc, &cond_root, (stop_words_t){.w1 = "do"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "do")) return PARSE_SYNTAX_ERROR;
  i++;

  int body_root = -1;
  r = parse_stmt_list_until(toks, ntok, &i, sc, &body_root, (stop_words_t){.w1 = "done"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "done")) return PARSE_SYNTAX_ERROR;
  i++;

  int idx = -1;
  r = new_node(sc, ST_WHILE, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].while_cond_root = cond_root;
  sc->nodes[idx].while_body_root = body_root;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_for_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'for'
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (toks[i].kind != TOK_WORD || toks[i].quote != QUOTE_NONE) return PARSE_SYNTAX_ERROR;
  if (!ush_is_valid_name(toks[i].text)) return PARSE_SYNTAX_ERROR;
  int name_tok = i;
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "in")) return PARSE_SYNTAX_ERROR;
  i++;

  // words until ';' (line break)
  skip_seps(toks, ntok, &i);
  int wstart = i;
  while (i < ntok && toks[i].kind == TOK_WORD) i++;
  int wend = i;

  // require separator before 'do'
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (toks[i].kind != TOK_SEMI) return PARSE_SYNTAX_ERROR;
  skip_seps(toks, ntok, &i);

  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "do")) return PARSE_SYNTAX_ERROR;
  i++;

  int body_root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, sc, &body_root, (stop_words_t){.w1 = "done"});
  if (r == PARSE_EMPTY) {
    return (i >= ntok) ? PARSE_INCOMPLETE : PARSE_SYNTAX_ERROR;
  }
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "done")) return PARSE_SYNTAX_ERROR;
  i++;

  int idx = -1;
  r = new_node(sc, ST_FOR, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].for_name_tok = name_tok;
  sc->nodes[idx].for_words.start = wstart;
  sc->nodes[idx].for_words.end = wend;
  sc->nodes[idx].for_body_root = body_root;

  *out_root = idx;
  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_case_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  int i = *io_i;
  // consume 'case'
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (toks[i].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
  int word_tok = i;
  i++;

  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_INCOMPLETE;
  if (!is_kw(toks, i, ntok, "in")) return PARSE_SYNTAX_ERROR;
  i++;

  // allow separators after 'in'
  skip_seps(toks, ntok, &i);

  int idx = -1;
  parse_result_t r = new_node(sc, ST_CASE, &idx);
  if (r != PARSE_OK) return r;
  sc->nodes[idx].case_word_tok = word_tok;
  sc->nodes[idx].case_nitems = 0;

  // case ... in <items> esac
  while (1) {
    skip_seps(toks, ntok, &i);
    if (i >= ntok) return PARSE_INCOMPLETE;
    if (is_kw(toks, i, ntok, "esac")) {
      i++;
      *out_root = idx;
      *io_i = i;
      return PARSE_OK;
    }

    if (sc->nodes[idx].case_nitems >= USH_MAX_CASE_ITEMS) return PARSE_TOO_MANY_TOKENS;

    ush_case_item_t *it = &sc->nodes[idx].case_items[sc->nodes[idx].case_nitems];
    memset(it, 0, sizeof(*it));
    it->npat = 0;
    it->body_root = -1;

    // pattern_list: WORD ( '|' WORD )* ')'
    while (1) {
      if (i >= ntok) return PARSE_INCOMPLETE;
      if (toks[i].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
      if (it->npat >= USH_MAX_CASE_PATS) return PARSE_TOO_MANY_TOKENS;
      it->pat_tok[it->npat++] = i;
      i++;

      if (i >= ntok) return PARSE_INCOMPLETE;
      if (toks[i].kind == TOK_PIPE) {
        i++;
        continue;
      }
      if (toks[i].kind == TOK_RPAREN) {
        i++;
        break;
      }
      return PARSE_SYNTAX_ERROR;
    }

    // body until ';;'
    int body_root = -1;
    r = parse_stmt_list_until_dsemi(toks, ntok, &i, sc, &body_root);
    if (r != PARSE_OK && r != PARSE_EMPTY) return r;

    skip_seps(toks, ntok, &i);
    if (i >= ntok) return PARSE_INCOMPLETE;
    if (toks[i].kind != TOK_DSEMI) return PARSE_SYNTAX_ERROR;
    i++;

    it->body_root = body_root;
    sc->nodes[idx].case_nitems++;
  }
}

static parse_result_t parse_stmt_list_until_dsemi(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  if (out_root == NULL) return PARSE_SYNTAX_ERROR;
  *out_root = -1;

  skip_seps(toks, ntok, io_i);
  int i = *io_i;

  if (i >= ntok) return PARSE_EMPTY;
  if (toks[i].kind == TOK_DSEMI) return PARSE_EMPTY;

  int left = -1;
  parse_result_t r = parse_stmt(toks, ntok, io_i, sc, &left);
  if (r != PARSE_OK) {
    if (r == PARSE_EMPTY) return PARSE_EMPTY;
    return r;
  }

  while (1) {
    int j = *io_i;
    skip_seps(toks, ntok, &j);
    if (j >= ntok) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }
    if (toks[j].kind == TOK_DSEMI) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }

    if (*io_i >= ntok || toks[*io_i].kind != TOK_SEMI) {
      *out_root = left;
      return PARSE_OK;
    }

    skip_seps(toks, ntok, io_i);
    int k = *io_i;
    if (k >= ntok || toks[k].kind == TOK_DSEMI) {
      *out_root = left;
      return PARSE_OK;
    }

    int right = -1;
    r = parse_stmt(toks, ntok, io_i, sc, &right);
    if (r == PARSE_EMPTY) continue;
    if (r != PARSE_OK) return r;

    int parent = -1;
    r = new_node(sc, ST_SEQ, &parent);
    if (r != PARSE_OK) return r;
    sc->nodes[parent].left = left;
    sc->nodes[parent].right = right;
    left = parent;
  }
}

static parse_result_t parse_stmt(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root
) {
  skip_seps(toks, ntok, io_i);
  int i = *io_i;
  if (i >= ntok) return PARSE_EMPTY;

  if (toks[i].kind != TOK_WORD) {
    if (toks[i].kind == TOK_DSEMI || toks[i].kind == TOK_RPAREN) return PARSE_SYNTAX_ERROR;
    return PARSE_SYNTAX_ERROR;
  }

  if (is_kw(toks, i, ntok, "if")) {
    parse_result_t r = parse_if_stmt(toks, ntok, io_i, sc, out_root);
    return r;
  }
  if (is_kw(toks, i, ntok, "while")) {
    return parse_while_stmt(toks, ntok, io_i, sc, out_root);
  }
  if (is_kw(toks, i, ntok, "for")) {
    return parse_for_stmt(toks, ntok, io_i, sc, out_root);
  }
  if (is_kw(toks, i, ntok, "case")) {
    return parse_case_stmt(toks, ntok, io_i, sc, out_root);
  }

  // stray reserved words
  if (is_kw(toks, i, ntok, "then") || is_kw(toks, i, ntok, "elif") || is_kw(toks, i, ntok, "else") ||
      is_kw(toks, i, ntok, "fi") || is_kw(toks, i, ntok, "do") || is_kw(toks, i, ntok, "done") ||
      is_kw(toks, i, ntok, "in") || is_kw(toks, i, ntok, "esac")) {
    return PARSE_SYNTAX_ERROR;
  }

  return parse_simple_stmt(toks, ntok, io_i, sc, out_root);
}

static parse_result_t parse_stmt_list_until(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_script_t *sc,
  int *out_root,
  stop_words_t stop
) {
  if (out_root == NULL) return PARSE_SYNTAX_ERROR;
  *out_root = -1;

  skip_seps(toks, ntok, io_i);
  int i = *io_i;

  if (i >= ntok) return PARSE_EMPTY;
  if (is_stop_kw(toks, i, ntok, stop)) {
    return PARSE_EMPTY;
  }

  int left = -1;
  parse_result_t r = parse_stmt(toks, ntok, io_i, sc, &left);
  if (r != PARSE_OK) {
    if (r == PARSE_EMPTY) return PARSE_EMPTY;
    return r;
  }

  while (1) {
    int j = *io_i;
    skip_seps(toks, ntok, &j);
    if (j >= ntok) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }
    if (is_stop_kw(toks, j, ntok, stop)) {
      *io_i = j;
      *out_root = left;
      return PARSE_OK;
    }

    // require at least one separator between statements
    if (*io_i >= ntok || toks[*io_i].kind != TOK_SEMI) {
      // no separator: this is a single simple statement, stop here
      *out_root = left;
      return PARSE_OK;
    }

    // consume separators
    skip_seps(toks, ntok, io_i);

    // if next is stop, allow trailing seps
    int k = *io_i;
    if (k >= ntok || is_stop_kw(toks, k, ntok, stop)) {
      *out_root = left;
      return PARSE_OK;
    }

    int right = -1;
    r = parse_stmt(toks, ntok, io_i, sc, &right);
    if (r == PARSE_EMPTY) {
      // tolerate empty statements due to multiple ';'
      continue;
    }
    if (r != PARSE_OK) return r;

    int parent = -1;
    r = new_node(sc, ST_SEQ, &parent);
    if (r != PARSE_OK) return r;
    sc->nodes[parent].left = left;
    sc->nodes[parent].right = right;
    left = parent;
  }
}

parse_result_t ush_parse_script(
  const token_t *toks,
  int ntok,
  ush_script_t *out,
  int *out_root
) {
  if (out == NULL || out_root == NULL) return PARSE_SYNTAX_ERROR;
  out->n = 0;
  *out_root = -1;

  if (toks == NULL || ntok <= 0) return PARSE_EMPTY;

  int i = 0;
  skip_seps(toks, ntok, &i);
  if (i >= ntok) return PARSE_EMPTY;

  int root = -1;
  parse_result_t r = parse_stmt_list_until(toks, ntok, &i, out, &root, (stop_words_t){});
  if (r == PARSE_EMPTY) return PARSE_EMPTY;
  if (r != PARSE_OK) return r;

  skip_seps(toks, ntok, &i);
  if (i != ntok) {
    // leftover tokens (e.g. stray ';;' or ')')
    if (i < ntok && (toks[i].kind == TOK_DSEMI || toks[i].kind == TOK_RPAREN)) return PARSE_SYNTAX_ERROR;
    return PARSE_SYNTAX_ERROR;
  }

  *out_root = root;
  return PARSE_OK;
}

```

## 14.14 script_exec（src/script_exec.c）

【実装用（貼り付け可）: src/script_exec.c】

```c
#include "ush_script.h"

#include "ush_exec.h"
#include "ush_expand.h"
#include "ush_parse.h"
#include "ush_utils.h"

#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { USH_ESC = 1 };

static void unmark_inplace(char *s) {
  if (s == NULL) return;
  size_t ri = 0;
  size_t wi = 0;
  while (s[ri] != '\0') {
    if ((unsigned char)s[ri] == (unsigned char)USH_ESC) {
      if (s[ri + 1] == '\0') break;
      s[wi++] = s[ri + 1];
      ri += 2;
      continue;
    }
    s[wi++] = s[ri++];
  }
  s[wi] = '\0';
}

static int marked_to_glob_pattern(const char *in, char *out, size_t cap) {
  if (out == NULL || cap == 0) return 1;
  out[0] = '\0';
  if (in == NULL) return 0;

  size_t wi = 0;
  for (size_t ri = 0; in[ri] != '\0';) {
    if ((unsigned char)in[ri] == (unsigned char)USH_ESC) {
      if (in[ri + 1] == '\0') return 1;
      if (wi + 3 > cap) return 1;
      out[wi++] = '\\';
      out[wi++] = in[ri + 1];
      out[wi] = '\0';
      ri += 2;
      continue;
    }
    if (wi + 2 > cap) return 1;
    out[wi++] = in[ri++];
    out[wi] = '\0';
  }
  return 0;
}

static int has_glob_meta_unescaped_marked(const char *s) {
  if (s == NULL) return 0;
  for (size_t i = 0; s[i] != '\0';) {
    if ((unsigned char)s[i] == (unsigned char)USH_ESC) {
      if (s[i + 1] == '\0') return 0;
      i += 2;
      continue;
    }
    if (s[i] == '*' || s[i] == '?') return 1;
    if (s[i] == '[') {
      for (size_t j = i + 1; s[j] != '\0';) {
        if ((unsigned char)s[j] == (unsigned char)USH_ESC) {
          if (s[j + 1] == '\0') break;
          j += 2;
          continue;
        }
        if (s[j] == ']') return 1;
        j++;
      }
    }
    i++;
  }
  return 0;
}

static int has_unsupported_bracket_range(const char *pattern) {
  if (pattern == NULL) return 0;
  for (size_t i = 0; pattern[i] != '\0'; i++) {
    if (pattern[i] == '\\') {
      if (pattern[i + 1] != '\0') i++;
      continue;
    }
    if (pattern[i] != '[') continue;

    size_t j = i + 1;
    int first = 1;
    while (pattern[j] != '\0' && pattern[j] != ']') {
      if (pattern[j] == '\\') {
        if (pattern[j + 1] != '\0') {
          j += 2;
          first = 0;
          continue;
        }
        break;
      }
      if (pattern[j] == '-' && !first && pattern[j + 1] != '\0' && pattern[j + 1] != ']') {
        return 1;
      }
      first = 0;
      j++;
    }
    if (pattern[j] == ']') i = j;
  }
  return 0;
}

static int build_expand_ctx(const ush_state_t *st, ush_expand_ctx_t *out) {
  if (out == NULL) return 1;
  out->last_status = (st != NULL) ? st->last_status : 0;
  out->script_path = (st != NULL && st->script_path != NULL) ? st->script_path : "ush";
  out->pos_argc = (st != NULL) ? st->pos_argc : 0;
  out->pos_argv = (st != NULL) ? st->pos_argv : NULL;
  out->cmdsub_base = st;
  return 0;
}

static int exec_simple_range(ush_state_t *st, const token_t *toks, tok_range_t r) {
  if (st == NULL || toks == NULL) return 1;
  int n = r.end - r.start;
  if (n <= 0) return 0;

  ush_ast_t ast;
  int root = -1;
  parse_result_t pr = ush_parse_line(&toks[r.start], n, &ast, &root);
  if (pr != PARSE_OK) {
    if (pr == PARSE_UNSUPPORTED) {
      ush_eprintf("unsupported syntax");
      st->last_status = 2;
      return 2;
    }
    ush_eprintf("syntax error");
    st->last_status = 2;
    return 2;
  }

  return ush_exec_ast(st, &ast, root);
}

static int eval_stmt(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx);

static int eval_seq(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  (void)eval_stmt(st, toks, ntok, sc, n->left);
  return eval_stmt(st, toks, ntok, sc, n->right);
}

static int expand_one_word(
  ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out[USH_MAX_TOKEN_LEN + 1]
) {
  ush_expand_ctx_t x;
  build_expand_ctx(st, &x);

  parse_result_t r = ush_expand_word(&x, q, raw, out, USH_MAX_TOKEN_LEN + 1);
  if (r == PARSE_UNSUPPORTED) {
    ush_eprintf("unsupported syntax");
    return 2;
  }
  if (r != PARSE_OK) {
    ush_eprintf("syntax error");
    return 2;
  }
  return 0;
}

static int expand_word_to_list(
  ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  int *io_n
) {
  if (io_n == NULL) return 2;
  int n = *io_n;
  if (n >= USH_MAX_ARGS) return 2;

  char tmp[USH_MAX_TOKEN_LEN + 1];
  int er = expand_one_word(st, q, raw, tmp);
  if (er != 0) return er;

  if (q == QUOTE_NONE && has_glob_meta_unescaped_marked(tmp)) {
    char pattern[USH_MAX_TOKEN_LEN + 1];
    if (marked_to_glob_pattern(tmp, pattern, sizeof(pattern)) != 0) {
      ush_eprintf("syntax error");
      return 2;
    }
    if (has_unsupported_bracket_range(pattern)) {
      ush_eprintf("unsupported syntax");
      return 2;
    }

    glob_t g;
    memset(&g, 0, sizeof(g));
    int gr = glob(pattern, GLOB_NOSORT, NULL, &g);
    if (gr == 0) {
      for (size_t k = 0; k < g.gl_pathc; k++) {
        if (n >= USH_MAX_ARGS) {
          globfree(&g);
          ush_eprintf("syntax error");
          return 2;
        }
        snprintf(out_words[n], USH_MAX_TOKEN_LEN + 1, "%s", g.gl_pathv[k]);
        n++;
      }
      globfree(&g);
      *io_n = n;
      return 0;
    }
    if (gr == GLOB_NOMATCH) {
      unmark_inplace(tmp);
      snprintf(out_words[n], USH_MAX_TOKEN_LEN + 1, "%s", tmp);
      n++;
      globfree(&g);
      *io_n = n;
      return 0;
    }

    globfree(&g);
    ush_eprintf("syntax error");
    return 2;
  }

  unmark_inplace(tmp);
  snprintf(out_words[n], USH_MAX_TOKEN_LEN + 1, "%s", tmp);
  n++;
  *io_n = n;
  return 0;
}

static int eval_if(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  int r = eval_stmt(st, toks, ntok, sc, n->if_cond_root);
  if (r == 0) return eval_stmt(st, toks, ntok, sc, n->if_then_root);

  for (int i = 0; i < n->if_n_elif; i++) {
    int cr = eval_stmt(st, toks, ntok, sc, n->if_elif_cond[i]);
    if (cr == 0) return eval_stmt(st, toks, ntok, sc, n->if_elif_then[i]);
  }

  if (n->if_else_root >= 0) return eval_stmt(st, toks, ntok, sc, n->if_else_root);
  return r;
}

static int eval_while(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  int last = 0;
  int ran = 0;
  while (1) {
    int cr = eval_stmt(st, toks, ntok, sc, n->while_cond_root);
    if (cr != 0) break;
    ran = 1;
    last = eval_stmt(st, toks, ntok, sc, n->while_body_root);
  }
  if (!ran) return 0;
  return last;
}

static int eval_for(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  if (n->for_name_tok < 0 || n->for_name_tok >= ntok) return 2;

  const char *name = toks[n->for_name_tok].text;
  if (!ush_is_valid_name(name)) {
    ush_eprintf("syntax error");
    return 2;
  }

  char vals[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  int nval = 0;

  for (int ti = n->for_words.start; ti < n->for_words.end; ti++) {
    if (ti < 0 || ti >= ntok) return 2;
    if (toks[ti].kind != TOK_WORD) return 2;

    int er = expand_word_to_list(st, toks[ti].quote, toks[ti].text, vals, &nval);
    if (er != 0) return er;
  }

  int last = 0;
  for (int vi = 0; vi < nval; vi++) {
    if (setenv(name, vals[vi], 1) != 0) {
      ush_perrorf("setenv");
      return 1;
    }
    last = eval_stmt(st, toks, ntok, sc, n->for_body_root);
  }

  return (nval == 0) ? 0 : last;
}

static int case_pat_matches(
  ush_state_t *st,
  const token_t *toks,
  int ntok,
  int pat_tok,
  const char *subject
) {
  if (pat_tok < 0 || pat_tok >= ntok) return 0;
  if (toks[pat_tok].kind != TOK_WORD) return 0;

  char expanded[USH_MAX_TOKEN_LEN + 1];
  int er = expand_one_word(st, toks[pat_tok].quote, toks[pat_tok].text, expanded);
  if (er != 0) return -1;

  if (toks[pat_tok].quote != QUOTE_NONE) {
    unmark_inplace(expanded);
    return (strcmp(expanded, subject) == 0) ? 1 : 0;
  }

  char pattern[USH_MAX_TOKEN_LEN + 1];
  if (marked_to_glob_pattern(expanded, pattern, sizeof(pattern)) != 0) {
    ush_eprintf("syntax error");
    return -1;
  }
  if (has_unsupported_bracket_range(pattern)) {
    ush_eprintf("unsupported syntax");
    return -1;
  }

  int fr = fnmatch(pattern, subject, 0);
  return (fr == 0) ? 1 : 0;
}

static int eval_case(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  const ush_stmt_t *n = &sc->nodes[idx];
  if (n->case_word_tok < 0 || n->case_word_tok >= ntok) return 2;
  if (toks[n->case_word_tok].kind != TOK_WORD) return 2;

  char subject[USH_MAX_TOKEN_LEN + 1];
  int er = expand_one_word(st, toks[n->case_word_tok].quote, toks[n->case_word_tok].text, subject);
  if (er != 0) return er;
  unmark_inplace(subject);

  for (int ii = 0; ii < n->case_nitems; ii++) {
    const ush_case_item_t *it = &n->case_items[ii];

    for (int pi = 0; pi < it->npat; pi++) {
      int mr = case_pat_matches(st, toks, ntok, it->pat_tok[pi], subject);
      if (mr < 0) return 2;
      if (mr == 1) {
        if (it->body_root < 0) {
          st->last_status = 0;
          return 0;
        }
        return eval_stmt(st, toks, ntok, sc, it->body_root);
      }
    }
  }

  st->last_status = 0;
  return 0;
}

static int eval_stmt(ush_state_t *st, const token_t *toks, int ntok, const ush_script_t *sc, int idx) {
  if (st == NULL || toks == NULL || sc == NULL) return 1;
  if (idx < 0 || idx >= sc->n) return 1;

  const ush_stmt_t *n = &sc->nodes[idx];

  int r = 1;
  switch (n->kind) {
    case ST_SIMPLE:
      r = exec_simple_range(st, toks, n->simple);
      break;
    case ST_SEQ:
      r = eval_seq(st, toks, ntok, sc, idx);
      break;
    case ST_IF:
      r = eval_if(st, toks, ntok, sc, idx);
      break;
    case ST_WHILE:
      r = eval_while(st, toks, ntok, sc, idx);
      break;
    case ST_FOR:
      r = eval_for(st, toks, ntok, sc, idx);
      break;
    case ST_CASE:
      r = eval_case(st, toks, ntok, sc, idx);
      break;
  }

  st->last_status = r;
  return r;
}

int ush_exec_script(
  ush_state_t *st,
  const token_t *toks,
  int ntok,
  const ush_script_t *sc,
  int root
) {
  (void)ntok;
  if (st == NULL || toks == NULL || sc == NULL || root < 0) return 1;
  return eval_stmt(st, toks, ntok, sc, root);
}

```
