# ush-0.0.2-詳細設計書.md
UmuOS User Shell (ush) — 詳細設計書（0.0.2 / MVP）  
Target OS: UmuOS-0.1.6-dev  

本書は [ush-0.0.2/docs/ush-0.0.2-基本設計書.md](ush-0.0.2/docs/ush-0.0.2-基本設計書.md) をコードレベルに落とし込む。

仕様の正は [ush-0.0.2/docs/ush-0.0.2-仕様書.md](ush-0.0.2/docs/ush-0.0.2-仕様書.md) とし、本書は実装手順・関数分割・データ構造を規定する。

---

# 1. 前提・設計原則
- 対話専用シェル（POSIX/b*sh 互換は追わない）
- `/bin/sh`（BusyBox）は残し、`execve()` が `ENOEXEC` のときのみ `/bin/sh` にフォールバック
- 静的リンク（musl）前提で、単一バイナリ `/umu_bin/ush` を成果物とする
- 未対応構文は「検出してエラー」で誤動作を避ける

---

# 2. ソース構成（ファイル/ディレクトリ）
基本設計のモジュール分割に沿って以下を作る（例）。

```
ush/
  include/
    ush.h
    ush_limits.h
    ush_err.h
    ush_utils.h
    ush_prompt.h
    ush_lineedit.h
    ush_tokenize.h
    ush_parse.h
    ush_exec.h
    ush_builtins.h
    ush_env.h
  src/
    main.c
    utils.c
    prompt.c
    lineedit.c
    tokenize.c
    parse.c
    exec.c
    builtins.c
    env.c
```

---

# 3. ビルド手順（開発ホスト）
- 開発ホストで musl により静的リンクビルドし、単一バイナリを UmuOS に持ち込む。

例:

```sh
cd ush
musl-gcc -static -O2 -Wall -Wextra \
  -Iinclude \
  -o ush \
  src/main.c src/utils.c src/prompt.c src/lineedit.c \
  src/tokenize.c src/parse.c src/exec.c src/builtins.c src/env.c
```

---

# 4. 定数・制限値（`ush_limits.h`）

```c
#pragma once

enum {
  USH_MAX_LINE_LEN  = 8192,
  USH_MAX_ARGS      = 128,
  USH_MAX_TOKEN_LEN = 1024,

  // パイプライン中の最大コマンド数（MVPでは控えめに固定）
  USH_MAX_CMDS      = 32,

  // トークン配列上限（MVPの経験則）
  // 単語と演算子が混ざるため argv 上限より大きめに取る。
  USH_MAX_TOKENS    = 256,

  // 簡易履歴
  USH_HISTORY_MAX   = 32,
};
```

注意:
- `argv` 配列サイズは `USH_MAX_ARGS + 1`（末尾NULL）

---

# 5. エラー/戻り値（`ush_err.h`）

## 5.1 token/parse 戻り値

```c
#pragma once

typedef enum {
  PARSE_OK = 0,
  PARSE_EMPTY,          // 空行/空白のみ/コメント行
  PARSE_TOO_LONG,       // 行長 or トークン長超過
  PARSE_TOO_MANY_TOKENS,
  PARSE_TOO_MANY_ARGS,  // argvが上限超過
  PARSE_UNSUPPORTED,    // 未対応構文を検出
  PARSE_SYNTAX_ERROR,   // 演算子の使い方が不正（MVP制約違反含む）
} parse_result_t;
```

- `PARSE_UNSUPPORTED` / `PARSE_SYNTAX_ERROR` は共に `unsupported syntax` として扱い、`last_status=2` にする。

---

# 6. グローバル状態（`ush.h`）

```c
#pragma once

typedef struct {
  int last_status;  // 初期値0
} ush_state_t;
```

---

# 7. utils（`ush_utils.h` / `utils.c`）

## 7.1 stderr 出力

```c
#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
void ush_perrorf(const char *context);  // ush: <context>: <strerror>
```

## 7.2 空白/コメント判定

```c
int ush_is_blank_line(const char *line);
int ush_is_comment_line(const char *line);
```

- 空白類はスペースとタブを対象
- コメント行は「最初の非空白文字が `#`」

## 7.3 文字列ユーティリティ

```c
int ush_starts_with(const char *s, const char *prefix);
```

---

# 8. prompt（`ush_prompt.h` / `prompt.c`）

## 8.1 API

```c
#pragma once
#include <stddef.h>

// out は NUL 終端される
int ush_prompt_render(char *out, size_t out_cap);
```

戻り値:
- 0: 成功
- 1: 失敗（`out` はデフォルト相当で埋めるか、短縮して返す）

## 8.2 入力（優先順位）
- `USH_PS1` → `PS1` → デフォルト（`\u@UmuOS:ush:\w\$ `）
- 未設定または空文字は「未設定扱い」とする

## 8.3 展開（MVP最小）
- `\u` : ユーザー名
  - `getenv("USER")` 優先、無ければ `getpwuid(getuid())`
- `\w` : カレントディレクトリ
  - `getcwd()`
  - `HOME` 配下なら `~` 省略（例: `/home/tama` → `~`）
- `\$` : rootなら `#`、それ以外は `$`
  - `geteuid()==0` で判定
- `\\` : `\`

未対応の `\<x>` はそのまま出力（エラーにしない）。

## 8.4 例（実装方針）
- `out` に対して入力文字列を走査し、`\\` 始まりのシーケンスを最小展開
- バッファが足りない場合は安全に打ち切って NUL 終端

---

# 9. line editor（`ush_lineedit.h` / `lineedit.c`）

## 9.1 API

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
  ush_history_t *hist,
  int last_status
);
```

戻り値:
- 0: 1行取得（`out_line` は `\n` を含まない）
- 1: EOF（Ctrl-D を空バッファで受けた）

## 9.2 raw mode
- `tcgetattr` / `tcsetattr` により canonical off, echo off
- 復元は `atexit` または呼び出し元で `finally` 相当

## 9.3 表示更新（最小）
行編集を簡単にするため、編集のたびに「行全体を再描画」する。

- `\r` で行頭へ
- `prompt + buffer` を表示
- `\x1b[K` で行末まで消去
- カーソル位置が末尾でない場合、必要分 `\x1b[<n>D` で左へ戻す

## 9.4 キー処理
- Enter（`\n`/`\r`）: 確定
- Ctrl-D（0x04）:
  - バッファ長 0 → EOF を返す
  - それ以外 → 無視
- BS/DEL（0x08/0x7f）: カーソル左を削除
- ESC シーケンス:
  - `ESC [ A` : 履歴↑
  - `ESC [ B` : 履歴↓
  - `ESC [ C` : 右
  - `ESC [ D` : 左
  - `ESC [ 3 ~` : Delete

履歴:
- 確定した非空行のみ保存

---

# 10. tokenize（`ush_tokenize.h` / `tokenize.c`）

## 10.1 トークン型

```c
#pragma once
#include <stddef.h>
#include "ush_limits.h"
#include "ush_err.h"

typedef enum {
  TOK_WORD = 0,
  TOK_PIPE,
  TOK_REDIR_IN,
  TOK_REDIR_OUT,
  TOK_REDIR_APPEND,
} token_kind_t;

typedef struct {
  token_kind_t kind;
  const char *text; // TOK_WORD のみ有効（トークン文字列）
} token_t;

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
);
```

設計意図:
- `out_buf` に「クォート除去済みの単語」を構築し、各 `TOK_WORD` は `out_buf` 内を指す
- 演算子は `text=NULL`

## 10.2 未対応検出（仕様準拠）
- ダブルクォート `"`
- バックスラッシュ `\\`
- 変数展開 `$`
- グロブ `* ? [ ]`
- 制御構文 `;` `&` `&&` `||`

注意:
- `|` はパイプとして対応。ただし `||` は未対応なので検出してエラー。
- `>` は対応。`>>` は append として対応。

## 10.3 シングルクォート対応（MVP最小）
- `'...'` を 1 トークンとして読み取る
- 展開なし、エスケープなし
- 未閉鎖 `'` は `PARSE_UNSUPPORTED`

## 10.4 演算子の独立トークン化
- 空白の有無に関わらず以下を独立トークン:
  - `|` `<` `>` `>>`
- ただしシングルクォート内は文字扱い

## 10.5 アルゴリズム（概略）
- `ush_is_comment_line()` が真なら `PARSE_EMPTY`
- `line` を走査し、以下の状態機械で処理:
  - 空白スキップ
  - 単語読み取り（通常）
  - 単語読み取り（シングルクォート）
  - 演算子読み取り
- `TOK_WORD` 生成時に長さ `USH_MAX_TOKEN_LEN` を超えたら `PARSE_TOO_LONG`
- `out_tokens` が溢れたら `PARSE_TOO_MANY_TOKENS`

---

# 11. parse（`ush_parse.h` / `parse.c`）

## 11.1 内部表現

```c
#pragma once
#include "ush_limits.h"
#include "ush_err.h"
#include "ush_tokenize.h"

typedef struct {
  char *argv[USH_MAX_ARGS + 1];
  int argc;
} ush_cmd_t;

typedef struct {
  ush_cmd_t cmds[USH_MAX_CMDS];
  int ncmd;

  const char *in_path;   // < file
  const char *out_path;  // > file, >> file
  int out_append;        // 0:>, 1:>>
} ush_pipeline_t;

parse_result_t ush_parse_pipeline(
  const token_t *toks,
  int ntok,
  ush_pipeline_t *out_pl
);
```

## 11.2 構文制約のチェック
- `|` はコマンドを区切る。
  - `|` が行頭/行末、または `||` は `PARSE_SYNTAX_ERROR`
- `<`:
  - 1回まで
  - ファイル名が必須（次トークンが `TOK_WORD`）
  - パイプライン先頭コマンドにのみ許可（`<` 出現時点のコマンド番号が 0）
- `>` / `>>`:
  - 1回まで
  - ファイル名が必須
  - パイプライン末尾コマンドにのみ許可（最終的に「最後のコマンド番号」であること）
  - `>`/`>>` の後に `|` が出現したら `PARSE_SYNTAX_ERROR`
- 各コマンドの `argv` は `USH_MAX_ARGS` を超えない

## 11.3 パース手順（簡略）
- `out_pl` を初期化
- 現在コマンド index を 0 とし、`TOK_WORD` を `cmds[idx].argv` に追加
- `TOK_PIPE` で index++
- リダイレクトは `in_path/out_path` を設定
- 最後に `argv[argc]=NULL`

---

# 12. builtins（`ush_builtins.h` / `builtins.c`）

## 12.1 API

```c
#pragma once
#include "ush.h"
#include "ush_parse.h"

int ush_is_builtin(const char *cmd);
int ush_run_builtin(ush_state_t *st, char *argv[]);
```

builtins 実行条件:
- `pipeline.ncmd==1` かつ `in_path==NULL` かつ `out_path==NULL`
- それ以外で builtins が現れた場合は `unsupported syntax`（`last_status=2`）

## 12.2 `cd`
- 引数なし: `$HOME` があればそこ、無ければ `/`
- `cd -` は未対応（エラー、`last_status=2`）
- 引数が2個以上はエラー（`last_status=2`）
- 成功時に `PWD` と `OLDPWD` を更新

## 12.3 `pwd`
- 引数なしのみ対応（それ以外は `last_status=2`）
- `getcwd()` を stdout に出力（末尾 `\n`）

## 12.4 `export`
- 0引数:
  - `environ` を `NAME=VALUE` 形式で列挙（順序未規定）
- 1引数:
  - `NAME=VALUE` → `setenv(NAME, VALUE, 1)`
  - `NAME` → 未設定なら空文字で `setenv(NAME, "", 1)`、設定済みなら何もしない
- 2引数以上はエラー（`last_status=2`）
- `NAME` は `[A-Za-z_][A-Za-z0-9_]*` のみ許可

## 12.5 `exit`
- `exit` → `exit(st->last_status)`
- `exit n` → `exit(n & 255)`
- 引数が数値でない/多すぎる → エラーとして終了しない（`last_status=2`）

## 12.6 `help`
- builtins 一覧、未対応事項、パイプ/リダイレクトの制約を短く表示

---

# 13. env（`ush_env.h` / `env.c`）

## 13.1 PATH 取得

```c
#pragma once

const char *ush_get_path_or_default(void);
```

- `getenv("PATH")` が NULL または空文字なら `"/umu_bin:/sbin:/bin"`

---

# 14. exec（`ush_exec.h` / `exec.c`）

## 14.1 API

```c
#pragma once
#include "ush.h"
#include "ush_parse.h"

int ush_exec_pipeline(ush_state_t *st, const ush_pipeline_t *pl);
```

戻り値:
- 実行後の `last_status`（`st->last_status` と同値）

## 14.2 リダイレクト open（親で先に検査）
- `< file`:
  - `open(file, O_RDONLY)`
- `> file`:
  - `open(file, O_WRONLY|O_CREAT|O_TRUNC, 0644)`
- `>> file`:
  - `open(file, O_WRONLY|O_CREAT|O_APPEND, 0644)`

open に失敗した場合:
- `ush: <op>: <strerror>` を stderr
- その行は実行しない（fork しない）
- `last_status=1`

## 14.3 パイプライン実行
- `ncmd` に対して `ncmd-1` 本 pipe
- fork した子で以下:
  - SIGINT をデフォルトへ
  - stdin/stdout を dup2
  - 余剰 FD を close
  - `execve`（PATH探索込み）

親:
- pipe FD を close
- 全子を wait
- `last_status` は末尾コマンド pid の終了状態

## 14.4 PATH探索（126/127）
- `/` を含む → そのまま `execve`
- 含まない → PATH 走査
- `EACCES` 等を見たら `saw_eacces=1`
- 最終的に起動できない:
  - `saw_eacces==1` → 126
  - それ以外 → 127

## 14.5 ENOEXEC フォールバック
- `execve(path, argv, environ)` が `ENOEXEC` の場合のみ:
  - `argv_sh[0] = "/bin/sh"`
  - `argv_sh[1] = path`
  - `argv_sh[2...] = argv[1...]`
  - `execve("/bin/sh", argv_sh, environ)`

---

# 15. main（`main.c`）

## 15.1 エントリ

```c
int main(int argc, char **argv);
```

## 15.2 初期化
- `ush_state_t st = {.last_status = 0};`
- 親 SIGINT は無視（`SIG_IGN`）
- `ush_history_t hist = {0};`

## 15.3 ループ（擬似コード）

```c
for (;;) {
  char prompt[256];
  ush_prompt_render(prompt, sizeof(prompt));

  char line[USH_MAX_LINE_LEN + 1];
  int r = ush_lineedit_readline(prompt, line, sizeof(line), &hist, st.last_status);
  if (r == 1) exit(st.last_status);

  if (ush_is_blank_line(line) || ush_is_comment_line(line)) continue;

  token_t toks[USH_MAX_TOKENS];
  int ntok = 0;
  char tokbuf[USH_MAX_LINE_LEN + 1];
  parse_result_t tr = ush_tokenize(line, toks, &ntok, tokbuf);
  if (tr != PARSE_OK) {
    // PARSE_EMPTY は continue
    // それ以外はエラー表示して st.last_status を更新
    continue;
  }

  ush_pipeline_t pl;
  parse_result_t pr = ush_parse_pipeline(toks, ntok, &pl);
  if (pr != PARSE_OK) {
    // unsupported syntax
    continue;
  }

  // builtins
  if (pl.ncmd == 1 && pl.in_path == NULL && pl.out_path == NULL &&
      ush_is_builtin(pl.cmds[0].argv[0])) {
    st.last_status = ush_run_builtin(&st, pl.cmds[0].argv);
    continue;
  }

  // builtins がパイプ/リダイレクトと組み合わさった場合は unsupported syntax
  if (pl.ncmd >= 1 && ush_is_builtin(pl.cmds[0].argv[0]) &&
      !(pl.ncmd == 1 && pl.in_path == NULL && pl.out_path == NULL)) {
    ush_eprintf("ush: unsupported syntax\n");
    st.last_status = 2;
    continue;
  }

  st.last_status = ush_exec_pipeline(&st, &pl);
}
```

---

# 16. 受け入れ基準（コードレベル確認項目）
- 仕様書の全項目が実装され、以下が成立すること:
  - シングルクォートが 1 トークンとして扱われ、未閉鎖は `unsupported syntax`
  - `||` / `&&` / `"` / `\\` / `$` / `* ? [ ]` / `;` / `&` を検出して実行しない
  - `|` `<` `>` `>>` が空白なしでも演算子トークン化される
  - リダイレクト制約（`<` は先頭、`>`/`>>` は末尾）が強制される
  - パイプライン実行後の `last_status` は末尾コマンド
  - builtins は単一コマンド・リダイレクトなしのみ
  - `USH_PS1` → `PS1` → デフォルトの優先順位でプロンプトが出る
