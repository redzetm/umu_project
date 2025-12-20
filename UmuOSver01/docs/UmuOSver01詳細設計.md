# UmuOS var0.1 詳細設計

## 仕様
- 電源投入からUEFI起動し、Linuxカーネル（例：6.18.1）を起動
- 起動経路：UEFI → GRUB（UEFI版） → Linux（bzImage + initramfs）
- 対象アーキテクチャ：x86_64（ arch/x86/boot/bzImage を使用）
- 自作init（C言語で実装）
- コマンドは、Busyboxを利用する
- login IDとpasswordでログイン（ユーザーは、rootユーザー、tamaユーザーの2名）
- 一応はマルチユーザー仕様
- ext4ファイルシステムを搭載し、揮発性RAM環境からの脱却
- 自宅サーバーの仮想マシンマネージャから.isoを読み込み、起動できる仕様
- telnetで接続できるようにする
- suコマンド、poweroffコマンドを利用して起動を停止する

### コンソール/表示の前提（重要）

本設計は virt-manager（QEMU/KVM）上での利用を前提に、以下を満たす。

- **TTY（シリアル）接続は必須**：ホスト側からシリアルコンソールに接続してログインできること（例：`ttyS0`）。
- **画面側（virt-managerの表示）にもログイン手段を用意**：UEFI 環境ではレガシーVGAテキストモードが前提にならないため、
  Linux の fbcon（フレームバッファコンソール）経由で `tty1` を表示先として用いる。

補足：`console=` カーネルパラメータは「カーネルログ出力先」と「/dev/console（PID1の標準入出力の既定先）」に影響する。
TTY必須要件を満たすため、/dev/console はシリアル側に寄せる（`console=ttyS0,...` を最後に指定）。

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

mkdir -p ~/umu/UmuOSver01/{kernel,initramfs,iso_root/boot/grub,logs}

※永続ストレージ（ext4イメージ）用
mkdir -p ~/umu/UmuOSver01/disk


2. カーネルビルド

2.1 ソース取得

cd ~/umu/UmuOSver01/kernel

wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.1.tar.xz

tar -xf linux-6.18.1.tar.xz

cd linux-6.18.1

補足：カーネルバージョンは固定ではない。以降の手順では例として 6.18.1 のファイル名を使う。


2.2 カーネル設定

今回はデフォルト設定でビルド。

cd linux-6.18.1
make mrproper
※カーネルソースツリーを「完全初期化」する
make defconfig
※Linuxカーネルのビルドにおける 「デフォルト設定ファイル（.config）の生成」 を行う

cp .config ../config-6.18.1
※バックアップを~/umu/UmuOSver01/kernelに保存

2.3 ビルド

make -j$(nproc)

※make -j$(nproc)は、make defconfigで生成した.configを使いカーネルを
ビルドする。


2.4 成果物コピー

cp arch/x86/boot/bzImage ~/umu/UmuOSver01/iso_root/boot/vmlinuz-6.18.1

※bzImageをブートイメージとしてvmlinuz-6.18.1として~/umu/UmuOSver01/iso_root/boot/にコピー。
vmlinuz-6.18.1は、GRUB（ブートローダー）が読み込む。

cp .config ~/umu/UmuOSver01/iso_root/boot/config-6.18.1

※vmlinuz-6.18.1と同じディレクトリに入れてるけど、もしconfig-6.18.1が無くても
影響しない。慣例的に同じディレクトリに入れてる


3. initramfs（BusyBox版）

3.1 構造作成

initramfsは、目的は「initramfs（初期RAMファイルシステム）」を構築するための作業場所。
この中で rootfs/ を作り、bin, etc, dev, proc, sys などのディレクトリを配置して、
最小限のLinux環境を再現する。
カーネルが起動直後に展開する「最初のルートファイルシステム」。
本格的なルートファイルシステムに切り替える前に、最低限のコマンドや設定を提供する。
BusyBoxを入れて ls, cat, ps, su などを使えるようにするのが典型的。

cd ~/umu/UmuOSver01/initramfs

mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,dev/pts,home/tama,root,persist}

cp "$(command -v busybox)" rootfs/bin/

### BusyBoxの所有者をrootにする（su等で権限が必要なため）
sudo chown root:root rootfs/bin/busybox

### su を使う場合は BusyBox を setuid root にする（必要に応じて）
sudo chmod 4755 rootfs/bin/busybox

cd ~/umu/UmuOSver01/initramfs/rootfs/bin


### BusyBoxコマンドを一度インストール
busybox --install -s .

### 全てのシンボリックリンクを相対パスに修正
for cmd in $(ls -1 | grep -v "^busybox$"); do
  rm "$cmd"
  ln -s busybox "$cmd"
done

cd ~/umu/UmuOSver01/initramfs

## ＜重要＞
BusyBox の各コマンドは symlink により提供しているが、
initramfs は実行時に RAM 上へ展開され、ルートディレクトリの
物理的な位置がビルド時とは異なる。

そのため、（環境によっては）BusyBox が生成する絶対パスの symlink をそのまま使用すると、
initramfs 展開後にリンク先が存在せず、コマンド実行に失敗する。

この問題を回避するため、全ての BusyBox コマンド symlink は
同一ディレクトリ内の busybox バイナリを指す相対パスに統一している。
これにより、initramfs がどのパスに展開されても確実に動作する。


3.2 ユーザー構成

/etc/passwd と /etc/shadow を作成する。
※ /etc/passwd はユーザー情報、/etc/shadow はパスワードハッシュを保持する。
※ su による root 昇格を計画通り動作させるため、root のパスワードは必須。
※ パスワードハッシュは openssl や mkpasswd で生成し、ここに埋め込む。
※ 公開用ドキュメントに実パスワードやハッシュを載せるのは避ける（以下は **テンプレート** とする）。

注意：`/etc/passwd` と `/etc/shadow` は **行末コメントを書かない**（例：`...:/bin/sh # comment` のようにしない）。
BusyBox のパーサが想定外の文字列を含むと、ログインに失敗したりシェル起動に失敗する。

注意：initramfs に入れるファイルは **root 所有** で固める（cpio でそのままUID/GIDが入る）。
少なくとも `/etc/passwd` `/etc/shadow` は root 所有にする。

### ~/umu/UmuOSver01/initramfs/rootfs/etc/passwd    パーミッションは644
root:x:0:0:root:/root:/bin/sh

tama:x:1000:1000:tama:/home/tama:/bin/sh

### ~/umu/UmuOSver01/initramfs/rootfs/etc/shadow    パーミッションは600
### フォーマット: 
### ユーザー名:パスワードハッシュ:最終変更日:最小日数:最大日数:警告日数:非アクティブ:有効期限
### root のパスワードは必須。tama は任意だが、ログイン時にパスワード入力を求めるなら設定する。
### パスワードハッシュ生成方法:
###   1. SHA-512 方式 (openssl)
###      $ openssl passwd -6  ※-6 は SHA-512 (crypt方式) を使うという指定です。
###      → パスワードを入力すると $6$... 形式のハッシュが出力される
###
###   2. yescrypt 方式 (mkpasswd)
###     $ mkpasswd --method=yescrypt
###     → パスワードを入力すると $y$j9T$... 形式のハッシュが出力される
###
### 生成したハッシュを以下のフィールドに貼り付ける。
### root / tama のパスワードは各自で生成して設定する。

root:<ROOT_HASH_HERE>:19000:0:99999:7:::

tama:<TAMA_HASH_HERE>:19000:0:99999:7:::

例：SHA-512 ハッシュ生成（対話入力）

openssl passwd -6

所有者・権限（ホスト側で設定してから initrd を作る）:

sudo chown root:root ~/umu/UmuOSver01/initramfs/rootfs/etc/passwd ~/umu/UmuOSver01/initramfs/rootfs/etc/shadow

sudo chmod 644 ~/umu/UmuOSver01/initramfs/rootfs/etc/passwd

sudo chmod 600 ~/umu/UmuOSver01/initramfs/rootfs/etc/shadow


3.3 init（C言語）作成

ここでは、Step4までの init シェルスクリプトと同じ動きをする init を C言語で実装し、
initramfs の /init として配置する。

動作要件（シェル版と同等）:
- /proc, /sys, /dev をマウント
- /dev/pts（devpts）をマウント（telnet/login の pseudo-tty 用）
- /proc/cmdline に "single" が含まれる場合は /bin/sh を起動
- 通常は getty を複数起動し、**シリアル（ttyS0）は必須**、画面側（tty1）も提供する
  - 例（BusyBox getty 想定。引数順は「ボーレート→TTY→TERM」）：
    - `getty -L 115200 ttyS0 vt100`
    - `getty 0 tty1 linux`

ソース配置先（リポジトリ管理用）:

### ~/umu/UmuOSver01/initramfs/src/init.c

ビルドと配置:

cd ~/umu/UmuOSver01/initramfs

### rootfs 側の /init（PID 1）として配置するため、基本は静的リンクでビルドする
gcc -static -Os -s -o rootfs/init src/init.c

### 実行パーミッション（/init は 755 推奨）
chmod 755 rootfs/init

### initramfs へ格納するファイルの所有者を root に揃えたい場合（任意）
sudo chown root:root rootfs/init


3.4 cpioアーカイブ作成

cd rootfs

### 注意：/etc/shadow を 600（rootのみ読み取り）にするため、initrd 作成は sudo で実行する。
### VS Code 等のコピペで Markdown リンクが混ざらないよう、ターミナルには生テキストで入力する。
find . -print0 | sudo cpio --null -o -H newc | gzip > ../initrd.img-6.18.1

cd ..

cp initrd.img-6.18.1 ../iso_root/boot/

ここまでで initrd の更新が iso_root に反映された。
次に ISO を再生成し、ローカルで起動確認（任意）する。

ISO再生成（UmuOSver01 で実行）:

cd ~/umu/UmuOSver01

grub-mkrescue -o UmuOSver01-boot.iso iso_root

ローカル起動テスト（QEMU/UEFI）:

補足：起動ログは基本的にコンソール（virt-manager の表示/シリアル）に出力される。
syslog/journald は用意していないため、ログをファイルに残したい場合はホスト側で保存する。
例（ホスト側）：

qemu-system-x86_64 ... -serial mon:stdio 2>&1 | tee boot.log

GUI無し環境（SSH先・DISPLAY無し等）:

cd ~/umu/UmuOSver01

qemu-system-x86_64 -m 2048 -smp 2 -machine q35,accel=kvm -cpu host \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/OVMF_VARS_umuos.fd \
  -cdrom UmuOSver01-boot.iso -boot d \
  -drive file=disk/umuos.ext4.img,if=virtio,format=raw \
  -nic user,model=virtio-net-pci \
  -display none \
  -serial mon:stdio

補足（環境差の吸収）:

- KVM が使えない環境では `accel=kvm` が失敗するため、`-machine q35,accel=tcg` にする。
- OVMF のパスはディストリにより異なる。見つからない場合は以下で探索する。

  dpkg -L ovmf | grep -E 'OVMF_(CODE|VARS).*fd$'

GUIあり環境（デスクトップ等）：上記から `-display none` を外す。


4. GRUB設定

### ~/umu/UmuOSver01/iso_root/boot/grub/grub.cfg
set timeout=20

set default=0

menuentry "Umu Project Linux kernel 6.18.1" {
  linux /boot/vmlinuz-6.18.1 ro console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}

menuentry "Umu Project rescue 6.18.1" {
  linux /boot/vmlinuz-6.18.1 ro single console=tty0 console=ttyS0,115200n8
  initrd /boot/initrd.img-6.18.1
}

### virt-manager 側の要件（TTY接続）

- VM に **Serial** デバイス（例：`pty`）と **Console** を追加する。
- ホスト側は virsh console <vmname> または割り当てられた pts へ screen <pts> 115200 等で接続し、ttyS0 の 
  getty が表示されること。

重要：virt-manager の Console が「VGA（グラフィック表示）」になっていると、起動ログが途中で止まったように見えることがある。
その場合は Console の設定（VMC）を **Serial（シリアル）** に変更し、ttyS0 側でログインできることを確認する。


1. ISOイメージ作成

cd ~/umu/UmuOSver01

grub-mkrescue -o UmuOSver01-boot.iso iso_root

補足：VS Code などでコマンドを共有/コピーする際、ファイル名が Markdown リンク（例：`[UmuOSver01-boot.iso](...)`）になることがある。
この形式がコマンドに混ざると bash が `(` を解釈して構文エラーになるため、
ターミナルには **生のパス**（例：`UmuOSver01-boot.iso` や `/home/.../UmuOSver01-boot.iso`）のみを入力する。

補足：GUI（GTK）が使えない環境では `gtk initialization failed` で起動に失敗する。
その場合は「3.4 の QEMU 例」のように `-display none` を付けて起動し、シリアル（stdio）でログインする。
GUI が使える環境（デスクトップ/virt-manager）では `-display none` を外してよい。


6. 永続ストレージ（ext4）

どのタイミングで ext4 を使えるようにするか：
- ver0.1：root は initramfs のまま維持し、/home 等を ext4 に載せ替えて永続化する（推奨）
- ver0.2以降：ext4 を /（ルート）にして switch_root/pivot_root する（発展）

6.1 ext4イメージ作成（ホスト側で作成）

cd ~/umu/UmuOSver01/disk

truncate -s 2G umuos.ext4.img

mkfs.ext4 -F -L UMU_PERSIST umuos.ext4.img

※パーティションを切らず「ディスク全体を ext4」として使う（手順を簡単化）。

6.2 virt-manager へ永続ディスクを追加

- VM に Storage を追加し、既存イメージとして umuos.ext4.img を指定
- バスは VirtIO 推奨（ゲスト側は多くの場合 /dev/vda）

6.3 init（C言語）で ext4 をマウントして永続領域を使う

マウントのタイミング：/proc, /sys, /dev（devtmpfs）が揃い、/dev 配下にブロックデバイスが出現した後。

注意：initramfs だけで動かす構成のため、ext4 と virtio-blk は「モジュール」ではなくカーネルに built-in（=y）で入っている必要がある。
mount -t ext4 が unknown filesystem になる場合は、.config で以下を確認する。
- CONFIG_EXT4_FS=y
- CONFIG_VIRTIO=y / CONFIG_VIRTIO_PCI=y / CONFIG_VIRTIO_BLK=y

補足：起動直後は /dev/vda がまだ出現していないことがあるため、init 側では短時間リトライしてから mount を試みる。

起動後の確認（ゲスト側）:
- `ls -l /dev/vda`（virtio-blk の想定。環境により /dev/vdb や /dev/sda の可能性もある）
- `cat /proc/mounts | grep -E ' /persist | /home '`
  - 期待：`/dev/vda /persist ext4 ...`
  - 期待：`/home` が ext4 側に載っていること
    - bind mount の結果は環境により `/proc/mounts` 上で `source` が `/persist/home` ではなく、
      `/dev/vda /home ext4 ...` のように **デバイス名で表示**されることがある（※正常）。

うまくいかない時の切り分け:
- `mount(ext4 ... ) failed: No such file or directory` → デバイス名が違う/未出現
- `mount(ext4 ... ) failed: No such device` → ext4/ブロックドライバが built-in でない可能性（上の CONFIG を確認）

起動後に実行されるコマンド相当（検証用）：
- mkdir -p /persist
- mount -t ext4 /dev/vda /persist
- mkdir -p /persist/home
- mount --bind /persist/home /home

初回起動の補足（推奨）：
- /persist/home/tama が無い場合は作成し、所有者を 1000:1000（tama）にする

仕様として明記すること（ver0.1）：
- 永続化対象：/home（少なくとも /home/tama）
- 永続ディスクが無い/マウント失敗時も起動は継続（ログに残す）


7. telnet 接続

目的：ホスト（自宅サーバー）からゲスト UmuOS に telnet 接続してログインできるようにする。

注意：telnet は平文であり安全ではない。検証用途（ローカル/隔離ネットワーク）に限定する。

7.1 ネットワーク設定（静的IP / DHCP）

UmuOSver0.1 は BusyBox の `ip` を使ってネットワークを設定する。

### virt-manager 側（どのネットワークにぶら下げるか）

- まず簡単なのは「Virtual network: default（NAT）」
  - ホストからゲストへは基本的に到達できる（同一ホスト上からの検証向き）
  - ネットワーク帯は環境により異なるが、多くは `192.168.0.204/24`（GW: `192.168.0.1`）
  - 静的IPを振る場合は、この帯域の未使用アドレスを選ぶ（例：`192.168.122.50/24`）
- 別ホストからも telnet したい場合は「ブリッジ（Bridge: br0 など）」が必要になることが多い
  - LAN からゲストへ直接到達できるネットワーク設計にする

### ゲスト側（手動で静的IPを設定）

起動後に実行（例。`eth0` や GW は環境に合わせて変更）:

- `ip link set dev eth0 up`
- `ip addr add 192.168.0.204/24 dev eth0`
- `ip route add default via 192.168.0.1 dev eth0`
- `echo 'nameserver 8.8.8.8' > /etc/resolv.conf`

これを設定したら即、アクティブになる。

### ゲスト側（起動時に自動で静的IPを設定）

自作 init はカーネルcmdlineで以下を受け取ると、DHCP（udhcpc）を実行せず静的IPを設定する。

- `umuip=<IP/CIDR>` 例：`umuip=192.168.0.204/24`
- `umugw=<GW>` 例：`umugw=192.168.0.1`
- `umudns=<DNS>` 例：`umudns=8.8.8.8`
- `umuifname=<IFNAME>`（任意）例：`umuifname=eth0` / `umuifname=ens3`

設定例（GRUBの linux 行に追記）：

`umuip=192.168.0.204/24 umugw=192.168.0.1 umudns=8.8.8.8`

DHCP運用にしたい場合は、上記 `umuip=` を付けずに起動する（従来どおり udhcpc を起動）。

7.2 telnetd 起動

BusyBox の telnetd を利用し、ログインプログラムは /bin/login を使用する。

事前確認（ホスト側で BusyBox の機能を確認）：
- busybox | grep -E "(telnetd|login|udhcpc|ip)"

起動コマンド例：
- telnetd -l /bin/login

7.3 ホストから接続

- ゲスト側で ip addr でIP確認
- ホストから telnet <guest-ip>
- root / tama それぞれでログインできること（パスワードは 3.2 の /etc/shadow で設定したもの）

7.4 検証（telnet）

目的：telnet接続が「ログインまで」成立していることを確認する。

ゲスト側（コンソール/シリアル）：
- ps で telnetd が起動していること
- ip addr / ip route でIPが付与されていること（udhcpc成功）

ホスト側：
- telnet <guest-ip> で接続し、root/tama それぞれでログインできること
- tama でログイン後、whoami が tama になること

7.5 検証（マルチユーザー）

目的：同時ログインが成立することを確認する。

- ホストでターミナルを2つ用意し、それぞれ telnet <guest-ip>
- 片方で root、もう片方で tama でログインできること


8. 検証（ext4永続化）

8.1 マウント状態確認（ゲスト側）

- mount で /persist が ext4 としてマウントされていること
- mount で /home が /persist/home の bind mount になっていること
- ls -ld /home/tama の所有者が tama (1000) になっていること

8.2 永続化確認（再起動）

手順：
- tama でログインし、/home/tama にファイルを作成する
  - 例：echo "persist-test" > /home/tama/persist.txt
- poweroff で停止 → 再起動
- 再ログイン後、/home/tama/persist.txt が残っていること




