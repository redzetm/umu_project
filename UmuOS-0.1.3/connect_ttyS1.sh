#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /dev/pts/N" >&2
  exit 2
fi

PTY="$1"
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${PROJECT_DIR}/logs"

mkdir -p "${LOG_DIR}"

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/ttyS1_${TS}.log"

echo "[connect_ttyS1] pty: ${PTY}"
echo "[connect_ttyS1] log: ${LOG_FILE}"

echo "[connect_ttyS1] NOTE: This logs the ttyS1 session (screen + script)."
exec script -q -f --command "screen ${PTY} 115200" "${LOG_FILE}"
