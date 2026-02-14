#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO_FILE="${BASE_DIR}/UmuOS-0.1.6-boot.iso"
DISK_FILE="${BASE_DIR}/disk.img"

TTYS1_PORT="${TTYS1_PORT:-5555}"
TAP_IF="${TAP_IF:-tap-umu}"
BRIDGE="${BRIDGE:-br0}"
NET_MODE="${NET_MODE:-tap}"

say() { echo "[UmuOS-0.1.6-dev_start] $*"; }
die() { echo "[UmuOS-0.1.6-dev_start] ERROR: $*" >&2; exit 1; }

[[ -f "${ISO_FILE}" ]] || die "file not found: ${ISO_FILE}"
[[ -f "${DISK_FILE}" ]] || die "file not found: ${DISK_FILE}"

need_cmd() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }
find_qemu() {
	if [[ -x /usr/libexec/qemu-kvm ]]; then echo /usr/libexec/qemu-kvm; return 0; fi
	if command -v qemu-kvm >/dev/null 2>&1; then command -v qemu-kvm; return 0; fi
	if command -v qemu-system-x86_64 >/dev/null 2>&1; then command -v qemu-system-x86_64; return 0; fi
	return 1
}

if [[ ${EUID} -ne 0 ]]; then
	say "This script usually needs root (KVM/TAP). Re-running via sudo..."
	exec sudo -E "${BASH_SOURCE[0]}" "$@"
fi

need_cmd ip
need_cmd script

QEMU_BIN="$(find_qemu)" || die "qemu binary not found"

if [[ "${NET_MODE}" == "tap" ]]; then
	if ! ip link show "${BRIDGE}" >/dev/null 2>&1; then
		die "bridge '${BRIDGE}' not found (or set NET_MODE=none)"
	fi
	if ! ip link show "${TAP_IF}" >/dev/null 2>&1; then
		ip link del dev "${TAP_IF}" >/dev/null 2>&1 || true
		ip tuntap add dev "${TAP_IF}" mode tap
		ip link set dev "${TAP_IF}" master "${BRIDGE}"
		ip link set dev "${TAP_IF}" up
		say "created tap '${TAP_IF}' and attached to '${BRIDGE}'"
	else
		ip link set dev "${TAP_IF}" master "${BRIDGE}" >/dev/null 2>&1 || true
		ip link set dev "${TAP_IF}" up >/dev/null 2>&1 || true
	fi
elif [[ "${NET_MODE}" == "none" ]]; then
	:
else
	die "invalid NET_MODE='${NET_MODE}' (expected: tap|none)"
fi

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${BASE_DIR}/host_qemu.console_${TS}.log"

QEMU_CMD=(
	"${QEMU_BIN}"
	-enable-kvm -cpu host -m 1024
	-machine q35,accel=kvm
	-nographic
	-serial stdio
	-serial "tcp:127.0.0.1:${TTYS1_PORT},server,nowait,telnet"
	-drive "file=${DISK_FILE},format=raw,if=virtio"
	-cdrom "${ISO_FILE}"
	-boot order=d
	-monitor none
)

if [[ "${NET_MODE}" == "tap" ]]; then
	QEMU_CMD+=(
		-netdev "tap,id=net0,ifname=${TAP_IF},script=no,downscript=no"
		-device virtio-net-pci,netdev=net0
	)
fi

say "qemu: ${QEMU_BIN}"
say "log: ${LOG_FILE}"
say "iso: ${ISO_FILE}"
say "disk: ${DISK_FILE}"
say "net: ${NET_MODE}"

CMD_STR="$(printf '%q ' "${QEMU_CMD[@]}")"
exec script -q -f -c "${CMD_STR}" "${LOG_FILE}"
