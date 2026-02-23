#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT_DIR/ush}"

build_ush() {
  local out="$1"
  musl-gcc -std=c11 -O2 -g -static \
    -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
    -I"$ROOT_DIR/include" \
    "$ROOT_DIR"/src/*.c \
    -o "$out"
}

if [[ ! -x "$BIN" ]]; then
  echo "[INFO] build: $BIN"
  build_ush "$BIN"
fi

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

assert_eq() {
  local name="$1"
  local got="$2"
  local exp="$3"
  if [[ "$got" != "$exp" ]]; then
    echo "[FAIL] $name" >&2
    echo "  expected: [$exp]" >&2
    echo "  got:      [$got]" >&2
    exit 1
  fi
  echo "[OK]   $name"
}

mk_script() {
  local path="$1"
  shift
  cat >"$path" <<'EOF'
$@
EOF
}

# 0.0.5: positional params ($0 $1..$9 $#)
POS=/tmp/ush_pos.ush
cat >"$POS" <<'EOF'
# positional params
printf '%s\n' "$0" "$1" "$2" "$#"
EOF
# Expect 4 lines
readarray -t lines < <($BIN "$POS" aa bb | tr -d '\r')
[[ ${#lines[@]} -eq 4 ]] || fail "positional params: expected 4 lines, got ${#lines[@]}"
assert_eq 'positional $0' "${lines[0]}" "$POS"
assert_eq 'positional $1' "${lines[1]}" "aa"
assert_eq 'positional $2' "${lines[2]}" "bb"
assert_eq 'positional $#' "${lines[3]}" "2"

# 0.0.5: command substitution $(...)
CMD=/tmp/ush_cmdsub.ush
cat >"$CMD" <<'EOF'
# cmdsub basic
printf '%s\n' "X$(echo hi there)Y"
EOF
out="$($BIN "$CMD" | tr -d '\r')"
assert_eq "cmdsub basic" "$out" "Xhi thereY"

# 0.0.5: cmdsub newline normalization (\n -> space, trim trailing)
CMDNL=/tmp/ush_cmdsub_nl.ush
cat >"$CMDNL" <<'EOF'
# cmdsub normalize newlines
printf '%s\n' "$(printf 'a\nb\n')"
EOF
out="$($BIN "$CMDNL" | tr -d '\r')"
assert_eq "cmdsub newline normalize" "$out" "a b"

# 0.0.4: pipeline + external
PIPE=/tmp/ush_pipe.ush
cat >"$PIPE" <<'EOF'
printf 'hello\n' | wc -l
EOF
out="$($BIN "$PIPE" | tr -d '\r' | tr -d ' ' )"
# wc output is typically padded; normalize spaces.
assert_eq "pipeline" "$out" "1"

# 0.0.4: seq ;
SEQ=/tmp/ush_seq.ush
cat >"$SEQ" <<'EOF'
echo A; echo B
EOF
out="$($BIN "$SEQ" | tr -d '\r')"
assert_eq "seq" "$out" $'A\nB'

# 0.0.4: and/or
ANDOR=/tmp/ush_andor.ush
cat >"$ANDOR" <<'EOF'
false && echo NG
false || echo OK
EOF
out="$($BIN "$ANDOR" | tr -d '\r')"
assert_eq "and/or" "$out" "OK"

# 0.0.4: redirection (external)
RED=/tmp/ush_redir.ush
TMP=/tmp/ush_redir_tmp.txt
rm -f "$TMP"
cat >"$RED" <<EOF
printf 'x\n' > "$TMP"
cat < "$TMP"
EOF
out="$($BIN "$RED" | tr -d '\r')"
assert_eq "redir" "$out" "x"

# 0.0.4: glob (unquoted)
GDIR=/tmp/ush_glob_dir
rm -rf "$GDIR"
mkdir -p "$GDIR"
: >"$GDIR/a.txt"
: >"$GDIR/b.txt"
GLOB=/tmp/ush_glob.ush
cat >"$GLOB" <<EOF
ls $GDIR/*.txt
EOF
out="$($BIN "$GLOB" | tr -d '\r')"
# Order may vary; just ensure both appear.
[[ "$out" == *"$GDIR/a.txt"* ]] || fail "glob missing a.txt"
[[ "$out" == *"$GDIR/b.txt"* ]] || fail "glob missing b.txt"
echo "[OK]   glob"

echo "ALL OK"
