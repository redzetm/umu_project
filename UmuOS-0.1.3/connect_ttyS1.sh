#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-5555}"
LOG_DIR=~/umu/umu_project/UmuOS-0.1.3/logs

mkdir -p "${LOG_DIR}"

TS="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/ttyS1_${TS}.log"

echo "[connect_ttyS1] tcp: 127.0.0.1:${PORT}"
echo "[connect_ttyS1] log: ${LOG_FILE}"

echo "[connect_ttyS1] NOTE: This logs the ttyS1 session (telnet + script)."
echo "[connect_ttyS1] NOTE: If you only see 'Connected' and no prompt, press Enter a few times to get '(none) login:'."
exec script -q -f --command "telnet 127.0.0.1 ${PORT}" "${LOG_FILE}"
