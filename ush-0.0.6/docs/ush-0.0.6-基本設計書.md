# ush-0.0.6-基本設計書.md
UmuOS User Shell (ush) — 基本設計書（0.0.6）  
Target OS: UmuOS-0.1.7-base-stable（想定）

本書は [ush-0.0.6-詳細設計書.md](ush-0.0.6-詳細設計書.md) を唯一の正として矛盾がないように整理した基本設計（構成・責務・入出力・例外）である。
仕様の要約は [ush-0.0.6-仕様書.md](ush-0.0.6-仕様書.md) に置く。

---

# 1. 目的と非目的

## 1.1 目的
- 0.0.5 までの「軽量・観測可能」方針を維持しつつ、0.0.6 で以下を追加する。
  - 複数行ブロック入力（対話/スクリプト）
  - 制御構文（if/while/for/case）
  - 条件評価 builtin（test / `[ ]`）

## 1.2 非目的
- POSIX 互換の追求、ジョブ制御、多段パイプ、ヒアドキュメント、高度な FD 操作
- 関数、算術式、配列、`break/continue`

---

# 2. 全体構成

## 2.1 主要モジュール

- main
  - 対話/スクリプトの分岐、読み取りループ
  - **複数行バッファ**を管理し、「完結したブロック単位」で評価する
- tokenize
  - 文字列→トークン列
  - 演算子（`;;` と `)` を含む）・クォート・コメント・最小エスケープ
- script_parse（文パーサ）
  - トークン列→文(AST)（`if/while/for/case` と単純コマンド列）
  - 未完結ブロックは `PARSE_INCOMPLETE` を返す
- script_exec（文実行器）
  - 文(AST) を評価し `last_status` を更新
  - 単純コマンド列の実行は既存の `parse/exec_ast` を再利用する
- parse / exec_ast
  - pipeline / and-or（`|` / `&&` / `||`）とリダイレクトの実行
- expand
  - 変数展開、位置パラメータ、コマンド置換、チルダ
- builtins
  - `cd/pwd/export/test/[ /exit/help`
- lineedit / prompt
  - 対話入力とプロンプト

---

# 3. 処理フロー

## 3.1 対話モード
1. プロンプトを描画し、1行入力を得る
2. 行を「ブロックバッファ」に追加（行末に `;` を補う）
3. バッファ全体を tokenize → script_parse
   - `PARSE_INCOMPLETE`: 追加行を読む
   - `PARSE_OK`: script_exec で実行し、バッファをクリア
   - その他: エラー表示してバッファをクリア

## 3.2 スクリプトモード
1. ファイルを上から 1 行ずつ読む（先頭 `#!` は無視）
2. 各行をブロックバッファに追加し、上記と同じ手順で評価
3. EOF で `PARSE_INCOMPLETE` のままなら `syntax error`（`last_status=2`）

---

# 4. データ設計

## 4.1 状態（ush_state_t）
- `last_status`: 直前の終了コード（`$?`）
- `script_path`, `pos_argc`, `pos_argv`: 位置パラメータ（`$0` `$1..` `$#`）

## 4.2 文(AST)
- script_parse は「文」単位のノード（ST_SIMPLE/ST_SEQ/ST_IF/ST_WHILE/ST_FOR/ST_CASE）を構築する
- ST_SIMPLE はトークン範囲（start/end）を保持し、実行時に `ush_parse_line` で小AST化して実行する

---

# 5. エラー設計

- tokenize/parse の失敗は `last_status=2`
  - `unsupported syntax` / `syntax error`
- 実行時のシステムコール失敗（例: `open`, `fork`）は `last_status=1`
- 外部コマンド探索:
  - 未発見: 127
  - 実行不可: 126
