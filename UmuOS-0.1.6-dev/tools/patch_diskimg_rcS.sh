#!/usr/bin/env bash
set -euo pipefail

say() { echo "[patch_diskimg_rcS] $*"; }
die() { echo "[patch_diskimg_rcS] ERROR: $*" >&2; exit 1; }

usage() {
	cat <<'USAGE'
Usage:
  sudo bash ./tools/patch_diskimg_rcS.sh /path/to/disk.img [path/to/new_rcS]

Defaults:
  new_rcS = ./tools/rcS_umuos016.sh

Environment:
  MNT_DIR=/mnt/umuos016_patch
USAGE
}

DISK_IMG="${1:-}"
NEW_RCS="${2:-}"

if [[ -z "${DISK_IMG}" || "${DISK_IMG}" == "-h" || "${DISK_IMG}" == "--help" ]]; then
	usage
	exit 2
fi

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${NEW_RCS}" ]]; then
	NEW_RCS="${BASE_DIR}/tools/rcS_umuos016.sh"
fi

if [[ ${EUID} -ne 0 ]]; then
	say "Re-running via sudo..."
	exec sudo -E "${BASH_SOURCE[0]}" "$@"
fi

[[ -f "${DISK_IMG}" ]] || die "disk image not found: ${DISK_IMG}"
[[ -f "${NEW_RCS}" ]] || die "rcS file not found: ${NEW_RCS}"

command -v mount >/dev/null 2>&1 || die "missing command: mount"
command -v umount >/dev/null 2>&1 || die "missing command: umount"
command -v mountpoint >/dev/null 2>&1 || die "missing command: mountpoint"

MNT_DIR="${MNT_DIR:-/mnt/umuos016_patch}"

cleanup() {
	sync || true
	if mountpoint -q "${MNT_DIR}"; then
		umount "${MNT_DIR}" || true
	fi
}
trap cleanup EXIT

mkdir -p "${MNT_DIR}"

if mountpoint -q "${MNT_DIR}"; then
	die "already mounted: ${MNT_DIR} (umount it first)"
fi

say "mounting: ${DISK_IMG} -> ${MNT_DIR}"
mount -o loop "${DISK_IMG}" "${MNT_DIR}"

mkdir -p "${MNT_DIR}/etc/init.d"

TS="$(date +%Y%m%d_%H%M%S)"
if [[ -f "${MNT_DIR}/etc/init.d/rcS" ]]; then
	say "backup: /etc/init.d/rcS -> /etc/init.d/rcS.bak.${TS}"
	cp -a "${MNT_DIR}/etc/init.d/rcS" "${MNT_DIR}/etc/init.d/rcS.bak.${TS}"
fi

say "install new rcS"
cp -f "${NEW_RCS}" "${MNT_DIR}/etc/init.d/rcS"
chown root:root "${MNT_DIR}/etc/init.d/rcS" || true
chmod 0755 "${MNT_DIR}/etc/init.d/rcS"

say "done"
