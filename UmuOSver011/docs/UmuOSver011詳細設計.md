# UmuOS var0.1.1 詳細設計

## 仕様
- 電源投入からUEFI起動し、Linuxカーネル6.18.1 を利用し、設定はデフォで起動
- 自作init（C言語で実装）※init.cを見直し、処理をシンプルにする。（AIと相談する）
- login IDとpasswordでログイン（ユーザーは、rootユーザー、tamaユーザーの2名）
- 一応はマルチユーザー仕様
- ext4ファイルシステムを搭載し、揮発性RAM環境からの脱却
- ext4ファイルシステムは、/ に実装し、ver0.1の/persistや/home以下のみ
  保存できるといった、揮発性との混在環境をなくす。すべて / にマウントする。
- 自宅サーバーの仮想マシンマネージャから.isoを読み込み、起動できる仕様
- 開発環境でQEMUを利用し、qemu-system-x86_64コマンドで、テスト起動できること。
- telnetで接続できるようにする
- ログを/logsに永続出力できるようにする（少なくとも起動ログ）
- DHCPは使わず、静的IPアドレスで動くようにする。IP 192.168.0.204/24 defaultGW 192.168.0.1 DNS 8.8.8.8

※ver0.1の不具合修正のほか、上記の機能を安定的に稼働できるようにする。ver0.1.1とする。
　ver0.1.1は、安定版としてver0.2以降のBusyBoxコマンドからの脱却を行うベースバージョンとする。

### ver0.1の不具合一覧
- ext4ファイルシステムは、/ に実装し、ver0.1の/persistや/home以下のみ
  保存できるといった、揮発性との混在環境をなくす。すべて / にマウントする。
- / にマウントできていないからipコマンドでネットワーク設定しても再起動すると設定が消える
- 不具合ではないが、init.cを何回も書き換えたため、複雑化しているので、再度チェックし、コードを最適化する。

### 方針（0.1.1で決めること）
- ext4の/は、仮想ディスクを作って永続化する（ISO内イメージのループマウントは採用しない）
- ISOは「ブートローダ + kernel + initramfs」を提供し、永続データは仮想ディスク側に持つ仕様とする。
- ブート手順は initramfs →（自作init）→ ext4を/としてマウント → switch_root（0.1.1ではBusyBox実装を利用する）
- ver0.1は参照して要件/落とし穴を学ぶが、コード/設定の流用（コピペ）はせずに再構成する（再現性重視）
- ローカル環境のQEMU起動は任意のテスト用途とし、本番は自宅サーバーのQEMU-KVM環境を優先して設計する

### BusyBoxの扱い（段階的に脱却する）
- 0.1.1では、安定化を優先しBusyBoxの利用を許容する（echo/ls/cat等を含む）
- ただし、利用範囲は固定化する（使うアプレットを明確化し、将来の置換対象をリスト化する）
- 0.2以降で、起動・ログイン・ネット初期化など「OSの芯」から置換していく

### 検討事項は、以下にレビューで決定した。
- ext4を/として使う際のデバイス特定方法：初期からUUID指定とする（QEMU/virt-manager差異に強く、再現性を上げる）
- switch_rootの呼び出し方法：0.1.1ではBusyBox実装を利用し、自作init側の前後処理を最小にする
- loginの実装方針：ver0.1と同じくBusyBox（getty/login相当）を利用する。0.2以降で置換を検討する
- telnet提供の前提：ver0.1と同じくBusyBoxのtelnetdを利用する。switch_root後（ext4の/に移行後）に起動し、BusyBoxのloginで認証する（/etc/passwd, /etc/shadowをext4側に配置）。運用はLAN内の開発用途を前提とし、必要時のみ有効化する

### 成功条件（受入基準）
- 再起動後も、/配下のファイル変更・ユーザー情報・ネットワーク設定が保持される
- QEMUと自宅サーバー（仮想マシンマネージャ）の両方で、同一ISOから起動できる
- telnetで接続し、login（root/tama）できる
- /logsに起動ログ（例：boot.log）を出力でき、再起動後もログが保持される


1. 環境準備
1.1 必要パッケージのインストール

sudo apt update

sudo apt install -y build-essential bc bison flex libssl-dev \
  libelf-dev libncurses-dev dwarves git wget \
  grub-efi-amd64-bin grub-common xorriso mtools \
  qemu-system-x86 ovmf \
  cpio gzip xz-utils busybox-static e2fsprogs

※現在の環境にすでに導入済みのPKGもあるが、Ubuntuでは、上書きは問題ないので
このコマンドラインでも大丈夫です！

1.2 ディレクトリ作成

mkdir -p ~/umu/UmuOSver011/{kernel,initramfs,iso_root/boot/grub,logs}

※永続ストレージ（ext4イメージ）用
mkdir -p ~/umu/UmuOSver011/disk


2. 永続ディスク（ext4の/）設計

## 結論（おすすめ）
- ISOは「起動するためだけ（ブートローダ + kernel + initramfs）」にし、永続データは別ディスク（disk.img）に持たせる
- disk.img は raw 形式の ext4 ファイルシステムとして作成し、VMには「追加ディスク」として接続する
- initramfs（自作init）は disk.img 側の ext4 を UUID 指定で `/` としてマウントし、`switch_root` で移行する

この方式だと「ISOの中身を変えなくても永続化できる」ので、再現性と運用が安定します。

## disk.img の作り方（raw + ext4）
※まずは一番シンプルに「パーティションを切らず、ディスク全体を ext4 にする」方式で進める。

例：2GBの永続ディスクを作る

```bash
cd ~/umu/UmuOSver011/disk
truncate -s 2G disk.img
mkfs.ext4 -F disk.img
```

UUIDの確認（どれか一つでOK）

```bash
sudo blkid -p -o value -s UUID disk.img
```

（blkidが読めない場合）

```bash
sudo dumpe2fs -h disk.img | grep -E '^Filesystem UUID:'
```

## QEMU で disk.img を接続する
例：disk.img を virtio-blk として追加

```bash
qemu-system-x86_64 \
  ...（ISO/UEFI等の既存オプション）... \
  -drive file=~/umu/UmuOSver011/disk/disk.img,format=raw,if=virtio
```

VM内では多くの場合 `/dev/vda` などとして見えます（ただし、デバイス名は環境で変わるのでUUIDマウント前提にする）。

## virt-manager で disk.img を接続する
- VMの「ハードウェアを追加」→「ストレージ」→ 既存のディスクイメージとして `disk.img` を指定
- バス（またはデバイス）は virtio を推奨

注意：virt-manager が動いているホストが別マシンの場合、ローカルの `disk.img` は参照できません。
その場合は「同じ作成手順でサーバ側に disk.img を作る」か「scp等で disk.img をサーバに転送」します。
（“同一ファイルを両方で使う”のは、同一ホスト上で共有パスが見えている場合に限り現実的です）

## initramfs（自作init）がやること（最小）
0.1.1は安定化優先なので、initramfs側の処理を「/への移行」に集中させる。

1) 早期ログ出力先の確保（/runや/devを整える）
- `/proc` `/sys` `/dev` をマウント（最低限）

2) 永続ディスクを `/newroot` にマウント
- `mount -t ext4 -o rw UUID=<disk.imgのUUID> /newroot`

3) switch_root
- `switch_root /newroot /sbin/init`（0.1.1は BusyBox の switch_root 実装を利用）

## 永続化される「実体」の置き場所（案）
ext4を `/` にした後は、原則として “OSの状態” はすべて ext4 側の `/` に置く。

- ユーザー/認証：`/etc/passwd` `/etc/shadow`
- 起動とサービス：`/sbin/init`（BusyBox） + `/etc/inittab`（BusyBox init用）
- ネット設定（静的IPの実体）：`/etc/umu/network.conf`（自作の単純な設定ファイル）
  - 例：`IP=192.168.0.204/24` `GW=192.168.0.1` `DNS=8.8.8.8`
  - 適用は `/etc/init.d/rcS` 等（起動スクリプト）から `ip` コマンドで実施
- telnet：`/etc/init.d/rcS` 等で `telnetd` を起動（必要時のみ有効化できるようフラグ化する）
- ログ：`/logs/boot.log`（起動ログを追記。ディレクトリは ext4 側に存在させる）






