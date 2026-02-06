# UmuOS-0.1.5-dev 機能追加（手動により実装）手順書：DNS を Google DNS にして外界通信する

本書は、UmuOS（0.1.4-base-stable 相当の構成）で **DNS を設定してホスト名解決を成立**させ、外界（インターネット）に通信できることを確認する手順をまとめる。

結論：`/etc/resolv.conf` に Google DNS（`8.8.8.8` など）を設定する。

## 前提

- ゲストのネットワーク（IP と default route）が成立していること
	- 0.1.4-base-stable の実装では、永続 rootfs（ext4）側の `/etc/init.d/rcS` が `/etc/umu/network.conf` を読み、BusyBox `ip` で固定IPと default route を設定する。
	- 例（固定値）：`192.168.0.202/24`、GW `192.168.0.1`
- QEMU が `--net tap` で起動していること（`--net none` では外界に出られない）

以降のコマンドは、原則ゲスト上で root で実行する。

## 1. まず IP / route を確認する（DNS以前）

DNS を設定しても、IP と default route が無いと外界には出られない。

```sh
ip link
ip addr
ip route
```

期待する状態（例）：

- `eth0` が `UP`
- `eth0` に `192.168.0.202/24` が付与されている
- `ip route` に `default via 192.168.0.1 dev eth0` が存在する

疎通（L3）だけ先に確認：

```sh
ping -c 1 192.168.0.1
ping -c 1 8.8.8.8
```

### IP / route が入っていない場合

補足：0.1.4-base-stable では **disk.img 作成時点で `/etc/umu/network.conf` を投入済み**の運用が前提になっている。
そのため、すでにゲストへ telnet（`telnetd`）で入れている状況なら、通常はこの節の作業は不要（IP/route は既に成立しているはず）である。

この節は、次のような「例外ケース」の切り分け用。

- disk.img を作り直した / 別手順で用意した
- `/etc/umu/network.conf` を消した・壊した
- `ip addr` / `ip route` を見ると IP や default route が入っていない

0.1.4-base-stable の実装では `/etc/umu/network.conf` に固定値があり、`/etc/init.d/rcS` がそれを読んで BusyBox `ip` で設定する。

ゲスト側で確認：

```sh
cat /etc/umu/network.conf
```

例（0.1.4-base-stable の詳細設計と一致）：

```sh
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=8.8.8.8
```

※この `DNS=` は 0.1.4-base-stable の rcS 実装では参照されない（DNS は `/etc/resolv.conf` を別途手動で設定する）。

ホスト側から永続ディスクに投入する場合（※例外時のみ。0.1.4-base-stable の `disk/disk.img` を作り直した等）：

```sh
sudo mkdir -p /mnt/umuos
sudo mount -o loop /home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/disk/disk.img /mnt/umuos

sudo mkdir -p /mnt/umuos/etc/umu
sudo tee /mnt/umuos/etc/umu/network.conf >/dev/null <<'EOF'
IFNAME=eth0
MODE=static
IP=192.168.0.202/24
GW=192.168.0.1
DNS=8.8.8.8
EOF

sync
sudo umount /mnt/umuos
```

## 2. DNS（/etc/resolv.conf）を Google DNS にする

`/etc/resolv.conf` を作成/上書きする。

```sh
cat > /etc/resolv.conf <<'EOF'
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF

cat /etc/resolv.conf
```

## 3. DNS ありの疎通確認

ホスト名解決の確認：

```sh
nslookup example.com
nslookup google.com
```

HTTP で外界に出られることを確認（BusyBox `wget` 想定）：

```sh
wget -O - http://example.com | head
```

## 4. 永続化（どこに書けば次回も効くか）

UmuOS-0.1.4-base-stable の構成では、`switch_root` 後の rootfs は永続ディスク（ext4）なので、ゲスト上で `/etc/resolv.conf` を編集すれば基本的に次回起動後も残る。

ホスト側から永続ディスクに入れてしまう場合：

```sh
sudo mkdir -p /mnt/umuos
sudo mount -o loop /home/tama/umu/umu_project/UmuOS-0.1.4-base-stable/disk/disk.img /mnt/umuos

sudo tee /mnt/umuos/etc/resolv.conf >/dev/null <<'EOF'
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF

sync
sudo umount /mnt/umuos
```

## トラブルシュート（よくある切り分け）

- `ping -c 1 8.8.8.8` は通るが `nslookup google.com` が失敗する
	- `/etc/resolv.conf` の内容を疑う（`nameserver` 行があるか）
- `ping -c 1 192.168.0.1` が通らない
	- ゲストのIP設定、`ip route`、ホストの `br0/tap`、QEMU が `--net tap` で起動しているかを疑う
- `ip` コマンド自体が無い/動かない
	- BusyBox のビルド設定（`CONFIG_IP`）を疑う（0.1.4-base-stable では必須）