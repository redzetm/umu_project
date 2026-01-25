#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${BASE_DIR}/logs"
RUN_DIR="${BASE_DIR}/run"
ISO="${BASE_DIR}/UmuOS-0.1.4-boot.iso"
DISK="${BASE_DIR}/disk/disk.img"

TTYS1_PORT="${TTYS1_PORT:-5555}"
TAP_IF="${TAP_IF:-tap-umu}"
BRIDGE="${BRIDGE:-br0}"
NET_MODE="${NET_MODE:-tap}"

say() { echo "[umuOSstart] $*"; }
die() { echo "[umuOSstart] ERROR: $*" >&2; exit 1; }

need_cmd() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }
need_file() { [[ -f "$1" ]] || die "file not found: $1"; }

find_qemu() {
  if [[ -x /usr/libexec/qemu-kvm ]]; then
    echo /usr/libexec/qemu-kvm
    return 0
  fi
  if command -v qemu-kvm >/dev/null 2>&1; then
    command -v qemu-kvm
    return 0
  fi
  if command -v qemu-system-x86_64 >/dev/null 2>&1; then
    command -v qemu-system-x86_64
    return 0
  fi
  return 1
}

need_cmd script
need_cmd ip

need_file "${ISO}"
need_file "${DISK}"

mkdir -p "${LOG_DIR}" "${RUN_DIR}"

QEMU_BIN="$(find_qemu)" || die "qemu binary not found (/usr/libexec/qemu-kvm or qemu-kvm or qemu-system-x86_64)"

if command -v ss >/dev/null 2>&1; then
  if ss -ltn 2>/dev/null | awk '{print $4}' | grep -qE ":${TTYS1_PORT}$"; then
    die "port ${TTYS1_PORT} is already in use (stop old QEMU, then retry)"
  fi
fi

if [[ "${NET_MODE}" == "tap" ]]; then
  if ! ip link show "${BRIDGE}" >/dev/null 2>&1; then
    die "bridge '${BRIDGE}' not found. Create it (br0) or set BRIDGE=... (or set NET_MODE=none to boot without networking)"
  fi

  if ! ip link show "${TAP_IF}" >/dev/null 2>&1; then
    if [[ ${EUID} -ne 0 ]]; then
      die "tap interface '${TAP_IF}' not found. Run as root or create it first (e.g. sudo ${BASE_DIR}/run/tap_up.sh ${TAP_IF})"
    fi

    ip link del dev "${TAP_IF}" >/dev/null 2>&1 || true
    ip tuntap add dev "${TAP_IF}" mode tap
    ip link set dev "${TAP_IF}" master "${BRIDGE}"
    ip link set dev "${TAP_IF}" up
    say "created tap '${TAP_IF}' and attached to '${BRIDGE}'"
  else
    # Ensure it is attached/up (idempotent)
    ip link set dev "${TAP_IF}" master "${BRIDGE}" >/dev/null 2>&1 || true
    ip link set dev "${TAP_IF}" up >/dev/null 2>&1 || true
  fi
elif [[ "${NET_MODE}" == "none" ]]; then
  :
else
  die "invalid NET_MODE='${NET_MODE}' (expected: tap|none)"
fi

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/host_qemu.console_${TS}.log"

say "base: ${BASE_DIR}"
say "log: ${LOG_FILE}"
say "ttyS1 tcp: 127.0.0.1:${TTYS1_PORT} (use ${BASE_DIR}/connect_ttyS1.sh)"
say "net: ${NET_MODE}"
if [[ "${NET_MODE}" == "tap" ]]; then
  say "tap: ${TAP_IF}"
  say "bridge: ${BRIDGE}"
fi

QEMU_CMD=(
  "${QEMU_BIN}"
  -enable-kvm -cpu host -m 1024
  -machine q35,accel=kvm
  -nographic
  -serial stdio
  -serial "tcp:127.0.0.1:${TTYS1_PORT},server,nowait,telnet"
  -drive "file=${DISK},format=raw,if=virtio"
  -cdrom "${ISO}"
  -boot order=d
  -monitor none
)

if [[ "${NET_MODE}" == "tap" ]]; then
  QEMU_CMD+=(
    -netdev "tap,id=net0,ifname=${TAP_IF},script=no,downscript=no"
    -device virtio-net-pci,netdev=net0
  )
fi

CMD_STR="$(printf '%q ' "${QEMU_CMD[@]}")"
exec script -q -f -c "${CMD_STR}" "${LOG_FILE}"
