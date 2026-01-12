# UmuOS 0.1.3 は 0.1.2 から何が変わったか（差分・総括）

作成日: 2026-01-12

この文書は、UmuOS-0.1.2 と UmuOS-0.1.3 の差分を「受入基準」「運用」「権限」「同時ログイン」の観点で整理する。

参照（正本）:
- 0.1.2: `~/umu/umu_project/UmuOS-0.1.2/docs/`
- 0.1.3: `~/umu/umu_project/UmuOS-0.1.3/docs/`

---

## 1. 結論（0.1.3 の到達点）

- 0.1.3 は 0.1.2 と同じ粒度（PKGインストール〜Kernelビルド〜disk.img作成〜initramfs生成〜ISO生成〜起動〜受入）で **最初から再生成できる**ベース版。
- 0.1.3 の主差分は以下の2点（付随して ttyS1 側ホストログ取得も必須化）。
  1) `tama` から `su -` で root に切り替えられる（安全寄り：setuid を `su` に限定）
  2) `ttyS1` を追加し、別ターミナルで root と tama を同時ログインして操作できる

---

## 2. 変えていないもの（0.1.2 と共通）

- ブートフローは同じ
  - UEFI → GRUB → Linux kernel → initramfs（自作 `/init`）→ ext4（disk.img）へ `switch_root`
- Kernel version は据え置き（差分最小化）
  - `6.18.1`
- ログ設計（二系統ログ）は同じ
  - ①ホスト側: `script(1)` で QEMU シリアル（ttyS0）を `logs/` に保存
  - ②ゲスト側: ext4 側 rcS が `/logs/boot.log` に追記（1ブート=1ブロック）

---

## 3. 主要差分（一覧）

| 観点 | 0.1.2 | 0.1.3 |
|---|---|---|
| `su -` | `su: must be suid to work properly` になり得る（BusyBoxがsetuidでない） | `tama`→`su -` が成立（**`busybox.su` を作り、`su` だけ setuid**） |
| 同時ログイン | ttyS0のみ | ttyS0 + ttyS1（ttyS1 は `-serial pty` + `inittab` の getty） |
| 起動スクリプト | `-serial stdio` のみ | `-serial stdio -serial pty` を追加（ttyS1 を提供） |
| ログ（ホスト側） | ttyS0（stdio）のセッションを `script` で保存 | ttyS0 に加え、**ttyS1 接続側も `script` で保存（`logs/ttyS1_*.log`）** |

---

## 4. 設計判断（なぜそうしたか）

### 4.1 `su` の setuid を BusyBox 全体に付けない

- BusyBox を丸ごと setuid にすると、意図せず強い権限が広範囲に広がりやすい。
- 0.1.3 では「最小差分」と「安全寄り」を両立するため、`su` 専用の BusyBox 実体（`busybox.su`）を作り、そこだけ setuid に限定する。

### 4.2 ttyS1 の追加は “観測主経路（ttyS0）” を壊さない

- ttyS0 は引き続き観測の生命線（GRUB/menu/kernelログの主経路）として維持。
- ttyS1 は同時ログイン用の追加経路。
- ホストログは ttyS0（起動端末の `script`）と ttyS1（接続端末の `script`）で **別ファイル**として記録し、合流させない。

---

## 5. まとめ（0.1.3 を固定してよい理由）

- 0.1.2 の「再現性 + 二系統ログ」を維持したまま、
  - 権限操作（`su`）
  - 同時操作（別ターミナル）
 という“OSとしての使い勝手”を最小拡張できる。
- 以後の開発では「root作業」と「一般ユーザー作業」を同時に進められるため、実験効率が上がる。
