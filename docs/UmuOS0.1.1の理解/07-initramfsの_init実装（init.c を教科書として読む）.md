# 07-initramfsの_init実装（init.c を教科書として読む）

UmuOS 0.1.1 の起動において、initramfs の `/init` は「核」と呼べる。
理由は単純で、この `/init` が成立しない限り、永続 root（disk.img）にもユーザーランドにも到達できないからだ。

本章では、`UmuOS-0.1.1/initramfs/src/init.c` を、

- 何を入力として受け取り
- どんな状態を作り
- 何を出力（次の段階へ渡す結果）として残すのか

という観点で“教科書として”読み解く。
コードを逐語的に追うのではなく、責務の塊（ブロック）に分割し、それぞれの設計判断を言語化する。

---

## 0. `/init` の位置づけ（この一瞬のユーザーランド）

Linux カーネルは initramfs を展開した後、そこにある `/init` を PID 1 として起動する。
UmuOS 0.1.1 では、その `/init` を C で自作し、静的リンクで initramfs に入れている。

- ソース：`UmuOS-0.1.1/initramfs/src/init.c`
- 実体：`UmuOS-0.1.1/initramfs/rootfs/init`

この `/init` の目的は `init.c` 冒頭コメントの通りであり、要約すると次の 3 点になる。

1. `/proc/cmdline` から `root=UUID=...` を読み取る
2. `/dev` を走査して、ext4 superblock の UUID が一致するデバイスを特定する
3. そのデバイスを `/newroot` に mount し、`/bin/switch_root` で永続 root の `/sbin/init` へ移行する

ここで重要なのは、`/init` の役割が「永続 root を作る」でも「ユーザーランドを構築する」でもなく、
**橋渡し**に徹している点である。

---

## 1. 全体の処理フロー（1枚で固定）

```mermaid
flowchart TD
  A[/init start] --> B[擬似FS mount: /proc /sys /dev /dev/pts]
  B --> C[/proc/cmdline から root=UUID を抽出]
  C --> D[/dev を走査して ext4 UUID を読む]
  D --> E[/newroot に ext4 を mount]
  E --> F[永続側の /sbin/init 等の存在を確認だけする]
  F --> G[exec: /bin/switch_root /newroot /sbin/init]
  G --> H[以後は永続 root のユーザーランド]

  A -->|失敗| X[emergency_loop: sleep し続ける]
  B -->|失敗| X
  C -->|失敗| X
  D -->|タイムアウト| X
  E -->|失敗| X
  G -->|失敗| X
```

この図の読み方はこうだ。

- 上の太い流れが「成功パス」
- どこかで失敗したら `emergency_loop()` に入り、ログを残しながら止まる

UmuOS 0.1.1 は“自動復旧”よりも“原因究明”を優先するため、この作りが合理的になる。

---

## 2. ログ設計：観測できる `/init` であること

`init.c` は最初に `log_printf()` を用意している。
特徴は次の通り。

- 文字列を組み立てた後、末尾改行を必ず付ける
- 出力先は stderr（fd=2）へ寄せる

この設計は「出力が混線して読めない」問題を避けるためのもので、
さらに `UmuOSstart.sh`（QEMU の `-serial stdio`）や GRUB のシリアル設定と組み合わさって、
**どこで止まってもログが見える**状態を作る。

また、`emergency_loop()` は sleep し続けるだけだが、
「再起動してしまってログが流れて消える」よりも、止めて観測できる方が目的に沿う。

---

## 3. ブロック1：擬似ファイルシステムの mount（地面を作る）

`main()` の冒頭で、`/proc` `/sys` `/dev` `/dev/pts` を mount する。

ここは単なる儀式ではない。
以降の処理に必要な“前提条件”を、段階の最初に満たすための必須作業である。

### 3.1 `/proc` が必要な理由

`/proc/cmdline` を読むために必要。
`root=UUID=...` は GRUB が kernel に渡した引数であり、カーネルはそれを `/proc/cmdline` として見せる。

つまり `/proc` が無ければ、次の入力（root UUID）を取得できない。

### 3.2 `/dev`（devtmpfs）が必要な理由

UmuOS 0.1.1 の `/init` は udev に依存しない。
その代わり、`/dev` に現れるブロックデバイス（例：`/dev/vda`）を直接読みに行く。

だから `devtmpfs` を mount して、
カーネルに `/dev` のデバイスノードを供給してもらう必要がある。

ここが成立していないと、いくら `/init` が正しくても「探す対象が現れない」ので詰む。

---

## 4. ブロック2：`root=UUID=...` の取得（入力を読む）

`parse_root_uuid_from_cmdline()` が入力を読む。
流れは次のように理解できる。

1. `/proc/cmdline` を読む
2. `root=UUID=` というキーを探す
3. 見つかったら UUID 文字列（`xxxxxxxx-....`）を 16 byte のバイナリにパースする
4. パースした UUID を文字列に戻してログに出す（観測）

### 4.1 UUID を 16 byte にする理由

比較が目的だからである。
後続の処理で ext4 superblock の UUID（16 byte）を読み取り、
それと一致するかどうかを `memcmp()` で判定する。

文字列比較にしてしまうと、

- 大文字小文字
- ハイフンの有無
- 余計な空白

などの表層差分に引きずられる。
本質は 16 byte の同一性なので、バイナリに落とす設計は妥当になる。

### 4.2 ここでの失敗の意味

`root=UUID=...` が見つからない／壊れている場合、
永続 root を特定できないので先へ進めない。

だからこの段階の失敗は即 `emergency_loop()`。
その際、`/proc/cmdline` 自体もログに出しているので、
「GRUB が渡した引数が想定と違う」を観測できる。

---

## 5. ブロック3：永続 root を探す（/dev走査 + ext4 UUID 読み）

ここが UmuOS 0.1.1 の `/init` の核心である。

一般的な Linux では `/dev/disk/by-uuid/` のような仕組みを使うことが多いが、
UmuOS 0.1.1 はそれに依存せず、
`/dev` を直接走査して ext4 superblock を読みに行く。

### 5.1 走査対象を絞る（候補名のフィルタ）

`is_candidate_dev_name()` は候補を最小限に絞る。

- `vd*`：virtio-blk（QEMU の `if=virtio`）
- `sd*`：SCSI/SATA など
- `nvme*`：NVMe

このフィルタの目的は 2 つ。

第一に、無関係な `/dev/tty` のような大量のエントリを読み続けないこと。
第二に、意図している“起動デバイス候補”だけに集中してログを読みやすくすること。

### 5.2 「ブロックデバイスである」ことの確認

`stat()` して `S_ISBLK` を確認する。

これは安全策であり、
「候補名っぽいがブロックデバイスではないもの」を弾く。

### 5.3 ext4 だとどうやって判定するか（superblock）

`read_ext4_uuid_from_device()` は、ext4 の superblock を読んで判定する。

最低限の知識として、次を覚える。

- ext4 の superblock はデバイス先頭から 1024 byte の位置にある
- magic（0xEF53）が一致するなら ext 系ファイルシステムと見なせる
- UUID は superblock 内に 16 byte で保持されている

UmuOS 0.1.1 の `/init` は、
この「最低限の構造」だけを使って UUID を読みに行く。

ここでの設計判断は、

- 便利な仕組みに依存しない
- 本物（ブロックデバイスの中身）を読む

という思想に一致している。

### 5.4 見つけるまで待つ（リトライ設計）

`/dev` は“最初から完全に揃っている”とは限らない。
起動直後は、ドライバの初期化順序やタイミングでブロックデバイスが遅れて出てくることがある。

そこで `/init` はリトライする。

- 最大 120 回
- 1回あたり 250ms
- 合計約 30 秒待つ

この待ちの設計は、「起動直後のタイミング問題」を仕様として吸収するためのものだ。

### 5.5 タイムアウトしたら何を出すか

見つからない場合、`dump_ext4_candidate_uuids()` を呼び、
候補デバイスの ext4 UUID を列挙してログに出す。

これは原因究明に強い。

- そもそも ext4 が見えていない（候補ゼロ）
- ext4 は見えているが UUID が違う（disk.img を作り直した、grub.cfg を更新していない）
- 期待していないデバイスを見ている（virtio-blk が効いていない、別の接続）

などを、ログだけで判断できる。

---

## 6. ブロック4：`/newroot` に mount（永続 root を“仮に”置く）

一致するデバイスが見つかったら、`/newroot` を作り、そこへ ext4 を mount する。

ここでのポイントは 2 つ。

### 6.1 なぜマウント先が `/newroot` なのか

`switch_root` の一般的な形は、

- “新しい root をどこかに mount しておき”
- “そこに root を切り替える”

である。

慣例的にこのマウント先が `/newroot` と呼ばれることが多く、
UmuOS 0.1.1 もその慣例に沿っている。

### 6.2 「永続へ書かない」方針の具体化

mount が成功したら、`/newroot` の中にあるべきファイルの存在を確認するが、
ここで“作る”ことはしない。

確認しているのは次の 3 点だ。

- `/newroot/sbin/init`
- `/newroot/etc/inittab`
- `/newroot/etc/init.d/rcS`

これらは `switch_root` 後にユーザーランドが成立するための最小条件であり、
欠けていれば次段で詰む。

それを **早い段階でログに出して可視化**しているのは、観測の設計である。

---

## 7. ブロック5：`switch_root` を exec する（責務を渡す）

最後に `/bin/switch_root` を `execv()` する。

- 実行：`/bin/switch_root /newroot /sbin/init`

ここで理解すべきことは、`exec` の意味である。

### 7.1 なぜ exec なのか

`exec` は「いま動いているプロセス（PID 1）を別のプログラムに置き換える」操作である。

この瞬間に、initramfs の `/init` は消え、永続 root の `/sbin/init` が PID 1 になる。

つまり、これは単なるコマンド起動ではなく、
**OS の責務の引き継ぎ点**そのものである。

### 7.2 `/bin/switch_root` はどこから来るか

UmuOS 0.1.1 の initramfs では BusyBox が入り、
`/bin/switch_root` は BusyBox applet として symlink で提供される。

したがって、

- BusyBox が入っている
- applet が生成されている

という前提が崩れると、ここで失敗する。
`init.c` はその点も `access("/bin/switch_root", X_OK)` でチェックし、warn を出す。

---

## 8. 典型的な失敗パターンの読み方（ログで切り分ける）

切り分けやすいように、「ログの出方」と原因を対応づける。

### 8.1 `root=UUID= not found in cmdline`

原因はほぼ次のどちらか。

- `grub.cfg` の `linux ... root=UUID=...` が入っていない
- `linux` 行が別の menuentry を見ている／編集が反映されていない

### 8.2 `device not found yet` が続いてタイムアウト

原因候補は段階的に絞れる。

- `/dev` が成立していない（devtmpfs が mount できていない）
- virtio-blk が無い（カーネル設定、または QEMU の `if=virtio`）
- disk.img 自体が接続されていない

このとき、最終的に候補 UUID 列挙が出るので、
「何が見えているか」をログで確認できる。

### 8.3 `mount root failed`

- ext4 ドライバが無い（`CONFIG_EXT4_FS=y` が必要）
- デバイスは一致しているが破損している

### 8.4 `execv switch_root failed`

- initramfs に `bin/switch_root` が無い、または実行できない
- BusyBox applet 生成が壊れている（ホスト絶対パス symlink など）

この問題は詳細設計で「必ず chroot して applet 生成」として対策されている。

---

## 9. 章のまとめ：UmuOS 0.1.1 の `/init` を一言で言う

UmuOS 0.1.1 の initramfs `/init` は、

- 入力：`/proc/cmdline` の `root=UUID=...`
- 処理：`/dev` を走査し ext4 superblock の UUID を照合
- 出力：`/newroot` への mount と、`switch_root` による責務移譲

を、最小依存で実現するプログラムである。

次章では、`switch_root` 後の世界（永続 root の `/sbin/init` と inittab/rcS）が何を担うかを解説する。
