# more

UmuOS向けの簡易ページャ（pager）。標準入力またはファイルを、ページ単位で表示する。

> UmuOS では `/umu_bin` が `PATH` 先頭になる想定のため、`/umu_bin/more` を置くことで BusyBox の `more` を置き換えられる。

## 使い方

- 標準入力をページング表示

```sh
uman uman | more
cat /logs/boot.log | more
```

- ファイルをページング表示

```sh
more /logs/boot.log
```

## キー操作

- Enter: 次の行
- Space: 次のページ
- Q: 終了
- R: 残りをすべて表示（ページングを解除）

## 備考（Enterが効かない問題）

シリアル/コンソール環境によっては Enter が `CR (\r)` として届くことがある。
UmuOS用 `more` は `CR`/`NL` の差を吸収し、Enter を「次の行」として扱えるようにしている。
