# umu_head（head の解析・最小実装）

このディレクトリは、GNU coreutils の `head` を題材にして
「最小ユーザーランドで head は何をするべきか」「汎用化の層をどこまで落とせるか」を
整理するための解析メモ／実験場です。

## これは何の解析か

- 対象: `head(1)` 相当コマンド
- 目的:
  - 入力の先頭 N 行を出力する、という本質の抽出
  - coreutils 実装に含まれる多機能化（バイト数指定、ヘッダ、複雑なオプション互換など）を切り離す

本ディレクトリの `src/umu_head.c` は、まず「最低限動く」ことを優先した最小版です。

## どこから来たコードか

出発点の参考実装は GNU coreutils 9.4 系の `src/head.c` です。
本リポジトリには参照用として coreutils のツリーが `external/coreutils-9.4-userland/` に置かれています。

ただし現在の `src/umu_head.c` は、gnulib/coreutils 依存を排除し、
学習用に自己完結する形へ書き直したものです。

## どう読むべきか（おすすめの読み順）

1. まず `man/umu_head.1` を読み、仕様（実装している範囲）を確定させる
2. `src/umu_head.c` を読み、
   - `-n N` のパース
   - stdin/`-`/ファイルの扱い
   - 行数カウントの停止条件
   を確認する
3. 参照として `external/coreutils-9.4-userland/src/head.c` を眺め、
   - バイト数指定（`-c`）
   - 複数ファイル時のヘッダ表示
   - 互換性やエラーハンドリング
   といった“汎用化の層”がどこで入るかを追う
4. 最後に `NOTES.roff` を読み、設計判断・削除した機能・差分の思想を把握する

## ビルド例（単体で確認）

```sh
cc -Wall -Wextra -O2 -o umu_head src/umu_head.c
./umu_head README.md
./umu_head -n 3 README.md
echo -e "a\nb\nc\nd" | ./umu_head -n 2
```
