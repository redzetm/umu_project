#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${PROJECT_DIR}/logs"
RUN_DIR="${PROJECT_DIR}/run"
ISO="${PROJECT_DIR}/UmuOS-0.1.2-boot.iso"
DISK="${PROJECT_DIR}/disk/disk.img"

mkdir -p "${LOG_DIR}" "${RUN_DIR}"

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/qemu-serial_${TS}.log"

echo "[umuOSstart] log: ${LOG_FILE}"

OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS_SRC="/usr/share/OVMF/OVMF_VARS_4M.fd"
OVMF_VARS="${RUN_DIR}/OVMF_VARS_umuos_0_1_2.fd"

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
  -monitor none
)

exec script -q -f --command "$(printf '%q ' "${QEMU_CMD[@]}")" "${LOG_FILE}"
