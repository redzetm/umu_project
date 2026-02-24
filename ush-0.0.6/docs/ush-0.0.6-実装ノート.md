# ush-0.0.6-実装ノート
UmuOS User Shell (ush) — 実装ノート（0.0.6）

本書は、ush-0.0.6 の開発・配布・運用で詰まりやすい点と、その解決策を記録する。

---

## 1. 目的
- 「ビルドはできたのに、ゲストで動かすと挙動が古い」等の混乱を防ぐ
- 0.0.6 の配布手順（ホスト→ゲスト反映）を固定し、再現性を上げる

---

## 2. よくある症状と原因

### 2.1 `ush --version` が動かず `ush: open: No such file or directory` になる

#### 症状（ゲスト側）
- `ush --version`
  - `ush: open: No such file or directory`
  - exit code: 1

#### 原因
実行している `ush` が古い `/umu_bin/ush` で、`--version` を「スクリプトファイル名」として解釈し
`open("--version")` して失敗している。

つまり「ビルド」ではなく **デプロイ（反映）漏れ** が原因。

#### 即確認（ゲスト側）
- `command -v ush`
- `ls -l /umu_bin/ush`
- `ush --version; echo $?`

`command -v ush` が `/umu_bin/ush` を指しているなら、ゲスト側のバイナリ更新が必要。

---

### 2.2 `$(...)` の改行の扱い（未クォート/クォートで違う）

#### 仕様（0.0.6）
- `"$(...)"`（クォートあり）: **内部の改行は保持**する（末尾の改行だけ削除）
- `$(...)`（未クォート）: 互換性の近似として **\r/\n をスペースに正規化**する

※ スモークテストでも「cmdsub newline unquoted / quoted」として確認している。

#### 参考
一般的な `/bin/sh` では、未クォートのときにフィールド分割が絡み、改行が空白相当になりやすい。
ush-0.0.6 はフル互換ではないため、まずは「クォート時に壊れない（改行が残る）」ことを優先している。

---

### 2.3 「スモークは通ったのに `./ush` のタイムスタンプが変わらない」

#### 症状（ホスト側）
- `bash tests/smoke_ush.sh` は `ALL OK` なのに、作業ツリーの `./ush` の更新時刻（mtime）が古いままに見える

#### 原因
スモークはビルド先が `BIN=${BIN:-/tmp/ush_smoke_ush_bin}` で、既定では `/tmp/ush_smoke_ush_bin` を作る。
そのため、スモークを回しても **作業ツリーの `./ush` は更新されない**。

#### 解決策（推奨）
「配布/コピーしたいのが `./ush`」なら、別途 `./ush` を明示的にビルドする。

- `cd /home/tama/umu_project/ush-0.0.6/ush`
- `musl-gcc -std=c11 -O2 -g -static -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -I"$PWD/include" "$PWD"/src/*.c -o ./ush`
- `./ush --version`（`ush-0.0.6` を確認）
- `stat ./ush`（mtime/size を確認）

ゲストへ反映したい場合は、`./ush` をゲストへ持っていき `/umu_bin/ush` を差し替える（2.1 の手順）。

---

## 3. 解決策（推奨手順）

### 3.1 ホスト側で最新をビルドして `ush` を更新する
ホスト（開発側）で実行:

- `cd /home/tama/umu_project/ush-0.0.6/ush`
- `musl-gcc -std=c11 -O2 -g -static -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -I"$PWD/include" "$PWD"/src/*.c -o ./ush`
- `./ush --version` が `ush-0.0.6` を表示することを確認

### 3.2 ゲスト側へ反映（/umu_bin/ush を差し替え）
ゲストで見える場所へバイナリを持ってきた後、ゲストで実行:

- `cp <持ってきたushバイナリのパス> /umu_bin/ush`
- `chown root:root /umu_bin/ush`
- `chmod 0755 /umu_bin/ush`
- `/umu_bin/ush --version`

---

## 4. 再発防止（運用固定）

### 4.1 「ビルド」と「ゲスト反映」を分けてチェックする
- ホスト: `./ush --version`（生成物の確認）
- ゲスト: `/umu_bin/ush --version`（実行物の確認）

### 4.2 実行バイナリの取り違えを防ぐチェックリスト（ゲスト）
- `command -v ush` が `/umu_bin/ush` か
- `ls -l /umu_bin/ush` の更新時刻が最新か
- `ush --version` が `ush-0.0.6` か

### 4.3 スモークを「配布OK」の基準にする（ホスト）
- `cd /home/tama/umu_project/ush-0.0.6/ush`
- `bash tests/smoke_ush.sh`
- `ALL OK` になった生成物のみゲストへ反映する

---

## 5. メモ
- 0.0.6 では制御構文（if/while/for/case）や `test`/`[` の追加により、0.0.5 より変更範囲が大きい。
  ビルドが通ったかどうかだけでなく、「どのバイナリを実行しているか」を常に確認すること。
