#pragma once

enum {
  USH_MAX_LINE_LEN  = 8192,
  USH_MAX_ARGS      = 128,
  USH_MAX_TOKEN_LEN = 1024,

  // 0.0.3: パイプは 1 段まで（最大 2 コマンド）
  USH_MAX_CMDS      = 2,

  // list := pipeline ( (&&||) pipeline )*
  // 1行内に許す pipeline 数の上限（実装簡易化）
  USH_MAX_PIPES     = 64,

  // トークン配列上限（経験則）
  USH_MAX_TOKENS    = 256,

  // 簡易履歴
  USH_HISTORY_MAX   = 32,
};
