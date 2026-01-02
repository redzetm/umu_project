# umu_cat（cat の解析・最小実装）

このディレクトリは、GNU coreutils の `cat` を題材にして
「ユーザーランドで最小限の cat は何をするべきか」「汎用化の層をどこまで落とせるか」を
整理するための解析メモ／実験場です。

## これは何の解析か

- 対象: `cat(1)` 相当コマンド
- 目的:
  - ファイル（または標準入力）から標準出力へバイト列をコピーする、という本質の抽出
  - coreutils の実装に含まれる最適化・互換・表示オプションの層を切り分ける

本ディレクトリの `src/umu_cat.c` は、まず「コマンドが最低限動く」ことを優先した最小版です。

## どこから来たコードか

出発点の参考実装は GNU coreutils 9.4 系の `src/cat.c` です。
本リポジトリには参照用として coreutils のツリーが `external/coreutils-9.4-userland/` に置かれています。

ただし現在の `src/umu_cat.c` は、gnulib/coreutils の依存（`safe-read`/`full-write`/i18n 等）を排除し、
学習用に自己完結する形へ書き直したものです。

## どう読むべきか（おすすめの読み順）

1. まず `man/umu_cat.1` を読み、仕様（実装している範囲）を確定させる
2. `src/umu_cat.c` を読み、
	- `stdin`/ファイルの扱い
	- `"-"` を標準入力として扱う規約
	- エラー時の戻り値
	を確認する
3. 参照として `external/coreutils-9.4-userland/src/cat.c` を眺め、
	- 表示オプション（`-n`/`-b`/`-v`/`-E`/`-T`/`-s` など）
	- 高速化・最適化（ブロックサイズ、低レベル I/O、アドバイス等）
	- i18n、ヘルプ、エラー整形
	の“汎用化の層”がどのように積み上がっているかを追う
4. 最後に `NOTES.roff` を読み、設計判断・削除した機能・差分の思想を把握する

## ビルド例（単体で確認）

```sh
cc -Wall -Wextra -O2 -o umu_cat src/umu_cat.c
./umu_cat README.md
echo hello | ./umu_cat
./umu_cat - README.md
```
