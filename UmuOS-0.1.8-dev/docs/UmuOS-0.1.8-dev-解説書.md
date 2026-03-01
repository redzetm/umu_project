---
title: UmuOS-0.1.8-dev 解説書
date: 2026-02-23
status: accepted
related_docs:
  - "./UmuOS-0.1.8-dev-機能一覧.md"
  - "./UmuOS-0.1.8-dev-基本設計書.md"
  - "./UmuOS-0.1.8-dev-詳細設計書.md"
  - "./UmuOS-0.1.8-dev-実装ノート.md"
---

# 0. このドキュメントの目的

UmuOS-0.1.8-dev を「起動して使う（観測して切り分ける）」ための前提と操作手順をまとめる。

- ① 最小の起動手順（3点セット / 起動スクリプト）
- ② 観測点（成功判定に直結する確認）
- ③ ネットワーク機能（telnetd / FTP / NTP）の使い方
- ④ シェル方針（標準の ash と、任意で持ち込む ush の使い分け）

注意：UmuOS は研究用であり、telnet/ftp は安全な運用を目的にしていないです。ネットワークを流れるデータの見え方観測、OSの起動メカニズムなど研究用であります。
また、UmuOS-0.1.8-dev版を利用し、自身でカスタム、研究、ユーザーランドの開発など行ってもらうためのものになります。
UmuOS-0.1.8-devでは、一旦機能追加はしない方針です。機能拡張は、次期UmuOS-0.1.x-devで行います。

この版は「便利に使う」よりも、「起動・初期化・ネットワーク・ログ・認証の成立」を観測できることを優先する。

# 0. Busyboxのコマンド群で実装した経緯

UmuOS-0.1.8-dev では、ユーザーランド（initramfs / rootfs 側）の多くを BusyBox のコマンド（applet）で揃えています。
これは「便利だから」ではなく、研究用OSとして **起動・観測・再現** を崩さないための設計判断です。

UmuOS-0.1.xで BusyBox を中心にした理由は次のとおり。

- 依存関係を最小化できる
  - 追加のライブラリや複雑なパッケージ構成に引きずられず、rootfs を小さく保てる
  - 「何が必要で、何が無くても起動できるか」を切り分けやすい
- 再現性が高い
  - 同じ BusyBox バージョン（1.36.1）とコンフィグを固定し、実行環境差分を減らす
  - `busybox --list` で「その成果物に何が入っているか」を確認できる
- 観測しやすい
  - コマンド数を絞ることで、`rcS` が何をしているかをログに落としやすい（例：`/logs/boot.log`）
  - シェルスクリプトで OS 初期化の流れ（mount / ip / ntpd / telnetd / ftpd）を直に追える
- 持ち込みやすい（開発環境とOSを分離しやすい）
  - UmuOS は開発環境を内包しない方針なので、「外で作って持ち込む」前提に合う
  - BusyBox を軸にすると、最低限の運用コマンドが最初から揃う

逆に言うと、ここで BusyBox を選んでいるのは「BusyBox が正解」だからではなく、
UmuOS-0.1.xの目的（最小構成で起動して、ネットワーク・ログ・認証を観測する）に対して、ブレが少ないからです。

UmuOS-0.1.xで BusyBox に寄せている代表例：

- 起動〜初期化：`/sbin/init`（BusyBox）→ `inittab` → `rcS`（sh）
- マウント：`mount`
- ネットワーク：`ip`
- ログイン：`getty` / `login`
- リモート：`telnetd` / `tcpsvd` / `ftpd`
- 時刻：`ntpd`

注意：BusyBox で揃えると「できないこと」も増える。
そのため UmuOS-0.1.8-dev では、足りない部分は無理に内側で解決せず、
次期 dev 版での拡張や、外部ツールの持ち込み（例：自作コマンドを `/umu_bin` に置く）で扱う方針にしている。

## 0.1 rcS で使うコマンド列と観測点

ここでは「rcS が本当に走って、どこまで到達したか」を、コマンド単位で観測できるように対応をまとめる。
（実装の正は詳細設計書の rcS テンプレとなります。）


### 0.1.1 proc/sys/dev を用意

- コマンド例：`mount -t proc` / `mount -t sysfs` / `mount -t devtmpfs` / `mount -t devpts`
- 観測点：`mount` の出力に `/proc` `/sys` `/dev` `/dev/pts` が見える
- 切り分け：`mount` 自体が失敗するなら kernel config / initramfs 側の成立条件を疑う

### 0.1.2 永続ログの土台を作る

- コマンド例：`mkdir -p /logs /run /var/run` / `: > /var/run/utmp`
- 観測点：`/logs/boot.log` が存在し、追記される
- 切り分け：`/logs` が作れないなら rootfs が read-only になっていないか確認

### 0.1.3 起動情報を記録する

- コマンド例：`cat /proc/...` / `date` / `mount` を `/logs/boot.log` へ追記
- 観測点：`/logs/boot.log` に `boot_id` / `time` / `uptime` / `cmdline` / `mount` が残る
- 切り分け：`boot.log` が増えないなら `inittab` の `::sysinit:/etc/init.d/rcS` を確認

### 0.1.4 ネットワーク初期化（static）

- コマンド例：`/etc/umu/network.conf` を読み `ip link/addr/route`
- 観測点：`ip a` で `192.168.0.202/24` が入る / `ip r` で default route がある
- 切り分け：`NET_MODE=none` だと `eth0` 自体が出ない。tap 起動とホスト側 bridge/TAP を確認

### 0.1.5 NTP 同期（1回）

- コマンド例：`/umu_bin/ntp_sync`（内部で `ping` / `ntpd`）
- 観測点：`/logs/boot.log` に `[ntp_sync] before/after` と出力が残る
- 切り分け：DNS/疎通が無いと失敗する。`/etc/resolv.conf` と `ping` を確認

### 0.1.6 LAN ログイン（telnetd）

- コマンド例：`telnetd -p 23 -l /bin/login`
- 観測点：`ps` に `telnetd` がいる / LAN から `telnet 192.168.0.202 23` で `login:` が出る
- 切り分け：23/TCP が塞がるならホスト側 FW/SELinux、br0 側到達性を確認

### 0.1.7 FTP サーバ

- コマンド例：`/umu_bin/ftpd_start`（内部で `tcpsvd` + `ftpd`）
- 観測点：`/run/ftpd.pid` があり、`ps` に `tcpsvd/ftpd` がいる
- 切り分け：21/TCP が塞がるならホスト側 FW/SELinux、または pid ファイル周りの権限を確認

### 0.1.8 rcS の到達点を出す

- コマンド例：`echo "[rcS] rcS done" > /dev/console`
- 観測点：コンソールに `rcS done` が見える（タイミング依存）
- 切り分け：見えなくても `boot.log` が増えていれば rcS は動いている

# 1. 成果物（3点セット）と配置

UmuOS-0.1.8-dev は、起動環境（例: RockyLinux 9.7）の任意ディレクトリに次の3点が揃っていれば起動できる。
（他ディストリでも同様に動く想定だが、環境差分はあり得る）

- ISO: `UmuOS-0.1.8-dev-boot.iso`
- 永続ディスク: `disk.img`
- 起動スクリプト: `UmuOS-0.1.8-dev_start.sh`

ポイント：リポジトリ内では `disk/disk.img` だが、起動スクリプトは「同じディレクトリにある `disk.img`」を参照する。


# 2. 起動（RockyLinux9.7 / QEMU）

起動は起動スクリプトが担当します。

```bash
cd /root   # 例: 3点セットを置いた場所
sudo ./UmuOS-0.1.8-dev_start.sh
```

ネットワーク無し起動（切り分け用）：QEMU環境やブリッジNWがなく、br0を設定していない環境の場合です。

```bash
cd /root   # 例: 3点セットを置いた場所
sudo NET_MODE=none ./UmuOS-0.1.8-dev_start.sh
```

起動スクリプトの主な環境変数：

- `NET_MODE=tap|none`（デフォルト `tap`）
- `BRIDGE=br0`（tap時の接続先ブリッジ）
- `TAP_IF=tap-umu`（tapデバイス名）
- `TTYS1_PORT=5555`（ttyS1 の TCP ポート）

ホスト側ログ：

- 起動スクリプトは `host_qemu.console_YYYYmmdd_HHMMSS.log` を作成する。ログは1起動につき1ファイル生成される仕様です。


# 3. コンソール（ttyS0 / ttyS1）

- `ttyS0`: 起動スクリプトを実行したターミナルが、そのままコンソールになる（`-serial stdio`）。
- `ttyS1`: 追加コンソール。ホストの `127.0.0.1:${TTYS1_PORT}` へ TCP 転送される。

補足：ログイン経路は大きく次の2つ。

- シリアル（ttyS0/ttyS1）: `getty` → `login`
- ネットワーク（telnetd）: telnet 接続 → `login`（セッションは `/dev/pts/*` として見える）

ttyS1 へ CLI で接続する例：

```bash
telnet 127.0.0.1 5555
```

接続後に何も出ない場合は Enter を数回押すとログインプロンプトになる。（表示タイミングの問題のことが多い）

（理由メモ）getty は入力待ち状態でも、クライアント側の表示タイミングによってはプロンプトが見えないことがあるため。


# 4. ゲスト内での「成功判定（最小）」

ゲストで確認する（詳細は詳細設計書に準拠）。

- rootfs が ext4 でマウントされている（例：`mount` に `/dev/vda on / type ext4`）
- `/logs/boot.log` に `boot_id` と `[ntp_sync] before/after` が追記される
- `echo "$PATH"` の先頭が `/umu_bin`
- `date` が JST で出る（NTP が通れば時刻も概ね合う）
- FTP が起動している（`/run/ftpd.pid` がある）

確認用コマンド例：

```sh
echo "[whoami]"; whoami
echo "[PATH]"; echo "$PATH"
echo "[date]"; date -R 2>/dev/null || date

echo "[boot.log tail]"; tail -n 80 /logs/boot.log 2>/dev/null || true

echo "[telnetd]"; ps w | grep -E 'telnetd|\[telnetd\]' | grep -v grep || true
echo "[ftpd pid]"; ls -l /run/ftpd.pid 2>/dev/null || true
echo "[ftpd ps]"; ps w | grep -E 'tcpsvd|ftpd|\[tcpsvd\]|\[ftpd\]' | grep -v grep || true
```


# 5. ネットワーク（ゲスト）

ネットワーク設定は `/etc/umu/network.conf` を I/F としている。

主なキー：

- `IFNAME=eth0`
- `MODE=static`
- `IP=192.168.0.202/24`
- `GW=192.168.0.1`

注意：ホスト側で `NET_MODE=none` の場合、ゲストの `eth0` は提供されないため、telnet/ftp/NTP は成立しないです。


# 6. リモート機能（telnetd / FTP / NTP）

## 6.1 telnetd（LAN からのログイン）

ゲスト側で `telnetd -p 23 -l /bin/login` が起動する。

例（LAN 内の別マシンから）：

```bash
telnet 192.168.0.202 23
```

## 6.2 FTP（ゲストがサーバ）

ゲスト側で BusyBox の `tcpsvd` + `ftpd` を使い、`0.0.0.0:21` で待ち受ける。

例（LAN 内の別マシンから）：

```bash
ftp 192.168.0.202
```

公開ルートは `/`（全ディレクトリが見える。実アクセス可否はパーミッションに従う仕様です）。

## 6.3 NTP（起動時に1回）

起動時に `/umu_bin/ntp_sync` が1回実行され、前後の時刻と出力が `/logs/boot.log` に追記される。


# 7. シェル方針（ash と ush）

## 7.1 標準のシェル

UmuOS-0.1.8-dev の標準は BusyBox ash（`/bin/sh`）であります。

- スクリプト実行・互換性寄り: `/bin/sh`

## 7.2 ush（任意で持ち込む、普段使い向け）

ush は「対話操作・軽量スクリプト」向けの別シェルで、UmuOS-0.1.8-dev の標準同梱は前提にしない。

使い分けの考え方（推奨）：

- 対話操作は ush を基本にする（導入した場合）
- 本格スクリプト（分岐/ループ/引数処理/多段パイプ等）が必要なら `/bin/sh`（ash）へ逃がす

### ush 0.0.4 のビルド例（開発ホスト）

```sh
cd /home/tama/umu_project/ush-0.0.4/ush  # 自分の環境のパスに合わせる

musl-gcc -static -O2 -Wall -Wextra -Wshadow -Wpointer-arith -Wwrite-strings \
  -Iinclude \
  -o ush \
  src/main.c src/utils.c src/env.c src/prompt.c src/lineedit.c \
  src/tokenize.c src/expand.c src/parse.c src/exec.c src/builtins.c
```

### UmuOS へ持ち込む例（概念）

- `ush` バイナリをゲストへ転送（FTP等）して、まずは `/home/tama/` など書き込み可能な場所へ置く
- ゲスト側で root になり（`/umu_bin/su`）、`/umu_bin/ush` として配置する

```sh
su
cp /home/tama/ush /umu_bin/ush
chown root:root /umu_bin/ush
chmod 0755 /umu_bin/ush
```

以降、対話で `ush` を起動して使う。

### ush0.0.4を使う意味

結論：UmuOS-0.1.8-dev において ush 0.0.4 は「必須ではない」が、入れる意味はあるかなとおもった。
これは「便利ツールを増やす」ためではなく、UmuOS の目的である **観測と検証** を、対話操作のレイヤで前に進めるためです。

#### 1) 0.1.7-base-stable の方針と矛盾しない

- 0.1.7-base-stable は“完成形のベース”として固定し、機能追加は基本しない
- その上で、ユーザーランドの研究は「外で作って、必要なバイナリだけ持ち込む」で進める

ush は標準同梱しない前提なので、入れても「ベースの固定」を壊さない（必要なら削除/差し替えできる）。

#### 2) ash と役割を分けられる（スクリプトと対話を分離する）

- `/bin/sh`（BusyBox ash）: 互換性寄り・スクリプト実行の基準
- ush: 対話操作・軽量スクリプト（最小の仕様で“日常操作で詰まらない”ラインを狙う）

対話で頻出の「履歴/補完/最低限の展開/`&&` `||` `;`」を ush 側に寄せ、
本格スクリプト（分岐/ループ/引数処理/多段パイプ/高度なリダイレクト等）は ash に逃がす。
この切り分け自体が、UmuOS の「責務分離の練習」になります。
（将来的に ush 側を強くしていくとしても、現時点では役割分担を前提にする）

#### 3) “シェルを使う”ではなく“シェルで観測する”に向く

ush は小さな C 実装で、概ね次の流れが明確に分かれています（＝観測ポイントが作りやすい）。

- tokenize（字句）
- parse（構文）
- expand（展開: 変数/グロブ等）
- exec（fork/exec、リダイレクト、パイプ）
- lineedit（対話入力）

例えば、次が「OS の挙動観測」に直結します。

- `fork/exec/wait` の観測（プロセス、終了コード `$?`）
- `SIGINT` の扱い（親が落ちない/子がどうなるか）
- `ttyS*` と `/dev/pts/*` の違い（どこにぶら下がっているセッションか）
- リダイレクトやパイプが FD としてどう見えるか

「自分がシェルの構造を知りたかった」だけだと ush を使う意味が弱く見えるが、
UmuOS の文脈だとそれは **プロセス/tty/FD/シグナルを観測するための入口** になり得る。

#### 4) 0.0.4 を選ぶ理由（0.0.3 より “詰まりにくい”）

0.0.4 では、対話操作で頻出の以下を ush 側で扱えるようにしている。

- コマンド区切り `;`
- 最小のバックスラッシュエスケープ
- `${VAR}` 展開
- グロブ（`* ? [ ]`）
- Tab 補完

つまり、ash に逃げなくても作業が進む範囲が増え、
「ush を使って観測する」時間が増える（＝研究の主役を ash に奪われにくい）。

#### 5) 使わない判断もできる（ここが大事）

ush は POSIX 互換を目的にしていないため、
スクリプトが複雑になった時点で `/bin/sh`（ash）へ切り替えるのが正しい。

ush は“常用のための標準”ではなく、
「小さく作って、動かして、観測して、必要なら捨てる」ための研究用コンポーネントとして置く感じです。
