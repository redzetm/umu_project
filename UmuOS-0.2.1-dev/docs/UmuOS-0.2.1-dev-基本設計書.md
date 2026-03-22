# UmuOS-0.2.1-dev 基本設計書

目的：RockyLinux9.7上で起動し、ブート途中の `switch_root` によってユーザーランドをUmuOSへ切り替える方式の、全体像（責務境界・構成要素・インターフェース・前提）を固める。

---

## 0. 用語

- **initramfs**：カーネルが最初に展開して使う最小ルートFS。`/init` が最初に実行される。
- **`/init`**：initramfs内で最初に走るユーザ空間プログラム。rootfs探索や `switch_root` を行う。
- **rootfs**：`switch_root` 後に「本物の `/`」になるファイルシステム。
- **`switch_root`**：initramfsから本rootfsへ切り替えて `/sbin/init` を起動する操作。
- **pre-switch_root**：カーネル～initramfsまで（Rocky土台）。
- **post-switch_root**：UmuOS rootfs上の `/sbin/init` 以降。

---

## 1. ゴール（Doneの定義）

- initramfsログ上で、rootfsのマウント成功と `switch_root` 実行が確認できる
- `switch_root` 後に UmuOS の `rcS` が実行され、ネットワークが成立する
- 外部から `telnet <PUBLIC_IP> 23` で `login:` が出る（到達性は環境のFW/ACLに依存）

---

## 2. 非ゴール

- UmuOS独自カーネルの常用
- initramfsの大改造（loop対応など）
- 防御層（SELinux/firewalld等）をUmuOSに揃える

---

## 3. 前提・制約

- 本環境は **BIOS起動** と確認済み。ただし、起動方式（BIOS/UEFI）により grub.cfg の実体パスが変わるため、作業時は必ず `readlink -f` で実体を確定する。
- initramfsのrootfs探索は「ブロックデバイス」を前提にする（推奨は rootfs のブロックデバイス化）。
- Telnetは平文。公開するならリスクを理解し、可能なら送信元制限を使う。

---

## 4. アーキテクチャ（構成要素）

### 4.1 ブートローダ（GRUB）

- Rocky通常起動エントリを温存
- UmuOS起動エントリは追加（`/etc/grub.d/40_custom` 等）
- 反映は `grub2-mkconfig -o "$(readlink -f /etc/grub2.cfg)"`（BIOSの場合の典型）

### 4.2 kernel

- kernelはRockyLinux9.7のものを使う（0.2.1-devの方針）

### 4.3 initramfs

- UmuOS用の `/init` を含む initramfs を `/boot` に配置
- `root=UUID=<UMUOS_ROOT_UUID>` をヒントに rootfs を見つけて `switch_root` する

### 4.4 UmuOS rootfs（ext4）

- ブロックデバイス（例：追加ディスク/パーティション/LVM LV）上に展開
- 最低限必要：
	- `/sbin/init`
	- `/etc/inittab`
	- `/etc/init.d/rcS`
	- `/etc/umu/network.conf`

---

## 5. データ/設定インターフェース

### 5.1 kernel cmdline（GRUBから渡す）

必須（最小）：
- `root=UUID=<UMUOS_ROOT_UUID>`
- `rw`

推奨（観測性・安定性）：
- `console=tty0 console=ttyS0,115200n8`（コンソールは環境依存）
- `loglevel=7 panic=-1`（デバッグ中）
- `net.ifnames=0 biosdevname=0`（UmuOS側が `eth0` 前提の場合）

### 5.2 UmuOSネットワーク設定（`/etc/umu/network.conf`）

- `MODE=static` の場合は `IP/GW/DNS` を明示
- 環境によってはDHCPが必須になるため、その場合は `rcS` 側に `udhcpc` を組み込む必要がある

---

## 6. 失敗時の復旧設計

- デフォルト起動は RockyLinux9.7 のままにする
- UmuOSの起動テストは GRUBのメニューで手動で選択して行う
- 管理コンソール/レスキュー/ISO起動など、最低1つの復旧経路を確保する

---

## 7. セキュリティ設計（最低限）

- 露出サービスは最小（原則Telnet/FTPのみ等）
- 可能なら上流で送信元制限
- 上流制限できない場合は、公開時間を短くする、監視する、都度更新する

---

## 8. 成果物（設計上のアウトプット）

- GRUBの追加エントリ（テンプレは詳細設計書に記載）
- UmuOS rootfs（UUID固定されたext4）
- UmuOS initramfs（/init含む）
- 設定テンプレ（network.conf等）
- トラブルシュート手順
