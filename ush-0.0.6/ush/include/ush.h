#pragma once

#define USH_VERSION "ush-0.0.6"

typedef struct {
  int last_status;  // 初期値0

  // 位置パラメータ（$0 $1..$9 $#）
  // 対話モード: script_path="ush", pos_argc=0
  // スクリプトモード: script_path=scriptのパス文字列, pos_argv=argv[2..]
  const char *script_path;
  int pos_argc;
  char **pos_argv;
} ush_state_t;
