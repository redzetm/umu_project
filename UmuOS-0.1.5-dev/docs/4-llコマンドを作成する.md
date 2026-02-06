# UmuOS-0.1.5-dev 機能追加（手動により実装）手順書：ll ラッパーコマンドを作成し `/umu_bin` に置く

本書は、BusyBox 環境でよく使う `ll` 相当を **ラッパースクリプト**として用意し、`/umu_bin/ll` に配置する手順をまとめる。

結論：`/umu_bin/ll` を `#!/bin/sh` で作り、`exec ls -lF "$@"` にする。

## 前提

- `/umu_bin` が存在し、`root:root 0755` になっていること
- `PATH` の先頭に `/umu_bin` が入っていること（例：`PATH=/umu_bin:/sbin:/bin`）

まだの場合は、先に `/umu_bin` と `PATH` の手順（`3-自作コマンド置き場を追加手順書.md`）を完了させる。

## 1. `/umu_bin/ll` を作成する

ゲスト上で root で実行：

```sh
cat > /umu_bin/ll <<'EOF'
#!/bin/sh
exec ls -lF "$@"
EOF

chown root:root /umu_bin/ll
chmod 0755 /umu_bin/ll
```

## 2. 動作確認

```sh
which ll
ll /
```

期待する状態：

- `which ll` が `/umu_bin/ll` を指す
- `ll /` が `ls -lF /` 相当の出力になる

## トラブルシュート

- `which ll` が見つからない
	- `echo "$PATH"` を確認し、`/umu_bin` が入っているかを見る
	- `/umu_bin/ll` が存在し、`chmod 0755` されているか確認
- `ll` を実行すると `ls: not found`
	- `PATH` に `/bin` が含まれているか確認（例：`/umu_bin:/sbin:/bin`）