# umu_tail（tail の解析・最小実装）

このディレクトリは、GNU coreutils の `tail` を題材にして
「最小ユーザーランドで tail は何をするべきか」「汎用化の層をどこまで落とせるか」を
整理するための解析メモ／実験場です。

## これは何の解析か

- 対象: `tail(1)` 相当コマンド
- 目的:
  - 入力の末尾 N 行を出力する、という本質の抽出
  - coreutils 実装に含まれる追従（`-f`）や複雑な互換仕様を切り離す

本ディレクトリの `src/umu_tail.c` は、まず「最低限動く」ことを優先した最小版です。

## どこから来たコードか

出発点の参考実装は GNU coreutils 9.4 系の `src/tail.c` です。
本リポジトリには参照用として coreutils のツリーが `external/coreutils-9.4-userland/` に置かれています。

ただし現在の `src/umu_tail.c` は、gnulib/coreutils 依存を排除し、
学習用に自己完結する形へ書き直したものです。

## どう読むべきか（おすすめの読み順）

1. まず `man/umu_tail.1` を読み、仕様（実装している範囲）を確定させる
2. `src/umu_tail.c` を読み、
   - `-n N` のパース
   - 最後の N 行を保持するリングバッファの考え方
   - stdin/`-`/ファイルの扱い
   を確認する
3. 参照として `external/coreutils-9.4-userland/src/tail.c` を眺め、
   - `-f`（追従）
   - バイト数指定（`-c`）、`+N` 形式
   - 高速化（seek、ブロック読み等）
   といった“汎用化の層”がどこで入るかを追う
4. 最後に `NOTES.roff` を読み、設計判断・削除した機能・差分の思想を把握する

## ビルド例（単体で確認）

```sh
cc -Wall -Wextra -O2 -o umu_tail src/umu_tail.c
./umu_tail README.md
./umu_tail -n 3 README.md
echo -e "a\nb\nc\nd" | ./umu_tail -n 2
```
