#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${BASE_DIR}/logs"

PORT="${1:-5555}"
mkdir -p "${LOG_DIR}"

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/ttyS1_${TS}.log"

echo "[connect_ttyS1] tcp: 127.0.0.1:${PORT}"
echo "[connect_ttyS1] log: ${LOG_FILE}"
echo "[connect_ttyS1] NOTE: If you only see 'Connected' and no prompt, press Enter a few times."

exec script -q -f -c "telnet 127.0.0.1 ${PORT}" "${LOG_FILE}"
