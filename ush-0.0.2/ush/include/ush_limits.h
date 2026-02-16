#pragma once

enum {
  USH_MAX_LINE_LEN  = 8192,
  USH_MAX_ARGS      = 128,
  USH_MAX_TOKEN_LEN = 1024,

  // パイプライン中の最大コマンド数（MVPでは控えめに固定）
  USH_MAX_CMDS      = 32,

  // トークン配列上限（MVPの経験則）
  // 単語と演算子が混ざるため argv 上限より大きめに取る。
  USH_MAX_TOKENS    = 256,

  // 簡易履歴
  USH_HISTORY_MAX   = 32,
};
