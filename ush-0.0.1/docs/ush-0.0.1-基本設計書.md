# ush-0.0.1-基本設計書.md
UmuOS User Shell (ush) — 基本設計書（0.1.0-MVP）  
Target OS: UmuOS-0.1.4-base-stable  

---

# 1. 目的
本書は、MVP 仕様（[ush-0.0.1/docs/ush-0.0.1-仕様書.md](ush-0.0.1/docs/ush-0.0.1-仕様書.md)）を実装可能な粒度に落とし込む基本設計を示す。

- `/bin/ush`: ユーザー向け対話シェル（本設計で実装）
- `/bin/sh` : BusyBox sh（サーバーソフト・スクリプト実行用として残す）

---

# 2. 範囲（スコープ）
## 2.1 対象（MVPで実装）
- 対話入力（1行）とプロンプト
- 空白区切りのトークナイズ（制限値あり）
- 行頭コメント（`#`）の無視
- PATH 探索（`/` を含む場合は探索しない）
- `fork`/`execve`/`waitpid`
- `execve` の `ENOEXEC` 時のみ `/bin/sh` へフォールバック
- builtins: `cd`, `exit`, `help`
- SIGINT（Ctrl+C）最小対応（親は無視、子はデフォルト）
- `last_status` の保持と終了コード規約

## 2.2 対象外（MVPでは実装しない）
- クォート（`'`, `"`）
- バックスラッシュエスケープ（`\`）
- 変数展開（`$VAR`）
- グロブ（`* ? []`）
- 演算子・構文（`| < > >> ; &`）
- ジョブ制御、履歴、補完、カラー、POSIX互換強化

---

# 3. ビルド／配布設計
## 3.1 ビルド方針
- 開発ホストで `musl-gcc` により静的リンクでビルドする
- MVP では動的リンクへのフォールバックは行わない

例（概念）:
```sh
musl-gcc -static -O2 -Wall -Wextra -o ush *.c
```

## 3.2 配置
- 生成したバイナリを UmuOS の `/bin/ush` に配置する
- `/bin/sh` は BusyBox のまま維持する

---

# 4. 全体アーキテクチャ
## 4.1 コンポーネント
仕様書のディレクトリ構造に対応し、責務を分離する。

- `main`（対話ループ・状態管理）
- `parser`（行入力→`argv` 生成、コメント判定、未対応記号検出、制限値チェック）
- `exec`（PATH探索、`fork/execve/waitpid`、ENOEXECフォールバック、`last_status`更新）
- `builtins`（`cd/exit/help`、`last_status`更新）
- `env`（`getenv`参照、`setenv`による `PWD/OLDPWD` 更新）
- `utils`（文字列処理、エラーメッセージ出力など）

## 4.2 状態（プロセス内）
- `int last_status` : 直前のコマンド（外部/内蔵）の終了状態（初期値 0）
- `char cwd[PATH_MAX]` など: プロンプト用（毎回 `getcwd()`）

---

# 5. 主要フロー設計
## 5.1 メインループ
擬似コード:
```c
last_status = 0;
for (;;) {
  print_prompt(getcwd());

  line = read_line();
  if (line == EOF) {
    exit(last_status);
  }

  if (is_blank(line)) {
    continue;
  }

  if (is_comment_line(line)) {
    continue;
  }

  parse_result = tokenize(line, argv, limits);
  if (parse_result == PARSE_UNSUPPORTED) {
    eprintf("ush: unsupported syntax\n");
    last_status = 2;
    continue;
  }
  if (parse_result == PARSE_TOO_LONG || parse_result == PARSE_TOO_MANY_ARGS) {
    eprintf("ush: input too long\n");
    last_status = 2;
    continue;
  }
  if (argv[0] == NULL) {
    continue;
  }

  if (is_builtin(argv[0])) {
    last_status = run_builtin(argv, &last_status);
    continue;
  }

  last_status = exec_external(argv, &last_status);
}
```

## 5.2 EOF（Ctrl-D）
- `read_line()` が EOF を返した場合、`exit(last_status)` で終了する

## 5.3 空行・空白行
- `is_blank(line)` が真の場合は何もせず再プロンプト

---

# 6. 入力／トークナイズ設計（parser）
## 6.1 入力取得
- 1行最大長の上限は 8192
- 実装は `getline()` 等で動的取得（ただし上限超過時はエラーにする）
  - 実装方針例: `getline()` 後に長さチェック、超過時は `last_status=2` で破棄

## 6.2 コメント判定
- 「最初の非空白文字が `#`」ならコメント行として無視

## 6.3 トークナイズ仕様
- 区切り: スペースおよびタブ
- 連続空白は 1 つの区切りとして扱う
- 先頭/末尾空白は無視

## 6.4 制限値
- 最大引数数: 128（`argv[0..127]` + 末尾 `NULL`）
- 1トークン最大長: 1024

## 6.5 未対応記号検出（方針B）
MVPでは以下の入力要素を「未対応」として検出したらエラーにし、当該行を実行しない。

- クォート: `'` `"`
- バックスラッシュ: `\`
- 変数展開の可能性: `$`
- グロブの可能性: `*` `?` `[` `]`
- 演算子: `|` `<` `>` `;` `&`（`>>` は `>` の検出で足りる）

設計上の扱い:
- 検出時: `ush: unsupported syntax` 相当を stderr に出す
- `last_status = 2`

## 6.6 `argv` の生成（メモリ方針）
- 行バッファ中の区切り文字（空白）を `\0` に置換し、`argv[]` は行バッファ内ポインタを参照する
- 追加の `malloc` を避ける

---

# 7. PATH探索と実行設計（exec）
## 7.1 PATH探索の有無
- `argv[0]` に `/` を含む場合: PATH探索しない（そのパスを直接 exec）
- 含まない場合: `$PATH` を `:` 区切りで走査

## 7.2 `$PATH` のデフォルト
- `getenv("PATH") == NULL` の場合は `/bin:/sbin` を利用する

## 7.3 実行可否とエラー優先順位
PATH走査中に以下を実施:
- 実行可能な候補が見つかったら即採用
- 「存在するが実行不可（例: EACCES）」が1回でも発生したら `saw_eacces=true`

最終的に実行できなかった場合:
- `saw_eacces==true` → `permission denied`（終了 126）
- それ以外 → `command not found`（終了 127）

## 7.4 子プロセス環境
- `execve(path, argv, environ)` を使用し、親の環境を子に引き継ぐ

## 7.5 ENOEXEC フォールバック
- `execve()` が `ENOEXEC` を返した場合のみ、`/bin/sh` にフォールバック
  - 例: `argv2 = {"/bin/sh", original_path, NULL...}`

## 7.6 `waitpid()` と `last_status`
- 通常終了: `WEXITSTATUS` を `last_status` に保存
- シグナル終了: `128 + WTERMSIG` を `last_status` に保存

---

# 8. builtins 設計
## 8.1 共通
- builtins は親プロセス側で実行する（`cd` 等の状態が反映される）
- 成功時 `last_status=0`、失敗時 `last_status=1`、引数不正などは `last_status=2`

## 8.2 `cd`
- 仕様:
  - `cd` 引数なし: `$HOME` があれば `$HOME`、なければ `/`
  - `cd -` は未対応（エラー）
  - 成功時に `PWD` と `OLDPWD` を更新
- 実装方針:
  - 現在の `PWD` を `getcwd()` で得て `OLDPWD` に `setenv`
  - `chdir(target)`
  - 成功後に新しい `PWD` を `getcwd()` で得て `setenv`

## 8.3 `exit`
- 仕様:
  - `exit` → `exit(last_status)`
  - `exit n` → `exit(n & 255)`
  - 引数が数値でない/多すぎる → エラーとして終了しない（`last_status=2`）

## 8.4 `help`
- builtins 一覧と、未対応事項（クォート・パイプ等）を簡潔に表示

---

# 9. シグナル設計
## 9.1 親（ush）
- 方針A: 親は SIGINT を無視する
  - 例: `signal(SIGINT, SIG_IGN);`

## 9.2 子（外部コマンド）
- `fork()` 後の子プロセスで SIGINT をデフォルトに戻してから `execve()`
  - 例: `signal(SIGINT, SIG_DFL);`

---

# 10. エラーメッセージ設計
- 出力先: stderr
- 接頭辞: `ush:`
- 代表メッセージ例（形式）:
  - `ush: command not found: <cmd>`
  - `ush: permission denied: <cmd>`
  - `ush: unsupported syntax`
  - `ush: cd: ...`

---

# 11. 将来拡張への接続点
- 演算子（`| < > >> ; &`）は parser の未対応検出を拡張し、将来は構文木へ移行可能
- SIGINT は将来「改行して再プロンプト」へ発展可能（方針B）

---

# 12. 受け入れ基準（仕様充足チェック）
以下をすべて満たすこと（MVP仕様の全項目を満たすこと）を受け入れ基準とする。

## 12.1 ビルド／配置
- [ ] 開発ホストで `musl-gcc` により静的リンクバイナリ `ush` が生成できる
- [ ] MVP では動的リンクへのフォールバックを行わない（ビルド失敗を成功扱いしない）
- [ ] UmuOS 上で `ush` が `/bin/ush` として配置される
- [ ] `/bin/sh`（BusyBox）は残っている（置換しない）

## 12.2 対話入力とプロンプト
- [ ] プロンプトが `UmuOS:ush:<cwd>$` 形式で表示される（`<cwd>` は `getcwd()` 相当）
- [ ] 1行入力を読み取れる
- [ ] EOF（Ctrl-D）で `last_status` を終了コードとして終了する
- [ ] 空行・空白だけの行は何もせず再プロンプトする

## 12.3 トークナイズ（空白区切り）
- [ ] 区切りがスペース/タブである
- [ ] 連続空白が1つの区切りとして扱われる
- [ ] 先頭/末尾空白が無視される
- [ ] 行頭コメント（最初の非空白が `#`）は実行されない

## 12.4 未対応機能（検出してエラー）
- [ ] `'` または `"` を含む行は未対応としてエラーになり実行されない
- [ ] `\` を含む行は未対応としてエラーになり実行されない
- [ ] `$` を含む行は未対応としてエラーになり実行されない
- [ ] `* ? [ ]` を含む行は未対応としてエラーになり実行されない
- [ ] `| < > >> ; &` を含む行は未対応としてエラーになり実行されない

## 12.5 PATH探索
- [ ] コマンドに `/` が含まれる場合、PATH探索せず直接実行を試みる
- [ ] `/` を含まない場合、`$PATH` を `:` 区切りで走査して探索する
- [ ] `PATH` 未設定時、デフォルトが `/bin:/sbin` として扱われる

## 12.6 実行（fork/exec/wait）と環境
- [ ] 外部コマンド実行で `fork()`→子で `execve()`→親で `waitpid()` の流れになる
- [ ] 子の `execve()` は親の環境（`environ`）を引き継いで実行される
- [ ] 終了コードは `waitpid()` の結果から `last_status` に保存される
  - [ ] 通常終了は `WEXITSTATUS` が反映される
  - [ ] シグナル終了は `128 + signal` が反映される

## 12.7 ENOEXEC フォールバック
- [ ] `execve()` が `ENOEXEC` の場合のみ `/bin/sh <file> ...` にフォールバックする
- [ ] `ENOEXEC` 以外のエラーで無条件フォールバックしない

## 12.8 エラーメッセージ
- [ ] エラーは stderr に出力される
- [ ] エラー出力は `ush:` で始まる
- [ ] `command not found` は 127 を返す
- [ ] `permission denied` は 126 を返す

## 12.9 builtins
- [ ] `cd` が動作する
  - [ ] 引数なし `cd` が `$HOME`（無ければ `/`）へ移動する
  - [ ] `cd -` は未対応としてエラーになる
  - [ ] 成功時に `PWD` と `OLDPWD` が更新される
- [ ] `exit` が動作する
  - [ ] 引数なしは `last_status` で終了する
  - [ ] `exit n` は `n & 255` で終了する
  - [ ] 引数不正/多すぎはエラーで終了しない（`last_status=2`）
- [ ] `help` が動作し、builtins と未対応事項を表示する

## 12.10 シグナル（SIGINT）
- [ ] 親（ush）が SIGINT で終了しない（SIGINT を無視する）
- [ ] 子プロセスは SIGINT をデフォルトに戻してから exec する

## 12.11 制限値
- [ ] 1行最大長が 8192 で制限され、超過時はエラーとして破棄される
- [ ] 最大引数数が 128 で制限され、超過時はエラーとして破棄される
- [ ] 1トークン最大長が 1024 で制限され、超過時はエラーとして破棄される

---

# 13. 仕様トレーサビリティ（要点）
- 仕様 4.1 入力処理 → 本設計 5/6/12.2
- 仕様 4.1 トークナイズ → 本設計 6/12.3/12.4
- 仕様 4.1 PATH 検索 → 本設計 7.1〜7.3/12.5
- 仕様 4.1 実行（exec） → 本設計 7/12.6
- 仕様 4.1 ENOEXEC → 本設計 7.5/12.7
- 仕様 4.1 builtins → 本設計 8/12.9
- 仕様 4.1 シグナル → 本設計 9/12.10
- 仕様 4.2 `last_status` → 本設計 5/7.6/8/12.2/12.6/12.9
- 仕様 4.2 エラーメッセージ → 本設計 10/12.8
- 仕様 4.2 制限値/メモリ → 本設計 6.4〜6.6/12.11
- 仕様 3 ビルド方針 → 本設計 3/12.1
