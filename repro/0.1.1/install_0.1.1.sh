#!/usr/bin/env bash

# UmuOS-0.1.1 再現用インストーラ（再現ツール）
#
# 目的:
# - 0.1.1 を何度でも再現できるようにして、失敗コストを下げる
# - 破壊的な実験を躊躇なく行えるようにする
#
# 方針:
# - UmuOS-0.1.1 側の成果物は変更しない（ベース版を温存する）
# - 生成物は、このスクリプトと同じディレクトリ配下に閉じる
# - 作り直しのたびに生成物を削除し、クリーンに初期化する（disk.img も毎回新規）

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}" )" && pwd)"

# ベース版（参照のみ。変更しない）
detect_umu_root() {
	local start_dir="$1"
	local d="$start_dir"
	while true; do
		if [[ -e "$d/UmuOS-0.1.1/disk/disk.img" ]]; then
			echo "$d"
			return 0
		fi
		if [[ "$d" == "/" ]]; then
			return 1
		fi
		d="$(cd -- "$d/.." && pwd)"
	done
}

UMU_ROOT_DETECTED="$(detect_umu_root "$SCRIPT_DIR" 2>/dev/null || true)"
UMU_ROOT_DEFAULT="${UMU_ROOT_DETECTED:-$(cd -- "$SCRIPT_DIR/../.." && pwd)}"
UMU_ROOT="${UMU_ROOT:-$UMU_ROOT_DEFAULT}"
BASE_DIR="$UMU_ROOT/UmuOS-0.1.1"

BASE_DISK="$BASE_DIR/disk/disk.img"
BASE_ISO_ROOT="$BASE_DIR/iso_root"
BASE_START_SH="$BASE_DIR/umuOSstart.sh"

# UMU_ROOT が誤って設定されていると、存在しないパスを見に行って失敗しやすい。
# 自動検出した UMU_ROOT_DEFAULT 側にベース成果物があれば、そちらへフォールバックする。
if [[ "$UMU_ROOT" != "$UMU_ROOT_DEFAULT" ]]; then
	DEFAULT_BASE_DIR="$UMU_ROOT_DEFAULT/UmuOS-0.1.1"
	DEFAULT_BASE_DISK="$DEFAULT_BASE_DIR/disk/disk.img"
	if [[ ! -e "$BASE_DISK" && -e "$DEFAULT_BASE_DISK" ]]; then
		echo "[install_0.1.1] WARN: UMU_ROOT points to missing base artifacts: $UMU_ROOT" >&2
		echo "[install_0.1.1]       Fallback to auto-detected UMU_ROOT: $UMU_ROOT_DEFAULT" >&2
		UMU_ROOT="$UMU_ROOT_DEFAULT"
		BASE_DIR="$DEFAULT_BASE_DIR"
		BASE_DISK="$DEFAULT_BASE_DISK"
		BASE_ISO_ROOT="$BASE_DIR/iso_root"
		BASE_START_SH="$BASE_DIR/umuOSstart.sh"
	fi
fi

# 生成先は後で決める（OUT_PARENT + /UmuOS-0.1.1）
TARGET_DIR=""
TARGET_DISK=""
TARGET_ISO_ROOT=""
TARGET_ISO=""
TARGET_START_SH=""

usage() {
	cat <<'USAGE'
使い方:
	./install_0.1.1.sh

入力（対話または環境変数）:
	USER_NAME             ユーザ名（プロンプトのデフォルトとして利用）
	OUT_PARENT            生成先の親ディレクトリ（プロンプトのデフォルトとして利用）

例:
	./install_0.1.1.sh

USAGE
}

need_cmd() {
	local cmd="$1"
	if ! command -v "$cmd" >/dev/null 2>&1; then
		echo "[install_0.1.1] ERROR: required command not found: $cmd" >&2
		exit 1
	fi
}

require_base_file() {
	local path="$1"
	if [[ ! -e "$path" ]]; then
		echo "[install_0.1.1] ERROR: base artifact not found: $path" >&2
		echo "[install_0.1.1]        Hint: verify UMU_ROOT or base UmuOS-0.1.1 directory" >&2
		exit 1
	fi
}

mkpasswd_sha512() {
	local plain="$1"
	# openssl passwd -6 は SHA-512 crypt を生成
	if command -v openssl >/dev/null 2>&1; then
		openssl passwd -6 -- "$plain"
		return 0
	fi
	PW="$plain" python3 - <<'PY'
import crypt, os, sys
pw = os.environ.get('PW')
salt = crypt.mksalt(crypt.METHOD_SHA512)
print(crypt.crypt(pw, salt))
PY
}

patch_grub_root_uuid() {
	local grub_cfg="$1"
	local uuid="$2"
	if [[ ! -f "$grub_cfg" ]]; then
		echo "[install_0.1.1] ERROR: grub.cfg not found: $grub_cfg" >&2
		exit 1
	fi
	# root=UUID=... の値だけを置換
	sed -i -E "s/root=UUID=[0-9a-fA-F-]+/root=UUID=${uuid}/g" "$grub_cfg"
}

debugfs_dump_edit_write() {
	local img="$1"
	local path_in_img="$2"
	local tmp_out="$3"
	local tmp_in="$4"

	debugfs -R "dump ${path_in_img} ${tmp_out}" "$img" >/dev/null 2>&1
	if [[ ! -f "$tmp_out" ]]; then
		echo "[install_0.1.1] ERROR: failed to dump ${path_in_img} from disk image" >&2
		exit 1
	fi

	# tmp_out を編集して tmp_in を作る（呼び出し側が作る）
	if [[ ! -f "$tmp_in" ]]; then
		echo "[install_0.1.1] ERROR: expected edited file not found: $tmp_in" >&2
		exit 1
	fi

	# 既存ファイルへ write することで、パーミッションを維持する
	debugfs -w -R "write ${tmp_in} ${path_in_img}" "$img" >/dev/null 2>&1
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
	usage
	exit 0
fi
if [[ $# -gt 0 ]]; then
	echo "[install_0.1.1] ERROR: this installer is interactive-only; no CLI args are supported" >&2
	usage >&2
	exit 1
fi

need_cmd realpath

need_cmd cp
need_cmd sed
need_cmd awk
need_cmd python3
need_cmd tune2fs
need_cmd e2fsck
need_cmd blkid
need_cmd debugfs
need_cmd grub-mkrescue
need_cmd xorriso

debugfs_replace_file() {
	local img="$1"
	local host_src="$2"
	local dest_path="$3"
	local mode_full="$4" # 例: 0100644（ファイル種別ビット込み）

	# debugfs の write は既存ファイルを上書きできないため、一旦削除して作り直す
	if ! debugfs -w -R "rm ${dest_path}" "$img" >/dev/null 2>&1; then
		echo "[install_0.1.1] ERROR: debugfs rm failed: ${dest_path}" >&2
		exit 1
	fi
	if ! debugfs -w -R "write ${host_src} ${dest_path}" "$img" >/dev/null 2>&1; then
		echo "[install_0.1.1] ERROR: debugfs write failed: ${dest_path}" >&2
		exit 1
	fi
	if ! debugfs -w -R "sif ${dest_path} mode ${mode_full}" "$img" >/dev/null 2>&1; then
		echo "[install_0.1.1] ERROR: debugfs chmod(mode) failed: ${dest_path}" >&2
		exit 1
	fi
}

debugfs_path_exists() {
	local img="$1"
	local path="$2"
	debugfs -R "stat ${path}" "$img" >/dev/null 2>&1
}

DEFAULT_OUT_PARENT="$UMU_ROOT/work"
if [[ ! -d "$DEFAULT_OUT_PARENT" ]]; then
	DEFAULT_OUT_PARENT="$SCRIPT_DIR"
fi

# OUT_PARENT は環境変数をデフォルトとしてのみ扱い、必ず対話で確認する
if [[ -n "${OUT_PARENT:-}" ]]; then
	DEFAULT_OUT_PARENT="$OUT_PARENT"
fi

read -r -p "生成先の親ディレクトリのフルパスを入力してください。入力したフルパスにそのまま作成されます。\
（この直下に UmuOS-0.1.1/ を作ります）（例）[${DEFAULT_OUT_PARENT}]: " OUT_PARENT
if [[ -z "$OUT_PARENT" ]]; then
	OUT_PARENT="$DEFAULT_OUT_PARENT"
fi

OUT_PARENT_EXPANDED="$OUT_PARENT"
if [[ "$OUT_PARENT_EXPANDED" == ~* ]]; then
	OUT_PARENT_EXPANDED="$HOME${OUT_PARENT_EXPANDED:1}"
fi

OUT_PARENT_ABS="$(realpath -m -- "$OUT_PARENT_EXPANDED")"
TARGET_DIR="$OUT_PARENT_ABS/UmuOS-0.1.1"
TARGET_DISK="$TARGET_DIR/disk/disk.img"
TARGET_ISO_ROOT="$TARGET_DIR/iso_root"
TARGET_ISO="$TARGET_DIR/UmuOS-0.1.1-boot.iso"
TARGET_START_SH="$TARGET_DIR/umuOSstart.sh"

echo "[install_0.1.1] SCRIPT_DIR=$SCRIPT_DIR"
echo "[install_0.1.1] UMU_ROOT=$UMU_ROOT"
echo "[install_0.1.1] BASE_DIR=$BASE_DIR"
echo "[install_0.1.1] OUT_PARENT=$OUT_PARENT_ABS"
echo "[install_0.1.1] TARGET_DIR=$TARGET_DIR"

require_base_file "$BASE_DISK"
require_base_file "$BASE_ISO_ROOT"
require_base_file "$BASE_ISO_ROOT/boot/grub/grub.cfg"
require_base_file "$BASE_START_SH"

# --- 入力（最低限）---
DEFAULT_USER_NAME="${USER_NAME:-}"
read -r -p "一般ユーザ名（例: tama）${DEFAULT_USER_NAME:+ [${DEFAULT_USER_NAME}]}: " USER_NAME_IN
if [[ -n "$USER_NAME_IN" ]]; then
	USER_NAME="$USER_NAME_IN"
elif [[ -n "$DEFAULT_USER_NAME" ]]; then
	USER_NAME="$DEFAULT_USER_NAME"
else
	echo "[install_0.1.1] ERROR: USER_NAME is required" >&2
	exit 1
fi

# パスワードは必ず対話で入力（環境変数は使わない）
echo "root と ${USER_NAME} のパスワードを設定してください。"
echo "（入力中は画面に表示されません）"
read -r -s -p "root のパスワード: " ROOT_PW
echo
read -r -s -p "${USER_NAME} のパスワード: " USER_PW
echo

# --- 生成先の初期化（毎回クリーン）---
mkdir -p -- "$OUT_PARENT_ABS"

if [[ -e "$TARGET_DIR" ]]; then
	echo
	echo "[install_0.1.1] WARNING: 既存の生成物を削除して作り直します。"
	echo "[install_0.1.1]          削除対象: $TARGET_DIR"
	read -r -p "続行する場合は YES と入力してください: " confirm
	if [[ "$confirm" != "YES" ]]; then
		echo "[install_0.1.1] ABORT: cancelled"
		exit 1
	fi

	# 誤爆防止: ルート直下や想定外のパスを消さない
	if [[ "$OUT_PARENT_ABS" == "/" || "$TARGET_DIR" == "/" ]]; then
		echo "[install_0.1.1] ERROR: safety check failed (root path)" >&2
		exit 1
	fi
	if [[ "$TARGET_DIR" != "$OUT_PARENT_ABS"/* ]]; then
		echo "[install_0.1.1] ERROR: safety check failed for TARGET_DIR=$TARGET_DIR" >&2
		exit 1
	fi

	rm -rf -- "$TARGET_DIR"
fi

mkdir -p "$TARGET_DIR" "$TARGET_DIR/disk" "$TARGET_DIR/logs" "$TARGET_DIR/run" "$TARGET_DIR/docs"

echo "[install_0.1.1] Prepared clean workspace: $TARGET_DIR"

echo "[install_0.1.1] Copy base artifacts (read-only base -> repro workspace)"

# 起動スクリプト
cp -a "$BASE_START_SH" "$TARGET_START_SH"

# ISO ルート（生成元）
cp -a "$BASE_ISO_ROOT" "$TARGET_ISO_ROOT"

# 永続ディスク（初期状態の複製）
mkdir -p "$(dirname -- "$TARGET_DISK")"
cp -a --reflink=auto --sparse=always "$BASE_DISK" "$TARGET_DISK"

echo "[install_0.1.1] Initialize disk.img UUID (random)"

# tune2fs は "freshly checked filesystem" を要求することがあるため、先に fsck する
E2FSCK_RC=0
e2fsck -f -y "$TARGET_DISK" >/dev/null 2>&1 || E2FSCK_RC=$?
if [[ $E2FSCK_RC -ne 0 && $E2FSCK_RC -ne 1 && $E2FSCK_RC -ne 2 ]]; then
	echo "[install_0.1.1] ERROR: e2fsck failed (rc=$E2FSCK_RC) for $TARGET_DISK" >&2
	exit 1
fi

tune2fs -U random "$TARGET_DISK" >/dev/null 2>&1

DISK_UUID="$(blkid -s UUID -o value "$TARGET_DISK" || true)"
if [[ -z "$DISK_UUID" ]]; then
	echo "[install_0.1.1] ERROR: failed to read UUID from $TARGET_DISK" >&2
	exit 1
fi
echo "[install_0.1.1] DISK_UUID=$DISK_UUID"

echo "[install_0.1.1] Update grub.cfg root=UUID"
patch_grub_root_uuid "$TARGET_ISO_ROOT/boot/grub/grub.cfg" "$DISK_UUID"

echo "[install_0.1.1] Update /etc/{passwd,group,shadow} inside disk.img"
TMP_DIR="$(mktemp -d)"
cleanup() { rm -rf -- "$TMP_DIR"; }
trap cleanup EXIT

PASSWD_OLD="$TMP_DIR/passwd"
GROUP_OLD="$TMP_DIR/group"
SHADOW_OLD="$TMP_DIR/shadow"

PASSWD_NEW="$TMP_DIR/passwd.new"
GROUP_NEW="$TMP_DIR/group.new"
SHADOW_NEW="$TMP_DIR/shadow.new"

debugfs -R "dump /etc/passwd $PASSWD_OLD" "$TARGET_DISK" >/dev/null 2>&1
debugfs -R "dump /etc/group $GROUP_OLD" "$TARGET_DISK" >/dev/null 2>&1
debugfs -R "dump /etc/shadow $SHADOW_OLD" "$TARGET_DISK" >/dev/null 2>&1

if [[ ! -f "$PASSWD_OLD" || ! -f "$GROUP_OLD" || ! -f "$SHADOW_OLD" ]]; then
	echo "[install_0.1.1] ERROR: failed to dump /etc files from disk image" >&2
	exit 1
fi

ROOT_HASH="$(mkpasswd_sha512 "$ROOT_PW")"
USER_HASH="$(mkpasswd_sha512 "$USER_PW")"

export USER_NAME ROOT_HASH USER_HASH PASSWD_OLD GROUP_OLD SHADOW_OLD PASSWD_NEW GROUP_NEW SHADOW_NEW

python3 - <<'PY'
import os

user_name = os.environ['USER_NAME']
root_hash = os.environ['ROOT_HASH']
user_hash = os.environ['USER_HASH']

def read_lines(path):
	with open(path, 'r', encoding='utf-8', errors='surrogateescape') as f:
		return f.read().splitlines()

def write_lines(path, lines):
	with open(path, 'w', encoding='utf-8', errors='surrogateescape') as f:
		f.write("\n".join(lines) + "\n")

passwd_in = read_lines(os.environ['PASSWD_OLD'])
group_in = read_lines(os.environ['GROUP_OLD'])
shadow_in = read_lines(os.environ['SHADOW_OLD'])

# /etc/passwd が 'x' を使っていれば shadow 運用とみなす。
uses_shadow = False
for line in passwd_in:
	if not line or line.startswith('#'):
		continue
	parts = line.split(':')
	if len(parts) >= 2 and parts[0] == 'root':
		uses_shadow = (parts[1] == 'x')
		break

# uid/gid=1000 を「通常ユーザ枠」として扱い、名前だけ差し替える
TARGET_UID = '1000'
TARGET_GID = '1000'

def patch_passwd(lines):
	out = []
	user_done = False
	for line in lines:
		if not line or line.startswith('#'):
			out.append(line)
			continue
		parts = line.split(':')
		if len(parts) < 7:
			out.append(line)
			continue
		name, pw, uid, gid, gecos, home, shell = parts[:7]
		if name == 'root':
			# shadow を使う場合は 'x'、使わない場合は passwd 側へハッシュを入れる
			if not uses_shadow and pw not in ('x', '*', '!'):
				pw = root_hash
			out.append(':'.join([name, pw, uid, gid, gecos, home, shell] + parts[7:]))
			continue
		if uid == TARGET_UID and gid == TARGET_GID and not user_done:
			home = f"/home/{user_name}"
			if not uses_shadow and pw not in ('x', '*', '!'):
				pw = user_hash
			out.append(':'.join([user_name, pw, uid, gid, gecos, home, shell] + parts[7:]))
			user_done = True
		else:
			out.append(line)
	if not user_done:
		# 1000番がいなければ新規追加（最小）
		pw_field = 'x' if uses_shadow else user_hash
		out.append(f"{user_name}:{pw_field}:1000:1000::{'/home/' + user_name}:/bin/sh")
	return out

def patch_group(lines):
	out = []
	grp_done = False
	for line in lines:
		if not line or line.startswith('#'):
			out.append(line)
			continue
		parts = line.split(':')
		if len(parts) < 4:
			out.append(line)
			continue
		name, pw, gid, members = parts[:4]
		if gid == TARGET_GID and not grp_done:
			# メンバーはそのまま
			out.append(':'.join([user_name, pw, gid, members] + parts[4:]))
			grp_done = True
		else:
			out.append(line)
	if not grp_done:
		out.append(f"{user_name}:x:1000:")
	return out

def patch_shadow(lines):
	out = []
	user_done = False
	root_done = False
	old_usernames = []
	# uid=1000 の元ユーザ名を拾うために passwd を参照
	for line in passwd_in:
		if not line or line.startswith('#'):
			continue
		parts = line.split(':')
		if len(parts) < 3:
			continue
		if parts[2] == TARGET_UID:
			old_usernames.append(parts[0])

	for line in lines:
		if not line or line.startswith('#'):
			out.append(line)
			continue
		parts = line.split(':')
		if len(parts) < 2:
			out.append(line)
			continue
		name = parts[0]
		if name == 'root':
			parts[1] = root_hash
			out.append(':'.join(parts))
			root_done = True
			continue

		# uid=1000 の旧ユーザ名の shadow 行は、1つだけ差し替えて残りは消す
		if name in old_usernames and not user_done:
			parts[0] = user_name
			parts[1] = user_hash
			out.append(':'.join(parts))
			user_done = True
			continue
		if name in old_usernames and user_done:
			# 旧ユーザの重複は落とす
			continue

		out.append(line)

	if not root_done:
		out.append(f"root:{root_hash}:0:0:99999:7:::")
	if not user_done:
		out.append(f"{user_name}:{user_hash}:0:0:99999:7:::")
	return out

passwd_out = patch_passwd(passwd_in)
group_out = patch_group(group_in)
shadow_out = patch_shadow(shadow_in)

write_lines(os.environ['PASSWD_NEW'], passwd_out)
write_lines(os.environ['GROUP_NEW'], group_out)
write_lines(os.environ['SHADOW_NEW'], shadow_out)
PY

debugfs_replace_file "$TARGET_DISK" "$PASSWD_NEW" "/etc/passwd" "0100644"
debugfs_replace_file "$TARGET_DISK" "$GROUP_NEW" "/etc/group" "0100644"
debugfs_replace_file "$TARGET_DISK" "$SHADOW_NEW" "/etc/shadow" "0100600"

# 検証: root と一般ユーザのハッシュが反映されていること
SHADOW_CHECK="$TMP_DIR/shadow.check"
PASSWD_CHECK="$TMP_DIR/passwd.check"
debugfs -R "dump /etc/shadow $SHADOW_CHECK" "$TARGET_DISK" >/dev/null 2>&1
debugfs -R "dump /etc/passwd $PASSWD_CHECK" "$TARGET_DISK" >/dev/null 2>&1

root_hash_in="$(awk -F: '$1=="root"{print $2; exit}' "$SHADOW_CHECK" || true)"
user_hash_in="$(awk -F: -v u="$USER_NAME" '$1==u{print $2; exit}' "$SHADOW_CHECK" || true)"

if [[ -z "$root_hash_in" || -z "$user_hash_in" ]]; then
	echo "[install_0.1.1] ERROR: shadow verification failed (missing entries)" >&2
	echo "[install_0.1.1]        user=$USER_NAME" >&2
	exit 1
fi
if [[ "$root_hash_in" != "$ROOT_HASH" ]]; then
	echo "[install_0.1.1] ERROR: root password hash was not applied" >&2
	exit 1
fi
if [[ "$user_hash_in" != "$USER_HASH" ]]; then
	echo "[install_0.1.1] ERROR: user password hash was not applied (user=$USER_NAME)" >&2
	exit 1
fi

# home ディレクトリは無ければ作る（所有者設定はできれば行うが、失敗しても止めない）
OLD_USER_NAME="$(awk -F: '$3=="1000"{print $1; exit}' "$PASSWD_OLD" 2>/dev/null || true)"
OLD_HOME="/home/${OLD_USER_NAME}"
NEW_HOME="/home/${USER_NAME}"

# ユーザー名を差し替えた場合、旧ホームを新ホームへ付け替える（中身を引き継ぐ）
if [[ -n "$OLD_USER_NAME" && "$OLD_USER_NAME" != "$USER_NAME" ]]; then
	if debugfs_path_exists "$TARGET_DISK" "$OLD_HOME"; then
		if ! debugfs_path_exists "$TARGET_DISK" "$NEW_HOME"; then
			# /home/<旧> に対して /home/<新> の別名リンクを作る
			debugfs -w -R "ln ${OLD_HOME} ${NEW_HOME}" "$TARGET_DISK" >/dev/null 2>&1 || true
		fi
		# 旧パスを消して見た目の混乱を避ける（unlink はディレクトリエントリ削除）
		debugfs -w -R "unlink ${OLD_HOME}" "$TARGET_DISK" >/dev/null 2>&1 || true
	fi
fi

# 新ホームが無ければ作る
debugfs -w -R "mkdir ${NEW_HOME}" "$TARGET_DISK" >/dev/null 2>&1 || true

# debugfs の mkdir は root:root で作られるため、一般ユーザ(UID/GID=1000)へ合わせる
debugfs -w -R "sif ${NEW_HOME} uid 1000" "$TARGET_DISK" >/dev/null 2>&1 || true
debugfs -w -R "sif ${NEW_HOME} gid 1000" "$TARGET_DISK" >/dev/null 2>&1 || true
debugfs -w -R "sif ${NEW_HOME} mode 040755" "$TARGET_DISK" >/dev/null 2>&1 || true

echo "[install_0.1.1] Build boot ISO"
grub-mkrescue -o "$TARGET_ISO" "$TARGET_ISO_ROOT" >/dev/null 2>&1

echo
echo "[install_0.1.1] DONE"
echo "[install_0.1.1] Generated: $TARGET_ISO"
echo "[install_0.1.1] Generated: $TARGET_DISK (UUID=$DISK_UUID)"
echo "[install_0.1.1] Start:     $TARGET_START_SH"
echo
echo "Next:"
echo "  cd \"$TARGET_DIR\""
echo "  ./umuOSstart.sh"
