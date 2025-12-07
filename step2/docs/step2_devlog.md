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

busybox --install -s ~/umu/step2/initramfs/rootfs/bin
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


























