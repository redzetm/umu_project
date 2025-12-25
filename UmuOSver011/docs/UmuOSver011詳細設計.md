# UmuOS var0.1.1 詳細設計

## 実装についての仕様、実装根拠、設計思想については同一ディレクトリの実装ノート.mdを確認する

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

例：20GBの永続ディスクを作る

```bash
cd ~/umu/UmuOSver011/disk
truncate -s 20G disk.img
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
# 例: ローカルQEMUで「ISO(UEFI) + disk.img(virtio)」を接続して起動する
# ※事前に ~/umu/UmuOSver011/UmuOSver011-boot.iso を作成してある前提（ISO作成後に実施）
cd ~/umu/UmuOSver011

qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=kvm -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic none \
  -serial mon:stdio
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
  - 例：`IP=192.168.0.204/24` `GW=192.168.0.1` `DNS=8.8.8.8` `TELNET_ENABLE=0`
  - 適用は `/etc/init.d/rcS` 等（起動スクリプト）から `ip` コマンドで実施
- telnet：`/etc/init.d/rcS` 等で `telnetd` を起動（必要時のみ有効化できるようフラグ化する）
- ログ：`/logs/boot.log`（起動ログを追記。ディレクトリは ext4 側に存在させる）


3. カーネルビルド（6.18.1）

0.1.1でもカーネルは 6.18.1 を利用し、設定はデフォルト（defconfig）でビルドする。

3.1 ソース取得

```bash
cd ~/umu/UmuOSver011/kernel
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.1.tar.xz
tar -xf linux-6.18.1.tar.xz
cd linux-6.18.1
```

3.2 カーネル設定（デフォルト）

```bash
make mrproper
make defconfig
cp .config ../config-6.18.1
```

3.3 ビルド

```bash
make -j$(nproc)
```

3.4 成果物コピー

```bash
cp arch/x86/boot/bzImage ~/umu/UmuOSver011/iso_root/boot/vmlinuz-6.18.1
cp .config ~/umu/UmuOSver011/iso_root/boot/config-6.18.1
```


4. initramfs（移行用）

0.1.1のinitramfsは「ext4の/へ移行（switch_root）」が主目的。
起動後のログイン・ネット設定・telnet起動は ext4 側の `/sbin/init`（BusyBox init）で行う。

4.1 構造作成

```bash
cd ~/umu/UmuOSver011/initramfs
mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,run,newroot}
# initramfs内では BusyBox が「静的」である必要がある。
# Ubuntuで busybox-static を入れている前提なら /bin/busybox を使う。
cp /bin/busybox rootfs/bin/
sudo chown root:root rootfs/bin/busybox
```

BusyBoxコマンドのsymlink（相対リンクに統一）

```bash
cd ~/umu/UmuOSver011/initramfs/rootfs/bin
busybox --install -s .
for cmd in $(ls -1 | grep -v "^busybox$"); do
  rm "$cmd"
  ln -s busybox "$cmd"
done
```

4.2 /init（自作init：C言語）

ここは0.1.1の重要ポイント。

要件（最小）:
- `/proc` `/sys` `/dev` `/dev/pts` をマウント
- `/proc/cmdline` から root のUUID（例：`root=UUID=...`）を取得
- `mount -t ext4 -o rw UUID=<...> /newroot` を実行（必要なら短時間リトライ）
- `/newroot/logs` を作成し、起動ログを `/newroot/logs/boot.log` に追記（できる範囲で）
- `switch_root /newroot /sbin/init`

ビルドと配置（例）:

```bash
cd ~/umu/UmuOSver011/initramfs
mkdir -p src
## src/init.c を作成したら
gcc -static -Os -s -o rootfs/init src/init.c
chmod 755 rootfs/init
sudo chown root:root rootfs/init
```

4.3 initramfs（cpio）作成

```bash
cd ~/umu/UmuOSver011/initramfs/rootfs
find . -print0 | sudo cpio --null -o -H newc | gzip > ../initrd.img-6.18.1
cd ..
cp initrd.img-6.18.1 ~/umu/UmuOSver011/iso_root/boot/
```


5. ext4ルート（disk.img）へ rootfs を作成

目的：switch_root後に動く「本物の/」を disk.img に用意する。
（ここに `/sbin/init` `/etc/inittab` `/etc/passwd` `/etc/shadow` `/etc/init.d/rcS` `/logs` 等が必要）

5.1 disk.img 作成（未作成なら）※手順書通りに実装した場合は、5.1は実施不要

```bash
cd ~/umu/UmuOSver011/disk
truncate -s 20G disk.img
mkfs.ext4 -F disk.img
```

5.2 disk.img をホストでマウントして rootfs を作る

※この作業は「どのディレクトリで実行してもOK」。`/mnt/umuos011` はホスト上の絶対パスのマウントポイントなので、
作業ディレクトリ（`cd` している場所）には依存しない。
（以降はマウントポイント名を `/mnt/umuos011` に統一）

```bash
sudo mkdir -p /mnt/umuos011
sudo mount -o loop ~/umu/UmuOSver011/disk/disk.img /mnt/umuos011
```

最低限のディレクトリ:

```bash
sudo mkdir -p /mnt/umuos011/{bin,sbin,etc,proc,sys,dev,dev/pts,run,root,home/tama,logs,etc/init.d,etc/umu}
```

BusyBox配置（ext4側）:

```bash
sudo cp /bin/busybox /mnt/umuos011/bin/
sudo chown root:root /mnt/umuos011/bin/busybox
```

`/sbin/init`（0.1.1は BusyBox init を使う）:

```bash
sudo ln -sf /bin/busybox /mnt/umuos011/sbin/init
```

BusyBox applet のsymlink（必要最低限だけでも可）:

```bash
cd /mnt/umuos011/bin
sudo ./busybox --install -s .
```

5.3 ユーザー/認証ファイル（ext4側）

`/mnt/umuos011/etc/passwd`（644）:

```text
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
```

`/mnt/umuos011/etc/shadow`（600）:

```text
root:$6$6kFhX6HtVFlCZFA4$ZzEfrpLNc3WeuhdHryq.83CSpQDXcIk2vN3qkgFM2a7z0vwcNqlnBzZnm3lV7AAp3w87eEdhTauJGLg6Wm3IJ/:19000:0:99999:7:::
tama:$6$L/U.mly9NN1l7Sbq$usVoDNweiQJj3343EfXLTTXk6D0sfeoEVKDF4Kn8FW057GneW257UCPvlcqGgedisVUcTPYXplibbOy.2Zj5r.:19000:0:99999:7:::
```

`<ROOT_HASH_HERE>` / `<TAMA_HASH_HERE>` の作り方（例: SHA-512）:

```bash
openssl passwd -6
```

権限:

```bash
sudo chown root:root /mnt/umuos011/etc/passwd /mnt/umuos011/etc/shadow
sudo chmod 644 /mnt/umuos011/etc/passwd
sudo chmod 600 /mnt/umuos011/etc/shadow
```

5.4 BusyBox init 設定（ext4側）

`/mnt/umuos011/etc/inittab`（最小）:

```bash
sudo mkdir -p /mnt/umuos011/etc
sudo tee /mnt/umuos011/etc/inittab > /dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
tty1::respawn:/sbin/getty 0 tty1 linux

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
EOF

sudo chown root:root /mnt/umuos011/etc/inittab
sudo chmod 644 /mnt/umuos011/etc/inittab

```

`/mnt/umuos011/etc/init.d/rcS`（起動スクリプト：最小）:
- `/proc` `/sys` `/dev` `/dev/pts` をマウント（未マウントなら）
- `/logs/boot.log` に起動ログを追記
- `ip` コマンドで静的IPを設定（設定ファイルから）
- 必要時のみ `telnetd -l /bin/login` を起動

※rcSの中身は0.1.1で段階的に作る。まずは「ログとネット」まででOK。

rcS（本番用）:

```sh
#!/bin/sh

PATH=/sbin:/bin

log() {
  echo "$1" >> /logs/boot.log
}

mkdir -p /logs
log "[rcS] boot: $(date)"

# 依存FS（未マウントなら）
mkdir -p /proc /sys /dev /dev/pts
grep -q " /proc " /proc/mounts 2>/dev/null || mount -t proc proc /proc
grep -q " /sys "  /proc/mounts 2>/dev/null || mount -t sysfs sys /sys
grep -q " /dev "  /proc/mounts 2>/dev/null || mount -t devtmpfs dev /dev
mkdir -p /dev/pts
grep -q " /dev/pts " /proc/mounts 2>/dev/null || mount -t devpts devpts /dev/pts

mkdir -p /etc/umu

# ---- ネットワーク（静的IP）----
# network.conf は KEY=VALUE 形式を想定（例は 5.5 を参照）
CONF=/etc/umu/network.conf

# デフォルト値（必要なら変更）
IFNAME=eth0
TELNET_ENABLE=0

if [ -f "$CONF" ]; then
  . "$CONF"
fi

if [ -z "$IP" ] || [ -z "$GW" ]; then
  log "[rcS] net: skip (IP/GW not set)"
else
  ip link set "$IFNAME" up

  # 冪等化: 同じIPが既に付いていれば追加しない
  if ! ip addr show dev "$IFNAME" | grep -Fq "inet $IP"; then
    ip addr add "$IP" dev "$IFNAME"
  fi

  # 冪等化: default route は replace で上書き
  ip route replace default via "$GW" dev "$IFNAME"

  if [ -n "$DNS" ]; then
    echo "nameserver $DNS" > /etc/resolv.conf
  fi

  log "[rcS] net: IF=$IFNAME IP=$IP GW=$GW DNS=$DNS"
fi

# ---- telnet（必要時のみ）----
if [ "$TELNET_ENABLE" = "1" ]; then
  telnetd -l /bin/login
  log "[rcS] telnetd started"
fi
```

権限（ext4側）:

```bash
sudo chmod 755 /mnt/umuos011/etc/init.d/rcS
sudo chown root:root /mnt/umuos011/etc/init.d/rcS
```

5.5 ネットワーク設定ファイル（ext4側）

`/mnt/umuos011/etc/umu/network.conf`（例）:

```text
IFNAME=eth0
IP=192.168.0.204/24
GW=192.168.0.1
DNS=8.8.8.8
TELNET_ENABLE=0
```
sudo chown root:root /mnt/umuos011/etc/umu/network.conf
sudo chmod 600 /mnt/umuos011/etc/umu/network.conf
sudo chown root:root /mnt/umuos011/etc/umu
sudo chmod 755 /mnt/umuos011/etc/umu


5.6 マウント解除

```bash
sudo umount /mnt/umuos011
```


6. GRUB設定

6.1 grub.cfg

作成先：`~/umu/UmuOSver011/iso_root/boot/grub/grub.cfg`

ポイント:
- `initrd` は initramfs（移行用）
- `root=UUID=...` は「自作initが読むための情報」として渡す（0.1.1はUUID指定を固定化）
- `console=tty0 console=ttyS0,115200n8` を付け、virt-managerでもシリアルでも見えるようにする

例:

```cfg
set timeout=20
set default=0

menuentry "UmuOS 0.1.1 kernel 6.18.1" {
  linux /boot/vmlinuz-6.18.1 ro root=UUID=<DISK_UUID_HERE> rootfstype=ext4 rootwait console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}

menuentry "UmuOS 0.1.1 rescue (single)" {
  linux /boot/vmlinuz-6.18.1 ro single root=UUID=<DISK_UUID_HERE> rootfstype=ext4 rootwait console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}
```


7. ISOイメージ作成

```bash
cd ~/umu/UmuOSver011
grub-mkrescue -o UmuOSver011-boot.iso iso_root
```


8. 起動テスト（QEMU）

8.1 起動のみ確認（ローカルQEMU / ネットワーク不要）

ローカル環境で「kernel + initramfs + switch_root + ext4の/」までを確認するだけなら、
ネットワークは不要。ブリッジ設定に依存しないため、この方式が最も簡単。

```bash
cd ~/umu/UmuOSver011
qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=kvm -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic none \
  -serial mon:stdio
```

8.2 静的IP/telnetまで確認（ブリッジが必要）

仕様として「DHCPなし・静的IP（`192.168.0.204/24`）」および「telnet接続」を成立させるには、
ゲストがLANにぶら下がる必要があるため、virt-manager本番と同様にブリッジ接続を用いる。

（※ローカルQEMUでもブリッジを張れれば同等に再現できる）

注記:
- 以下のコマンドは「virt-managerではなく、ホスト上で `qemu-system-x86_64` を直接実行する」場合の任意手順
- 実行するディレクトリは固定ではない（例として `~/umu/UmuOSver011` を使っているだけ）
  - `UmuOSver011-boot.iso` と `disk/disk.img` のパスが解決できる場所に合わせて、`cd` や `-cdrom` / `-drive file=...` のパスを調整する
  - virt-manager で起動する場合は、このコマンドは実行せず、GUI側で ISO と disk.img とブリッジNICを設定して起動する

推奨の進め方（段階的に切り分け）:
- まずは 8.1（`-nic none`）で「起動 + switch_root + ext4が/」を確認できたら成功とする
- 次の工程として virt-manager 本番で ISO と disk.img をセットして起動し、LAN上の疎通を確認する
- 疎通確認の最初は virt-manager のコンソールから手動で切り分けしてよい
  - ここで言う「手動」は 2種類ある点に注意
    - `ip addr add ...` のようなコマンド直打ち（ランタイム設定）: 再起動すると消える（受入基準の“保持”は満たさない）
    - ext4側のファイルを編集して反映する: 変更自体は ext4 に残る
      - 例：`/etc/umu/network.conf` を作り、`/etc/init.d/rcS` で起動時に `ip` を実行する
  - 受入基準（再起動後もネット設定が保持される）を満たすには、最終的に ext4 側の起動処理（例：`/etc/init.d/rcS`）で自動適用する

```bash
cd ~/umu/UmuOSver011
qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=kvm -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic bridge,br=br0,model=virtio-net-pci \
  -serial mon:stdio
```

補足:
- `br0` はホスト環境のブリッジ名に合わせて変更する（例：`br0`/`virbr0` 等）
- ブリッジ前提のため、ゲストの静的IP（`192.168.0.204/24`）はホストLAN側のセグメントと一致している必要がある


8.3 共通の補足

```bash
cd ~/umu/UmuOSver011
qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=kvm -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/OVMF_VARS_umuos011.fd \
  -cdrom UmuOSver011-boot.iso -boot d \
  -drive file=disk/disk.img,if=virtio,format=raw \
  -nic bridge,br=br0,model=virtio-net-pci \
  -serial mon:stdio
```

- KVMが使えない場合は `-machine q35,accel=tcg` に変更
- OVMFのパスは環境差があるため、見つからない場合は `dpkg -L ovmf | grep -E 'OVMF_(CODE|VARS).*fd$'` で探索


9. telnet / 静的IP / ログの動作確認

9.1 永続化確認（最小）
- 起動後に `/home/tama` にファイルを作成し、再起動後も残ること

9.2 ログ確認
- `/logs/boot.log` が存在し、再起動後も追記されること

9.3 ネット確認
- `ip addr` / `ip route` で静的IPが設定されていること

永続化確認（ネット設定）:
- 一度再起動し、手動で `ip addr add ...` などを打たなくても 9.3 が成立すること

補足（初回の疎通確認として「手動設定」で切り分けしたい場合）:

※以下は「virt-managerのブリッジ接続が正しく、ゲストがLANに出られるか」を素早く確認するための手順。
最終的には ext4 側の `rcS` に落とし込み、再起動後も自動適用される状態にする。

```sh
IF=eth0
ip link set "$IF" up
ip addr add 192.168.0.204/24 dev "$IF"
ip route add default via 192.168.0.1
echo 'nameserver 8.8.8.8' > /etc/resolv.conf
```

9.4 telnet確認
- `telnet 192.168.0.204` で接続し、root/tamaでログインできること

補足（手動でtelnetを立ち上げて疎通確認する場合）:

```sh
telnetd -l /bin/login
```

補足（実行環境の前提）:
- 本番運用（virt-manager）はブリッジ接続を前提とする
- ローカルQEMU起動は補助的なテスト用途だが、静的IP要件を満たすにはブリッジ相当の接続が必要







