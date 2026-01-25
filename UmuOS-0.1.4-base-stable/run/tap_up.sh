#!/usr/bin/env bash
set -euo pipefail

TAP_IF="${1:-tap-umu}"
BRIDGE="${BRIDGE:-br0}"
OWNER="${OWNER:-${SUDO_USER:-}}"

say() { echo "[tap_up] $*"; }
die() { echo "[tap_up] ERROR: $*" >&2; exit 1; }

if [[ ${EUID} -ne 0 ]]; then
  die "run with sudo (needs CAP_NET_ADMIN)"
fi

if ! ip link show "${BRIDGE}" >/dev/null 2>&1; then
  die "bridge '${BRIDGE}' not found. Set BRIDGE=... or create br0, then retry"
fi

# idempotent
ip link set dev "${TAP_IF}" down >/dev/null 2>&1 || true
ip link del dev "${TAP_IF}" >/dev/null 2>&1 || true

if [[ -n "${OWNER}" ]]; then
  if ip tuntap add dev "${TAP_IF}" mode tap user "${OWNER}" 2>/dev/null; then
    say "created tap '${TAP_IF}' (owner=${OWNER})"
  else
    ip tuntap add dev "${TAP_IF}" mode tap
    say "created tap '${TAP_IF}' (owner option unsupported; you may need to run QEMU as root)"
  fi
else
  ip tuntap add dev "${TAP_IF}" mode tap
  say "created tap '${TAP_IF}' (no owner set; OK if QEMU runs as root. If you run QEMU as a non-root user, set OWNER=USER and re-run)"
fi

ip link set dev "${TAP_IF}" master "${BRIDGE}"
ip link set dev "${TAP_IF}" up

ip -d link show "${TAP_IF}"
