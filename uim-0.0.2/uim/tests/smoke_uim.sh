#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT_DIR/uim}"

build_uim() {
  local out="$1"
  musl-gcc -std=c11 -O2 -g -static \
    -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
    -I"$ROOT_DIR/include" \
    "$ROOT_DIR"/src/*.c \
    -o "$out"
}

needs_build() {
  [[ -x "$BIN" ]] || return 0
  local f
  for f in "$ROOT_DIR"/include/*.h "$ROOT_DIR"/src/*.c "$ROOT_DIR"/tests/smoke_uim.sh; do
    [[ "$f" -nt "$BIN" ]] && return 0
  done
  return 1
}

if needs_build; then
  echo "[INFO] build: $BIN"
  build_uim "$BIN"
fi

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

assert_file_eq() {
  local name="$1"
  local path="$2"
  local exp="$3"
  local tmp
  tmp="/tmp/uim_exp_$$.txt"
  printf '%s' "$exp" >"$tmp"
  if ! cmp -s "$tmp" "$path"; then
    echo "[FAIL] $name" >&2
    echo "  expected (hexdump):" >&2
    hexdump -C "$tmp" | head -n 20 >&2 || true
    echo "  got (hexdump):" >&2
    hexdump -C "$path" | head -n 20 >&2 || true
    rm -f "$tmp"
    exit 1
  fi
  rm -f "$tmp"
  echo "[OK]   $name"
}

# Test 1: insert + Japanese + :wq
F1=/tmp/uim_t1.txt
rm -f "$F1"
# keys: i, text, Enter, '# 日本語コメント', Esc, :wq, Enter
printf 'iHello\n# 日本語コメント\033:wq\n' | "$BIN" --batch "$F1"
assert_file_eq "insert+japanese" "$F1" $'Hello\n# 日本語コメント\n'

# Test 2: dd/yy/p then :wq
F2=/tmp/uim_t2.txt
cat >"$F2" <<'EOF'
A
B
C
EOF
# keys:
#   j (to B)
#   dd (delete B)
#   k (to A)
#   yy (yank A)
#   p (paste below)
#   :wq Enter
printf 'jddkyyp\033:wq\n' | "$BIN" --batch "$F2"
assert_file_eq "dd+yy+p" "$F2" $'A\nA\nC\n'

# Test 3: 'a' (append) then insert then :wq
F3=/tmp/uim_t3.txt
cat >"$F3" <<'EOF'
Hlo
EOF
# keys:
#   a (enter insert after cursor)
#   el (make "Hello")
#   Esc, :wq Enter
printf 'ael\033:wq\n' | "$BIN" --batch "$F3"
assert_file_eq "a+insert" "$F3" $'Hello\n'

# Test 4: numeric prefix 3yy (yank 3 lines) + p (paste multiple lines)
F4=/tmp/uim_t4.txt
cat >"$F4" <<'EOF'
A
B
C
D
E
EOF
# keys:
#   jj     (to C)
#   3yy    (yank C,D,E)
#   kk     (back to A)
#   p      (paste below A)
#   :wq Enter
printf 'jj3yykkp\033:wq\n' | "$BIN" --batch "$F4"
assert_file_eq "3yy+p" "$F4" $'A\nC\nD\nE\nB\nC\nD\nE\n'

# Test 5: numeric prefix 3dd (delete 3 lines, also yanks) then paste them back
F5=/tmp/uim_t5.txt
cat >"$F5" <<'EOF'
A
B
C
D
E
EOF
# keys:
#   j      (to B)
#   3dd    (delete B,C,D)
#   k      (to A)
#   p      (paste B,C,D below A)
#   :wq Enter
printf 'j3ddkp\033:wq\n' | "$BIN" --batch "$F5"
assert_file_eq "3dd+p" "$F5" $'A\nB\nC\nD\nE\n'

# Test 6: line start (0) and line end ($)
F6=/tmp/uim_t6.txt
cat >"$F6" <<'EOF'
abc
EOF
# keys:
#   $ a X      (append X at end)
#   Esc 0 i Y  (insert Y at start)
#   Esc :wq Enter
printf '$aX\0330iY\033:wq\n' | "$BIN" --batch "$F6"
assert_file_eq "0+\$" "$F6" $'YabcX\n'

# Test 7: gg and 1G cursor movement
F7=/tmp/uim_t7.txt
cat >"$F7" <<'EOF'
A
B
C
EOF
# keys:
#   jggdd  (go to B, then gg to A, delete A)
#   :wq Enter
printf 'jggdd\033:wq\n' | "$BIN" --batch "$F7"
assert_file_eq "gg" "$F7" $'B\nC\n'

F8=/tmp/uim_t8.txt
cat >"$F8" <<'EOF'
A
B
C
EOF
# keys:
#   j1Gdd  (go to B, then 1G to line 1, delete A)
#   :wq Enter
printf 'j1Gdd\033:wq\n' | "$BIN" --batch "$F8"
assert_file_eq "1G" "$F8" $'B\nC\n'

# Test 9: forward search (/pattern) and repeat (n)
F9=/tmp/uim_t9.txt
cat >"$F9" <<'EOF'
A
TARGET one
TARGET two
Z
EOF
# keys:
#   /TARGET Enter   (jump to first TARGET)
#   0iX Esc $       (mark the first hit line, then move to end so next search goes forward)
#   n 0iY Esc       (jump to next TARGET, mark it)
#   :wq Enter
printf '/TARGET\n0iX\033$n0iY\033:wq\n' | "$BIN" --batch "$F9"
assert_file_eq "search+/+n" "$F9" $'A\nXTARGET one\nYTARGET two\nZ\n'

# Test 10: forward search with Japanese pattern (previously failed when preceded by Japanese)
F10=/tmp/uim_t10.txt
cat >"$F10" <<'EOF'
A
あ日本語CSS日本語 one
あ日本語CSS日本語 two
Z
EOF
# keys:
#   /日本語CSS日本語 Enter (jump to first hit; hit starts after a Japanese char)
#   0iX Esc $       (mark the first hit line, then move to end so next search goes forward)
#   n 0iY Esc       (jump to next hit, mark it)
#   :wq Enter
printf '/日本語CSS日本語\n0iX\033$n0iY\033:wq\n' | "$BIN" --batch "$F10"
assert_file_eq "search+japanese+/+n" "$F10" $'A\nXあ日本語CSS日本語 one\nYあ日本語CSS日本語 two\nZ\n'

# Test 11: --version
V=$(/bin/echo -n "" | "$BIN" --version)
if [[ "$V" != "0.0.2" ]]; then
  echo "[FAIL] version" >&2
  echo "  expected: 0.0.2" >&2
  echo "  got: $V" >&2
  exit 1
fi
echo "[OK]   version"

echo "ALL OK"
