# UmuOS-0.1.5-dev 機能追加（手動により実装）手順書：ftpd を使ってファイル転送できるようにする

本書は、UmuOS（最小ユーザーランド / BusyBox）上で **BusyBox の `ftpd`** を使い、
LAN 内からファイル転送（アップロード/ダウンロード）できるようにする手順をまとめる。

FTP は平文（暗号化なし）なので、**LAN 内限定**の用途に絞ること。

結論：`ftpd` を単体で常駐させるのではなく、前段に `tcpsvd` を置き、
接続ごとに `ftpd` を起動する（`inetd` のミニ版のような構成）にする。

また、転送モードは原則 `binary` を推奨する（テキストも `binary` で問題ない）。

## 前提

- ゲストへ telnet でログインできること（例：`tama` で入り、必要に応じて root になる）
- ネットワーク設定が済んでいること（固定IP/route が成立していること）
  - 例：ゲストIPが `192.168.0.202`
- BusyBox に `ftpd` と `tcpsvd` が含まれていること

## 0. 事前確認（ゲスト）

まず、BusyBox に applet があるか確認する。

```sh
busybox --list | grep -E '^(ftpd|tcpsvd)$' || echo "NG: applet missing"
```

オプションは BusyBox のビルド設定で差が出るため、ヘルプで実機確認する。

```sh
busybox ftpd --help || true
busybox tcpsvd --help || true
```

## 1. FTP の公開ディレクトリ（FTP_ROOT）を決める

本書では「新しい公開用ツリーは作らず、既存ディレクトリを FTP のルートにする」方針とする。
例として `/tmp` を使う（必要なら `/srv/ftp` のようなディレクトリに変えてよい）。

```sh
FTP_ROOT=/tmp
ls -ld "$FTP_ROOT"
```

アップロードを許可したい場合は、書き込み先の権限（所有者/パーミッション）を先に決める。

## 2. 手動で起動して動作確認する（まずは foreground）

まずは foreground で起動してログを見ながら確認する（止めるときは `Ctrl+C`）。

```sh
FTP_ROOT=/tmp
busybox tcpsvd -vE 0.0.0.0 21 busybox ftpd "$FTP_ROOT"
```

ポイント：

- LAN 内限定にしたいなら `0.0.0.0` の代わりにゲストの固定IP（例：`192.168.0.202`）を指定する
- `21/tcp` が使えない（すでに使用中、または起動に失敗する）場合は、切り分けのため一時的に `2121` などへ変更してよい

例（ポートを 2121 にする）：

```sh
FTP_ROOT=/tmp
busybox tcpsvd -vE 0.0.0.0 2121 busybox ftpd "$FTP_ROOT"
```

## 3. クライアント（Ubuntu 側）から転送する

以降は、LAN 内の Ubuntu（ホスト）からゲスト `192.168.0.202` に接続する例。
`21` 以外のポートを使っている場合は `:2121` のように読み替える。

### 3.1 `ftp` コマンド（対話）

Ubuntu に `ftp` が無い場合はインストールする。

```sh
sudo apt-get update
sudo apt-get install -y ftp
```

接続：

```sh
ftp 192.168.0.202
```

基本操作例（クライアント側）：

```text
ftp> binary
ftp> ls
ftp> put local_file
ftp> get remote_file
ftp> bye
```

### 3.2 `curl` でアップロード/ダウンロード（任意）

環境に `curl` がある場合、簡単な転送ができる。

アップロード例（anonymous を仮定。必要ならユーザー名/パスワードを合わせる）：

```sh
curl --user anonymous: -T local_file ftp://192.168.0.202/
```

ダウンロード例：

```sh
curl -o downloaded_file ftp://192.168.0.202/remote_file
```

## 4. 常駐化（起動時に自動で上げる）

起動時に常駐させたい場合は、`/etc/init.d/rcS` に `tcpsvd` の起動を追加する。
PID ファイルは `/run/ftpd.pid` に保存し、二重起動を避ける。

`/etc/init.d/rcS` へ追記（例）：

```sh
mkdir -p /run

FTP_ROOT=/tmp

if [ -f /run/ftpd.pid ] && kill -0 "$(cat /run/ftpd.pid)" 2>/dev/null; then
	:
else
	busybox tcpsvd -vE 0.0.0.0 21 busybox ftpd "$FTP_ROOT" &
	echo $! > /run/ftpd.pid
fi
```

## 5. 停止方法

バックグラウンド起動（PID 管理）している場合は、`tcpsvd` を止めればよい。

```sh
test -f /run/ftpd.pid && kill "$(cat /run/ftpd.pid)" && rm -f /run/ftpd.pid
```

## トラブルシュート

- 接続できない（Connection refused など）
	- まずゲストで `ps | grep -E 'tcpsvd|ftpd'` を確認
	- ポートを `2121` などに変えて切り分け（`21/tcp` 周りの問題と分ける）
	- ゲストのIPが想定どおりか（`ip a`）と、同一LANから疎通できるか（`ping 192.168.0.202`）を確認

- ログインできない／認証が通らない
	- `busybox ftpd --help` を確認し、その環境の認証方式（anonymous の可否など）に合わせる

- アップロードできない
	- 書き込み許可は `ftpd` のオプションや、FTP_ROOT 配下のパーミッションに依存する
	- `busybox ftpd --help` を確認し、必要なオプションを付ける
	- 書き込み先ディレクトリの所有者/パーミッションを確認

- 転送が途中で止まる／`ls` はできるが `get/put` が失敗する
	- FTP は制御接続（21）とは別にデータ接続が張られるため、
		クライアント側の「パッシブ/アクティブ」設定や、経路上のフィルタで失敗することがある
	- まずは LAN 内で、フィルタが無い状態で試す。必要ならクライアント側でパッシブ設定を切り替える

- `tcpsvd: permission denied` / bind できない
	- `21/tcp` は特権ポートのため、ゲストで root で起動しているか確認
	- すでに別プロセスが使用していないか確認し、切り分けで `2121` を使う