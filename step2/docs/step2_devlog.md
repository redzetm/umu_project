# Umu Project Step2 実装ログ

## [2025-12-07 07:28] 1.1 必要パッケージのインストール
- 実行: sudo apt update
        sudo apt install -y build-essential bc bison flex libssl-dev \
            libelf-dev libncurses-dev dwarves git wget \
            grub-efi-amd64-bin grub-common xorriso mtools \
            qemu-system-x86 ovmf \
            cpio gzip busybox-static
- 結果: OK。全パッケージ導入済み
- 課題: 特になし

## [2025-12-07 07:33] 1.2 ディレクトリ作成
- 実行: mkdir -p ~/umu/step2/{kernel,initramfs,iso_root/boot/grub,logs}
- 結果: OK
- 課題: 特になし

## [2025-12-07 07:35] 2.1 ソース取得と解凍
- 実行: cd ~/umu/step2/kernel
        wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.58.tar.xz
        tar -xf linux-6.6.58.tar.xz
        cd linux-6.6.58
- 結果: OK
- 課題: 特になし

## [2025-12-07 07:41] 2.2 カーネル設定（今回はデフォルト設定でビルド。）
- 実行: cd linux-6.6.58
        make mrproper     ※カーネルソースツリーを「完全初期化」する
        make defconfig    ※Linuxカーネルのビルドにおける 「デフォルト設定ファイル（.config）の生成」 を行う
        cp .config ../config-6.6.58    ※バックアップを~/umu/step2/kernelに保存
- 結果: OK
- 課題: 特になし

## [2025-12-07 08:21] 2.3 カーネルビルド（今回はデフォルト設定でビルド。）
- 実行: make -j$(nproc)
        ※make -j$(nproc)は、make defconfigで生成した.configを使いカーネルを
        ビルドする。
- 結果: arch/x86/boot/bzImageとしてカーネルイメージ生成できた。
- 課題: 特になし

## [2025-12-07 08:48] 2.4 成果物コピー
- 実行: cp arch/x86/boot/bzImage ~/umu/step2/iso_root/boot/vmlinuz-6.6.58
        ※bzImageをブートイメージとしてvmlinuz-6.6.58として~/umu/step2/iso_root/boot/にコピー
        vmlinuz-6.6.58は、GRUB（ブートローダー）が読み込む。

        cp .config ~/umu/step2/iso_root/boot/config-6.6.58
        ※vmlinuz-6.6.58と同じディレクトリに入れてるけど、もしconfig-6.6.58が無くても
        影響しない。慣例的に同じディレクトリに入れてる
- 結果: OK
- 課題: 特になし

## [2025-12-07 14：13] 3.1 構造作成(initramfs（BusyBox版）)
- 実行: cd ~/umu/step2/initramfs
# 作業ディレクトリを initramfs に移動

mkdir -p rootfs/{bin,sbin,etc,proc,sys,dev,home/tama,root}
# initramfs 内に必要なディレクトリ構造を作成
# - bin: 基本コマンド配置
# - sbin: 管理者用コマンド配置
# - etc: 設定ファイル配置
# - proc: procfs マウントポイント
# - sys: sysfs マウントポイント
# - dev: デバイスファイル配置
# - home/tama: ユーザー tama のホームディレクトリ
# - root: root ユーザーのホームディレクトリ

cp /usr/bin/busybox rootfs/bin/
# busybox バイナリを rootfs/bin にコピー
# → initramfs 内で基本コマンドを提供するため

cd ~/umu/step2/initramfs/rootfs/bin
# busybox を配置した bin ディレクトリに移動

# busybox --install -s ~/umu/step2/initramfs/rootfs/bin
busybox --install -s .
# busybox が提供するコマンド群を ~/umu/step2/initramfs/rootfs/bin にシンボリックリンクとして展開
# 例: ln -s busybox ls, ln -s busybox cat など
# → initramfs 内で ls, cat, ps, su などが利用可能になる
# ~/umu/step2/initramfs/rootfs/bin内にBusyBoxのlnによるコマンドが大量にできるので
# VScodeの.gitignoreファイルで追跡除外を設定

cd ~/umu/step2/initramfs
# 作業ディレクトリを initramfs のルートに戻す
- 結果: OK
- 課題: 特になし

## [2025-12-07 14：37] 3.2 ユーザー構成
- 実行: /etc/passwd と /etc/shadow を作成する。
※ /etc/passwd はユーザー情報、/etc/shadow はパスワードハッシュを保持する。
※ su による root 昇格を計画通り動作させるため、root のパスワードは必須。
※ パスワードハッシュは openssl や mkpasswd で生成し、ここに埋め込む。

# ~/umu/step2/initramfs/rootfs/etc/passwd    パーミッションは644
root:x:0:0:root:/root:/bin/sh        # root ユーザー。ホームは /root、シェルは /bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh  # 一般ユーザー tama。ホームは /home/tama、シェルは /bin/sh

# ~/umu/step2/initramfs/rootfs/etc/shadow    パーミッションは600
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
- 結果: OK
- 課題: 特になし

## [2025-12-07 16：27] 3.3 initスクリプト作成
- 実行: ~/umu/step2/initramfs/rootfs/init    ※initファイル作成

#!/bin/sh
# filepath: ~/umu/step2/initramfs/rootfs/init

# 仮想ファイルシステムのマウント
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

# カーネルコマンドライン引数を取得
CMDLINE=$(cat /proc/cmdline)

# 起動モード判定
if echo "$CMDLINE" | grep -q "single"; then
  # シングルユーザーモード（レスキューモード）
  echo "Umu Project Step2: Single-user rescue mode"
  exec /bin/sh
else
  # マルチユーザーモード（通常起動）
  echo "Umu Project Step2: Multi-user mode"
  exec /bin/getty -L ttyS0 115200 vt100
fi

chmod +x rootfs/init    
#実行パーミッション追加  本来はrootユーザー所有が望ましいが今回は研究様なのでtamaのままとする755
- 結果: OK
- 課題: 特になし

## [2025-12-07 16：27] 3.4 cpioアーカイブ作成
- 実行: 
cd rootfs
find . | cpio -o -H newc | gzip > ../initrd.img-6.6.58
cd ..
cp initrd.img-6.6.58 ../iso_root/boot/
- 結果: OK
- 課題: 特になし

# [2025-12-07 17：06] 4. GRUB設定
- 実行: # ~/umu/step2/iso_root/boot/grub/grub.cfg
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
- 結果: OK
- 課題: 特になし

# [2025-12-07 17:16] 5. ISOイメージ作成
- 実行: 
- cd ~/umu/step2
- grub-mkrescue -o step2-boot.iso iso_root
- 結果: OK
- 課題: 特になし

# [2025-12-07 17:54] 6. QEMU検証
- 実行: 
-cd ~/umu/step2
qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 \
  -cdrom step2-boot.iso \
  -nographic
- 結果: kernel panicでinitのあたりで止まる
- 課題: 改善修正

[    3.722726]  dump_stack_lvl+0x36/0x50
[    3.722726]  panic+0x174/0x330
[    3.722726]  do_exit+0x956/0xac0
[    3.722726]  do_group_exit+0x2c/0x80
[    3.722726]  __x64_sys_exit_group+0x13/0x20
[    3.722726]  do_syscall_64+0x39/0x90
[    3.722726]  entry_SYSCALL_64_after_hwframe+0x78/0xe2
[    3.722726] RIP: 0033:0x4474cd
[    3.722726] Code: 66 2e 0f 1f 84 00 00 00 00 00 0f 1f 00 f3 0f 1e fa 48 c7 c6 e0 ff ff ff ba e7 00 00 00 eb 07 66 0f 1f 44 00 00 f4 e
[    3.722726] RSP: 002b:00007fff43749f28 EFLAGS: 00000202 ORIG_RAX: 00000000000000e7
[    3.722726] RAX: ffffffffffffffda RBX: 0000000000000001 RCX: 00000000004474cd
[    3.722726] RDX: 00000000000000e7 RSI: ffffffffffffffe0 RDI: 0000000000000001
[    3.722726] RBP: 00007fff43749f80 R08: 0000000000000001 R09: 0000000000000007
[    3.722726] R10: 0000000004f4e700 R11: 0000000000000202 R12: 0000000000000000
[    3.722726] R13: 0000000000000001 R14: 0000000000000001 R15: 0000000000606720
[    3.722726]  </TASK>
[    3.722726] Kernel Offset: 0x400000 from 0xffffffff81000000 (relocation range: 0xffffffff80000000-0xffffffffbfffffff)
[    3.722726] ---[ end Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100 ]---

ここで止まる

確認
原因の可能性：

/bin/getty が存在しない、または実行できない
busyboxにgettyが含まれているか確認が必要。
回答
/bin/getty    存在確認済み
/sbin/getty   存在確認済み

/bin/gettyの依存ライブラリが不足
busybox-staticを使っているはずだが、initramfs内に正しく配置されているか確認。
回答
存在確認済み

initスクリプトのexecが失敗している
exec /bin/getty -L ttyS0 115200 vt100が失敗すると、initプロセスが終了してkernel panicになる。
回答
改善策を知りたいです

パスワードハッシュがexampleRootHashHereのまま
回答
ハッシュですでに変更している（GutHubでPublicにしてるからダミーで記載

6. kernel panicの直接原因を特定するための追加ログ
initスクリプトにデバッグログを追加：

#!/bin/sh

# デバッグメッセージを追加
echo "[DEBUG] Mounting proc, sys, dev..."
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo "[DEBUG] Reading kernel cmdline..."
CMDLINE=$(cat /proc/cmdline)
echo "[DEBUG] CMDLINE=$CMDLINE"

if echo "$CMDLINE" | grep -q "single"; then
  echo "[DEBUG] Single-user rescue mode"
  exec /bin/sh
else
  echo "[DEBUG] Multi-user mode, starting getty..."
  
  # gettyが存在するか確認
  if [ ! -x /bin/getty ]; then
    echo "[ERROR] /bin/getty not found or not executable!"
    exec /bin/sh  # フォールバック
  fi
  
  exec /bin/getty -L ttyS0 115200 vt100
fi
回答
変更した



# initスクリプトを本番用に修正
vim ~/umu/step2/initramfs/rootfs/init     修正した

# initramfs再作成
cd ~/umu/step2/initramfs/rootfs
find . | cpio -o -H newc | gzip > ../initrd.img-6.6.58

# ISO更新
cd ~/umu/step2
cp initramfs/initrd.img-6.6.58 iso_root/boot/
grub-mkrescue -o step2-boot.iso iso_root

# QEMU検証
qemu-system-x86_64 -enable-kvm -m 2048 -cdrom step2-boot.iso -nographic






















