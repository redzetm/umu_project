# ush-spec.md
UmuOS User Shell (ush) — MVP 仕様書  
Version: 0.1.0-MVP  
Target OS: UmuOS-0.1.4-base-stable  

---

# 1. 概要
ush（ウッシュ）は **UmuOS の対話型ユーザーシェル**として設計される。

既存の `/bin/sh`（BusyBox）は以下の用途で継続利用する：

- `/bin/sh` → サーバーソフト・スクリプト実行用（BusyBox sh）
- `/bin/ush` → ユーザー向け対話シェル（本仕様で実装）

ush は **最小限・高速・シンプル・教育的** を理念とし、UmuOS の世界観に沿った独自性を持つ。

---

# 2. 哲学（Philosophy）
ush の設計思想は以下の通り。

- **最小限・高速・シンプル**
- **教育的で読みやすいコード**
- **UmuOS の世界観に沿った独自性**
- **POSIX 互換性は追わない（MVPでは不要）**
  - ただし将来 Apache などを動かすために `/bin/sh`（BusyBox）を残す

ush は「ユーザー向けの対話シェル」であり、POSIX sh とは役割を分離する。

---

# 3. ビルド方針
- **musl-gcc による静的リンクビルド**
  - glibc に依存しない自己完結バイナリを生成する
  - Alpine Linux と同様の軽量・堅牢な構成を目指す

例：
musl-gcc -static -o ush main.o ...


---

# 4. MVP（Minimum Viable Product）
ush の MVP は「最小限の実用性を持つ最初の完成形」とする。

## 4.1 MVPで実装する機能
### ✔ 入力処理
- プロンプト表示
- 標準入力から1行取得

### ✔ トークナイズ（parser）
- 空白区切り
- 引用符対応は後回し

### ✔ PATH 検索
- `$PATH` を走査して実行ファイルを探す

### ✔ 実行（exec）
- `fork()`
- 子プロセスで `execve()`
- 親プロセスで `waitpid()`

### ✔ builtins（内蔵コマンド）
- `cd`
- `exit`
- `help`（UmuOSらしい説明を表示）

### ✔ プロンプト
- `UmuOS:ush:<cwd>$`
- `<cwd>` は `getcwd()` で取得

例：
UmuOS:ush:/home/tama$


---

# 5. ディレクトリ構造
ush のソースツリーは以下の構造とする。

ush/
├── main.c            // メインループ
├── parser/           // トークナイザ・構文解析
├── exec/             // 実行系（fork/exec）
├── builtins/         // 内蔵コマンド
├── env/              // 環境変数管理
└── utils/            // 便利関数


---

# 6. UmuOS への組み込み
- ビルドした `ush` バイナリを `/bin/ush` に配置
- `/bin/sh` は BusyBox のまま維持
- 必要に応じて `/etc/shells` に登録（任意）

---

# 7. 将来の拡張（MVP以降）
- パイプ `|`
- リダイレクト `< > >>`
- シグナル処理（Ctrl+C）
- ジョブ制御（簡易版）
- コマンド履歴
- 補完機能
- カラー表示
- スクリプト実行
- POSIX 互換性の強化（必要に応じて）

---

# 8. 付録：help コマンドの例
ush - UmuOS User Shell (MVP)
Builtins:
cd <dir>     Change directory
exit         Exit ush
help         Show this help


---

# 9. ライセンス
UmuOS プロジェクトのライセンス方針に従う（未定の場合は後日追記）。




