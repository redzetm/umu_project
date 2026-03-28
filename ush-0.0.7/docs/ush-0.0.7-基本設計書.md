UmuOS User Shell (ush) — 基本設計書（0.0.7）  
Target OS: UmuOS 系（少なくとも 0.1.7-base-stable 系で成立することを狙う）

本書は [ush-0.0.7-詳細設計書.md](ush-0.0.7-詳細設計書.md) を唯一の正として矛盾がないように整理した基本設計である。  
利用者向けの仕様要約は [ush-0.0.7-仕様書.md](ush-0.0.7-仕様書.md) に置く。

---

# 1. 目的と非目的

## 1.1 目的

- UmuOS における軽量な対話シェルとして、ush 自身で UTF-8 日本語を壊さず扱える操作環境を提供する。
- 0.0.6 までの制御構文、複数行ブロック、展開、glob、外部コマンド実行の枠組みを維持する。
- 最小限の brace expansion を追加し、`cp {aaa,bbb} /umu_bin/` のような日常操作を ush で扱えるようにする。
- 静的リンクされた単一バイナリとして持ち込みやすい実装を維持する。

## 1.2 非目的

- POSIX sh / bash 互換の追求
- locale や IME に依存した高度な日本語入力環境の提供
- 厳密な Unicode 全面対応（結合文字、絵文字クラスタ、East Asian Width 完全準拠など）
- ジョブ制御、関数、算術式、配列、高度な FD 操作
- bash 完全互換の brace expansion

---

# 2. 0.0.7 の位置づけ

## 2.1 ash と ush の役割分担

- `/umu_bin/ush`
  - 対話操作
  - 小物スクリプト
  - UTF-8 日本語を含むコマンドライン入力
- `/bin/sh`（BusyBox ash）
  - 互換性重視の既存スクリプト
  - ush の対象外である複雑な sh スクリプト

0.0.7 は ash を置き換えるものではないが、「日本語を含む対話操作は ush 側でも成立する」状態を目標とする。

## 2.2 0.0.6 からの主要変更点

- lineedit を ASCII 前提から UTF-8 文字境界前提へ改める。
- `ush_utf8` 補助モジュールを追加する。
- unquoted WORD に対する最小限の brace expansion を追加する。
- tokenize / parse / expand / exec の基本意味論はできるだけ維持する。

---

# 3. 全体構成

## 3.1 主要モジュール

- main
  - 対話/スクリプトの分岐
  - 読み取りループ
  - 複数行ブロックバッファ管理
- prompt
  - プロンプト文字列の生成と描画
- lineedit
  - raw mode での対話入力
  - UTF-8 文字境界単位のカーソル移動と削除
  - 履歴移動、最低限の補完
- utf8
  - UTF-8 文字長判定
  - 前後文字境界への移動
  - 表示幅の簡易計算
- tokenize
  - 文字列からトークン列への分割
  - 演算子、クォート、コメント、brace expansion 候補の保持
- script_parse
  - 複数行ブロックを含む文単位の解析
  - `if/while/for/case` を含む AST 構築
- script_exec
  - 文 AST の評価
  - for/in や case に必要な語展開の適用
- parse / exec
  - 単純コマンド、1 段パイプ、and-or、リダイレクト、外部実行
- expand
  - `$VAR`, `${VAR}`, `$?`, `$0..$9`, `$#`, `~`, `$(...)` を処理
- builtins
  - `cd`, `pwd`, `export`, `test`, `[`, `exit`, `help`
- utils / env
  - 共有補助関数、環境変数や PATH 補助

## 3.2 成果物

- 単一静的バイナリ `/umu_bin/ush`
- 補助テスト `tests/smoke_ush.sh`

---

# 4. 処理フロー

## 4.1 対話モード

1. prompt がプロンプトを表示する。
2. lineedit が 1 行入力を受け取る。
3. 入力行をブロックバッファに追加する。
4. tokenize → script_parse を行う。
5. 結果に応じて分岐する。
   - `PARSE_INCOMPLETE`: 続きの行を読む。
   - `PARSE_OK`: script_exec で実行し、バッファをクリアする。
   - それ以外: エラー表示して `last_status=2` とし、バッファをクリアする。

## 4.2 スクリプトモード

1. ファイルを上から読む。
2. 行をブロックバッファへ追加する。
3. 対話モードと同じ手順で tokenize → script_parse → script_exec を行う。
4. EOF 時に未完結ブロックなら `syntax error` とする。

## 4.3 単純コマンド実行までの流れ

1. raw の WORD 列を受け取る。
2. unquoted WORD に対して brace expansion を適用する。
3. 生成された各語に対して通常の word 展開を適用する。
4. 必要なら glob を適用する。
5. builtins か外部コマンドかを判定して実行する。

この順序により、`${VAR}` は従来通り使え、変数値に含まれる `{A,B}` が brace expansion として誤解釈されない。

---

# 5. 入出力設計

## 5.1 入力

- 対話入力
  - tty raw mode による 1 文字単位入力
  - 日本語を含む UTF-8 バイト列
- スクリプト入力
  - テキストファイル
  - 先頭の `#!` は無視可能

## 5.2 出力

- 外部コマンドや builtins の標準出力/標準エラー
- プロンプト表示
- 構文エラーや実行エラーの通知

## 5.3 対話入力の設計方針

- バッファの内部表現は NUL 終端バイト列のままとする。
- カーソル位置は byte index で保持する。
- 左右移動、Backspace、Delete は UTF-8 1 文字単位で行う。
- redraw は表示幅差でカーソル位置を復元する。

---

# 6. データ設計

## 6.1 シェル状態

- `last_status`
  - 直前の終了コード
  - `$?` の値として利用する
- `script_path`
  - `$0` に相当するスクリプトパス
- `pos_argc`, `pos_argv`
  - `$1..$9`, `$#` の基礎情報

## 6.2 文字列とトークン

- tokenize / expand / exec の内部では、文字列は UTF-8 文字列であっても byte 列として保持する。
- 非 ASCII を特別扱いせず、空白や演算子でない限り WORD に含める。
- `{` `}` `,` は tokenize 時点では brace expansion 用か `${VAR}` 用かを確定しない。

## 6.3 文 AST

- `ST_SIMPLE`
  - 単純コマンド列
- `ST_SEQ`
  - 文の並び
- `ST_IF`
- `ST_WHILE`
- `ST_FOR`
- `ST_CASE`

文 AST は 0.0.6 と同じ枠組みを維持し、日本語対応のために構文規則そのものは増やさない。

---

# 7. UTF-8 対応の設計

## 7.1 対応範囲

- 日本語を含む入力の受け付け
- UTF-8 文字境界単位の左右移動
- UTF-8 文字境界単位の Backspace / Delete
- 日本語混在時に大きく崩れない redraw

## 7.2 対応しないもの

- locale 設定に依存する文字分類
- 結合文字列や絵文字クラスタの厳密操作
- 端末依存の表示幅差異の完全吸収

## 7.3 設計判断

- lineedit だけを UTF-8 文字境界対応にし、tokenize / parse / expand / exec は byte 列中心の単純さを維持する。
- これにより実装量を抑えつつ、利用者が最も困る「日本語入力で壊れる」問題を先に解消する。

---

# 8. brace expansion の設計

## 8.1 対象

- unquoted WORD のみ
- 1 語につき 1 組の brace pair のみ
- prefix / suffix 付きの語を許可

例:

- `cp {aaa,bbb,ccc,ddd} /umu_bin/`
- `cp foo{1,2}.txt /tmp/`
- `echo /home/{tama,root}/tmp`

## 8.2 未対応

- nested brace expansion
- range 形式
- quoted brace expansion
- 1 語内の複数 brace pair の直積展開
- 空要素

## 8.3 実行責務

- tokenize
  - brace 候補を WORD として保持する
- exec / script_exec
  - raw の unquoted WORD に対して最初に brace expansion を適用する
- expand
  - brace expansion 後の各語に対して通常展開を行う
- glob
  - 通常展開後の各語に適用する

---

# 9. エラー設計

## 9.1 構文エラー

- tokenize / parse の失敗は `last_status=2`
- 未完結ブロックは `PARSE_INCOMPLETE` とし、EOF まで閉じなければ `syntax error`
- 未対応構文は黙って通さず、明示的にエラーとする

## 9.2 実行エラー

- builtins の実行エラーは 1
- builtins の構文/引数不正は 2
- 外部コマンド未発見は 127
- 外部コマンド実行不可は 126
- システムコール失敗は原則 1

## 9.3 tty 復帰

- 対話入力中に異常が起きても、可能な限り tty 設定を元へ戻す。
- 0.0.7 では raw mode 導入により、この復帰責務を lineedit 側で明確に持つ。

---

# 10. ビルドと持ち込み

- `musl-gcc -static` を基本とする。
- 成果物は静的リンクバイナリとする。
- UmuOS へはバイナリを持ち込む前提とする。
- `file` コマンドで静的リンクを確認してから `/umu_bin/ush` として配置する運用を想定する。

---

# 11. テスト方針

## 11.1 自動確認

- `--version`
- 日本語リテラル出力
- 日本語を含む未クォート引数
- 日本語ファイル名へのリダイレクト
- brace expansion 基本動作
- `${VAR}` と brace expansion の共存

## 11.2 手動確認

- 日本語貼り付け入力
- 左右移動が 1 文字単位で動くこと
- Backspace / Delete が 1 文字単位で動くこと
- 日本語混在時に redraw が著しく崩れないこと

---

# 12. まとめ

ush 0.0.7 は、0.0.6 のシェル言語と実行モデルを大きく崩さずに、対話入力の実用性を引き上げる版である。

中心となる設計判断は次の 3 点である。

- 日本語対応の主戦場を lineedit に限定すること
- 文字列内部表現は byte 列のまま維持すること
- brace expansion を最小サブセットに限定し、`${VAR}` と矛盾しない順序で適用すること

この方針により、実装規模を過度に増やさず、UmuOS で実際に困る対話操作の問題を優先的に解消する。