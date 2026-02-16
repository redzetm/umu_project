# ush-0.0.2-詳細設計書.md
UmuOS User Shell (ush) — 詳細設計書（0.0.2 / MVP）  
Target OS: UmuOS-0.1.6-dev  

本書は [ush-0.0.2/docs/ush-0.0.2-基本設計書.md](ush-0.0.2/docs/ush-0.0.2-基本設計書.md) をコードレベルに落とし込む。

仕様の正は [ush-0.0.2/docs/ush-0.0.2-仕様書.md](ush-0.0.2/docs/ush-0.0.2-仕様書.md) とし、本書は実装手順・関数分割・データ構造を規定する。

---

# 0. この文書の読み方（コピペ区分）
- 本書中のコードブロックは、直前のラベルで用途を明確に区分する。
  - 【実装用（貼り付け可）: <貼り付け先パス>】: そのまま貼り付けて実装を進めることを想定する。
  - 【参考（擬似コード）】: 実装の流れ説明用（そのままではコンパイル前提ではない）。
  - 【説明】: 背景・意図・補足。
- パス表記は原則「ush ディレクトリからの相対パス」を貼り付け先として示す。

---

# 1. 前提・設計原則
- 対話専用シェル（POSIX/b*sh 互換は追わない）
- `/bin/sh`（BusyBox）は残し、`execve()` が `ENOEXEC` のときのみ `/bin/sh` にフォールバック
- 静的リンク（musl）前提で、単一バイナリ `/umu_bin/ush` を成果物とする
- 未対応構文は「検出してエラー」で誤動作を避ける

---

# 2. ソース構成（ファイル/ディレクトリ）
基本設計のモジュール分割に沿って以下を作る（例）。

【説明】

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

【実装用（貼り付け可）】

```sh
# プロジェクト直下で実行（ush/ が無い場合でも作る）

cd /home/tama/umu_project/ush-0.0.2/

mkdir -p ush/include ush/src

touch \
  ush/include/ush.h \
  ush/include/ush_limits.h \
  ush/include/ush_err.h \
  ush/include/ush_utils.h \
  ush/include/ush_prompt.h \
  ush/include/ush_lineedit.h \
  ush/include/ush_tokenize.h \
  ush/include/ush_parse.h \
  ush/include/ush_exec.h \
  ush/include/ush_builtins.h \
  ush/include/ush_env.h \
  ush/src/main.c \
  ush/src/utils.c \
  ush/src/prompt.c \
  ush/src/lineedit.c \
  ush/src/tokenize.c \
  ush/src/parse.c \
  ush/src/exec.c \
  ush/src/builtins.c \
  ush/src/env.c
```

---

# 3. ビルド手順（開発ホスト）
- 開発ホストで musl により静的リンクビルドし、単一バイナリを UmuOS に持ち込む。

【説明】
- `cd ush` で入る想定の `ush/` は、本ワークスペースでは `/home/tama/umu_project/ush-0.0.2/ush`となる。
- **コードは、理解しながら進めないと意味ないので、ご注意を！**


【実装用（貼り付け可）】

```sh
cd ush
musl-gcc -static -O2 -Wall -Wextra \
  -Iinclude \
  -o ush \
  src/main.c src/utils.c src/prompt.c src/lineedit.c \
  src/tokenize.c src/parse.c src/exec.c src/builtins.c src/env.c
```

---

# 4. 定数・制限値（ush/include/ush_limits.h）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_limits.h`

【実装用（貼り付け可）: ush/include/ush_limits.h】

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

# 5. エラー/戻り値（ush/include/ush_err.h）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_err.h`

## 5.1 token/parse 戻り値

【実装用（貼り付け可）: ush/include/ush_err.h】

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

# 6. グローバル状態（ush/include/ush.h）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush.h`

【実装用（貼り付け可）: ush/include/ush.h】

```c
#pragma once

typedef struct {
  int last_status;  // 初期値0
} ush_state_t;
```

---

# 7. utils（ush/include/ush_utils.h / ush/src/utils.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_utils.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/utils.c`

## 7.1 stderr 出力

【実装用（貼り付け可）: ush/include/ush_utils.h】

```c
#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
void ush_perrorf(const char *context);  // ush: <context>: <strerror>
```

## 7.2 空白/コメント判定

【実装用（貼り付け可）: ush/include/ush_utils.h】

```c
int ush_is_blank_line(const char *line);
int ush_is_comment_line(const char *line);
```

- 空白類はスペースとタブを対象
- コメント行は「最初の非空白文字が `#`」

## 7.3 文字列ユーティリティ

【実装用（貼り付け可）: ush/include/ush_utils.h】

```c
int ush_starts_with(const char *s, const char *prefix);
```

---

# 8. prompt（ush/include/ush_prompt.h / ush/src/prompt.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_prompt.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/prompt.c`

## 8.1 API

【実装用（貼り付け可）: ush/include/ush_prompt.h】

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

# 9. line editor（ush/include/ush_lineedit.h / ush/src/lineedit.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_lineedit.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/lineedit.c`

## 9.1 API

【実装用（貼り付け可）: ush/include/ush_lineedit.h】

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
- Tab（`\t`）: コマンド名補完（先頭トークンのみ）
  - 候補が1つなら、その候補で確定補完（バッファ書き換え＋再描画）
  - 候補が複数なら、共通プレフィックスが伸びる分だけ補完
  - それ以上決められない場合は、改行して候補一覧を表示→プロンプト＋入力行を再描画
- ESC シーケンス:
  - `ESC [ A` : 履歴↑
  - `ESC [ B` : 履歴↓
  - `ESC [ C` : 右
  - `ESC [ D` : 左
  - `ESC [ 3 ~` : Delete

履歴:
- 確定した非空行のみ保存

---

# 10. tokenize（ush/include/ush_tokenize.h / ush/src/tokenize.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_tokenize.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/tokenize.c`

## 10.1 トークン型

【実装用（貼り付け可）: ush/include/ush_tokenize.h】

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

# 11. parse（ush/include/ush_parse.h / ush/src/parse.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_parse.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/parse.c`

## 11.1 内部表現

【実装用（貼り付け可）: ush/include/ush_parse.h】

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

# 12. builtins（ush/include/ush_builtins.h / ush/src/builtins.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_builtins.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/builtins.c`

## 12.1 API

【実装用（貼り付け可）: ush/include/ush_builtins.h】

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

# 13. env（ush/include/ush_env.h / ush/src/env.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_env.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/env.c`

## 13.1 PATH 取得

【実装用（貼り付け可）: ush/include/ush_env.h】

```c
#pragma once

const char *ush_get_path_or_default(void);
```

- `getenv("PATH")` が NULL または空文字なら `"/umu_bin:/sbin:/bin"`

---

# 14. exec（ush/include/ush_exec.h / ush/src/exec.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/include/ush_exec.h` / `/home/tama/umu_project/ush-0.0.2/ush/src/exec.c`

## 14.1 API

【実装用（貼り付け可）: ush/include/ush_exec.h】

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

# 15. main（ush/src/main.c）

【説明】
- この環境の絶対パス: `/home/tama/umu_project/ush-0.0.2/ush/src/main.c`

## 15.1 エントリ

【実装用（貼り付け可）: ush/src/main.c】

```c
int main(int argc, char **argv);
```

## 15.2 初期化
- `ush_state_t st = {.last_status = 0};`
- 親 SIGINT は無視（`SIG_IGN`）
- `ush_history_t hist = {0};`

## 15.3 ループ（擬似コード）

【参考（擬似コード）: ush/src/main.c】

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
  - Tab によるコマンド名補完が動作する（候補1つ:確定、複数:共通プレフィックス伸長、伸長不可:候補一覧表示→再描画）

---

# 付録A: 完全版コード（*.h / 貼り付け可）

【説明】
- 以下は **貼り付け用の“完全版”** として、ファイル単位でそのまま使える形をまとめたもの。
- 本文中に同名ファイルの断片があっても、迷ったら **この付録の内容を正** とする（実装者がコピペしてビルドが通ることを優先）。

## A.1 ush/include/ush_limits.h

【実装用（貼り付け可）: ush/include/ush_limits.h】

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

## A.2 ush/include/ush_err.h

【実装用（貼り付け可）: ush/include/ush_err.h】

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

## A.3 ush/include/ush.h

【実装用（貼り付け可）: ush/include/ush.h】

```c
#pragma once

typedef struct {
  int last_status;  // 初期値0
} ush_state_t;
```

## A.4 ush/include/ush_utils.h

【実装用（貼り付け可）: ush/include/ush_utils.h】

```c
#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
void ush_perrorf(const char *context);  // ush: <context>: <strerror>

int ush_is_blank_line(const char *line);
int ush_is_comment_line(const char *line);

int ush_starts_with(const char *s, const char *prefix);
```

## A.5 ush/include/ush_prompt.h

【実装用（貼り付け可）: ush/include/ush_prompt.h】

```c
#pragma once
#include <stddef.h>

// out は NUL 終端される
int ush_prompt_render(char *out, size_t out_cap);
```

## A.6 ush/include/ush_lineedit.h

【実装用（貼り付け可）: ush/include/ush_lineedit.h】

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

## A.7 ush/include/ush_tokenize.h

【実装用（貼り付け可）: ush/include/ush_tokenize.h】

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

## A.8 ush/include/ush_parse.h

【実装用（貼り付け可）: ush/include/ush_parse.h】

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

## A.9 ush/include/ush_builtins.h

【実装用（貼り付け可）: ush/include/ush_builtins.h】

```c
#pragma once
#include "ush.h"
#include "ush_parse.h"

int ush_is_builtin(const char *cmd);
int ush_run_builtin(ush_state_t *st, char *argv[]);
```

## A.10 ush/include/ush_env.h

【実装用（貼り付け可）: ush/include/ush_env.h】

```c
#pragma once

const char *ush_get_path_or_default(void);
```

## A.11 ush/include/ush_exec.h

【実装用（貼り付け可）: ush/include/ush_exec.h】

```c
#pragma once
#include "ush.h"
#include "ush_parse.h"

int ush_exec_pipeline(ush_state_t *st, const ush_pipeline_t *pl);
```

---

# 付録B: 完全版コード（*.c / 貼り付け可）

【説明】
- 以下は `musl-gcc -static` で **ビルド確認済み** の `*.c` 完全版。
- 「対話(TTY)」だけでなく、`</dev/null` のような **stdin 非TTY** でも `tcgetattr` エラーにならないように、`lineedit` はフォールバック実装を含む。

## B.1 ush/src/utils.c

【実装用（貼り付け可）: ush/src/utils.c】

```c
#include "ush_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

void ush_eprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

void ush_perrorf(const char *context) {
  int saved = errno;
  ush_eprintf("ush: %s: %s\n", context, strerror(saved));
}

int ush_is_blank_line(const char *line) {
  if (line == NULL) return 1;
  for (const char *p = line; *p; p++) {
    if (*p != ' ' && *p != '\t') return 0;
  }
  return 1;
}

int ush_is_comment_line(const char *line) {
  if (line == NULL) return 0;
  const char *p = line;
  while (*p == ' ' || *p == '\t') p++;
  return *p == '#';
}

int ush_starts_with(const char *s, const char *prefix) {
  if (s == NULL || prefix == NULL) return 0;
  while (*prefix) {
    if (*s != *prefix) return 0;
    s++;
    prefix++;
  }
  return 1;
}
```

## B.2 ush/src/env.c

【実装用（貼り付け可）: ush/src/env.c】

```c
#include "ush_env.h"

#include <stdlib.h>

const char *ush_get_path_or_default(void) {
  const char *p = getenv("PATH");
  if (p == NULL || p[0] == '\0') return "/umu_bin:/sbin:/bin";
  return p;
}
```

## B.3 ush/src/prompt.c

【実装用（貼り付け可）: ush/src/prompt.c】

```c
#include "ush_prompt.h"

#include <errno.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *ush_prompt_default(void) {
  return "\\u@UmuOS:ush:\\w\\$ ";
}

static const char *ush_get_prompt_template(void) {
  const char *p = getenv("USH_PS1");
  if (p != NULL && p[0] != '\0') return p;
  p = getenv("PS1");
  if (p != NULL && p[0] != '\0') return p;
  return ush_prompt_default();
}

static const char *ush_get_user(void) {
  const char *u = getenv("USER");
  if (u != NULL && u[0] != '\0') return u;

  struct passwd *pw = getpwuid(getuid());
  if (pw != NULL && pw->pw_name != NULL) return pw->pw_name;
  return "?";
}

static int ush_append(char *out, size_t cap, size_t *io_len, const char *s) {
  if (cap == 0) return 1;
  while (*s) {
    if (*io_len + 1 >= cap) {
      out[cap - 1] = '\0';
      return 1;
    }
    out[*io_len] = *s;
    (*io_len)++;
    s++;
  }
  out[*io_len] = '\0';
  return 0;
}

static int ush_append_ch(char *out, size_t cap, size_t *io_len, char ch) {
  if (cap == 0) return 1;
  if (*io_len + 1 >= cap) {
    out[cap - 1] = '\0';
    return 1;
  }
  out[*io_len] = ch;
  (*io_len)++;
  out[*io_len] = '\0';
  return 0;
}

static int ush_render_w(char *out, size_t cap, size_t *io_len) {
  char cwd[4096];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    return ush_append(out, cap, io_len, "?");
  }

  const char *home = getenv("HOME");
  if (home != NULL && home[0] != '\0') {
    size_t home_len = strlen(home);
    if (strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
      if (cwd[home_len] == '\0') {
        return ush_append(out, cap, io_len, "~");
      }
      int t = ush_append(out, cap, io_len, "~");
      if (t) return 1;
      return ush_append(out, cap, io_len, cwd + home_len);
    }
  }

  return ush_append(out, cap, io_len, cwd);
}

int ush_prompt_render(char *out, size_t out_cap) {
  if (out == NULL || out_cap == 0) return 1;

  const char *tmpl = ush_get_prompt_template();
  size_t len = 0;
  out[0] = '\0';

  int truncated = 0;
  for (size_t i = 0; tmpl[i] != '\0'; i++) {
    if (tmpl[i] != '\\') {
      truncated |= ush_append_ch(out, out_cap, &len, tmpl[i]);
      continue;
    }

    char next = tmpl[i + 1];
    if (next == '\0') {
      truncated |= ush_append_ch(out, out_cap, &len, '\\');
      break;
    }

    i++;
    switch (next) {
      case 'u':
        truncated |= ush_append(out, out_cap, &len, ush_get_user());
        break;
      case 'w':
        truncated |= ush_render_w(out, out_cap, &len);
        break;
      case '$':
        truncated |= ush_append_ch(out, out_cap, &len, (geteuid() == 0) ? '#' : '$');
        break;
      case '\\':
        truncated |= ush_append_ch(out, out_cap, &len, '\\');
        break;
      default:
        truncated |= ush_append_ch(out, out_cap, &len, '\\');
        truncated |= ush_append_ch(out, out_cap, &len, next);
        break;
    }
  }

  return truncated ? 1 : 0;
}
```

## B.4 ush/src/lineedit.c

【実装用（貼り付け可）: ush/src/lineedit.c】

```c
#include "ush_lineedit.h"

#include "ush_env.h"
#include "ush_utils.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int ush_write_all(int fd, const char *buf, size_t len);
static int ush_redraw(const char *prompt, const char *buf, size_t len, size_t cursor);

static int is_space_ch(char c) {
  return c == ' ' || c == '\t';
}

enum {
  USH_TAB_CAND_MAX = 256,
};

static int cand_add_unique(char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1], int *io_n, const char *name) {
  if (io_n == NULL || name == NULL) return 1;
  if (*io_n >= USH_TAB_CAND_MAX) return 1;
  for (int i = 0; i < *io_n; i++) {
    if (strcmp(cands[i], name) == 0) return 0;
  }
  strncpy(cands[*io_n], name, USH_MAX_TOKEN_LEN);
  cands[*io_n][USH_MAX_TOKEN_LEN] = '\0';
  (*io_n)++;
  return 0;
}

static size_t common_prefix_len(char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1], int n) {
  if (n <= 0) return 0;
  size_t l = strlen(cands[0]);
  for (int i = 1; i < n; i++) {
    size_t li = strlen(cands[i]);
    if (li < l) l = li;
  }
  for (size_t k = 0; k < l; k++) {
    char ch = cands[0][k];
    for (int i = 1; i < n; i++) {
      if (cands[i][k] != ch) return k;
    }
  }
  return l;
}

static int is_executable_in_dir(const char *dir, const char *name) {
  if (dir == NULL || name == NULL) return 0;

  char full[8192];
  if (snprintf(full, sizeof(full), "%s/%s", dir, name) >= (int)sizeof(full)) {
    return 0;
  }

  struct stat st;
  if (stat(full, &st) != 0) return 0;
  if (S_ISDIR(st.st_mode)) return 0;
  return access(full, X_OK) == 0;
}

static int find_cmd_token_region(const char *buf, size_t len, size_t cursor, size_t *out_start, size_t *out_end) {
  if (buf == NULL || out_start == NULL || out_end == NULL) return 1;

  size_t i = 0;
  while (i < len && is_space_ch(buf[i])) i++;
  if (i >= len) return 1;  // empty or only spaces

  size_t start = i;
  size_t end = start;
  while (end < len && !is_space_ch(buf[end])) end++;

  // Only complete the first token (command name), and only when cursor is at end of that token.
  if (cursor != end) return 1;

  *out_start = start;
  *out_end = end;
  return 0;
}

static int buf_replace_range(char *buf, size_t buf_cap, size_t *io_len, size_t *io_cursor,
                             size_t start, size_t end, const char *insert) {
  if (buf == NULL || io_len == NULL || io_cursor == NULL || insert == NULL) return 1;
  if (start > end || end > *io_len) return 1;

  size_t ins_len = strlen(insert);
  size_t old_len = end - start;

  if (*io_len - old_len + ins_len + 1 > buf_cap) return 1;

  if (ins_len != old_len) {
    memmove(buf + start + ins_len, buf + end, *io_len - end);
  }
  memcpy(buf + start, insert, ins_len);
  *io_len = *io_len - old_len + ins_len;
  buf[*io_len] = '\0';

  *io_cursor = start + ins_len;
  return 0;
}

static void gather_cmd_candidates(const char *prefix, char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1], int *out_n) {
  if (out_n == NULL) return;
  *out_n = 0;
  if (prefix == NULL) prefix = "";

  // builtins
  static const char *builtins[] = {"cd", "pwd", "export", "exit", "help"};
  for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
    if (ush_starts_with(builtins[i], prefix)) {
      (void)cand_add_unique(cands, out_n, builtins[i]);
    }
  }

  // PATH executables
  const char *path = ush_get_path_or_default();
  const char *seg = path;
  while (1) {
    const char *colon = strchr(seg, ':');
    size_t seg_len = colon ? (size_t)(colon - seg) : strlen(seg);

    char dir[4096];
    if (seg_len == 0) {
      strcpy(dir, ".");
    } else {
      if (seg_len >= sizeof(dir)) seg_len = sizeof(dir) - 1;
      memcpy(dir, seg, seg_len);
      dir[seg_len] = '\0';
    }

    DIR *d = opendir(dir);
    if (d != NULL) {
      for (;;) {
        struct dirent *ent = readdir(d);
        if (ent == NULL) break;
        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (name[0] == '.' && prefix[0] != '.') continue;

        if (!ush_starts_with(name, prefix)) continue;
        if (!is_executable_in_dir(dir, name)) continue;
        if (*out_n >= USH_TAB_CAND_MAX) break;
        (void)cand_add_unique(cands, out_n, name);
      }
      closedir(d);
    }

    if (!colon) break;
    seg = colon + 1;
  }
}

static void handle_tab_completion(const char *prompt, char *buf, size_t buf_cap, size_t *io_len, size_t *io_cursor) {
  size_t start = 0;
  size_t end = 0;
  if (find_cmd_token_region(buf, *io_len, *io_cursor, &start, &end) != 0) {
    return;
  }

  size_t token_len = end - start;
  if (token_len > (size_t)USH_MAX_TOKEN_LEN) token_len = (size_t)USH_MAX_TOKEN_LEN;

  char prefix[USH_MAX_TOKEN_LEN + 1];
  memcpy(prefix, buf + start, token_len);
  prefix[token_len] = '\0';

  char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1];
  int n = 0;
  gather_cmd_candidates(prefix, cands, &n);
  if (n <= 0) return;

  if (n == 1) {
    (void)buf_replace_range(buf, buf_cap, io_len, io_cursor, start, end, cands[0]);
    (void)ush_redraw(prompt, buf, *io_len, *io_cursor);
    return;
  }

  size_t cpl = common_prefix_len(cands, n);
  if (cpl > strlen(prefix)) {
    char tmp[USH_MAX_TOKEN_LEN + 1];
    if (cpl > (size_t)USH_MAX_TOKEN_LEN) cpl = (size_t)USH_MAX_TOKEN_LEN;
    memcpy(tmp, cands[0], cpl);
    tmp[cpl] = '\0';
    (void)buf_replace_range(buf, buf_cap, io_len, io_cursor, start, end, tmp);
    (void)ush_redraw(prompt, buf, *io_len, *io_cursor);
    return;
  }

  // No further extension: list candidates then redraw.
  (void)ush_write_all(STDOUT_FILENO, "\n", 1);
  for (int i = 0; i < n; i++) {
    (void)ush_write_all(STDOUT_FILENO, cands[i], strlen(cands[i]));
    (void)ush_write_all(STDOUT_FILENO, "\n", 1);
  }
  (void)ush_redraw(prompt, buf, *io_len, *io_cursor);
}

static int ush_write_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t w = write(fd, buf, len);
    if (w < 0) {
      if (errno == EINTR) continue;
      return 1;
    }
    buf += (size_t)w;
    len -= (size_t)w;
  }
  return 0;
}

static int ush_redraw(const char *prompt, const char *buf, size_t len, size_t cursor) {
  char seq[64];

  if (ush_write_all(STDOUT_FILENO, "\r", 1)) return 1;
  if (ush_write_all(STDOUT_FILENO, prompt, strlen(prompt))) return 1;
  if (ush_write_all(STDOUT_FILENO, buf, len)) return 1;
  if (ush_write_all(STDOUT_FILENO, "\x1b[K", 3)) return 1;

  size_t tail = len - cursor;
  if (tail > 0) {
    snprintf(seq, sizeof(seq), "\x1b[%zuD", tail);
    if (ush_write_all(STDOUT_FILENO, seq, strlen(seq))) return 1;
  }
  return 0;
}

static int ush_read_byte(unsigned char *out_ch) {
  for (;;) {
    ssize_t r = read(STDIN_FILENO, out_ch, 1);
    if (r == 1) return 0;
    if (r == 0) return 1;
    if (errno == EINTR) continue;
    return 1;
  }
}

static void ush_hist_push(ush_history_t *hist, const char *line) {
  if (hist == NULL) return;
  if (line == NULL || line[0] == '\0') return;

  if (hist->count < USH_HISTORY_MAX) {
    strncpy(hist->items[hist->count], line, USH_MAX_LINE_LEN);
    hist->items[hist->count][USH_MAX_LINE_LEN] = '\0';
    hist->count++;
  } else {
    for (int i = 1; i < USH_HISTORY_MAX; i++) {
      strcpy(hist->items[i - 1], hist->items[i]);
    }
    strncpy(hist->items[USH_HISTORY_MAX - 1], line, USH_MAX_LINE_LEN);
    hist->items[USH_HISTORY_MAX - 1][USH_MAX_LINE_LEN] = '\0';
  }
  hist->cursor = hist->count;
}

int ush_lineedit_readline(
  const char *prompt,
  char *out_line,
  size_t out_cap,
  ush_history_t *hist,
  int last_status
) {
  (void)last_status;

  if (out_line == NULL || out_cap == 0) return 1;
  out_line[0] = '\0';

  struct termios orig;
  if (tcgetattr(STDIN_FILENO, &orig) != 0) {
    // stdin が tty でない場合は、最小の「非対話」フォールバックで読む。
    // これにより `</dev/null` 等でもエラー出力せず EOF として扱える。
    if (errno == ENOTTY) {
      if (prompt != NULL) {
        (void)ush_write_all(STDOUT_FILENO, prompt, strlen(prompt));
      }
      fflush(stdout);

      if (fgets(out_line, (int)out_cap, stdin) == NULL) {
        return 1;
      }
      size_t n = strlen(out_line);
      while (n > 0 && (out_line[n - 1] == '\n' || out_line[n - 1] == '\r')) {
        out_line[--n] = '\0';
      }
      if (hist != NULL && out_line[0] != '\0') {
        ush_hist_push(hist, out_line);
      }
      return 0;
    }

    ush_perrorf("tcgetattr");
    return 1;
  }

  struct termios raw = orig;
  raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
    ush_perrorf("tcsetattr");
    return 1;
  }

  char buf[USH_MAX_LINE_LEN + 1];
  size_t len = 0;
  size_t cursor = 0;
  buf[0] = '\0';

  char saved[USH_MAX_LINE_LEN + 1];
  int saved_valid = 0;

  if (hist != NULL) hist->cursor = hist->count;

  (void)ush_redraw(prompt, buf, len, cursor);

  int eof = 0;
  for (;;) {
    unsigned char ch = 0;
    if (ush_read_byte(&ch)) {
      eof = 1;
      break;
    }

    if (ch == '\t') {
      handle_tab_completion(prompt, buf, sizeof(buf), &len, &cursor);
      continue;
    }

    if (ch == '\r' || ch == '\n') {
      ush_write_all(STDOUT_FILENO, "\n", 1);
      break;
    }

    if (ch == 0x04) {
      if (len == 0) {
        eof = 1;
        break;
      }
      continue;
    }

    if (ch == 0x08 || ch == 0x7f) {
      if (cursor > 0) {
        memmove(buf + cursor - 1, buf + cursor, len - cursor);
        len--;
        cursor--;
        buf[len] = '\0';
        ush_redraw(prompt, buf, len, cursor);
      }
      continue;
    }

    if (ch == 0x1b) {
      unsigned char a = 0;
      if (ush_read_byte(&a)) continue;
      if (a != '[') continue;

      unsigned char b = 0;
      if (ush_read_byte(&b)) continue;

      if (b == 'A') {
        if (hist != NULL && hist->count > 0 && hist->cursor > 0) {
          if (hist->cursor == hist->count && !saved_valid) {
            strncpy(saved, buf, sizeof(saved) - 1);
            saved[sizeof(saved) - 1] = '\0';
            saved_valid = 1;
          }
          hist->cursor--;
          strncpy(buf, hist->items[hist->cursor], sizeof(buf) - 1);
          buf[sizeof(buf) - 1] = '\0';
          len = strlen(buf);
          cursor = len;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == 'B') {
        if (hist != NULL && hist->count > 0 && hist->cursor < hist->count) {
          hist->cursor++;
          if (hist->cursor == hist->count) {
            if (saved_valid) {
              strncpy(buf, saved, sizeof(buf) - 1);
              buf[sizeof(buf) - 1] = '\0';
            } else {
              buf[0] = '\0';
            }
          } else {
            strncpy(buf, hist->items[hist->cursor], sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
          }
          len = strlen(buf);
          cursor = len;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == 'C') {
        if (cursor < len) {
          cursor++;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == 'D') {
        if (cursor > 0) {
          cursor--;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == '3') {
        unsigned char t = 0;
        if (ush_read_byte(&t)) continue;
        if (t == '~') {
          if (cursor < len) {
            memmove(buf + cursor, buf + cursor + 1, len - cursor - 1);
            len--;
            buf[len] = '\0';
            ush_redraw(prompt, buf, len, cursor);
          }
        }
        continue;
      }

      continue;
    }

    if (ch < 0x20 || ch == 0x7f) {
      continue;
    }

    if (len + 1 >= sizeof(buf) || len + 1 >= out_cap) {
      continue;
    }

    if (cursor == len) {
      buf[len] = (char)ch;
      len++;
      buf[len] = '\0';
      cursor = len;
    } else {
      memmove(buf + cursor + 1, buf + cursor, len - cursor);
      buf[cursor] = (char)ch;
      len++;
      cursor++;
      buf[len] = '\0';
    }
    ush_redraw(prompt, buf, len, cursor);
  }

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

  if (eof) return 1;

  strncpy(out_line, buf, out_cap - 1);
  out_line[out_cap - 1] = '\0';

  if (hist != NULL && out_line[0] != '\0') {
    ush_hist_push(hist, out_line);
  }

  return 0;
}
```

## B.5 ush/src/tokenize.c

【実装用（貼り付け可）: ush/src/tokenize.c】

```c
#include "ush_tokenize.h"

#include "ush_utils.h"

#include <string.h>

static int is_space(char c) {
  return c == ' ' || c == '\t';
}

static int is_op_char(char c) {
  return c == '|' || c == '<' || c == '>';
}

static int is_unsupported_char(char c) {
  switch (c) {
    case '"':
    case '\\':
    case '$':
    case '*':
    case '?':
    case '[':
    case ']':
    case ';':
    case '&':
      return 1;
    default:
      return 0;
  }
}

static parse_result_t push_tok(token_t out_tokens[USH_MAX_TOKENS], int *io_ntok, token_kind_t kind, const char *text) {
  if (*io_ntok >= USH_MAX_TOKENS) return PARSE_TOO_MANY_TOKENS;
  out_tokens[*io_ntok].kind = kind;
  out_tokens[*io_ntok].text = text;
  (*io_ntok)++;
  return PARSE_OK;
}

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
) {
  if (out_ntok == NULL || out_tokens == NULL || out_buf == NULL) return PARSE_UNSUPPORTED;
  *out_ntok = 0;
  out_buf[0] = '\0';

  if (line == NULL) return PARSE_EMPTY;
  if (ush_is_blank_line(line)) return PARSE_EMPTY;
  if (ush_is_comment_line(line)) return PARSE_EMPTY;

  const size_t buf_cap = (size_t)USH_MAX_LINE_LEN + 1;
  size_t bi = 0;
  size_t i = 0;

  while (line[i] != '\0') {
    while (is_space(line[i])) i++;
    if (line[i] == '\0') break;

    char c = line[i];

    if (c == '|') {
      if (line[i + 1] == '|') return PARSE_UNSUPPORTED;
      parse_result_t r = push_tok(out_tokens, out_ntok, TOK_PIPE, NULL);
      if (r != PARSE_OK) return r;
      i++;
      continue;
    }

    if (c == '<') {
      parse_result_t r = push_tok(out_tokens, out_ntok, TOK_REDIR_IN, NULL);
      if (r != PARSE_OK) return r;
      i++;
      continue;
    }

    if (c == '>') {
      if (line[i + 1] == '>') {
        parse_result_t r = push_tok(out_tokens, out_ntok, TOK_REDIR_APPEND, NULL);
        if (r != PARSE_OK) return r;
        i += 2;
      } else {
        parse_result_t r = push_tok(out_tokens, out_ntok, TOK_REDIR_OUT, NULL);
        if (r != PARSE_OK) return r;
        i++;
      }
      continue;
    }

    if (c == '\'') {
      i++; // skip opening
      size_t start = bi;
      size_t wlen = 0;
      while (line[i] != '\0' && line[i] != '\'') {
        if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
        if (wlen + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = line[i++];
        wlen++;
      }
      if (line[i] != '\'') return PARSE_UNSUPPORTED;
      i++; // closing
      if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
      out_buf[bi++] = '\0';
      parse_result_t r = push_tok(out_tokens, out_ntok, TOK_WORD, &out_buf[start]);
      if (r != PARSE_OK) return r;
      continue;
    }

    // word (unquoted)
    size_t start = bi;
    size_t wlen = 0;
    while (line[i] != '\0' && !is_space(line[i]) && !is_op_char(line[i]) && line[i] != '\'') {
      char ch = line[i];
      if (is_unsupported_char(ch)) return PARSE_UNSUPPORTED;
      if (ch == '|' && line[i + 1] == '|') return PARSE_UNSUPPORTED;
      if (ch == '&' && line[i + 1] == '&') return PARSE_UNSUPPORTED;

      if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
      if (wlen + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;

      out_buf[bi++] = ch;
      wlen++;
      i++;
    }

    if (wlen == 0) return PARSE_SYNTAX_ERROR;
    if (bi + 1 >= buf_cap) return PARSE_TOO_LONG;
    out_buf[bi++] = '\0';
    parse_result_t r = push_tok(out_tokens, out_ntok, TOK_WORD, &out_buf[start]);
    if (r != PARSE_OK) return r;
  }

  return (*out_ntok == 0) ? PARSE_EMPTY : PARSE_OK;
}
```

## B.6 ush/src/parse.c

【実装用（貼り付け可）: ush/src/parse.c】

```c
#include "ush_parse.h"

#include <string.h>

static void init_pipeline(ush_pipeline_t *pl) {
  memset(pl, 0, sizeof(*pl));
  pl->ncmd = 1;
  for (int i = 0; i < USH_MAX_CMDS; i++) {
    pl->cmds[i].argc = 0;
    pl->cmds[i].argv[0] = NULL;
  }
}

parse_result_t ush_parse_pipeline(
  const token_t *toks,
  int ntok,
  ush_pipeline_t *out_pl
) {
  if (toks == NULL || out_pl == NULL) return PARSE_UNSUPPORTED;
  if (ntok <= 0) return PARSE_EMPTY;

  init_pipeline(out_pl);

  int cmd_i = 0;

  for (int i = 0; i < ntok; i++) {
    token_kind_t k = toks[i].kind;

    if (k == TOK_WORD) {
      ush_cmd_t *cmd = &out_pl->cmds[cmd_i];
      if (cmd->argc >= USH_MAX_ARGS) return PARSE_TOO_MANY_ARGS;
      cmd->argv[cmd->argc++] = (char *)toks[i].text;
      continue;
    }

    if (k == TOK_PIPE) {
      if (out_pl->out_path != NULL) return PARSE_SYNTAX_ERROR;
      if (out_pl->cmds[cmd_i].argc == 0) return PARSE_SYNTAX_ERROR;
      if (cmd_i + 1 >= USH_MAX_CMDS) return PARSE_SYNTAX_ERROR;
      cmd_i++;
      out_pl->ncmd = cmd_i + 1;
      continue;
    }

    if (k == TOK_REDIR_IN) {
      if (out_pl->in_path != NULL) return PARSE_SYNTAX_ERROR;
      if (cmd_i != 0) return PARSE_SYNTAX_ERROR;
      if (i + 1 >= ntok) return PARSE_SYNTAX_ERROR;
      if (toks[i + 1].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
      out_pl->in_path = toks[i + 1].text;
      i++;
      continue;
    }

    if (k == TOK_REDIR_OUT || k == TOK_REDIR_APPEND) {
      if (out_pl->out_path != NULL) return PARSE_SYNTAX_ERROR;
      if (i + 1 >= ntok) return PARSE_SYNTAX_ERROR;
      if (toks[i + 1].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;
      out_pl->out_path = toks[i + 1].text;
      out_pl->out_append = (k == TOK_REDIR_APPEND) ? 1 : 0;
      i++;
      continue;
    }

    return PARSE_SYNTAX_ERROR;
  }

  if (out_pl->cmds[cmd_i].argc == 0) return PARSE_SYNTAX_ERROR;

  for (int c = 0; c < out_pl->ncmd; c++) {
    out_pl->cmds[c].argv[out_pl->cmds[c].argc] = NULL;
  }

  return PARSE_OK;
}
```

## B.7 ush/src/exec.c

【実装用（貼り付け可）: ush/src/exec.c】

```c
#include "ush_exec.h"

#include "ush_env.h"
#include "ush_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int has_slash(const char *s) {
  for (; *s; s++) {
    if (*s == '/') return 1;
  }
  return 0;
}

static void child_exec_fallback_sh(const char *path, char *const argv[]) {
  // argv_sh = {"/bin/sh", path, argv[1..], NULL}
  char *argv_sh[USH_MAX_ARGS + 3];
  int ai = 0;
  argv_sh[ai++] = (char *)"/bin/sh";
  argv_sh[ai++] = (char *)path;
  for (int i = 1; argv[i] != NULL && ai < (int)(USH_MAX_ARGS + 2); i++) {
    argv_sh[ai++] = argv[i];
  }
  argv_sh[ai] = NULL;
  execve("/bin/sh", argv_sh, environ);
}

static void child_exec_path_or_die(const char *cmd, char *const argv[]) {
  if (has_slash(cmd)) {
    execve(cmd, argv, environ);
    if (errno == ENOEXEC) {
      child_exec_fallback_sh(cmd, argv);
    }
    ush_perrorf(cmd);
    _exit(126);
  }

  const char *path = ush_get_path_or_default();
  int saw_eacces = 0;

  const char *seg = path;
  while (1) {
    const char *colon = strchr(seg, ':');
    size_t seg_len = colon ? (size_t)(colon - seg) : strlen(seg);

    char dir[4096];
    if (seg_len == 0) {
      strcpy(dir, ".");
    } else {
      if (seg_len >= sizeof(dir)) seg_len = sizeof(dir) - 1;
      memcpy(dir, seg, seg_len);
      dir[seg_len] = '\0';
    }

    char full[8192];
    snprintf(full, sizeof(full), "%s/%s", dir, cmd);

    execve(full, argv, environ);
    if (errno == ENOEXEC) {
      child_exec_fallback_sh(full, argv);
    }

    if (errno == EACCES) saw_eacces = 1;

    if (!colon) break;
    seg = colon + 1;
  }

  if (saw_eacces) {
    ush_eprintf("ush: permission denied: %s\n", cmd);
    _exit(126);
  }

  ush_eprintf("ush: command not found: %s\n", cmd);
  _exit(127);
}

int ush_exec_pipeline(ush_state_t *st, const ush_pipeline_t *pl) {
  if (st == NULL || pl == NULL) return 1;

  int in_fd = -1;
  int out_fd = -1;

  if (pl->in_path != NULL) {
    in_fd = open(pl->in_path, O_RDONLY);
    if (in_fd < 0) {
      ush_perrorf("<");
      st->last_status = 1;
      return st->last_status;
    }
  }

  if (pl->out_path != NULL) {
    int flags = O_WRONLY | O_CREAT;
    flags |= pl->out_append ? O_APPEND : O_TRUNC;
    out_fd = open(pl->out_path, flags, 0644);
    if (out_fd < 0) {
      ush_perrorf(pl->out_append ? ">>" : ">");
      if (in_fd >= 0) close(in_fd);
      st->last_status = 1;
      return st->last_status;
    }
  }

  int pipes[USH_MAX_CMDS - 1][2];
  for (int i = 0; i < pl->ncmd - 1; i++) {
    if (pipe(pipes[i]) != 0) {
      ush_perrorf("pipe");
      for (int j = 0; j < i; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      if (in_fd >= 0) close(in_fd);
      if (out_fd >= 0) close(out_fd);
      st->last_status = 1;
      return st->last_status;
    }
  }

  pid_t pids[USH_MAX_CMDS];
  for (int i = 0; i < pl->ncmd; i++) pids[i] = -1;

  for (int i = 0; i < pl->ncmd; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      ush_perrorf("fork");
      st->last_status = 1;
      // cleanup
      for (int j = 0; j < pl->ncmd - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      if (in_fd >= 0) close(in_fd);
      if (out_fd >= 0) close(out_fd);
      return st->last_status;
    }

    if (pid == 0) {
      signal(SIGINT, SIG_DFL);

      if (i == 0) {
        if (in_fd >= 0) {
          dup2(in_fd, STDIN_FILENO);
        }
      } else {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }

      if (i == pl->ncmd - 1) {
        if (out_fd >= 0) {
          dup2(out_fd, STDOUT_FILENO);
        }
      } else {
        dup2(pipes[i][1], STDOUT_FILENO);
      }

      for (int j = 0; j < pl->ncmd - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      if (in_fd >= 0) close(in_fd);
      if (out_fd >= 0) close(out_fd);

      char *cmd = pl->cmds[i].argv[0];
      if (cmd == NULL) _exit(127);
      child_exec_path_or_die(cmd, pl->cmds[i].argv);
      _exit(127);
    }

    pids[i] = pid;
  }

  for (int j = 0; j < pl->ncmd - 1; j++) {
    close(pipes[j][0]);
    close(pipes[j][1]);
  }
  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  int last_status = 0;
  pid_t last_pid = pids[pl->ncmd - 1];

  for (int i = 0; i < pl->ncmd; i++) {
    int stw = 0;
    if (waitpid(pids[i], &stw, 0) < 0) {
      ush_perrorf("waitpid");
      continue;
    }
    if (pids[i] == last_pid) {
      if (WIFEXITED(stw)) {
        last_status = WEXITSTATUS(stw);
      } else if (WIFSIGNALED(stw)) {
        last_status = 128 + WTERMSIG(stw);
      } else {
        last_status = 1;
      }
    }
  }

  st->last_status = last_status;
  return st->last_status;
}
```

## B.8 ush/src/builtins.c

【実装用（貼り付け可）: ush/src/builtins.c】

```c
#include "ush_builtins.h"

#include "ush_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static int argv_count(char *argv[]) {
  int c = 0;
  while (argv != NULL && argv[c] != NULL) c++;
  return c;
}

static int is_valid_name(const char *s) {
  if (s == NULL || s[0] == '\0') return 0;
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
  for (size_t i = 1; s[i] != '\0'; i++) {
    if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
  }
  return 1;
}

static int bi_cd(char *argv[]) {
  int argc = argv_count(argv);
  if (argc == 1) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') home = "/";

    char oldpwd[4096];
    if (getcwd(oldpwd, sizeof(oldpwd)) == NULL) oldpwd[0] = '\0';

    if (chdir(home) != 0) {
      ush_perrorf("cd");
      return 1;
    }

    char newpwd[4096];
    if (getcwd(newpwd, sizeof(newpwd)) != NULL) {
      if (oldpwd[0] != '\0') setenv("OLDPWD", oldpwd, 1);
      setenv("PWD", newpwd, 1);
    }

    return 0;
  }

  if (argc == 2) {
    if (strcmp(argv[1], "-") == 0) {
      ush_eprintf("ush: unsupported syntax\n");
      return 2;
    }

    char oldpwd[4096];
    if (getcwd(oldpwd, sizeof(oldpwd)) == NULL) oldpwd[0] = '\0';

    if (chdir(argv[1]) != 0) {
      ush_perrorf("cd");
      return 1;
    }

    char newpwd[4096];
    if (getcwd(newpwd, sizeof(newpwd)) != NULL) {
      if (oldpwd[0] != '\0') setenv("OLDPWD", oldpwd, 1);
      setenv("PWD", newpwd, 1);
    }

    return 0;
  }

  ush_eprintf("ush: cd: invalid args\n");
  return 2;
}

static int bi_pwd(char *argv[]) {
  int argc = argv_count(argv);
  if (argc != 1) {
    ush_eprintf("ush: pwd: invalid args\n");
    return 2;
  }

  char cwd[4096];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    ush_perrorf("pwd");
    return 1;
  }

  printf("%s\n", cwd);
  return 0;
}

static int bi_export(char *argv[]) {
  int argc = argv_count(argv);
  if (argc == 1) {
    for (char **p = environ; p != NULL && *p != NULL; p++) {
      puts(*p);
    }
    return 0;
  }

  if (argc == 2) {
    char *arg = argv[1];
    char *eq = strchr(arg, '=');

    if (eq != NULL) {
      *eq = '\0';
      const char *name = arg;
      const char *value = eq + 1;
      if (!is_valid_name(name)) {
        *eq = '=';
        ush_eprintf("ush: export: invalid name\n");
        return 2;
      }
      int r = setenv(name, value, 1);
      *eq = '=';
      if (r != 0) {
        ush_perrorf("export");
        return 1;
      }
      return 0;
    }

    const char *name = arg;
    if (!is_valid_name(name)) {
      ush_eprintf("ush: export: invalid name\n");
      return 2;
    }

    if (getenv(name) == NULL) {
      if (setenv(name, "", 1) != 0) {
        ush_perrorf("export");
        return 1;
      }
    }

    return 0;
  }

  ush_eprintf("ush: export: invalid args\n");
  return 2;
}

static int bi_exit(ush_state_t *st, char *argv[]) {
  int argc = argv_count(argv);
  if (argc == 1) {
    exit(st->last_status);
  }

  if (argc == 2) {
    char *end = NULL;
    long v = strtol(argv[1], &end, 10);
    if (end == NULL || *end != '\0') {
      ush_eprintf("ush: exit: invalid number\n");
      return 2;
    }
    exit((int)(v & 255));
  }

  ush_eprintf("ush: exit: invalid args\n");
  return 2;
}

static int bi_help(char *argv[]) {
  int argc = argv_count(argv);
  if (argc != 1) {
    ush_eprintf("ush: help: invalid args\n");
    return 2;
  }

  puts("ush 0.0.2 (MVP)\n");
  puts("builtins: cd pwd export exit help");
  puts("features: | < > >>, minimal line editor, PATH search");
  puts("notes:");
  puts("  - unsupported syntax is detected and rejected");
  puts("  - < is only allowed on the first command");
  puts("  - > and >> are only allowed on the last command");
  puts("  - builtins work only as a single command (no pipe/redir)");

  return 0;
}

int ush_is_builtin(const char *cmd) {
  if (cmd == NULL) return 0;
  return strcmp(cmd, "cd") == 0 ||
         strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "export") == 0 ||
         strcmp(cmd, "exit") == 0 ||
         strcmp(cmd, "help") == 0;
}

int ush_run_builtin(ush_state_t *st, char *argv[]) {
  if (argv == NULL || argv[0] == NULL) return 2;

  if (strcmp(argv[0], "cd") == 0) return bi_cd(argv);
  if (strcmp(argv[0], "pwd") == 0) return bi_pwd(argv);
  if (strcmp(argv[0], "export") == 0) return bi_export(argv);
  if (strcmp(argv[0], "exit") == 0) return bi_exit(st, argv);
  if (strcmp(argv[0], "help") == 0) return bi_help(argv);

  return 2;
}
```

## B.9 ush/src/main.c

【実装用（貼り付け可）: ush/src/main.c】

```c
#include "ush.h"

#include "ush_builtins.h"
#include "ush_err.h"
#include "ush_exec.h"
#include "ush_lineedit.h"
#include "ush_parse.h"
#include "ush_prompt.h"
#include "ush_tokenize.h"
#include "ush_utils.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static void print_parse_error(parse_result_t r) {
  switch (r) {
    case PARSE_EMPTY:
      return;
    case PARSE_TOO_LONG:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_TOO_MANY_TOKENS:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_TOO_MANY_ARGS:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_UNSUPPORTED:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_SYNTAX_ERROR:
      ush_eprintf("ush: unsupported syntax\n");
      return;
    case PARSE_OK:
    default:
      return;
  }
}

static int any_builtin_in_pipeline(const ush_pipeline_t *pl) {
  for (int i = 0; i < pl->ncmd; i++) {
    const char *cmd = pl->cmds[i].argv[0];
    if (cmd != NULL && ush_is_builtin(cmd)) return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  ush_state_t st = {.last_status = 0};
  signal(SIGINT, SIG_IGN);
  ush_history_t hist;
  hist.count = 0;
  hist.cursor = 0;

  for (;;) {
    char prompt[256];
    (void)ush_prompt_render(prompt, sizeof(prompt));

    char line[USH_MAX_LINE_LEN + 1];
    int rr = ush_lineedit_readline(prompt, line, sizeof(line), &hist, st.last_status);
    if (rr == 1) exit(st.last_status);

    if (ush_is_blank_line(line) || ush_is_comment_line(line)) continue;

    token_t toks[USH_MAX_TOKENS];
    int ntok = 0;
    char tokbuf[USH_MAX_LINE_LEN + 1];

    parse_result_t tr = ush_tokenize(line, toks, &ntok, tokbuf);
    if (tr != PARSE_OK) {
      if (tr == PARSE_EMPTY) continue;
      print_parse_error(tr);
      st.last_status = 2;
      continue;
    }

    ush_pipeline_t pl;
    parse_result_t pr = ush_parse_pipeline(toks, ntok, &pl);
    if (pr != PARSE_OK) {
      print_parse_error(pr);
      st.last_status = 2;
      continue;
    }

    // builtins
    if (pl.ncmd == 1 && pl.in_path == NULL && pl.out_path == NULL &&
        pl.cmds[0].argv[0] != NULL && ush_is_builtin(pl.cmds[0].argv[0])) {
      st.last_status = ush_run_builtin(&st, pl.cmds[0].argv);
      continue;
    }

    // builtins with pipe/redir are unsupported
    if (any_builtin_in_pipeline(&pl)) {
      ush_eprintf("ush: unsupported syntax\n");
      st.last_status = 2;
      continue;
    }

    st.last_status = ush_exec_pipeline(&st, &pl);
  }
}
```
