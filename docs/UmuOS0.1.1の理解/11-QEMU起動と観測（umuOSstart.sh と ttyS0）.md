# 11-QEMU起動と観測（umuOSstart.sh と ttyS0）

第10章で、ISO（起動メディア）を作った。
第9章で、disk.img（永続 root）を作った。

この章では、それらを QEMU に接続し、
UmuOS 0.1.1 の設計思想である **「観測できる起動」** を実際の起動オプションとして理解する。

観測の中心は ttyS0（シリアル）である。
UmuOS 0.1.1 は、GRUB メニューからカーネルログ、initramfs `/init` のログ、BusyBox init のログインプロンプトまで、
一貫してシリアルに流すことを重視している。

---

## 0. 起動に必要な 3 つの入力

QEMU で UmuOS 0.1.1 を起動するには、最低限次の 3 つが必要になる。

- ISO：`UmuOS-0.1.1-boot.iso`
- 永続ディスク：`disk/disk.img`
- UEFI ファーム：OVMF（CODE と VARS）

ISO と disk.img はプロジェクト内にある。
OVMF はホストのパッケージ（Ubuntu 24.04 なら `ovmf`）が提供する。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 9 章

---

## 1. OVMF の CODE と VARS は役割が違う

OVMF は、UEFI の “ファームウェア” を QEMU で実現するための材料である。
ここで 2 種類のファイルが出てくる。

- `OVMF_CODE_4M.fd`：ファーム本体（基本的に read-only）
- `OVMF_VARS_4M.fd`：NVRAM 相当の保存領域（起動中に更新される）

詳細設計および起動スクリプト `umuOSstart.sh` は、VARS を `run/` へコピーして利用する。
これは「テンプレートを汚さない」「実行ごとの状態（UEFI の書き込み）をプロジェクト内に閉じる」ための運用設計である。

---

## 2. `umuOSstart.sh` は“混線防止”のためのラッパ

UmuOS 0.1.1 には `umuOSstart.sh` が同梱されている。
これは単に起動を楽にするだけではなく、
**起動に必要なものが無い状態で走り出して混乱する**のを防ぐための、受け入れチェックとして機能する。

- ISO が無ければ、理由を出して止める
- disk.img が無ければ、理由を出して止める
- OVMF が無ければ、インストールを促して止める

また、スクリプトは `PROJECT_DIR` を自前で計算し、どこから実行しても同じ結果になるようにしている。
この性質は「作業ディレクトリが違って相対パスが壊れる」事故を減らす。

- 参照：`UmuOS-0.1.1/umuOSstart.sh`

---

## 3. ttyS0 を“観測の中心”にするためのオプション

UmuOS 0.1.1 の観測は、主に次の 2 つで成立する。

1) GRUB 設定（`grub.cfg`）で、GRUB 自体の入出力を serial にも出す
2) カーネル引数で `console=ttyS0,115200n8` を入れ、カーネルログをシリアルに流す

`grub.cfg` には次が含まれている（第4章で解説済み）。

```properties
serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input serial console
terminal_output serial console
```

そしてカーネル引数は次のようになっている。

```properties
console=tty0 console=ttyS0,115200n8
```

この組み合わせがあると、QEMU を `-nographic` で起動したときでも、
「何が起きているか」がシリアル（ホストの端末）に出続ける。

---

## 4. QEMU の接続：ISO と disk.img の役割が分かれる

詳細設計には、QEMU 起動コマンド例が載っている。

- `-cdrom UmuOS-0.1.1-boot.iso`：起動メディア（ISO）
- `-drive file=disk/disk.img,format=raw,if=virtio`：永続 root（virtio-blk）

この分離が、UmuOS 0.1.1 の「永続性の確認」を可能にする。

- ISO を作り直しても、disk.img が変わらなければ永続状態は残る
- disk.img を作り直したら、UUID が変わるので GRUB 側も更新が必要（第9章）

起動スクリプト `umuOSstart.sh` は、これを明示的に行う。

---

## 5. “ログが混ざらない”起動のための注意

QEMU のシリアル出力は、オプションによっては QEMU monitor の文字が混ざる。
詳細設計では「混線防止」の観点で `-serial stdio -monitor none` を推奨している。

起動スクリプト `umuOSstart.sh` は、まさにこの方針で起動している。

- `-display none -nographic`：画面を持たず端末へ寄せる
- `-serial stdio`：シリアルを標準入出力へ
- `-monitor none`：monitor を無効化して混線を避ける

“観測の精度”が目的なら、この設計は合理的になる。

---

## 6. 観測点のキー行（実測例と抽出コマンド）

この章のポイントは「起動できたか」ではなく、**どこまで到達したかをログで断定できる**こと。
そこで、起動が成立しているときに “必ず見るべきキー行” を、実測例として最小抜粋する。

### 6.1 キー行（最小抜粋）

```text
[init] UmuOS initramfs init start
[init] /proc/cmdline: ... root=UUID=... console=ttyS0,115200n8 ...
[init] want root UUID: ...
[init] scan: /dev/vda
[init] matched: dev=/dev/vda uuid=...
[init] mount root ok (rw): /dev/vda
[init] exec: /bin/switch_root /newroot /sbin/init
[rcS] rcS done
(none) login:
```

ここまで揃えば、「UEFI→GRUB→kernel→initramfs→ext4 root→getty/login」まで到達している。

### 6.2 ホスト側での抽出コマンド例

QEMU のフルログは長い。
混線させないために、ホスト側では次のように **キー行だけ** 抽出して読むとよい。

```bash
LOG=/tmp/umuos_serial.log
egrep -n "\\[init\\] (UmuOS initramfs init start|/proc/cmdline:|want root UUID:|scan:|matched:|mount root ok|exec: /bin/switch_root)|\\(none\\) login:|\\[rcS\\] rcS done" "$LOG"
```

（補足）第12章では、この方法で「永続性まで一周した」合格ログ例も示す。

---

## まとめ

この章では、UmuOS 0.1.1 の起動が QEMU 上で成立するために必要な入力（ISO/disk.img/OVMF）を整理し、
観測の中心を ttyS0 に置く設計が、GRUB 設定・カーネル引数・QEMU オプションまで一貫していることを確認した。

次章では、ここまで作ったものを「最初からやり直せる形（再現手順）」としてまとめ、永続性の確認まで通しで実行できるようにする。
