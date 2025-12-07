# step3バグの再現までの手順
1. 環境準備

1.1 必要パッケージのインストール

sudo apt update
sudo apt install -y build-essential bc bison flex libssl-dev \
  libelf-dev libncurses-dev dwarves git wget \
  grub-efi-amd64-bin grub-common xorriso mtools \
  qemu-system-x86 ovmf \
  cpio gzip busybox-static

※現在の環境にすでに導入済みのPKGもあるが、Ubuntuでは、上書きは問題ないので
このコマンドラインでも大丈夫です！


1.2 ディレクトリ作成

mkdir -p ~/umu/step3/{kernel,initramfs,iso_root/boot/grub,logs}

2. カーネルビルド

2.1 ソース取得

cd ~/umu/step3/kernel
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.58.tar.xz
tar -xf linux-6.6.58.tar.xz
cd linux-6.6.58


2.2 カーネル設定

今回はデフォルト設定でビルド。

cd linux-6.6.58
make mrproper        ※カーネルソースツリーを「完全初期化」する
make defconfig       ※Linuxカーネルのビルドにおける 「デフォルト設定ファイル（.config）の生成」 を行う
cp .config ../config-6.6.58     ※バックアップを~/umu/step3/kernelに保存

2.3 ビルド

make -j$(nproc)

※make -j$(nproc)は、make defconfigで生成した.configを使いカーネルを
ビルドする。


2.4 成果物コピー

cp arch/x86/boot/bzImage ~/umu/step3/iso_root/boot/vmlinuz-6.6.58
※bzImageをブートイメージとしてvmlinuz-6.6.58として~/umu/step3/iso_root/boot/にコピー
　vmlinuz-6.6.58は、GRUB（ブートローダー）が読み込む。

cp .config ~/umu/step3/iso_root/boot/config-6.6.58
※vmlinuz-6.6.58と同じディレクトリに入れてるけど、もしconfig-6.6.58が無くても
　影響しない。慣例的に同じディレクトリに入れてる

3. initramfs（BusyBox版）

3.1 構造作成

initramfsは、目的は「initramfs（初期RAMファイルシステム）」を構築するための作業場所。
この中で rootfs/ を作り、bin, etc, dev, proc, sys などのディレクトリを配置して、
最小限のLinux環境を再現する。
カーネルが起動直後に展開する「最初のルートファイルシステム」。
本格的なルートファイルシステムに切り替える前に、最低限のコマンドや設定を提供する。
BusyBoxを入れて ls, cat, ps, su などを使えるようにするのが典型的。

cd ~/umu/step3/initramfs

mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,home/tama,root}

cp /usr/bin/busybox rootfs/bin/

cd ~/umu/step3/initramfs/rootfs/bin

busybox --install -s .

cd ~/umu/step3/initramfs


3.2 ユーザー構成

/etc/passwd と /etc/shadow を作成する。
※ /etc/passwd はユーザー情報、/etc/shadow はパスワードハッシュを保持する。
※ su による root 昇格を計画通り動作させるため、root のパスワードは必須。
※ パスワードハッシュは openssl や mkpasswd で生成し、ここに埋め込む。

# ~/umu/step3/initramfs/rootfs/etc/passwd    パーミッションは644
root:x:0:0:root:/root:/bin/sh        # root ユーザー。ホームは /root、シェルは /bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh  # 一般ユーザー tama。ホームは /home/tama、シェルは /bin/sh

# ~/umu/step3/initramfs/rootfs/etc/shadow    パーミッションは600
# フォーマット: 
# ユーザー名:パスワードハッシュ:最終変更日:最小日数:最大日数:警告日数:非アクティブ:有効期限
# root のパスワードは必須。tama は任意だが、ログイン時にパスワード入力を求めるなら設定する。
# パスワードハッシュ生成方法:
#   1. SHA-512 方式 (openssl)
#      $ openssl passwd -6  ※-6 は SHA-512 (crypt方式) を使うという指定です。
#      → パスワードを入力すると $6$... 形式のハッシュが出力される
#
#   2. yescrypt 方式 (mkpasswd)
#      $ mkpasswd --method=yescrypt
#      → パスワードを入力すると $y$j9T$... 形式のハッシュが出力される
#
# 生成したハッシュを以下のフィールドに貼り付ける。
# roor    パスワードは実験用として  UmuR1207  とする
# tama    パスワードは実験用として  UmuT1207  とする

root:$y$j9T$exampleRootHashHere:19000:0:99999:7:::   # root のパスワードハッシュ
tama:$y$j9T$exampleTamaHashHere:19000:0:99999:7:::   # tama のパスワードハッシュ



3.3 initスクリプト作成

# ~/umu/step3/initramfs/rootfs/init

#!/bin/sh

# --- 仮想ファイルシステムのマウント ---
mount -t proc none /proc        # プロセス情報を提供する /proc をマウント
mount -t sysfs none /sys        # カーネルやデバイス情報を提供する /sys をマウント
mount -t devtmpfs none /dev     # デバイスノードを管理する /dev をマウント

# --- カーネル起動時のコマンドライン引数を取得 ---
CMDLINE=$(cat /proc/cmdline)    # GRUB から渡されたカーネルパラメータを読み込む

# --- 起動モードの判定 ---
if echo "$CMDLINE" | grep -q "single"; then
  # カーネルパラメータに "single" が含まれている場合 → シングルユーザーモード
  echo "Umu Project step3: Single-user rescue mode"
  exec /bin/sh                   # root シェルを直接起動（パスワードなしで root ログイン）
else
  # 通常起動の場合 → マルチユーザーモード
  echo "Umu Project step3: Multi-user mode"
  exec /bin/getty -L ttyS0 115200 vt100   # シリアルコンソールにログインプロンプトを表示
fi

chmod +x rootfs/init    #実行パーミッション追加


3.4 cpioアーカイブ作成

cd rootfs
find . | cpio -o -H newc | gzip > ../initrd.img-6.6.58
cd ..
cp initrd.img-6.6.58 ../iso_root/boot/


4. GRUB設定

# ~/umu/step3/iso_root/boot/grub/grub.cfg
set timeout=10
set default=0

menuentry "Umu Project Linux kernel 6.6.58" {
  linux /boot/vmlinuz-6.6.58 ro console=ttyS0,115200
  initrd /boot/initrd.img-6.6.58
}

menuentry "Umu Project rescue 6.6.58" {
  linux /boot/vmlinuz-6.6.58 ro single console=ttyS0,115200
  initrd /boot/initrd.img-6.6.58
}


5. ISOイメージ作成

cd ~/umu/step3
grub-mkrescue -o step3-boot.iso iso_root


6. QEMU検証

cd ~/umu/step3
qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 \
  -cdrom step3-boot.iso \
  -nographic

↑↑↑↑↑↑　　ここで、カーネル起動が止まる　　↑↑↑↑


詳細設計(bug修正).md で検証する

