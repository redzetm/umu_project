# 4-GRUB設定とカーネル引数（root=UUID, console 等）

前章までで、UmuOS 0.1.1 の起動は
UEFI → GRUB → Linux kernel → initramfs(`/init`) → `switch_root` → ユーザーランド
という「段階の連鎖」であることを見た。

この章では、その連鎖の“つなぎ目”のうち、GRUB が担う部分を丁寧に固定する。
GRUB は単に「カーネルを選ぶメニュー」ではない。
**カーネルへ渡す入力（カーネル引数）を決める**という意味で、起動の仕様そのものを持っている。

## 1. GRUB 設定ファイルはどこか

UmuOS 0.1.1 の GRUB 設定はここにある。

- `UmuOS-0.1.1/iso_root/boot/grub/grub.cfg`

ここで重要なのは、`iso_root/` が「ISO の中身の配置」である点だ。
つまり `iso_root/boot/grub/grub.cfg` は、ISO の中では `/boot/grub/grub.cfg` になる。

## 2. grub.cfg の全体像（観測を優先する設計）

`grub.cfg` を読むと、次の意図が見えてくる。

1つ目は、起動時の観測を安定させたい（シリアルを優先）。
2つ目は、GRUB が読む kernel/initrd の位置を固定したい。

### 2.1 シリアルを優先する理由

`grub.cfg` には、GRUB 自身の入出力先をシリアルへ寄せる設定が入っている。

- `serial --unit=0 --speed=115200 ...`
- `terminal_input serial console`
- `terminal_output serial console`

この設定は、QEMU を `-nographic -serial stdio` で運用する設計と整合する。
つまり「表示が出ないから何が起きたか分からない」を避けるための“観測の土台”である。

## 3. linux 行（カーネル起動）で何を決めているか

`linux /boot/vmlinuz-6.18.1 ...` の行は、カーネルを起動するだけでなく、
その後の initramfs `/init` が読む入力（`/proc/cmdline`）の中身を決める。

ここでは、特に重要な引数を 3 つに分けて説明する。

### 3.1 `root=UUID=...` は何のためか

UmuOS 0.1.1 は「最終的な `/` は永続ディスク（ext4, disk.img）」という仕様を持つ。
その永続ディスクを指定するのが `root=UUID=...` である。

ただし注意点がある。
この `root=...` は “カーネルが勝手に永続 root をマウントしてくれる” という意味ではない。
UmuOS 0.1.1 では initramfs の `/init` が自作であり、
その `/init` が `/proc/cmdline` を読んで `root=UUID=...` を解釈し、
自分で `/newroot` に ext4 を mount している。

つまり `root=UUID=...` は、

- 「どの永続ディスクを最終 root にしたいか」という要求
- initramfs `/init` が受け取る入力

の両方の意味を持つ。

この UUID の取得方法は、詳細設計で固定されている。

- `UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 4.2
- `sudo blkid -p -o value -s UUID disk/disk.img`

重要：disk.img を作り直すと UUID は変わる。
その場合、`grub.cfg` の `root=UUID=...` も必ず更新する必要がある。

### 3.2 `rw` は何を意味するか

`rw` は「最終 root を読み書き可能として扱う」という意思表示である。
UmuOS 0.1.1 の initramfs `/init` も、永続 root を `mount(..., "ext4", 0, ...)` として rw で mount する。

ここで押さえるべきなのは、
initramfs 段階は“永続に書かない”方針だが、永続 root に切り替えた後は書ける、という責務分離である。

- initramfs：永続へ書かない（混線防止）
- 永続 root（rcS 等）：必要なら永続へ書く（例：/logs/boot.log）

### 3.3 `console=` は何を意味するか

`console=` はカーネルがログやコンソール出力をどこへ出すかを決める。
UmuOS 0.1.1 では

- `console=tty0`
- `console=ttyS0,115200n8`

が指定されている。

教科書として覚えるべきことは次の一点である。

> 「どこに出力が出るか」を決めないと、失敗しても“沈黙”になって観測できない。

QEMU を `-nographic` で動かす場合、ttyS0 への出力が観測の生命線になる。

### 3.4 `loglevel=7` と `panic=-1`

- `loglevel=7` はログを詳細に出す方向（観測優先）
- `panic=-1` はパニック後に再起動せず止める方向（観測優先）

UmuOS 0.1.1 の設計思想（研究と観測）に沿った選択である。

## 4. initrd 行（initramfs の指定）

`initrd /boot/initrd.img-6.18.1` は、
「カーネルが最初に展開する initramfs」を指定する。

ここで重要なのは、initrd（initramfs）が “永続 root” ではない点である。
initrd はあくまで橋渡しであり、最終 root は disk.img（ext4）になる。

## 5. この章のまとめ：GRUB は “入力仕様” を決める

UmuOS 0.1.1 において GRUB は、
単に起動の入口ではなく、initramfs `/init` の入力（`/proc/cmdline`）を確定する場所である。

だからこそ、`root=UUID=...` の値が disk.img の UUID と一致していることは、
起動が成立するかどうかの最重要チェックポイントになる。
