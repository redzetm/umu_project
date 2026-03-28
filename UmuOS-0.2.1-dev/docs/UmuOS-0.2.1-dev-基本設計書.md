# UmuOS-0.2.1-dev 基本設計書

目的：UmuOS-0.2.1-dev を、Ubuntu で成果物を生成し、RockyLinux 9.7 上の QEMU で起動し、QEMU ゲスト内部で `switch_root` 後のユーザーランドを観測・運用する構成として定義する。

この文書は、[UmuOS-0.2.1-dev-詳細設計書](UmuOS-0.2.1-dev-詳細設計書.md) を唯一の正としたうえで、その意味づけと責務境界をまとめる上位文書である。具体的な手順、固定値、コマンド、成果物名、確認方法が本書と衝突した場合は、詳細設計書を優先する。

---

## 0. この版で確定するもの

- 0.2.1-dev は `0.1.7-base-stable` を機械的に全部継承する版ではない。
- 0.2.1-dev は `0.1.7-base-stable` の既知動作を土台にしつつ、必要な範囲だけを再構成して固定する版である。
- 0.2.1-dev で確定対象に含めるのは、次の 5 系統とする。
	- RockyLinux 9.7 上での QEMU 起動
	- telnet 接続
	- FTP ログインと upload/download
	- アクセスログ
	- 永続 rootfs と起動観測
- `initramfs/src/init.c` は `0.1.7-base-stable` の実績版を土台として採用する。
- `su`、`ll`、その他の補助コマンド群や、日本語入力・日本語ファイル名の完全対応は、0.2.1-dev の確定対象には含めない。

---

## 1. 用語

- Ubuntu：成果物を作るビルド環境。
- RockyLinux 9.7：QEMU を実行するホスト環境。UmuOS そのものではない。
- QEMU ゲスト：RockyLinux 9.7 上で起動する仮想マシン。UmuOS はこの中で動く。
- initramfs：ゲストカーネル起動直後に展開される最小 rootfs。`/init` を含む。
- pre-switch_root：initramfs の `/init` が動作している段階。
- post-switch_root：永続 rootfs へ制御を渡した後、`/sbin/init` 以降が動作している段階。
- rootfs：`switch_root` 後に本物の `/` になる ext4 ファイルシステム。本設計では `disk.img` の中身を指す。
- `switch_root`：initramfs から永続 rootfs へ制御を移し、新しい `/sbin/init` を起動する操作。
- QEMU user-net：QEMU 内蔵の簡易 NAT ネットワーク。
- hostfwd：QEMU user-net でホストの TCP ポートをゲストへ転送する仕組み。
- virtio-net：QEMU ゲストへ提示する仮想 NIC。

---

## 2. Done の定義

- Ubuntu で以下の 3 成果物を再現可能に生成できる。
	- `UmuOS-0.2.1-dev-boot.iso`
	- `disk.img`
	- `UmuOS-0.2.1-dev_start.sh`
- RockyLinux 9.7 上で QEMU が起動する。
- QEMU ゲストで `switch_root` が成功し、`/sbin/init` → `inittab` → `rcS` が進行する。
- ゲストコンソールまたは telnet 経由で `login:` が確認できる。
- FTP で `tama` ユーザーのログインが成功し、upload/download が成立する。
- `/var/log/access.log` と `/var/log/messages` の両方で必要なログが観測できる。
- `/logs/boot.log` に起動観測情報が追記される。

---

## 3. 非ゴール

- RockyLinux 9.7 自体を UmuOS に置き換えること。
- 実機互換の起動方式や UEFI への展開を、この版の必須要件にすること。
- Rocky 相当の防御機構を UmuOS 側へ持ち込むこと。
- `0.1.7-base-stable` の補助機能一式を、そのまま全部取り込むこと。
- 日本語入力、日本語ファイル名、日本語表示を 0.2.1-dev の完成条件に含めること。

---

## 4. 前提と制約

### 4.1 起動方式

- 0.2.1-dev は QEMU 上の BIOS 起動を前提とする。
- ブート媒体は ISO とし、kernel と initramfs は ISO から供給する。
- 永続 rootfs は ext4 の `disk.img` とし、QEMU には virtio disk として渡す。

### 4.2 ホストとゲストの分離

- RockyLinux 9.7 は QEMU ホスト、firewalld 管理点、復旧拠点として残す。
- UmuOS は QEMU ゲスト内部のみで成立させる。
- `switch_root` は Rocky 側ではなく、ゲスト内部の initramfs において実行される。

### 4.3 ネットワーク方式

- 0.2.1-dev は QEMU user-net + hostfwd + virtio-net を採用する。
- 外部から RockyLinux 9.7 の `23/tcp` に来た接続は、ゲストの telnet へ転送する。
- 外部から RockyLinux 9.7 の `21/tcp` に来た接続は、ゲストの FTP 制御接続へ転送する。
- FTP データ接続は passive mode 前提とし、`21000-21031/tcp` を利用する。
- QEMU の `hostfwd` と RockyLinux 9.7 の `firewalld` は、これらのポート範囲と一致していなければならない。

### 4.4 rootfs の扱い

- `disk.img` の中身は ext4 とし、UUID を固定する。
- initramfs の `/init` は、その UUID と候補ブロックデバイス探索により rootfs を見つける。
- `disk.img` を loop ファイルとして扱う設計ではなく、ゲスト内部では virtio block device として扱う。

---

## 5. 全体アーキテクチャ

### 5.1 3 層構成

本設計は、以下の 3 層で成立する。

1. Ubuntu のビルド層
2. RockyLinux 9.7 の QEMU ホスト層
3. QEMU ゲスト内部の UmuOS 実行層

### 5.2 各層の流れ

- Ubuntu
	- kernel をビルドする。
	- BusyBox を静的リンクでビルドする。
	- initramfs を構築する。
	- grub-mkrescue で ISO を作る。
	- ext4 の `disk.img` を作る。
	- Rocky 側で使う起動スクリプトを生成する。
- RockyLinux 9.7
	- QEMU を実行する。
	- user-net と hostfwd で公開入口を作る。
	- firewalld と SSH を維持し、復旧経路を保持する。
- QEMU ゲスト
	- ISO から kernel と initramfs を起動する。
	- initramfs の `/init` が rootfs を見つける。
	- `switch_root` 後に `/sbin/init` → `inittab` → `rcS` を実行する。
	- `telnetd`、`tcpsvd`、`ftpd`、syslog 系を起動する。

---

## 6. 責務境界

### 6.1 Ubuntu 側の責務

- 成果物を再現可能に生成する。
- kernel、BusyBox、initramfs、ISO、`disk.img` の整合を保証する。
- rootfs に必要な設定ファイル、認証情報、起動スクリプトを埋め込む。
- Rocky 側へ渡す成果物を 3 点に固定する。

### 6.2 RockyLinux 9.7 側の責務

- QEMU 実行環境を提供する。
- `qemu-kvm`、`/dev/kvm`、`script` などの実行条件を満たす。
- `hostfwd` と `firewalld` により公開入口を定義する。
- 管理用 SSH と復旧経路を保持する。

### 6.3 initramfs 側の責務

- `/proc`、`/sys`、`/dev`、`/dev/pts` を最低限成立させる。
- kernel cmdline から rootfs 情報を読み取る。
- rootfs を探索し、マウントし、`switch_root` を実行する。
- post-switch_root 側へ制御を渡すまでに責務を限定する。

### 6.4 UmuOS rootfs 側の責務

- `/sbin/init` と `/etc/inittab` で起動を継続する。
- `rcS` でログ基盤、ネットワーク、時刻同期、サービス起動を行う。
- `/etc/umu/network.conf` に基づいて static IP を設定する。
- telnet、FTP、アクセスログ、起動観測を成立させる。

---

## 7. インターフェース設計

### 7.1 Rocky 側へ渡す成果物

Rocky 側へ持ち込む成果物は、以下の 3 点だけとする。

- `UmuOS-0.2.1-dev-boot.iso`
- `disk.img`
- `UmuOS-0.2.1-dev_start.sh`

この 3 点に固定することで、Rocky 側の責務を「起動・転送・検証・実行」に限定する。

### 7.2 kernel cmdline

この版で重要なのは以下である。

- `console=ttyS0,115200n8`
- `root=UUID=<ROOTFS_UUID>`
- `rootfstype=ext4`
- `rw`
- `net.ifnames=0 biosdevname=0`
- `loglevel=3`
- `panic=-1`

### 7.3 rootfs 内の主要ファイル

最低限必要なファイル群は以下とする。

- `/etc/inittab`
- `/etc/init.d/rcS`
- `/etc/umu/network.conf`
- `/etc/profile`
- `/etc/syslog.conf`
- `/etc/passwd`
- `/etc/group`
- `/etc/shadow`
- `/etc/os-release`
- `/umu_bin/ntp_sync`
- `/umu_bin/ftpd_start`
- `/umu_bin/ftpd_stop`

### 7.4 ログ I/F

- 認証系および telnet 接続記録は `/var/log/access.log` に残す。
- FTP を含む一般動作ログは `/var/log/messages` で観測する。
- 起動観測ログは `/logs/boot.log` に残す。

---

## 8. 成功判定と観測点

### 8.1 起動前

- Rocky 側で QEMU 実体が確認できる。
- `/dev/kvm` の有無が把握できる。
- `script` コマンドが利用できる。
- 転送済み成果物のハッシュ検証が通る。

### 8.2 起動中

- QEMU コンソールが出る。
- rootfs 探索が成功する。
- `switch_root` 後に `login:` が出る。

### 8.3 起動後

- `/logs/boot.log` に `boot_id`、時刻、uptime、cmdline、mount 情報が残る。
- `telnetd`、`tcpsvd`、`ftpd` が動作している。
- telnet ログインが成功する。
- FTP ログインと upload/download が成功する。
- `/var/log/access.log` と `/var/log/messages` で必要な観測ができる。

---

## 9. セキュリティと運用上の割り切り

- telnet と FTP は平文であり、0.2.1-dev では研究・観測用として割り切って使う。
- 公開面は hostfwd で明示したポートに限定する。
- 可能なら Rocky 側または上流で送信元制限をかける。
- 復旧拠点は常に Rocky 側に残し、ゲスト障害時でもホストから介入できるようにする。

---

## 10. 未確定事項の扱い

- 日本語入力、日本語ファイル名、日本語表示の完全対応は、0.2.1-dev の成功条件には含めない。
- これらは BusyBox の入力系やファイル名処理の見直しを伴う可能性があるため、次版課題として切り出す。
- 0.2.1-dev では、起動、telnet、FTP、ログ、永続 rootfs、起動観測の成立をもって完成とする。

---

## 11. 文書体系

- 基本方針書：0.2.x 系全体の研究対象、責務分離、採用原則を定める。
- 基本設計書：本書。0.2.1-dev の構成、責務境界、観測対象を定める。
- 詳細設計書：実際のコマンド、固定値、生成手順、検証手順、トラブルシュートを定める。

本版では、詳細設計書を実装時の正とし、基本設計書はその意味づけと判断基準を与える文書として保守する。
