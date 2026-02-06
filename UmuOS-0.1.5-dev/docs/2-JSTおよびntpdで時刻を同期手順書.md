# UmuOS-0.1.5-dev 機能追加（手動により実装）手順書：JST タイムゾーン固定および ntpd で時刻を同期

本書は、UmuOS（0.1.4-base-stable 相当の最小 BusyBox ユーザーランド）で、

1. 表示タイムゾーンを JST（日本時間, UTC+9）にする
2. BusyBox の `ntpd` を使って時刻を同期する

ための手順をまとめる。

結論：**先にタイムゾーン（JST）を設定し、その後に NTP 同期**する。

## 前提

- ゲストのネットワークが成立していること（`ip addr` / `ip route`）
- DNS が設定済みであること（`/etc/resolv.conf`）
	- 例：`nameserver 8.8.8.8` / `nameserver 8.8.4.4`
- 以降のコマンドはゲスト上で root で実行する

NTP は DNS と外界疎通が前提なので、必要なら先に DNS 手順書（`1-DNSをGloogleを使い外界で通信手順書.md`）を完了させる。

## 0. 事前チェック（ネットワークと現在時刻）

```sh
ip addr
ip route

ping -c 1 8.8.8.8
```

現在の時刻（まずは現状把握）：

```sh
date
date -u
```

## 1. タイムゾーンを JST にする（固定）

UmuOS の最小 rootfs では zoneinfo（`/usr/share/zoneinfo`）が無い可能性があるため、
**BusyBox でも確実に動く `/etc/TZ` 方式**を基本とする。

### 1.1 `/etc/TZ` を作成する（推奨）

`JST-9` は POSIX TZ 文字列で、JST（UTC+9）を表す。

```sh
echo 'JST-9' > /etc/TZ
cat /etc/TZ

date
```

補足：セッションだけ変えたい場合は、次でも同じ効果になる（永続化はしない）。

```sh
export TZ=JST-9
date
```

### 1.2 （任意）zoneinfo がある場合の `Japan/Tokyo`

もし `ls /usr/share/zoneinfo/Japan` 等で zoneinfo が存在する場合のみ、
次のように `Japan/Tokyo` を使える。

```sh
ls -l /usr/share/zoneinfo/Japan 2>/dev/null || true
ls -l /usr/share/zoneinfo/Asia/Tokyo 2>/dev/null || true
```

存在する場合の例：

```sh
ln -sf /usr/share/zoneinfo/Asia/Tokyo /etc/localtime
date
```

※最小構成では存在しないことが多いので、無理にこの方式に寄せない。

## 2. BusyBox `ntpd` で時刻を同期する

まず `ntpd` が使えることを確認：

```sh
ntpd --help 2>/dev/null || busybox ntpd --help
```

### 2.1 1回だけ同期して終了（推奨）

環境によりオプションが異なるため、以下のどちらかが通ればOK。

```sh
# パターンA（対応していれば最短）：同期したら終了
ntpd -n -q -p time.google.com
```

もし `-q` が使えない/エラーになる場合：

```sh
# パターンB：しばらく動かしてから止める（Ctrl+C）
ntpd -n -p time.google.com
```

同期後に確認：

```sh
date
date -u
```

### 2.2 常駐させる（任意）

手動で常駐させたい場合（再起動まで動かす）：

```sh
ntpd -p time.google.com &
echo $! > /run/ntpd.pid
```

停止：

```sh
kill "$(cat /run/ntpd.pid)" 2>/dev/null || true
rm -f /run/ntpd.pid
```

## 3. 永続化（次回起動後も JST にしたい）

- `/etc/TZ` を書いた場合：永続 rootfs（ext4）なら次回起動後も残る。
- `export TZ=...` だけの場合：再起動で消える。

※もし起動のたびに確実に設定したいなら、`/etc/profile` か `/etc/init.d/rcS` に `export TZ=JST-9` を追加する運用も可能（ただし本書は手動手順に留める）。

## トラブルシュート

- `ntpd: bad address` / 名前解決できない
	- `/etc/resolv.conf` を確認し、`nslookup google.com`（無ければ `ping -c 1 google.com`）でDNSを切り分ける
- `ntpd` が応答しない/同期しない
	- `ping -c 1 8.8.8.8` と `ip route` を確認（default route が無いと外界に出られない）
	- ホスト側のネットワークモードが `--net tap` であることを確認（`--net none` は不可）
- JST にならない
	- `/etc/TZ` の内容が `JST-9` になっているか確認し、`date` を取り直す