#!/usr/bin/env bash
set -euo pipefail

TAP_IF="${1:-tap-umu}"

say() { echo "[tap_down] $*"; }
die() { echo "[tap_down] ERROR: $*" >&2; exit 1; }

if [[ ${EUID} -ne 0 ]]; then
  die "run with sudo (needs CAP_NET_ADMIN)"
fi

ip link set dev "${TAP_IF}" down >/dev/null 2>&1 || true
ip link del dev "${TAP_IF}" >/dev/null 2>&1 || true
say "deleted tap '${TAP_IF}'"
