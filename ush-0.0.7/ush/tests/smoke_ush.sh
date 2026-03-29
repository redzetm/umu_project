#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-/tmp/ush_smoke_ush_bin}"

build_ush() {
  local out="$1"
  musl-gcc -std=c11 -O2 -g -static \
    -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
    -I"$ROOT_DIR/include" \
    "$ROOT_DIR"/src/*.c \
    -o "$out"
}

echo "[INFO] build: $BIN"
build_ush "$BIN"

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

out="$($BIN --version | tr -d '\r')"
assert_eq "version" "$out" "ush-0.0.7"

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

# 0.0.6: cmdsub newline handling
# - unquoted: normalize \n/\r to spaces
# - quoted: preserve internal newlines (trailing newlines trimmed)
CMDNL=/tmp/ush_cmdsub_nl.ush
cat >"$CMDNL" <<'EOF'
# cmdsub newlines (unquoted)
printf '%s\n' $(printf 'a\nb\n')
EOF
out="$($BIN "$CMDNL" | tr -d '\r')"
assert_eq "cmdsub newline unquoted" "$out" "a b"

CMDNLQ=/tmp/ush_cmdsub_nl_q.ush
cat >"$CMDNLQ" <<'EOF'
# cmdsub newlines (quoted)
printf '%s\n' "$(printf 'a\nb\n')"
EOF
out="$($BIN "$CMDNLQ" | tr -d '\r')"
assert_eq "cmdsub newline quoted" "$out" $'a\nb'

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

# 0.0.6: test / [ ] + if/elif/else/fi (multi-line)
IFTMP=/tmp/ush_if_tmp.txt
rm -f "$IFTMP"
IF=/tmp/ush_if.ush
cat >"$IF" <<EOF
if [ -f "$IFTMP" ]
then
  echo NG
elif test -f "$IFTMP"
then
  echo NG
else
  echo OK
fi
EOF
out="$($BIN "$IF" | tr -d '\r')"
assert_eq "if/elif/else (multi-line)" "$out" "OK"

# 0.0.6: while/do/done (runs exactly once)
WFLAG=/tmp/ush_while_flag.txt
rm -f "$WFLAG"
WH=/tmp/ush_while.ush
cat >"$WH" <<EOF
while [ ! -f "$WFLAG" ]
do
  echo LOOP
  printf '' > "$WFLAG"
done
EOF
out="$($BIN "$WH" | tr -d '\r')"
assert_eq "while" "$out" "LOOP"

# 0.0.6: for/in/do/done (word list)
FOR=/tmp/ush_for.ush
cat >"$FOR" <<'EOF'
for x in a b
do
  printf '%s\n' "$x"
done
EOF
out="$($BIN "$FOR" | tr -d '\r')"
assert_eq "for" "$out" $'a\nb'

# 0.0.6: case/in/esac + pattern + default
CASE=/tmp/ush_case.ush
cat >"$CASE" <<'EOF'
case foo in
  bar) echo NG ;;
  foo) echo OK ;;
  *) echo NG ;;
esac
EOF
out="$($BIN "$CASE" | tr -d '\r')"
assert_eq "case" "$out" "OK"

JP1=/tmp/ush_jp_echo.ush
cat >"$JP1" <<'EOF'
printf '%s\n' '日本語'
EOF
out="$($BIN "$JP1" | tr -d '\r')"
assert_eq "japanese-literal" "$out" "日本語"

JP2=/tmp/ush_jp_arg.ush
cat >"$JP2" <<'EOF'
printf '%s\n' 日本語abc
EOF
out="$($BIN "$JP2" | tr -d '\r')"
assert_eq "japanese-unquoted-word" "$out" "日本語abc"

JPFILE=/tmp/日本語_ush_test.txt
rm -f "$JPFILE"
JP3=/tmp/ush_jp_redir.ush
cat >"$JP3" <<EOF
printf '%s\n' '中身' > "$JPFILE"
cat < "$JPFILE"
EOF
out="$($BIN "$JP3" | tr -d '\r')"
assert_eq "japanese-redirection" "$out" "中身"

BR1=/tmp/ush_brace_1.ush
cat >"$BR1" <<'EOF'
printf '%s\n' {aaa,bbb,ccc,ddd}
EOF
out="$($BIN "$BR1" | tr -d '\r')"
assert_eq "brace-basic" "$out" $'aaa\nbbb\nccc\nddd'

BR2=/tmp/ush_brace_2.ush
cat >"$BR2" <<'EOF'
printf '%s\n' pre{A,B}post
EOF
out="$($BIN "$BR2" | tr -d '\r')"
assert_eq "brace-prefix-suffix" "$out" $'preApost\npreBpost'

BR3=/tmp/ush_brace_3.ush
cat >"$BR3" <<'EOF'
X='{A,B}'
printf '%s\n' "${X}"
EOF
out="$($BIN "$BR3" | tr -d '\r')"
assert_eq "brace-var-literal-preserved" "$out" '{A,B}'

BR4=/tmp/ush_brace_4.ush
cat >"$BR4" <<'EOF'
X=Z
printf '%s\n' pre{A,${X}}post
EOF
out="$($BIN "$BR4" | tr -d '\r')"
assert_eq "brace-before-var" "$out" $'preApost\npreZpost'

JP4=/tmp/ush_jp_touch_zenkaku.ush
cat >"$JP4" <<'EOF'
touch　全角空白touch.txt
ls -1 全角空白touch.txt
EOF
out="$($BIN "$JP4" | tr -d '\r')"
assert_eq "zenkaku-space-separator" "$out" "全角空白touch.txt"

echo "ALL OK"
