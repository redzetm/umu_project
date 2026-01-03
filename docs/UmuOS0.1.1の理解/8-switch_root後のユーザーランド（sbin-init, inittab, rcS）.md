# 8-switch_root後のユーザーランド（sbin-init, inittab, rcS）

前章で、initramfs の `/init` が `switch_root` を `exec` して
永続 root（disk.img, ext4）へ責務を渡す点が UmuOS 0.1.1 の核だと確認した。

この章では、その「渡した先」であるユーザーランド側が
何を担い、何を前提にし、どんな最小構成で成立しているのかを教科書として解説する。

結論から言えば、`switch_root` 後の世界の核は次の三点である。

- `/sbin/init`（PID 1）
- `/etc/inittab`（init の“台本”）
- `/etc/init.d/rcS`（最初の初期化スクリプト）

これらが揃うと、ttyS0 に getty が出てログインできる。
これが 0.1.1 の「成立」の重要観測点になっている。

---

## 0. `switch_root` 後に何が変わるか（1枚で固定）

`switch_root` の前後で、少なくとも次の 2 つが変わる。

- `/`（root ファイルシステム）が initramfs から ext4（disk.img）へ切り替わる
- PID 1 が initramfs の `/init` から、ext4 側の `/sbin/init` へ置き換わる

この変化は「プロセスの起動が進む」というより、
**OS が頼る基盤が切り替わる**という意味で重要である。

```mermaid
flowchart LR
  subgraph Before[Before: initramfs]
    A1[PID 1: /init] --> A2[/ is initramfs]
  end

  subgraph After[After: ext4 (disk.img)]
    B1[PID 1: /sbin/init] --> B2[/ is disk.img]
    B1 --> B3[/etc/inittab を読む]
    B3 --> B4[rcS を実行]
    B3 --> B5[getty を respawn]
  end

  A1 -->|exec switch_root| B1
```

ここで「/sbin/init が起動できない」と、以後のユーザーランドは成立しない。
だから initramfs `/init` は、永続側の `/sbin/init` と `inittab` と `rcS` の存在を
（書き換えずに）確認し、warn を出す設計になっている。

- 参照：`UmuOS-0.1.1/initramfs/src/init.c`

---

## 1. `/sbin/init`：なぜ BusyBox init なのか

UmuOS 0.1.1 の永続 root では、`/sbin/init` は BusyBox に向けられる。
詳細設計では、次の2つの手段が示されている。

1. BusyBox applet 生成によって `/sbin/init` を作る
2. それでも不安なら、明示的に symlink を張り直す（`ln -sf`）

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 5.4〜5.5

### 1.1 BusyBox init の意味（初心者向けに言い換える）

「init」とは、OS がユーザーランドへ移行した瞬間に最初に動く“司令塔”である。
この司令塔がいないと、

- プロセスを起動する
- 端末でログインできるようにする
- 何らかの初期化を行う

といったことができない。

BusyBox init は、最小環境での init として広く使われている。
UmuOS 0.1.1 において BusyBox init を採用するメリットは、
「最小構成でも成立しやすい」ことと、「依存関係を増やさない」ことにある。

### 1.2 重要：applet 生成は必ず chroot で行う

詳細設計が最重要として警告しているのはここである。

ホスト側で disk.img を `/mnt/umuos011` にマウントしているときに
そのまま `busybox --install` を叩くと、symlink がホストの絶対パスを指して壊れることがある。

その結果、起動後の ext4 側では `/bin/sh` などが解決できず、
`rcS` の最初の行（`#!/bin/sh`）すら実行できなくなる。

だから UmuOS 0.1.1 では次のように **chroot してから** applet を作る。

- `sudo chroot /mnt/umuos011 /bin/busybox --install -s /bin`
- `sudo chroot /mnt/umuos011 /bin/busybox --install -s /sbin`

この手順は「運用上の注意」ではなく、
**再現性を壊さないための仕様**として理解する。

---

## 2. `/etc/inittab`：ユーザーランドを起動する“台本”

BusyBox init は、`/etc/inittab` を読んで「何を起動するか」を決める。
UmuOS 0.1.1 の `inittab` は、詳細設計で次の内容として固定されている。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 5.6

ここでは、その内容を「行ごとの責務」として解説する。

### 2.1 `::sysinit:/etc/init.d/rcS` は何か

`sysinit` は「システム初期化のために、最初に 1 回だけ実行するもの」という意味である。

つまり、この行はこう読める。

> 最初に `/etc/init.d/rcS` を実行して、最低限の初期化を行え。

UmuOS 0.1.1 の設計では、
initramfs は“永続に触らない橋渡し”であり、
永続 root 側の初期化（mount、ログ、表示）は `rcS` の責務に寄せている。

そのため `rcS` が `sysinit` に置かれているのは必然である。

### 2.2 `ttyS0::respawn:/sbin/getty ...` は何か

これは「ログインできるようにする行」である。

- `ttyS0`：シリアルコンソール
- `respawn`：プロセスが死んだら起動し直す
- `/sbin/getty ...`：端末のログインプロンプトを出す

UmuOS 0.1.1 は観測を ttyS0 へ寄せるため、
ここを“成立の観測点”として固定している。

この 1 行が正しく動くと、起動後に ttyS0 でログインプロンプトが出る。
逆に言えば、起動が止まったように見える場合でも
「getty が出ない」なら、ユーザーランドが最後まで到達していない可能性が高い。

### 2.3 `::ctrlaltdel:` と `::shutdown:`

これは“終了の台本”である。

- Ctrl+Alt+Del で reboot
- shutdown 時に umount

UmuOS 0.1.1 は日常利用の OS ではないが、
再現実験としては「止め方が曖昧だと混線する」ので、
最低限の終了動作が定義されている。

---

## 3. `/etc/init.d/rcS`：最初の初期化スクリプト

UmuOS 0.1.1 の rcS は「大規模な初期化」ではなく、
起動の観測と安定性のための最小初期化である。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 5.7

ここでは、rcS の内容を“なぜ必要か”で説明する。

### 3.1 rcS が最初に mount しているもの

rcS は次を mount する（ただし失敗しても止めない）。

- proc（/proc）
- sysfs（/sys）
- devtmpfs（/dev）
- devpts（/dev/pts）

初心者向けに言い換えるなら、
「ユーザーランドとして、最低限の観測と操作ができる地面を作る」ためである。

initramfs `/init` でも同様の mount をしていたが、
root が切り替わると mount 名前空間上の見え方も変わり得る。
だから rcS 側でも “改めて” mount を試みる。

### 3.2 なぜ `|| true` で止めないのか

rcS の方針は、詳細設計にこう書かれている。

- 失敗しても止めない（観測を優先）

起動シーケンスの本質は「成立点まで到達したか」を観測することなので、
rcS の一部が失敗しても getty を出してログイン可能にし、
中で状況を調べられる方が研究に向く。

### 3.3 永続ログ（/logs/boot.log）を rcS で追記する意味

UmuOS 0.1.1 は「永続性があること」を成立条件に含めている。
その確認方法として、rcS が `/logs/boot.log` に追記する例が示されている。

ここで重要なのは “initramfs では書かない” という一貫性である。

- initramfs：永続へ書かない（混線防止）
- ext4 側（rcS）：必要なら永続へ書く（永続性の観測）

この責務分離があるから、
起動失敗が起きても「永続側が汚れたせいで次の失敗が混ざる」事故が起きにくい。

### 3.4 `/dev/console` へメッセージを書く意味

rcS の最後に、`/dev/console` へ簡単なメッセージを書く。
これは “どこまで来たか” を最短で観測するための合図である。

シリアルログが流れる中でも、このメッセージが見えれば
少なくとも「switch_root の後、rcS が動いた」ことが確定する。

---

## 4. ここが壊れるとどう見えるか（典型症状の読み方）

この章は重要なので、失敗時の見え方も教科書として固定する。

### 4.1 initramfs の warn が出ている場合

initramfs `/init` は、永続側のファイルが無いと warn を出す。

- `/newroot/sbin/init not found`
- `/newroot/etc/inittab not found`
- `/newroot/etc/init.d/rcS not found`

この warn が出た状態で `switch_root` しても、その後のユーザーランドは成立しない。
だから詳細設計は「ext4 側の必須ファイル存在確認」を観測点として置いている。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 5.9

### 4.2 getty が出ない場合

getty が出ないときは、次の段階で止まっている可能性が高い。

- `/sbin/init` が起動できていない
- init は起動したが `inittab` を読めず sysinit が走っていない
- rcS が実行できない（`/bin/sh` が壊れている等）

特にありがちなのは「BusyBox applet の symlink が壊れて `/bin/sh` が実行不能」ケースである。
このとき rcS の `#!/bin/sh` が動かず、初期化が進まない。

この事故を避けるために、applet 生成を必ず chroot で行う。

---

## 5. 章のまとめ：ユーザーランドは“台本”で動く

UmuOS 0.1.1 の `switch_root` 後は、次の構造で動く。

- PID 1 が `/sbin/init`（BusyBox init）
- init が `/etc/inittab` を読み
- `sysinit` として `rcS` を実行し
- `respawn` として ttyS0 の getty を維持する

つまりユーザーランドは「台本（inittab）」で動き、
その最初の 1 章が rcS である。

次章では、永続 root（disk.img）そのものの作り方（UUID、内容、観測点）を
“再現”の観点で教科書として解説する。
