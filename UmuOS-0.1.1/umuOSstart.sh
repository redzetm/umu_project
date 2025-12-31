#!/bin/bash

# bash が苦手でも追えるように、意図をコメントで残す。
# このスクリプトは「UmuOS-0.1.1 を QEMU+UEFI(OVMF) で起動する」だけ。

# 失敗したら即終了する（途中で失敗しても次へ進んで混線しないように）
set -e

# どこから実行しても、常にプロジェクト直下基準で起動する。
# 例：別ディレクトリから ./UmuOS-0.1.1/umuOSstart.sh を叩いてもOK。
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# 固定パス（0.1.1 の想定）
# - ISO は grub+kernel+initrd を含む
# - disk.img は ext4 永続ディスク（再起動しても中身が残る）
# - OVMF_VARS は UEFI の NVRAM 相当（UEFIが書き換えるのでコピーして使う）
ISO_PATH="$PROJECT_DIR/UmuOS-0.1.1-boot.iso"
DISK_PATH="$PROJECT_DIR/disk/disk.img"
VARS_PATH="$PROJECT_DIR/run/OVMF_VARS_umuos_0_1_1.fd"

# Ubuntu 24.04 + ovmf パッケージ想定。
# もし環境でパスが違う場合は「ここだけ」書き換える。
# - OVMF_CODE は readonly（UEFIファーム本体）
# - OVMF_VARS_TEMPLATE は NVRAM の初期状態（テンプレ）
OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS_TEMPLATE="/usr/share/OVMF/OVMF_VARS_4M.fd"

# QEMU に渡すメモリ量（MiB）
MEM_MB=1024

# 必要なコマンドがあるか（無いならインストールする）
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "[umuOSstart] ERROR: qemu-system-x86_64 not found" >&2
	exit 1
fi

# run/ は「実行時に生成されるファイル置き場」
mkdir -p "$PROJECT_DIR/run"

# 起動に必要なファイルがあるかチェック（無いなら理由を出して止める）
if [[ ! -f "$ISO_PATH" ]]; then
	echo "[umuOSstart] ERROR: ISO not found: $ISO_PATH" >&2
	exit 1
fi

if [[ ! -f "$DISK_PATH" ]]; then
	echo "[umuOSstart] ERROR: disk image not found: $DISK_PATH" >&2
	exit 1
fi

if [[ ! -f "$OVMF_CODE" ]]; then
	echo "[umuOSstart] ERROR: OVMF_CODE not found: $OVMF_CODE" >&2
	echo "[umuOSstart]        Hint: sudo apt install ovmf" >&2
	exit 1
fi

# OVMF_VARS は QEMU 実行中に更新されるので、
# 1) run/ にコピーして
# 2) それを毎回使う
# という運用にする（元のテンプレを汚さない）
if [[ ! -f "$VARS_PATH" ]]; then
	if [[ ! -f "$OVMF_VARS_TEMPLATE" ]]; then
		echo "[umuOSstart] ERROR: OVMF_VARS template not found: $OVMF_VARS_TEMPLATE" >&2
		echo "[umuOSstart]        Hint: sudo apt install ovmf" >&2
		exit 1
	fi
	cp -f "$OVMF_VARS_TEMPLATE" "$VARS_PATH"
fi

echo "[umuOSstart] PROJECT_DIR=$PROJECT_DIR"
echo "[umuOSstart] ISO=$ISO_PATH"
echo "[umuOSstart] DISK=$DISK_PATH"
echo "[umuOSstart] OVMF_CODE=$OVMF_CODE"
echo "[umuOSstart] OVMF_VARS=$VARS_PATH"

# 念のため、プロジェクト直下で起動する
cd "$PROJECT_DIR"

# exec を使うと、このシェルが QEMU プロセスに置き換わる（終了コードが素直になる）
# ここから下は「QEMUの起動オプション」。困ったらこのブロックだけを見ればOK。
#
# 重要：バックスラッシュ（\）で行を継続している途中にコメント行（# ...）を挟むと、
# うっかり後続オプションがコメントアウトされて起動が壊れることがある。
exec qemu-system-x86_64 \
	-machine q35,accel=tcg \
	-m "$MEM_MB" \
	-cpu qemu64 \
	-drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
	-drive if=pflash,format=raw,file="$VARS_PATH" \
	-cdrom "$ISO_PATH" \
	-drive file="$DISK_PATH",format=raw,if=virtio \
	-display none \
	-nographic \
	-serial stdio \
	-monitor none
