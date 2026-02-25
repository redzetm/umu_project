# uman

UmuOS向けの「日本語マニュアル（Markdown）」を表示するための最小ビューアです。

- ページは Markdown（`.md`）で管理
- `uman <name>` でページをそのまま標準出力へ出す
- 長い場合は `| more` などで閲覧

## ディレクトリ構成

- `uman/pages/ja/` : 日本語ページ（管理用）
- `uman/src/` : `uman` 本体ソース（C）

UmuOS（disk.img）へ配置するときの想定:
- バイナリ: `/umu_bin/uman`
- ページ: `/usr/share/uman/ja/*.md`

## ビルド

```sh
make -C uman
```

### UmuOS向け（静的リンク）

UmuOS の最小環境では動的リンカが無い/見えない場合があるため、静的リンク版でビルドします。

```sh
make -C uman clean all STATIC=1 CC=musl-gcc
```

## ローカルで試す

```sh
UMAN_PATH=uman/pages/ja uman/uman uman
```

## 使い方

- `uman <name>`: `<name>.md` を表示
- `uman --list`: ページ一覧を表示
- `uman -w <name>`: 参照するページのパスを表示
- `uman -k <word>`: ページ内容を簡易検索

検索パス:
- 既定: `/usr/share/uman/ja`
- 上書き: 環境変数 `UMAN_PATH`（単一ディレクトリ）

## インストール（UmuOSへ持ち込み）

1) `uman/uman` を `/umu_bin/uman` へコピー
2) `uman/pages/ja/*.md` を `/usr/share/uman/ja/` へコピー

※UmuOS では静的リンク版（`make -C uman STATIC=1 CC=musl-gcc`）を入れてください。
動的リンク版のELFを入れると、シェルのエラー出力が見えない環境では「何も起きない」ように見えることがあります。

表示:

```sh
uman netstat
uman netstat | more
```

## トラブルシュート（UmuOS上で何も出ない）

まず参照しているパスを確認します:

```sh
uman -w netstat
```

表示されたファイルが存在するかを確認します:

```sh
ll /usr/share/uman/ja/netstat.md
```
