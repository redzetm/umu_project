#pragma once

typedef enum {
  PARSE_OK = 0,
  PARSE_EMPTY,          // 空行/空白のみ/コメント行
  PARSE_TOO_LONG,       // 行長 or トークン長超過
  PARSE_TOO_MANY_TOKENS,
  PARSE_TOO_MANY_ARGS,  // argvが上限超過
  PARSE_UNSUPPORTED,    // 未対応構文を検出
  PARSE_SYNTAX_ERROR,   // 演算子の使い方が不正（MVP制約違反含む）
} parse_result_t;
