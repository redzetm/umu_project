---
title: UmuOS-0.1.6-dev 実装ノート
date: 2026-02-15
status: accepted
---

# UmuOS-0.1.6-dev 実装ノート

このノートは、[UmuOS-0.1.6-dev 詳細設計書](UmuOS-0.1.6-dev-詳細設計書.md) を手動で実行した際の「差分・詰まり・判断」を記録する。

---

## 進捗

### 2026-02-15

- 章「1. Ubuntu 事前準備（パッケージ）」〜「9. ISO（grub.cfg + grub-mkrescue）」まで実施
- 結果: 問題なし（詳細設計書の手順どおりに完了、途中修正なし）

**観測点（この時点）**
- BusyBox: `busybox` が static で生成され、`ntpd` / `tcpsvd` / `ftpd` が `--list` で確認できた
- initramfs: `initramfs/initrd.img-6.18.1` が生成され、`iso_root/boot/initrd.img-6.18.1` にコピーできた
- ISO: `UmuOS-0.1.6-boot.iso` が生成できた（サイズ0でない）
- ログ: kernel / busybox のビルドログは以下に出力されている想定
  - `logs/kernel_build_bzImage.log`
  - `logs/busybox_build.log`

---

## メモ（判断・意図）

### initramfs rootfs を毎回 `rm -rf` する理由

- 2章の `initramfs/rootfs` は「作業ディレクトリの器」を作るだけ
- 5章の `rm -rf initramfs/rootfs` は「成果物としての initramfs をクリーンに再生成」するため
  - `busybox --install -s` が大量の symlink を作るため、残骸があると再現性が崩れやすい
  - `sudo chroot` を挟む関係で root 所有/権限のファイルが混ざることがあり、次回試行の障害になりうる

### `sudo chroot ... /bin/busybox --install -s /bin` / `/sbin` の意図

- `chroot`：このコマンドの「/」を initramfs の rootfs に切り替える（ホストの `/bin` `/sbin` を汚さない）
- `--install -s`：各コマンド名を実体コピーではなく symlink で配置する（例：`/sbin/getty -> /bin/busybox`）
- `/bin` と `/sbin` の両方に入れる：起動・管理系も含め、呼ばれる場所の違いでコマンド探索が壊れないようにする

---

## 起動観測（ゲスト）

### 2026-02-15

- `date` が `JST` 表示になる（例：`Sun Feb 15 07:58:40 JST 2026`）
- `/logs/boot.log` に `boot_id` / `time` / `uptime` / `cmdline` / `mount` が追記される（= `rcS` が実行されている）
- `[ntp_sync] before/after` があり、少なくともこの回は NTP 同期処理が実行されている（`before` から約10秒後に `after`）
- `mount` で `/dev/vda on / type ext4 (rw,...)` を確認（virtio-blk + 永続ディスク rootfs）
- kernel のログに `kworker/u4:2 ... used greatest stack depth ...` が出たが、現時点では起動は継続（要監視）

補足：`/proc/cmdline` の末尾が `... panic=-1 ne0` と表示されており、`net.ifnames=0 biosdevname=0` が省略/崩れて見えている可能性がある。
次回はゲストで `cat /proc/cmdline` を直接確認する。
また、NTP が成功しているため、この回はネットワークが有効だった可能性が高い（`NET_MODE=none` だと `ping` が落ちやすい）。

---

## 開発環境でのテスト結果（ゲスト）

### 2026-02-15

- JST / ntpd 同期: OK
- su: OK
- ll: OK
- FTP: OK
- telnet: OK
- マルチユーザーアクセス: OK

---

## 受け入れ

### 2026-02-15

- この詳細設計書ですべて Tera Term で実装し、問題なくテストを全て終了
- 受け入れ合格とする
