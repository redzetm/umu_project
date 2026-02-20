# ush-0.0.3-シェル開発の設計に必要な判断.md
UmuOS User Shell (ush) — 開発判断メモ（0.0.3）  
Target OS: UmuOS-0.1.6-dev

この文書は、実装に入る前/入りながら「**判断が必要な点**」を漏れなく洗い出し、仕様どおりに収束させるための資料である。

- 仕様のソース・オブ・トゥルース: [ush-0.0.3-仕様書.md](ush-0.0.3-仕様書.md)
- 構成と責務の整理: [ush-0.0.3-基本設計書.md](ush-0.0.3-基本設計書.md)
- “貼り付け可”コードまで含む詳細: [ush-0.0.3-詳細設計書.md](ush-0.0.3-詳細設計書.md)

本書は「仕様に追加する新機能」を提案するものではなく、0.0.3の範囲を**破綻なく実装するための判断点**（境界条件・落とし穴・テスト観点）を列挙する。

---

# 0. 使い方（運用）

## 0.1 この文書の読み順
1. **最重要判断**（ブレると実装が破綻する）を先に固定する
2. tokenize/parse/expand/exec の順で、境界条件とエラー分類を固定する
3. line editor / prompt / script mode を最後に確認し、受け入れ基準で締める

## 0.2 「判断」の型
実装中の迷いは、だいたい以下のどれかに分類できる。

- **範囲判断**: それは0.0.3でやる/やらない？（未対応は検出してエラー）
- **意味論判断**: その入力は成功扱いか、`syntax error`か、`unsupported syntax`か
- **タイミング判断**: どの段階でやる？（tokenize/parse/exec/expand）
- **責務判断**: それは親でやる？子でやる？（builtins/リダイレクト/シグナル）
- **観測性判断**: ユーザーにどう見える？（メッセージ、`last_status`、再描画）

## 0.3 （方針）docs 配下だけで完結させる
本リポジトリでは `ush-0.0.3/` 配下は **docs のみ**を保持する。

意図:
- 判断（本書）・仕様・基本設計・詳細設計の4点が、常に同じ場所で参照できる
- 実行可能な検証環境（抽出器・ビルド作業場・テストランナー等）でリポジトリが肥大化しない

ただし、議論を「感想」から「証拠」に寄せるには、受け入れテストの“実ファイル化”が有効。
この文書では 11.3 に「スクリプト化できる例」を残し、必要に応じて手元の作業ディレクトリで `.ush` 化して回帰にする。

（詳細設計書の「貼り付け可」コードも同様に、手元作業ディレクトリにコピーしてビルド検証すればよい。）

# 1. 最重要判断（ここが揺れると破綻する）

## 1.1 「未対応は検出してエラー」方針の徹底
0.0.3は POSIX 互換ではない。**未対応構文を“それっぽく解釈しない”**ことが最優先。

- 実害: たとえば `;` を空白として扱うと、意図せぬコマンド連結が起きる
- 一貫した挙動: `unsupported syntax`（終了コード2）に倒す

判断として固定すること:
- tokenize で弾くのか、parse で弾くのか、expand で弾くのかを明確にする
- どの層で弾いても最終的に「ユーザーが理解できる」同じ分類に収束させる

## 1.2 リダイレクト失敗時の「行丸ごと不実行」
仕様・詳細設計のコア。

- ルール: リダイレクトの `open()` に失敗したら、**その行は1つもforkしない**
- 意味: 失敗した行で、左だけ実行される/右だけ実行される等の部分実行を禁止

実装での落とし穴:
- pipeline のとき、左入力/右出力の `open()` を**fork前**に全て試す必要がある
- `pipe()` 自体も「fork前のリソース確保」なので、失敗時は当然 no-fork
- `open()` だけ先にして、後続で失敗したときに fd close を漏らす

テスト観点:
- `cat < NOFILE | wc` は `cat` も `wc` も動かない
- `echo hi > /root/deny`（権限不可）も何も実行されない（forkが走らない）

## 1.3 展開（expand）は parse ではなく exec 直前
`$?` とメモリ寿命と短絡評価の3点で、実装が壊れやすい。

- `$?` は「直前の終了コード」なので、評価順序（特に `&&` / `||`）に追従する必要がある
- parse 時点で展開すると、`last_status` が確定していない/後から変わる
- tokenize/parse のバッファ寿命と、展開後文字列の寿命が混ざるとダングリングしやすい

判断として固定すること:
- 実際に `exec_command()` / `exec_pipeline()` で argv/パスを展開してから open/fork する
- 展開は「失敗分類（unsupported / too long）」も含め、実行直前で確定する

## 1.4 builtins の扱い（親で走る条件を明示）
`cd` や `export` は親プロセスの状態を変えるため、基本は親で実行する。

0.0.3 の判断（詳細設計の方針）:
- builtins は「**パイプなし/リダイレクトなし**」のみ許可
- builtins が pipeline に出たら `unsupported syntax`（2）
- builtins にリダイレクトが付いたら `unsupported syntax`（2）

理由:
- 最小実装で意味論がブレない（`cd | x` のような“サブシェル問題”を避ける）
- リダイレクトを伴う builtins をサポートすると、親で fd をいじる責務が増える

テスト観点:
- `cd / | pwd` は `unsupported syntax`（実行しない）
- `pwd > out` は `unsupported syntax`（実行しない）

## 1.5 `/bin/sh` フォールバックは ENOEXEC のみ
ash 委譲は「互換性のための最終逃げ道」だが、乱用すると観測性が落ちる。

判断として固定すること:
- `execve()` が `ENOEXEC` のときだけ `/bin/sh` にフォールバック
- それ以外（未発見/権限/オプション不正/構文エラー等）は ush で完結してエラー

テスト観点:
- テキストスクリプト（shebangなし）実行 → ENOEXEC → `/bin/sh` で動く
- `nosuchcmd` は 127 のまま（ashに委譲しない）

---

# 2. エラー分類（`syntax error` vs `unsupported syntax`）

## 2.1 目的
ユーザーが「何がダメだったか」を学習でき、テストが書けるようにする。

## 2.2 推奨ルール（0.0.3での一貫性）
- `syntax error`:
  - 字句/構文として壊れている（例: クォート未閉鎖、演算子の並びが不正、必要な WORD がない）
- `unsupported syntax`:
  - sh なら意味があるが 0.0.3では扱わない（例: `;`、`<<`、多段パイプ、環境代入 `NAME=... cmd`、グロブ）

## 2.3 どこで検出するか
- tokenize: 単文字禁止/クォート未閉鎖/グロブ文字/`<<` 等
- parse: 演算子列の制約（`|` 位置、リダイレクトの付き先、1段パイプ制限）
- expand: `$` 記法の破綻、展開結果が上限超過
- exec: open/fork/pipe/wait 失敗（`open: strerror` 等）

注意:
- どこで検出しても、ユーザーに出すメッセージは最終的に仕様の代表例に収束させる
- 失敗時の `last_status` は、基本的に 2（構文/引数不正）か 1（実行エラー）に収束させる

## 2.4 `last_status` の優先順位（衝突したときの決め方）
同じ行の評価で複数の失敗要因があり得る（例: 未対応構文 + リダイレクト失敗、など）。
0.0.3では仕様の優先順位を固定し、テストで観測できるようにする。

- 字句/構文段階の失敗（`unsupported` / `syntax`）: `last_status=2`（その行は実行しない）
- リダイレクトの失敗（`open()`等）: `last_status=1`（その行は実行しない）
- コマンド探索失敗: 126/127
- 実行結果: 外部は終了コード or `128+signal`、builtinは0/1/2

---

# 3. tokenize（字句解析）で決めること

## 3.1 コメント規則
判断（0.0.3）:
- 未クォートで「**トークン先頭の `#`**」のみコメント開始
- トークン途中の `#` は文字

落とし穴:
- 空白を飛ばした直後に `#` を見たら即 break（以降無視）
- クォート内の `#` は当然文字

テスト:
- `echo a#b` は `a#b`
- `echo a # b` は `echo a` まで

## 3.2 クォートの最小仕様
判断（0.0.3）:
- `'...'` は展開なし（tokenize は quote 種別だけ付与）
- `"..."` は変数展開のみ（実体の展開は expand で）
- 未閉鎖は `syntax error`
- クォートをまたいだ連結（例: `ab"cd"ef`）は 0.0.3では **非対応**（`syntax error` に倒す）

理由:
- 連結対応は tokenize を複雑化し、argv の寿命/展開との整合が難しくなる

テスト:
- `echo "a b"` は 1引数
- `echo a"b"` は `syntax error`（0.0.3では）

## 3.3 演算子の分割
判断（0.0.3）:
- `|` `&&` `||` `<` `>` `>>` は空白の有無に関わらず独立トークン
- `&` 単体は未対応（`unsupported syntax`）

テスト:
- `echo a|wc` が動く
- `echo a&` は `unsupported syntax`

## 3.4 未対応文字/構文の早期検出
判断（0.0.3）:
- `; ( ) { }` は未対応 → `unsupported syntax`
- 行継続（バックスラッシュ改行: 行末の `\`）は未対応 → `unsupported syntax`
- グロブ `* ? [ ]` は未クォートで検出したら `unsupported syntax`
  - 例外: `$?` は許す（`?` 単体はNGだが、直前が `$` のときだけOK）
- ヒアドキュメント `<<` / `<<<` は未対応 → `unsupported syntax`

落とし穴:
- `?` を一律NGにすると `$?` が壊れる（過去に実際に詰まったポイント）
- `[` `]` は配列/条件式などと誤解釈されやすいので、早期に排除する方が安全
 
補足:
- 0.0.3ではバックスラッシュ（`\`）自体はエスケープとして解釈しない（通常文字）。
- ただし「行末 `\` を行継続として扱う」機能は実装しないため、行末 `\` は未対応として検出する（観測可能性優先）。

## 3.5 上限（行長/トークン/引数）とエラー
判断（0.0.3）:
- `USH_MAX_LINE_LEN`/`USH_MAX_TOKENS`/`USH_MAX_TOKEN_LEN`/`USH_MAX_ARGS` を超えたら `PARSE_TOO_LONG`/`PARSE_TOO_MANY_TOKENS`
- ユーザー表示は概ね `syntax error`（終了コード2）でよい（詳細設計方針に合わせる）

実装判断:
- 0.0.3では「どの上限で落ちたか」を細かく表示しなくても良い（観測性より簡潔優先）

---

# 4. parse（構文解析）で決めること

## 4.1 演算子の優先順位と結合規則
判断（0.0.3・詳細設計の方向）:
- `|` は pipeline ノードを作る（最大1段）
- `&&` / `||` は **同順位** として左結合で AST を作る（左から順に評価される）
- パースの出発点は「pipeline を単位とした論理結合」

テスト:
- `false || true && false` の解釈は「左結合 + 同順位」か「&&優先」かで変わる
  - 0.0.3では “POSIX互換” を目標にしないので、**実装どおり**にテストを固定する（設計書の通りの結合規則にする）

※注意: 仕様は `|` が高く、`&&/||` が低いことは明示している。一方で `&&` と `||` の相対優先順位は明示が薄いので、0.0.3では「同順位・左結合」で固定し、受け入れテストでロックする。

## 4.2 1段パイプ制限
判断（0.0.3）:
- `a | b` は可
- `a | b | c` は `syntax error`（文法に合わない入力として扱う）

テスト:
- `echo a | tr a b | cat` は `syntax error`

## 4.3 リダイレクトの結び付き（どのコマンドに適用されるか）
判断（0.0.3・詳細設計の方向）:
- `cmd < in` は `cmd` の stdin
- `cmd > out` / `cmd >> out` は `cmd` の stdout
- pipeline では次の制約にする（簡潔で誤解しにくい）:
  - 左コマンド側に許すのは `<`（stdin）
  - 右コマンド側に許すのは `>`/`>>`（stdout）
  - それ以外（例: `a > out | b` や `a | b < in`）は `syntax error`（設計の簡易文法制約として扱う）

理由:
- 0.0.3の「1段パイプ」設計と整合し、open 事前検査もしやすい

テスト:
- `cat < in | wc > out` は可
- `echo a > out | wc` は `syntax error`

## 4.4 演算子の並びのエラー
判断:
- `||` や `&&` の左右が欠けている/`|` の左右が欠けている → `syntax error`
- リダイレクト演算子の直後に WORD がない → `syntax error`

---

# 5. expand（展開）で決めること

## 5.1 どの展開をやるか（0.0.3の範囲）
判断（仕様準拠）:
- 環境変数: `$VAR`
- 直前の終了コード: `$?`
- チルダ: `~` / `~/...`

非目的:
- 位置パラメータ `$1` 等
- コマンド置換 `$(...)` / `` `...` ``
- ブレース `${VAR}`
- 変数修飾（`:-` 等）

## 5.2 変数名規則と `$` の扱い
判断（推奨・詳細設計に合わせる）:
- `$` の次が `?` なら `$?`
- `$` の次が `[A-Za-z_]` なら `$NAME` 形式
- それ以外（`$1`、`$-`、`$` 末尾など）は 0.0.3では「未対応構文」として扱い、`unsupported syntax` に倒す

理由:
- “何となく空文字にする”とスクリプトが静かに壊れる

テスト:
- `echo $1` は `unsupported syntax`
- `echo $` は `unsupported syntax`

## 5.3 クォート種別と展開
判断:
- `QUOTE_SINGLE`: 展開しない
- `QUOTE_DOUBLE`: **変数展開のみ**（`$VAR` と `$?`）。チルダ `~` は展開しない。
- `QUOTE_NONE`: 変数 + チルダ

注意:
- 詳細設計では `ush_expand_word()` が quote を見て処理する。ここが仕様（「ダブルは変数のみ」）を守る最終防波堤。

## 5.4 チルダ展開の範囲
判断（0.0.3）:
- `~` か `~/` で始まるときだけ HOME 展開
- `~user` は未対応（0.0.3では未対応として検出する）

落とし穴:
- `~` の判定は “トークン先頭” に限定する（中間の `a~b` は展開しない）
- HOME 未設定時は `/` とみなして展開する（詳細設計の方針）

## 5.5 展開結果の長さ制限
判断:
- 展開結果が `USH_MAX_TOKEN_LEN` を超えたら `PARSE_TOO_LONG` 相当で落とす
- ユーザー表示は `syntax error`（2）に収束させる

---

# 6. exec（実行）で決めること

## 6.1 `last_status` 更新の厳密性
判断（仕様準拠）:
- 外部コマンド: exit code / signal code を仕様どおりに変換
- builtin: 成功0、実行エラー1、構文/引数不正2
- `&&` / `||` の短絡時も、左を評価した結果は **必ず `last_status` に反映**させる（右側の `$?` のため）

落とし穴:
- `eval_node()` が再帰で右を呼ぶ前に `last_status` を更新しないと `$?` がズレる

テスト:
- `false && echo $?` は echo が走らないが `last_status==1` が残る
- `false || echo $?` は `echo 1`

## 6.2 外部コマンド解決（PATH）
判断（仕様準拠）:
- cmd に `/` が含まれる場合はそれを直接実行対象とする
- それ以外は PATH 検索（未設定時は `/umu_bin:/sbin:/bin`）
- 未発見: 127 / 実行不可（EACCES 等）: 126

落とし穴:
- PATH 中の空要素（`::`）はどう扱うか（0.0.3では単純にスキップでよい）
- `access(X_OK)` と `execve()` の間のTOCTOUは 0.0.3では気にしない（教育/簡潔優先）

## 6.3 事前展開→事前open→fork/exec の順序
判断（詳細設計の方向）:
1. argv を展開（未対応検出もここで確定）
2. builtins 判定（builtins なら pipe/redirect を弾く）
3. リダイレクトパスを展開
4. open（全て親で）
5. fork/exec

理由:
- open 失敗時に no-fork を保証
- pipeline でも「両側 argv を展開してから」 builtins/代入の未対応を統一的に弾ける

## 6.4 パイプラインの責務分離
判断（0.0.3）:
- 1段パイプは `pipe()` を親で作り、左子が `stdout→pipe write`、右子が `stdin←pipe read`
- `waitpid()` は両方を待つが、`last_status` として返すのは **右側の終了コード**

落とし穴:
- 片方 fork 失敗時の取り扱い（0.0.3では簡易化して「待って返す」方針だが、fd close を漏らさない）
- 親で不要な pipe fd を close しないと子が EOF を受け取れない

## 6.5 builtins の“未対応”の見せ方
判断:
- pipeline 中 builtins は `unsupported syntax`（2）
- redirect 付き builtins も `unsupported syntax`（2）

テスト:
- `help | cat` は `unsupported syntax`

## 6.6 環境代入 `NAME=... cmd` を未対応として弾く
判断（詳細設計の方向）:
- 先頭トークンが `NAME=...` 形式なら `unsupported syntax`（2）

理由:
- POSIX互換領域で、スコープ（そのコマンドだけの環境）等を考え出すと爆発する

## 6.7 シグナル（SIGINT）
判断（仕様準拠）:
- 親（ush本体）は SIGINT で落ちない（無視 or ハンドラで握り潰す）
- 子（外部コマンド）は SIGINT デフォルト

落とし穴:
- 親が raw mode のまま落ちると端末が壊れる → 終了経路で必ず端末設定を戻す

---

# 7. builtins で決めること

## 7.1 `cd`
判断:
- `cd` 引数なしは HOME（HOME 未設定時は `/` とみなす）
- 余計な引数は `syntax/args error`（2）にするか、単に無視せずエラーにする（観測性優先）

## 7.2 `pwd`
判断:
- 引数は受け付けない（あれば2）
- `getcwd()` 失敗時は1

## 7.3 `export`
判断（0.0.3）:
- `export NAME=VALUE` / `export NAME` のみ
- `export` 引数なしはエラー（2）
- NAME の妥当性をチェック（空/先頭数字/記号などは2）

落とし穴:
- `export A=B C=D` の複数指定をどうするか（0.0.3では未対応で2に倒すのが単純）

## 7.4 `exit`
判断:
- `exit` 引数なし: `exit(last_status)`
- `exit N`: `exit(N & 255)`
- 不正なN/引数多すぎ: 2

## 7.5 `help`
判断:
- 常に0
- 仕様の範囲（演算子、ash委譲方針）を短く表示

---

# 8. line editor（対話入力）で決めること

## 8.1 raw mode の安全性
判断:
- raw mode へ入る/出る処理は必ず対にする
- エラー経路や `exit` でも復元される設計にする（`atexit` 等）

## 8.2 キー入力の範囲
判断（仕様準拠）:
- Enter/Ctrl-D/BS/DEL/矢印/Delete/Tab
- それ以外は無視（壊れないこと優先）

## 8.3 履歴
判断:
- 固定長リングでよい（上限を超えたら古いものから捨てる）
- 空行は履歴に入れない

## 8.4 Tab 補完
判断（仕様準拠、0.0.2同様）:
- 対象は「先頭トークンのコマンド名」だけ
- 候補1つ: 確定補完
- 候補複数: 共通プレフィックスまで伸ばす
- それ以上決められない: 改行して候補一覧 → プロンプト＋入力行を再描画

実装判断:
- 候補生成に builtins を含める
- PATH を走査するとき、存在確認と重複排除をどうするか（0.0.3では単純でよいが、重複が出てもUI的に許容するかは決める）

---

# 9. prompt で決めること

## 9.1 参照順序
判断（基本設計準拠）:
- `USH_PS1` → `PS1` → デフォルト

## 9.2 最小展開
判断:
- `\u` `\w` `\$` `\\` 程度に絞る
- 失敗しても「崩れない」ことを優先（未知のエスケープはそのまま表示でよい）

---

# 10. script mode（`ush <script>`）で決めること

## 10.1 shebang 行の扱い
判断:
- 先頭行が `#!` なら 1行目は無視
- shebang のパスは `#!/umu_bin/ush` を想定

## 10.2 エラー時の継続
判断（基本設計準拠）:
- 各行は独立に評価
- エラーでも次行へ進む（fail-fast なし）
- `exit` builtin で終了

テスト:
- 途中に `unsupported syntax` があっても後続行は動く

---

# 11. 受け入れ基準（チェックリスト）

## 11.1 仕様準拠チェック
- 対話: プロンプト/行編集/履歴/Tab補完が仕様通り
- 言語: `'`/`"`、`$VAR`/`$?`/`~`、`|`（1段）/`&&`/`||`、`<`/`>`/`>>`
- 未対応: `;`/`<<`/多段パイプ/グロブ 等が“検出してエラー”
- 実行: PATH デフォルト、`last_status`、SIGINT（親は落ちない）
- ash委譲: `ENOEXEC` のみ `/bin/sh`

## 11.2 重要な回帰テスト（最小セット）
- `false || echo $?` → `1`
- `true && echo $?` → `0`
- `echo a|wc` が動く
- `cat < NOFILE | wc` が “何も実行しない”
- `pwd > out` / `help | cat` が `unsupported syntax`
- `echo $1` が `unsupported syntax`

## 11.3 受け入れテスト（スクリプト化できる例）
ここは「将来テストを起こすときの材料」を兼ねる。

- 実行方法（例）:
  - `./ush t01.ush`（script mode）
- 期待出力は **プロンプトなし**を前提（script mode）
- エラー文言は基本 `ush:` プレフィックスを期待する

### t01: コメント規則（トークン先頭 `#` のみ）
入力（t01_comment.ush）:

```sh
echo A # comment
echo B#not_comment
```

期待 stdout:

```text
A
B#not_comment
```

期待 stderr: なし  
期待 exit: 0

### t02: クォート（singleは展開なし、doubleは変数展開のみ）
入力（t02_quotes.ush）:

```sh
export X=world
echo '$X'
echo "$X"
echo "~"
```

期待 stdout:

```text
$X
world
~
```

期待 stderr: なし  
期待 exit: 0

### t03: 未対応の検出（`;`）は `unsupported syntax`
入力（t03_semicolon.ush）:

```sh
echo ok1
echo a; echo b
echo status=$?
echo ok2
```

期待 stdout:

```text
ok1
status=2
ok2
```

期待 stderr:
- `ush: unsupported syntax` を含む（1行）

期待 exit: 0（最後の `echo ok2` が成功するため）

### t04: グロブは未対応（`* ? [ ]`）だが `$?` は許す
入力（t04_glob_and_qmark.ush）:

```sh
echo $?
echo ?
echo status=$?
```

期待 stdout（例）:

```text
0
status=2
```

期待 stderr:
- `ush: unsupported syntax` を含む

期待 exit: 0

### t05: 多段パイプは `syntax error`
入力（t05_multipipe.ush）:

```sh
echo a | cat | cat
echo status=$?
```

期待 stdout:

```text
status=2
```

期待 stderr:
- `ush: syntax error` を含む

期待 exit: 0

### t15: `&&` / `||` の結合規則（同順位・左結合）をロックする
目的:
- 仕様が薄い部分を「受け入れテスト」で固定し、次バージョンで変えるときはテスト差分で議論できるようにする

入力（t15_and_or_assoc.ush）:

```sh
true || false && false
echo status=$?
```

期待 stdout:

```text
status=1
```

期待 stderr: なし  
期待 exit: 0

### t16: リダイレクト位置（wordsの後ろにまとめる）
入力（t16_redir_position.ush）:

```sh
echo hi > out
cat > out hi
echo status=$?
```

期待:
- 2行目が文法違反として `ush: syntax error`（`status=2`）

期待 stdout（例）:

```text
status=2
```

### t17: 行末バックスラッシュは「行継続未対応」として弾く
入力（t17_line_continuation.ush）:

```sh
echo hi\\
echo status=$?
```

期待:
- 1行目は `ush: unsupported syntax`（`status=2`）

注意:
- ここは「仕様がそう決めている」ためのテスト。もし実装が現状“弾かない”なら、仕様/詳細設計/実装のどれを直すかを議題に上げる。

### t18: 未定義の環境変数は空文字（`$VAR`）
入力（t18_undef_env_empty.ush）:

```sh
export X=ok
echo X=$X
echo U=$UNDEF
```

期待 stdout:

```text
X=ok
U=
```

### t06: パイプ時のリダイレクト制約（左の `>` / 右の `<` は `syntax error`）
入力（t06_pipe_redir_restriction.ush）:

```sh
echo a > out | wc
echo status=$?
```

期待 stdout:

```text
status=2
```

期待 stderr:
- `ush: syntax error` を含む

期待 exit: 0

### t07: 1段パイプ + 右リダイレクト（成功ケース）
入力（t07_pipe_ok.ush）:

```sh
rm -f out
echo hi | cat > out
cat out
```

期待 stdout:

```text
hi
```

期待 stderr: なし  
期待 exit: 0

### t08: 短絡評価（右が実行されない）と `$?`
入力（t08_short_circuit.ush）:

```sh
false && echo SHOULD_NOT_RUN
echo after_false=$?

true || echo SHOULD_NOT_RUN
echo after_true=$?

false || echo ok
echo after_or=$?
```

期待 stdout:

```text
after_false=1
after_true=0
ok
after_or=0
```

期待 stderr: なし  
期待 exit: 0

### t09: PATH探索の失敗（未発見127）を `$?` で観測
入力（t09_not_found.ush）:

```sh
nosuchcmd
echo status=$?
```

期待 stdout:

```text
status=127
```

期待 stderr:
- 0.0.3の詳細設計では「未発見」自体のメッセージを必須にしていないため、**出ない場合がある**

期待 exit: 0

### t10: リダイレクト `open()` 失敗時は “行を丸ごと不実行（no-fork）”
目的:
- `open` 失敗でも「コマンドだけは実行される」を防ぐ（副作用の有無で検出する）

入力（t10_redir_open_nofork.ush）:

```sh
rm -f ran
touch ran > /no_such_dir/out

echo after_open_fail=$?

echo "--- check ran exists ---"
cat ran
echo cat_status=$?
```

期待:
- stderr に `ush: open:`（strerror付き）を含む
- `ran` は作られていない（`cat ran` が失敗し、`cat_status` は0以外）

注意:
- `cat` のエラーメッセージ本文は環境差が出るので、ここでは厳密一致にしない

### t11: パイプラインでも `open()` 失敗時は “行を丸ごと不実行（no-fork）”
入力（t11_pipe_open_nofork.ush）:

```sh
rm -f ran
touch ran | cat > /no_such_dir/out

echo after_open_fail=$?

echo "--- check ran exists ---"
cat ran
echo cat_status=$?
```

期待:
- stderr に `ush: open:` を含む
- `ran` は作られていない（`cat_status` は0以外）

### t12: 環境代入 `NAME=... cmd` は未対応
入力（t12_env_assign_unsupported.ush）:

```sh
A=B echo hi
echo status=$?
```

期待 stdout:

```text
status=2
```

期待 stderr:
- `ush: unsupported syntax` を含む

### t13: リダイレクトパスにも変数展開が効く
入力（t13_expand_in_redir.ush）:

```sh
rm -f out
export F=out
echo hi > $F
cat out
```

期待 stdout:

```text
hi
```

期待 stderr: なし  
期待 exit: 0

### t14: ENOEXEC のみ `/bin/sh` へ委譲（フォールバック）
前提:
- `printf` / `chmod` / `/bin/sh` が利用可能

入力（t14_enoexec_fallback.ush）:

```sh
rm -f t_no_shebang
printf 'echo delegated\n' > t_no_shebang
chmod +x t_no_shebang
./t_no_shebang
```

期待 stdout:

```text
delegated
```

期待 stderr: なし  
期待 exit: 0

## 11.4 対話モードの手動受け入れ（出力より“操作感”）
対話は出力の取り回しが難しいので、最低限は手動で確認する。

- 文字挿入/左右移動/BS/DEL/Delete が破綻しない
- ↑↓で履歴が遷移し、Enterで確定できる
- Tab補完:
  - 候補1つ→確定補完
  - 候補複数→共通プレフィックスまで伸びる
  - それ以上決められない→候補一覧→プロンプト＋入力行を再描画

---

# 12. よくある落とし穴（実装チェック）

## 12.1 `$?` と `?`（グロブ検出）の衝突
- `?` を未クォートで弾く方針だと `$?` まで弾きがち
- tokenize で「直前が `$` の `?` は例外」として扱う

## 12.2 短絡評価と `last_status` 更新順
- `&&/||` の左評価後に `last_status` を更新しないと、右の `$?` がズレる

## 12.3 fd close 漏れ
- パイプの親 close 漏れ → 右が EOF を受け取れない
- open 失敗時に片側だけ close し忘れる

## 12.4 raw mode 復帰漏れ
- SIGINT/EOF/exit など多経路で端末復帰が必要

---

# 13. 次バージョン仕様検討を“対等に回す”ための型

この章は「0.0.3を実装する」ではなく、「0.0.4+ の仕様を議論するときに、抜け漏れなく合意形成する」ための型。
ぴこぴんと対等に話すために必要なのは、知識量というより **論点の切り方** と **テストで収束させる力**。

## 13.1 仕様提案を出すときのテンプレ
提案1つにつき、最低限これを埋める。

- 目的: なぜ必要か（運用上の痛み/教育効果/互換性）
- 非目的: どこまでやらないか（POSIX互換を狙わない、など）
- 入力例: 受け付けるコマンド列（成功/失敗を両方）
- 意味論: `last_status`/短絡/パイプ/リダイレクトとどう整合するか
- エラー分類: `syntax error` か `unsupported syntax` か、メッセージは固定するか
- 実装位置: tokenize/parse/expand/exec のどこで扱うべきか
- 互換影響: 既存の“未対応検出”が壊れないか（誤解釈のリスク）
- テスト: 受け入れテスト（script mode）を最低2本（成功/失敗）追加

## 13.2 変更が入りやすい不変条件（invariants）
次バージョンでも、これを壊すと一気に“説明可能性”が落ちる。議論では最初にここを守るか確認する。

- 未対応は検出してエラー（「それっぽく動く」を禁止）
- expand は exec 直前（`$?` と短絡の整合）
- `open()` 失敗は行丸ごと不実行（no-fork）
- `/bin/sh` 委譲は ENOEXEC のみ（乱用しない）

## 13.3 コスト見積りの観点（実装難易度の地雷）
議論で「それは意外と重い」を言語化するための観点。

- **字句が増える**: tokenize の状態機械が壊れやすい（特にクォート連結やエスケープ）
- **構文が増える**: parse のエラー分類が増え、`syntax error` と `unsupported` の境界が曖昧になる
- **展開が増える**: メモリ寿命と `$?` の時点が絡む（`${VAR}`、コマンド置換など）
- **親の責務が増える**: builtins + リダイレクト対応は fd 管理が増え、端末復帰も難しくなる
- **互換性が増える**: `/bin/sh` への自動委譲を増やすほど、観測性が落ちる

## 13.4 “議論を終わらせる”ための合意の取り方
仕様議論は、最後はテストで収束させる。

- 仕様書に「例（期待stdout/期待stderr/期待status）」を入れる
- 本資料（判断メモ）に「落とし穴と不変条件への影響」を追記
- 受け入れテスト（11.3）にケースを追加し、差分が観測できるようにする

---

# 14. 現時点で“仕様が薄い/ブレやすい”論点（議論メモ）

ここは、次の仕様検討で議題に上げやすいように、曖昧になりがちな点を列挙する。
（0.0.3の実装を変える必要はないが、次バージョンで方針転換するなら先に合意しておくと強い。）

## 14.1 `&&` / `||` の優先順位
現状は「左結合で組む」方針だが、POSIX互換を狙わないとしても、ユーザーの直感とのズレは起きやすい。

- どの優先順位にするか
- “互換を取らない”なら、help や docs で明示するか
- 受け入れテストで固定するか

## 14.2 `syntax error` と `unsupported syntax` の境界
0.0.3でも十分整理しているが、拡張のたびに境界が動く。

- 「文法として禁止」なのか「将来やりたいが今は未対応」なのか
- ユーザーが次に取る行動（修正すべき/`/bin/sh`へ回すべき）をメッセージで誘導するか

## 14.3 builtins + リダイレクトの扱い
次バージョンで対応するか否かは、実装コストと学習コストが大きい。

- 対応する場合: 親で fd を差し替えて戻す必要がある（失敗経路も含む）
- 対応しない場合: “unsupported”のまま固定し、代替は外部コマンドを使う

## 14.4 `/bin/sh` 自動委譲の範囲
今は ENOEXEC のみ。

- 未対応構文に遭遇したら `/bin/sh -c` に投げる案は、観測性と安全性（意図しない解釈）を落とす
- もしやるなら「どの条件で委譲するか」「委譲したことを表示するか」を必ず決める

## 14.5 メッセージの安定性（テストのしやすさ）
受け入れテストを“厳密一致”にするか、“含む/前方一致”にするか。

- `ush: unsupported syntax` / `ush: syntax error` は固定しやすい
- `open: <strerror>` や `cat: ...` は環境依存が出やすいので、テストは「`ush: open:` を含む」等に寄せる

