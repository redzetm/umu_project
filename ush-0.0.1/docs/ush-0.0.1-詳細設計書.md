# ush-0.0.1-詳細設計書.md
UmuOS User Shell (ush) — 詳細設計書（0.1.0-MVP）  
Target OS: UmuOS-0.1.4-base-stable  

本書は [ush-0.0.1/docs/ush-0.0.1-基本設計書.md](ush-0.0.1/docs/ush-0.0.1-基本設計書.md) をコードレベルに落とし込む。

---

# 1. 前提・設計原則
- 対話専用シェル（POSIX互換は追わない）
- `/bin/sh`（BusyBox）は残し、`ENOEXEC` のときのみ `/bin/sh` にフォールバック
- 静的リンク（musl）前提で、単一バイナリ `/bin/ush` を成果物とする
- 未対応構文は「検出してエラー（方針B）」で誤動作を避ける

---

# 2. ソース構成（ファイル/ディレクトリ）
仕様書のディレクトリ構造に沿って以下を作る（例）。

```
ush/
  include/
    ush.h
    ush_limits.h
    ush_err.h
    ush_parser.h
    ush_exec.h
    ush_builtins.h
    ush_env.h
    ush_utils.h
  src/
    main.c
    parser.c
    exec.c
    builtins.c
    env.c
    utils.c
```

- `include/` に公開ヘッダを集約し、各 `.c` は対応ヘッダを持つ
- 「外部公開API」は ush 内部だけで完結する前提（ライブラリ化はしない）

---

# 3. ビルド手順（開発ホスト）
本MVPは「開発ホストで musl により静的リンクビルドし、単一バイナリを UmuOS に持ち込む」方針とする。

## 3.1 前提
- 開発ホストに `musl-gcc` が存在すること
- 生成物は静的リンク単一バイナリ `ush` とする（動的リンクへのフォールバックは行わない）

## 3.2 ビルドコマンド（例）
ソースツリー `ush/` が存在する前提。

```sh
cd ush
musl-gcc -static -O2 -Wall -Wextra \
  -Iinclude \
  -o ush \
  src/main.c src/parser.c src/exec.c src/builtins.c src/env.c src/utils.c
```

## 3.3 生成物
- `ush/ush`（実行ファイル）

## 3.4 UmuOS への配置
- 生成した `ush` を UmuOS の `/bin/ush` に配置する
- `/bin/sh`（BusyBox）は残す（`ENOEXEC` フォールバックで利用）

---

# 4. 定数・制限値（`ush_limits.h`）
仕様の制限値をコードで固定する。

```c
#pragma once

enum {
  USH_MAX_LINE_LEN  = 8192,
  USH_MAX_ARGS      = 128,
  USH_MAX_TOKEN_LEN = 1024,
};
```

注意:
- `USH_MAX_ARGS` は「引数の個数」であり、`argv` 配列サイズは `USH_MAX_ARGS + 1`（末尾NULL）

---

# 5. エラー/戻り値（`ush_err.h`）
## 4.1 Parser 戻り値

```c
#pragma once

typedef enum {
  PARSE_OK = 0,
  PARSE_EMPTY,          // 空行/空白のみ/コメントのみ等
  PARSE_TOO_LONG,       // 行長 or トークン長超過
  PARSE_TOO_MANY_ARGS,  // argvが上限超過
  PARSE_UNSUPPORTED,    // 未対応記号を検出
} parse_result_t;
```

## 4.2 Exec 戻り値
- `exec_external()` は「実行後に更新された last_status」を返す（0/1/2/126/127 などを含む）

---

# 6. グローバル状態（`ush.h`）
グローバル変数は原則使わず、状態は `struct ush_state` にまとめる。

```c
#pragma once

typedef struct {
  int last_status;  // 初期値0
} ush_state_t;
```

---

# 7. utils（`ush_utils.h` / `utils.c`）
## 6.1 stderr出力

```c
#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
```

実装要件:
- `vfprintf(stderr, ...)` で出力
- 可能なら常に `ush:` 接頭辞を付けるユーティリティも用意:

```c
void ush_perrorf(const char *context); // 例: ush: cd: <strerror>
```

## 6.2 空白判定

```c
int ush_is_blank_line(const char *line);
```

- `\n` を含む/含まない双方を許容
- 空白類はスペースとタブを対象（MVPの仕様上）

## 6.3 行頭コメント判定

```c
int ush_is_comment_line(const char *line);
```

- 最初の非空白文字が `#` なら真

---

# 8. 入力取得（main / utils）
## 7.1 API

```c
// 戻り値: 0=成功, 1=EOF
int ush_read_line(char **line_buf, size_t *cap);
```

要件:
- 実装は `getline()` を使用
- EOFなら 1 を返す
- 取得した行の長さが `USH_MAX_LINE_LEN` を超える場合:
  - `ush: input too long` を出して、その行は破棄（必要なら残りを読み捨て）
  - `last_status=2` を設定できるよう、呼び出し側で扱う

---

# 9. parser（`ush_parser.h` / `parser.c`）
## 8.1 API

```c
#pragma once
#include "ush_err.h"
#include "ush_limits.h"

parse_result_t ush_tokenize_inplace(
  char *line,                 // 入力（破壊的に\0挿入）
  char *argv[USH_MAX_ARGS+1], // 出力 argv
  int *argc_out               // 出力 argc
);
```

## 8.2 アルゴリズム（空白区切り）
- `line` を先頭から走査し、区切り（space/tab）を `\0` にする
- 連続空白はスキップ
- 各トークン開始位置を `argv[argc++]` に格納
- `argv[argc] = NULL` で終端

## 8.3 トークン長チェック
- トークン開始から次の区切りまでの長さが `USH_MAX_TOKEN_LEN` を超えたら `PARSE_TOO_LONG`

## 8.4 引数数チェック
- `argc == USH_MAX_ARGS` を超える場合は `PARSE_TOO_MANY_ARGS`

## 8.5 未対応記号検出（方針B）
未対応記号（仕様書 4.2.3）を含む場合は `PARSE_UNSUPPORTED`。

検出対象文字（1文字検出で十分）:
- クォート: `'` `"`
- バックスラッシュ: `\\`
- 変数展開: `$`
- グロブ: `* ? [ ]`
- 演算子: `| < > ; &`

実装方針:
- 走査中に上記文字が現れた時点で `PARSE_UNSUPPORTED` を返す

## 8.6 コメント行
- `ush_is_comment_line(line)` が真の場合は `PARSE_EMPTY` を返す（実行しない）

---

# 10. builtins（`ush_builtins.h` / `builtins.c`）
## 9.1 API

```c
#pragma once
#include "ush.h"

int ush_is_builtin(const char *cmd);

// 戻り値: 更新後の last_status
int ush_run_builtin(ush_state_t *st, char *argv[]);
```

## 9.2 `cd`
- 仕様:
  - 引数なし: `$HOME` があればそこ、なければ `/`
  - `cd -` は未対応（エラー、last_status=2）
  - 成功時に `PWD` と `OLDPWD` を更新

実装詳細:
- `int ush_builtin_cd(ush_state_t *st, char *argv[])`
- 現在ディレクトリ: `getcwd()`（失敗時は `ush: cd: ...` で `last_status=1`）
- `setenv("OLDPWD", oldpwd, 1)`
- `chdir(target)` の失敗は errno に応じてエラーメッセージ
- 成功後 `getcwd()` → `setenv("PWD", newpwd, 1)`

## 9.3 `exit`
- 仕様:
  - `exit` → `exit(st->last_status)`
  - `exit n` → `exit(n & 255)`
  - 引数が数値でない/多すぎる → エラーとして終了しない（`st->last_status=2`）

実装詳細:
- 数値判定: 先頭 `+`/`-` を許すかは実装で統一（推奨: `strtol` で厳密判定）
- `argv[2] != NULL` なら「引数多すぎ」としてエラー

## 9.4 `help`
- 仕様:
  - builtins 一覧と未対応事項（クォート/パイプ等）を表示

---

# 11. env（`ush_env.h` / `env.c`）
## 10.1 PATH取得

```c
#pragma once

const char *ush_get_path_or_default(void);
```

- `getenv("PATH")` が NULL なら `"/bin:/sbin"`

---

# 12. exec（`ush_exec.h` / `exec.c`）
## 11.1 API

```c
#pragma once
#include "ush.h"

// 戻り値: 更新後の last_status
int ush_exec_external(ush_state_t *st, char *argv[]);
```

## 11.2 PATH探索の判定
- `strchr(argv[0], '/') != NULL` の場合:
  - そのまま `execve(argv[0], argv, environ)` を試行
- そうでない場合:
  - `PATH` を `:` 区切りで走査してフルパスを組み立てて試行

## 11.3 PATH走査でのエラー優先順位
内部変数:
- `int saw_eacces = 0;`

ルール:
- 候補が存在するが `EACCES` などで実行できない場合は `saw_eacces=1` にする
- 最終的に実行できなかった場合:
  - `saw_eacces` が真 → 126
  - それ以外 → 127

## 11.4 fork/execve/waitpid
- 親は `waitpid(pid, &status, 0)`
- `st->last_status` 更新:
  - `WIFEXITED(status)` → `WEXITSTATUS(status)`
  - `WIFSIGNALED(status)` → `128 + WTERMSIG(status)`

## 11.5 SIGINT
- 親（ush）: 起動時に `signal(SIGINT, SIG_IGN)`
- 子:
  - `signal(SIGINT, SIG_DFL)` を設定してから `execve`

## 11.6 ENOEXECフォールバック
- `execve` が `ENOEXEC` の場合のみ:
  - `argv_sh[0] = "/bin/sh"`
  - `argv_sh[1] = path`（実行しようとしたファイル）
  - `argv_sh[2...] = 元の argv[1...]`
  - `execve("/bin/sh", argv_sh, environ)`

---

# 13. プロンプト（main）
## 12.1 形式
- `UmuOS:ush:<cwd>$`

## 12.2 実装
- `getcwd()` で都度取得（`PWD` 環境変数に依存しない）
- 取得失敗時は `<cwd>` を `?` にするなどのフォールバックを決める

---

# 14. main（`main.c`）
## 13.1 エントリ

```c
int main(int argc, char **argv);
```

- 対話専用のため、引数は基本的に無視（将来拡張の余地として残してもよい）

## 13.2 初期化
- `ush_state_t st = {.last_status = 0};`
- `signal(SIGINT, SIG_IGN);`

## 13.3 ループ
- 入力取得→空行/コメントスキップ→トークナイズ→builtin判定→外部実行
- EOFなら `exit(st.last_status)`

---

# 15. 受け入れ基準（コードレベル確認項目）
本詳細設計に沿って実装され、基本設計書の受け入れ基準（仕様全項目）を全て満たすこと。

特にコード観点での最低確認:
- `ush_tokenize_inplace()` が未対応記号を必ず検出し、誤って実行しない
- `ush_exec_external()` が 126/127 の区別を仕様通りに返す
- `ENOEXEC` のときだけ `/bin/sh` フォールバックし、それ以外では行わない
- 親SIGINT無視、子SIGINTデフォルトが確実に設定される
