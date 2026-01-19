#!/usr/bin/env bash
set -euo pipefail

LOG_DIR=~/umu/umu_project/UmuOS-0.1.3/logs
RUN_DIR=~/umu/umu_project/UmuOS-0.1.3/run
ISO=~/umu/umu_project/UmuOS-0.1.3/UmuOS-0.1.3-boot.iso
DISK=~/umu/umu_project/UmuOS-0.1.3/disk/disk.img
TTYS1_PORT=5555

die() {
  echo "[umuOSstart] ERROR: $*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

need_file() {
  [[ -f "$1" ]] || die "file not found: $1"
}

need_cmd qemu-system-x86_64
need_cmd script

need_file "${ISO}"
need_file "${DISK}"

mkdir -p "${LOG_DIR}" "${RUN_DIR}"

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/qemu-serial_${TS}.log"

echo "[umuOSstart] log: ${LOG_FILE}"

echo "[umuOSstart] ttyS1 tcp: 127.0.0.1:${TTYS1_PORT}"

# ttyS1ポートが既に使われていたら、接続が混線するので止める
if command -v ss >/dev/null 2>&1; then
  if ss -ltn 2>/dev/null | awk '{print $4}' | grep -qE ":${TTYS1_PORT}$"; then
    die "port ${TTYS1_PORT} is already in use (stop old QEMU, then retry)"
  fi
fi

OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS_SRC="/usr/share/OVMF/OVMF_VARS_4M.fd"
OVMF_VARS="${RUN_DIR}/OVMF_VARS_umuos_0_1_3.fd"

# 環境差吸収（パスが違う場合がある）
if [[ ! -f "${OVMF_CODE}" ]]; then
  for cand in \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/edk2/ovmf/OVMF_CODE.fd
  do
    if [[ -f "${cand}" ]]; then OVMF_CODE="${cand}"; break; fi
  done
fi

if [[ ! -f "${OVMF_VARS_SRC}" ]]; then
  for cand in \
    /usr/share/OVMF/OVMF_VARS.fd \
    /usr/share/edk2/ovmf/OVMF_VARS.fd
  do
    if [[ -f "${cand}" ]]; then OVMF_VARS_SRC="${cand}"; break; fi
  done
fi

if [[ ! -f "${OVMF_CODE}" || ! -f "${OVMF_VARS_SRC}" ]]; then
  echo "[umuOSstart] ERROR: OVMF files not found" >&2
  echo "[umuOSstart] OVMF_CODE=${OVMF_CODE}" >&2
  echo "[umuOSstart] OVMF_VARS_SRC=${OVMF_VARS_SRC}" >&2
  exit 1
fi

if [[ ! -f "${OVMF_VARS}" ]]; then
  cp -f "${OVMF_VARS_SRC}" "${OVMF_VARS}"
fi

QEMU_CMD=(
  qemu-system-x86_64
  -machine q35,accel=tcg
  -m 1024
  -cpu qemu64
  -drive if=pflash,format=raw,readonly=on,file="${OVMF_CODE}"
  -drive if=pflash,format=raw,file="${OVMF_VARS}"
  -cdrom "${ISO}"
  -drive file="${DISK}",format=raw,if=virtio
  -nographic
  -serial stdio
  -serial tcp:127.0.0.1:${TTYS1_PORT},server,nowait,telnet
  -monitor none
)

exec script -q -f --command "$(printf '%q ' "${QEMU_CMD[@]}")" "${LOG_FILE}"
