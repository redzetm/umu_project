# 06-initramfs設計（何を入れて何を捨てるか）

initramfs（early userspace）は、OS 全体から見ると短い一瞬の存在である。
しかし UmuOS 0.1.1 では、この短い一瞬に「最重要の責務」を背負わせている。

- GRUB が渡した `root=UUID=...` を読み取り
- `/dev` から永続ディスク（ext4）を見つけ
- `/newroot` に mount し
- `switch_root` で永続 root へ移行する

この章では、UmuOS 0.1.1 の initramfs を
「どんな思想で、何を入れて、何を捨てたか」という観点で説明する。

## 1. initramfs は“橋渡し専用”である

UmuOS 0.1.1 の initramfs は、最終的なユーザーランドの場所ではない。
目的はただ一つ、永続 root（disk.img）へ移行することだ。

この思想は、initramfs `/init` のコメントにも明示されている。

- `UmuOS-0.1.1/initramfs/src/init.c`

とくに重要なのは、次の一文である。

> initramfs の間は、永続 root（/newroot にマウントした ext4）には書き込まない

これは「きれい好き」ではなく、混線防止の設計である。
initramfs 段階は失敗しやすい。
そこで永続へ書いてしまうと、

- 失敗時に中途半端な状態が残る
- 次回の起動で“残骸”が原因になって切り分けが難しくなる

という問題が出る。

UmuOS 0.1.1 はそれを避け、
永続側へは「存在確認だけ」してログに出す形にしている。

## 2. 何を入れるか：最小の道具（BusyBox）

initramfs の rootfs（生成元）は、詳細設計で次のように作られる。

- `UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 6.1

ここでの中心は BusyBox である。
BusyBox は、たくさんの小さなコマンド（applet）を 1 つの実行ファイルとして提供する。

### 2.1 なぜ BusyBox を入れるのか

ここで混線しやすいポイントを言い換えるなら、initramfs で必要なのは
「mount したり、最低限のシェルを提供したり、switch_root を呼び出したりする道具」である。

それを全部自作してもよいが、
0.1.1 の目的は “ユーザーランドのコマンド開発” ではなく
“起動の成立と観測” である。

だから initramfs には BusyBox を入れ、
initramfs の責務を「橋渡し」に集中させる。

### 2.2 なぜ静的リンク版（busybox-static）が嬉しいのか

最小 rootfs では、必要な共有ライブラリが揃っているとは限らない。
動的リンクの BusyBox を入れると、
「必要なライブラリが無くて実行できない」という別の問題が混ざる可能性がある。

そのため、詳細設計は busybox-static を推奨している。

- `UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 5.3

## 3. 何を捨てるか：udev に依存しない

一般的な Linux では、`/dev/disk/by-uuid` のような便利な仕組みがある。
しかしそれは、udev などの周辺仕組みに依存している。

UmuOS 0.1.1 の initramfs `/init` は、そこに依存しない。
代わりに `/dev` を直接走査し、ext4 superblock を読んで UUID を照合する。

よく混ざるポイントを言うと、

- “便利な一覧（by-uuid）” を信じるのではなく
- “本物（ブロックデバイスの中身）” を読みに行く

という設計である。

これにより、
「どのレイヤが何をしているか」が見えやすくなり、観測にも強くなる。

## 4. /init はなぜ musl 静的なのか

initramfs の `/init` は C で自作され、
`musl-gcc -static` で静的リンクされる。

- `UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 6.2

この判断の目的は、
initramfs 段階で “動的リンクの依存” を混ぜないことにある。

`/init` が起動できないと、そこから先に何も進まない。
だから `/init` は外部依存を最小にして「確実に動く」ことを優先する。

## 5. BusyBox applet 生成で「chroot」が必須な理由

BusyBox の `--install -s` は、各 applet を symlink として生成する。

ここでつまずきやすいのは、
ホスト側のマウント先で実行すると symlink がホストの絶対パスを指して壊れることがある点だ。

詳細設計は、この混線を避けるために、initramfs でも ext4 側でも
**必ず chroot してから** applet を生成する方針を取っている。

- initramfs 側：`sudo chroot rootfs /bin/busybox --install -s /bin`
- ext4 側：`sudo chroot /mnt/umuos011 /bin/busybox --install -s /bin` など

これは「運用上の注意」ではなく、
“再現性を壊さないための仕様”として理解するべきポイントである。

## 6. initrd の生成は「一覧→cpio→gzip」である

initramfs（生成元ツリー）から initrd.img（圧縮イメージ）を作る手順は、
詳細設計で明確に固定されている。

- `find . -print0 > initrd.filelist0`（収録一覧）
- `cpio --null -ov --format=newc < filelist > initrd.cpio`（アーカイブ化）
- `gzip -9 -c initrd.cpio > initrd.img-6.18.1`（圧縮）

ここで重要なのは、
initrd.img は“魔法の箱”ではなく、
中身の一覧（initrd.list / initrd.cpio.list）で検証できるという点だ。

### 6.1 つまずき：`head` だけだと /init が無いように見える

initrd の中身確認で `head` だけを見ると、一覧の先頭に `init` が出てこないことがある。
それは「入っていない」ではなく「見えていない」だけの可能性が高い。

だから U0.1.1 の設計では、ピンポイントに grep する。

- `grep -E '^(init|bin/switch_root)$' initramfs/initrd.list`

この小さな工夫も「観測を壊さない」ための設計判断である。

## 7. initramfs の責務分界（図で固定）

最後に、initramfs が何をし、何をしないかを図で固定する。

```mermaid
flowchart TD
  A[initramfs /init 起動] --> B[/proc /sys /dev を mount]
  B --> C[/proc/cmdline を読む]
  C --> D[/dev を走査して ext4 UUID を照合]
  D --> E[/newroot に mount]
  E --> F[永続側の必須ファイルを「存在確認だけ」]
  F --> G[switch_root で / を切替]
  G --> H[永続 root の /sbin/init へ引継ぎ]

  subgraph NG[initramfs がやらないこと]
    X[永続ログへ追記]
    Y[永続側の設定を書き換え]
  end
```

initramfs は短い一瞬だが、
この責務分界があるおかげで、
UmuOS 0.1.1 は「壊れたときの切り分け」ができる形になる。
