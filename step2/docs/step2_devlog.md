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















