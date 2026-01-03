# UmuOS 0.1.1 の理解（教科書）

このディレクトリは、UmuOS 0.1.1 を **初学者にも説明できる粒度**で、体系立てて理解するための「本」である。
結論（仕様）だけでなく、なぜその構成にしたのか（設計判断）まで含めて、後から 100% 言語化できる状態を目指す。

本書は「UmuOS 0.1.1 の範囲」だけを扱う。スコープ外（ネットワーク等）は扱わない。

---

## 読み方（おすすめ）

最初は次の順で読むと迷いにくい。

1. まず全体像（起動の責務分界）を把握する
2. つぎに成果物（ISO / initrd / disk.img など）を「どの段階（どのコンポーネント）が参照するか」で整理する
3. その上で initramfs の `/init` と `switch_root` を理解し、永続 root 側へ移る
4. 最後に「再現手順」を通して理解を確定させる

---

## 本書の約束（スタイル）

- **教科書寄り**に書く：用語定義 → 背景 → 仕組み → UmuOS 0.1.1 での実装 → 設計判断。
- 図を使う：Mermaid でフロー図・責務分界を描く。
- 文章中心：箇条書きは最小限（目次や、短い整理にだけ使う）。
- 一次情報に紐付ける：UmuOS-0.1.1 配下のファイルを根拠として説明する。

---

## 目次（連番）

以下のファイルを順に読めば、UmuOS 0.1.1 の全体を“線”として理解できる。

1. [01-導入（目的・非目的・用語の地図）.md](01-導入（目的・非目的・用語の地図）.md)
2. [02-成果物（何がどこにありどの段階が参照するか）.md](02-成果物（何がどこにありどの段階が参照するか）.md)
3. [03-起動フロー全体（UEFI→GRUB→kernel→initramfs→switch_root）.md](03-起動フロー全体（UEFI→GRUB→kernel→initramfs→switch_root）.md)
4. [04-GRUB設定とカーネル引数（root=UUID, console 等）.md](04-GRUB設定とカーネル引数（root=UUID, console 等）.md)
5. [05-kernel側の成立条件（devtmpfs, initrd, ext4, virtio-blk）.md](05-kernel側の成立条件（devtmpfs, initrd, ext4, virtio-blk）.md)
6. [06-initramfs設計（何を入れて何を捨てるか）.md](06-initramfs設計（何を入れて何を捨てるか）.md)
7. [07-initramfsの_init実装（init.c を教科書として読む）.md](07-initramfsの_init実装（init.c を教科書として読む）.md)
8. [08-switch_root後のユーザーランド（sbin-init, inittab, rcS）.md](08-switch_root後のユーザーランド（sbin-init, inittab, rcS）.md)
9. [09-永続ディスクdisk.img（作成・UUID・中身）.md](09-永続ディスクdisk.img（作成・UUID・中身）.md)
10. [10-ISO作成（iso_root と grub-mkrescue）.md](10-ISO作成（iso_root と grub-mkrescue）.md)
11. [11-QEMU起動と観測（umuOSstart.sh と ttyS0）.md](11-QEMU起動と観測（umuOSstart.sh と ttyS0）.md)
12. [12-再現手順（ビルド→起動→永続性確認）.md](12-再現手順（ビルド→起動→永続性確認）.md)
13. [13-トラブルシュート（よくある詰まりどころ）.md](13-トラブルシュート（よくある詰まりどころ）.md)
14. [14-用語集（最小の言葉で最大の説明）.md](14-用語集（最小の言葉で最大の説明）.md)

---

## 参照する一次情報（このリポジトリ内）

本書は、主に以下を根拠にする。

- `UmuOS-0.1.1/iso_root/boot/grub/grub.cfg`
- `UmuOS-0.1.1/umuOSstart.sh`
- `UmuOS-0.1.1/initramfs/src/init.c`
- `UmuOS-0.1.1/docs/詳細設計-0.1.1.md`


