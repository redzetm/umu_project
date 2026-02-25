# uman

UmuOS向けの「日本語マニュアル（Markdown）」を表示するための仕組み。

## できること

- `uman <名前>` で `名前.md` を表示する（例: `uman netstat`）
- 出力はプレーンテキスト（Markdownをそのまま標準出力へ出す）
- 長い場合はパイプで読む（例: `uman netstat | more`）

追加機能（簡易）:

- `uman --list`（ページ一覧）
- `uman -w <名前>`（参照するページのパス表示）
- `uman -k <単語>`（ページ内容の簡易検索）

## ページの置き場所

### リポジトリ内（管理用）

- `uman/pages/ja/<名前>.md`

例:

- `uman/pages/ja/netstat.md`
- `uman/pages/ja/nc.md`

### UmuOS内（配置用）

- `/usr/share/uman/ja/<名前>.md`

参照ルートは環境変数で上書きできる:

- `UMAN_PATH=/usr/share/uman/ja`

## 書き方（おすすめ）

Markdownは装飾がなくても読めるように、見出し・箇条書き・コードブロック中心で書く。

テンプレ:

```markdown
# <名前>

## 概要
（何ができるか）

## よく使う
- `...`

## 例
### 1) 〜〜〜
`...`

## 注意
（BusyBox版の差分など）
```

## 収録の考え方

- 「UmuOSでよく使うコマンド」から順に追加する
- まずは `netstat` / `nc` のような運用直結のものが効果が高い
