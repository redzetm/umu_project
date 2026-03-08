---
title: 0011 HTTPSで外部RockyLinux9.7にログイン（UmuOSへ到達する）計画
date: 2026-03-09
status: plan
---

# 目的（ゴール）

会社環境など **SSH 接続が制限**されている状況でも、
Windows から **HTTPS(443) だけ**で外部（インターネット上）の RockyLinux 9.7 に到達し、
その Rocky 上で常時稼働している UmuOS（QEMU）へ **さらに telnet 相当で接続して操作**できる状態を作る。

ここでの重要方針：

- 外部に公開する入口は **HTTPS(443)のみ** とする（23/5555等の生ポートは外部公開しない）。
- UmuOS の既存設計（ttyS1 の TCP シリアルを `telnet 127.0.0.1 5555` で掴む）を最大限に流用する。
- TELNET は「理解・観測のための通過点」。インターネット越しに平文 TELNET を流さず、
  **Rocky 内部で閉じる**。

# 非目的（スコープ外）

- UmuOS を“セキュアに外部公開するOS”として運用すること。
- インターネット越しに telnet(23) や QEMU ttyS1(5555) を直接公開してアクセスすること。
- 会社プロキシのルール回避（規約違反になり得る手段）。

# 前提

## ネットワーク/環境前提

- 外部 RockyLinux 9.7 は自分で管理できる（root権限あり）。
- Rocky 側で HTTPS(443) 受信が可能（ファイアウォール/クラウドSG含む）。
- 会社PCはブラウザでHTTPSアクセスができる（WebSocket/WSSが通るかは要確認）。

## UmuOS の前提（既存）

UmuOS には少なくとも以下2系統の「端末入口」が存在する。

- **A: QEMU ttyS1 を TCP 公開して掴む（ホストローカル用途）**
  - QEMU: `-serial tcp:127.0.0.1:<PORT>,server,nowait,telnet`
  - クライアント: `telnet 127.0.0.1 <PORT>`
- **B: ゲスト `telnetd` で待受（LANログイン用途）**
  - ゲスト: `telnetd -p 23 -l /bin/login`

本計画では、外部公開をHTTPSのみに絞りやすい **A（ttyS1）** を主ルートとする。

# 用語（混同潰し）

- **HTTPS**: TCP/443 上の HTTP + TLS。ブラウザが通常許可されることが多い。
- **WSS(WebSocket over TLS)**: HTTPSの上で双方向・低遅延に文字をやり取りする仕組み。
  - Web端末（ブラウザ上のターミナル）は通常WSSを使う。
- **Web端末**: ブラウザで表示されるターミナルUI。
  - 実体は「ブラウザ ⇄(WSS)⇄ Rocky の中継プロセス ⇄(PTY)⇄ telnet/nc」
- **ttyS1（UmuOS側）**: QEMUの「2本目シリアル」。TCPに紐づけて外から掴める。
- **telnet（ここで言う意味）**: アプリが telnet クライアントとして TCP シリアルに接続すること。
  - インターネット越しにtelnetを流す、という意味ではない。

# ざっくり構成（推奨アーキテクチャ）

Windows(ブラウザ)
  ↓ HTTPS(443)
Rocky: Nginx(HTTPS終端 + 認証)
  ↓ (リバースプロキシ)
Rocky: Web端末(ttyd等)
  ↓ (PTY)
Rocky: `telnet 127.0.0.1 5555`
  ↓ (localhost)
Rocky: QEMU(稼働中)
  ↓ (仮想シリアル)
UmuOS: ttyS1 login

ポイント：

- `telnet 127.0.0.1 5555` は Rocky 内部で完結。
- 外部へ見せるのは Nginx の 443 だけ。

# 実現方式の候補と意思決定

## 方式1（第一候補）：ttyd で「telnetをWeb化」

狙い：

- Rocky 上で `ttyd ... telnet 127.0.0.1 5555` を起動し、
  ブラウザがその端末に入るだけで UmuOS に到達する。

利点：

- 構成が単純で、最短で動く。
- “Windows→HTTPS→UmuOS ttyS1” が直線になる。

懸念：

- 会社プロキシが WebSocket をブロックすると詰む。

## 方式2：wetty/shellinabox 等の別Web端末

狙い：

- ttydが通らない環境（WSS制限）で代替。

備考：

- 実際に会社ネットワークで通るかは検証が必要。

## 方式3（最終手段）：noVNC/画面転送

狙い：

- 文字端末の双方向が難しい場合に、画面として操作する。

懸念：

- UmuOSの観測経路（ttyS1）とズレる可能性。
- 帯域と操作性が悪化しやすい。

# セキュリティ方針（最低ライン）

- インターネットへ公開するのは **HTTPS(443)のみ**。
- Nginx 側で **認証必須**（最低でも Basic 認証、可能なら追加要素）。
- Rocky のファイアウォール/クラウドSGで 443 以外は閉じる。
- UmuOS ゲストの `telnetd`（23番）を外部公開しない。
  - もしゲストtelnetdを使う場合でも、到達範囲は Rocky 内部 or VPN 内に限定する。

# 受入基準（合格条件）

- Windows のブラウザで `https://<domain>/` にアクセスできる。
- 認証後、ブラウザ上にターミナルが表示される。
- そのターミナル操作だけで UmuOS の `login:` まで到達できる。
- UmuOS にログインし、最低限のコマンド（例：`uname -a`, `cat /logs/boot.log` 等）が実行できる。
- Rocky 側で `ss -lntp` を見ても、外部公開は 443 のみ（23/5555 が外に出ていない）。

# 実装計画（手順を固定）

## Phase 0: 会社側制限の確認（先にやる）

- ブラウザで HTTPS は通るか。
- WebSocket(WSS) が通るか。
  - これがダメなら方式1は不成立なので方式2/3へ。

## Phase 1: Rocky側でUmuOS(QEMU)常時稼働を固定

- UmuOS の起動スクリプトで ttyS1 を localhost 公開する。
  - 例：`127.0.0.1:5555`
- QEMU プロセスが落ちても復旧できるように、
  systemd service 化（または tmux 常駐など）を検討。

確認:

- Rocky 上で `telnet 127.0.0.1 5555` が繋がり、Enter数回で `login:` が出る。

## Phase 2: Rocky側でWeb端末（ttyd）を立てる

- `ttyd` をインストール。
- `ttyd` が次のコマンドを起動するようにする：
  - `telnet 127.0.0.1 5555`

確認:

- Rocky ローカル（ブラウザ or curl等）で Web端末に到達できる。

## Phase 3: NginxでHTTPS終端 + 認証 + リバースプロキシ

- `https://<domain>/` → `http://127.0.0.1:<ttyd_port>/` に転送。
- 認証を付ける（Basic認証など）。
- 証明書を設定（Let’s Encrypt等）。

確認:

- 外部から `https://<domain>/` でアクセスすると認証が出る。
- 認証後にWeb端末が開き、UmuOSに入れる。

## Phase 4: 外形監査（ポート公開の確認）

- Rocky:
  - `ss -lntp` で LISTEN ポートを確認。
  - `firewall-cmd --list-all` 等で許可ポート確認。
- クラウド/ルータ:
  - セキュリティグループで 443 のみ。

# トラブルシュート（順序固定）

## 1) ブラウザでページ自体が開かない

- DNS/証明書/443到達性を疑う。
- 会社側のフィルタでドメインがブロックされていないか。

## 2) ページは開くが端末が真っ白/繋がらない

- WebSocket(WSS) がプロキシに落とされている可能性。
- Nginx 側の WebSocket 用ヘッダ転送設定不足の可能性。
- 代替として方式2（別Web端末）を検討。

## 3) 端末は出るが UmuOS の `login:` が出ない

- ttyS1の仕様として「接続より前の出力は流れない」ことがある。
  - Enter を数回押して `login:` を出す。
- QEMU 側で ttyS1 が `127.0.0.1:5555` になっているか確認。

## 4) セキュリティ的に怖い

- 23/5555 を外部公開していないことを再確認。
- 認証を強化（IP制限、追加認証、ログ監視）。

# 観測ポイント（UmuOS思想に合わせた“見える化”）

- Windows側:
  - ブラウザの開発者ツールで WebSocket 接続が張れているかを見る。
- Rocky側:
  - Nginx access/error log
  - ttyd のログ
  - `ss -ntp` で接続状態（443→ローカルport→telnet）
- UmuOS側:
  - `/logs/boot.log` の確認
  - ログインプロンプトの出方（ttyS1/ttyS0差分）

# 次のアクション（最初にやること）

- 会社環境で WSS が通るか簡易判定（最重要）。
- Rocky 上で「UmuOS起動→`telnet 127.0.0.1 5555`で入れる」ことを固定。
- その後に ttyd + Nginx + HTTPS を積む。
