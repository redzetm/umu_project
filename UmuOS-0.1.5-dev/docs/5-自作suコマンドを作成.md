# UmuOS-0.1.5-dev 機能追加（手動により実装）手順書：自作 su コマンドを C 言語で作成する

本書は、BusyBox の `su` が setuid を伴う想定どおりの動作にならず解決できない場合に、
**自作の `su` コマンド**を用意して「一般ユーザー → root」へ切り替えできるようにする手順をまとめる。

要件（本書の前提）：

- 開発環境は Ubuntu（ホスト）
- UmuOS-0.1.4（ゲスト）は `192.168.0.202` で動作し、バイナリを転送して使う
- コマンド名は `su`
	- BusyBox の `su` と衝突させないため、`/umu_bin/su` に配置する
- root になる際は **パスワード必須**
	- `/etc/shadow` のハッシュで認証する

結論：Ubuntu 側で `crypt(3)` を使う小さな C プログラムを **静的リンク**でビルドし、
ゲストへ転送して `/umu_bin/su` として `root:root` + `chmod 4755`（setuid）で配置する。

注意：setuid root バイナリは強い権限を持つ。ソースは最小構成だが、脆弱性があると即 root になり得る。
この手順は「自分の OS 開発・検証」用途に限定する。

## 前提

- `/umu_bin` が存在し、`root:root 0755` になっていること
- ゲストへ telnet で入れること（`tama` でログインして `id` が確認できること）
- ゲストが外部からファイルを受け取れること（例：BusyBox `nc` がある）
- ゲスト側に `/etc/shadow` があり、root のパスワードハッシュ（例：`$6$...`）が設定済みであること

## 0. 事前の切り分け（setuid が無効になっていないか）

setuid が効かない原因が「ファイルシステムの `nosuid`」や「所有者不正」の可能性がある。

ゲストで確認：

```sh
mount
```

ポイント：

- `/`（永続 rootfs）が `nosuid` でマウントされていると、setuid は効かない。
- `/umu_bin/su` が `root:root` でないと setuid は成立しない。

## 1. 自作 `su`（/etc/shadow 認証版）を作る

Ubuntu 側（開発環境）で `umu_su.c` を作成する。

仕様（最小）：

- 実行ユーザーは `tama` など任意（一般ユーザー想定）
- `su` 実行時に root パスワードを入力
- `/etc/shadow` の root 行を読み、`crypt(3)` で検証
- OKなら `setuid(0)` して `/bin/sh` を起動

```c
#define _GNU_SOURCE

#include <crypt.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int read_shadow_hash_root(char *out, size_t out_len) {
	FILE *fp = fopen("/etc/shadow", "r");
	if (!fp) {
		perror("fopen(/etc/shadow)");
		return -1;
	}

	char line[2048];
	while (fgets(line, sizeof(line), fp)) {
		/* root:<hash>:... */
		if (strncmp(line, "root:", 5) != 0) {
			continue;
		}
		char *p = strchr(line, ':');
		if (!p) {
			break;
		}
		p++; /* hash begins */
		char *q = strchr(p, ':');
		if (!q) {
			break;
		}
		*q = '\0';

		if (strlen(p) == 0 || strcmp(p, "!") == 0 || strcmp(p, "*") == 0) {
			fprintf(stderr, "su: root password is locked/empty in /etc/shadow\n");
			fclose(fp);
			return -1;
		}

		if (strlen(p) + 1 > out_len) {
			fprintf(stderr, "su: shadow hash too long\n");
			fclose(fp);
			return -1;
		}

		strncpy(out, p, out_len);
		out[out_len - 1] = '\0';
		fclose(fp);
		return 0;
	}

	fclose(fp);
	fprintf(stderr, "su: root entry not found in /etc/shadow\n");
	return -1;
}

int main(void) {
	/* setuid root バイナリとして動く前提。ここで euid!=0 なら setuid が無効。 */
	if (geteuid() != 0) {
		fprintf(stderr, "su: euid!=0 (setuid bit/owner/nosuid を確認)\n");
		return 1;
	}

	char shadow_hash[512];
	if (read_shadow_hash_root(shadow_hash, sizeof(shadow_hash)) != 0) {
		return 1;
	}

	char *pw = getpass("Password: ");
	if (!pw) {
		fprintf(stderr, "su: failed to read password\n");
		return 1;
	}

	errno = 0;
	char *calc = crypt(pw, shadow_hash);
	if (!calc) {
		perror("crypt");
		return 1;
	}

	if (strcmp(calc, shadow_hash) != 0) {
		fprintf(stderr, "su: Authentication failure\n");
		return 1;
	}

	/* ruid/euid/suid と gid を root に揃える */
	if (setgid(0) != 0) {
		perror("setgid");
		return 1;
	}
	if (setuid(0) != 0) {
		perror("setuid");
		return 1;
	}

	setenv("HOME", "/root", 1);
	setenv("USER", "root", 1);
	setenv("LOGNAME", "root", 1);
	setenv("SHELL", "/bin/sh", 1);

	execl("/bin/sh", "sh", (char *)NULL);
	perror("execl");
	return 1;
}
```

## 2. Ubuntu 側で静的リンクしてビルドする

`crypt(3)` を使うため、`-lcrypt`（環境によっては `-lxcrypt`）が必要になる。

### 2.1 依存パッケージ（Ubuntu）

環境により異なるが、目安：

- `build-essential`
- `libcrypt-dev`（または `libxcrypt-dev`）

### 2.2 ビルド

まずは glibc + libcrypt で静的リンクを試す。

```sh
gcc -static -Os -s -o umu_su umu_su.c -lcrypt
file umu_su
```

`not a dynamic executable` 等が出ていれば静的リンクできている。

もし `-lcrypt` が見つからない等で失敗する場合：

- `-lcrypt` の代わりに `-lxcrypt` が必要な環境がある
- 静的リンク用のライブラリが入っていない可能性がある

最終的に `file umu_su` で `statically linked`/`not a dynamic executable` になっていることを確認する。

### 2.3 （参考）musl-gcc を使うのは「美しい」が、今回はハマりやすい

`musl-gcc` で静的リンクする方針は「ゲスト側の動的リンク解決に依存しない」という意味で見通しが良い。
一方、この `su` は `crypt(3)` と `getpass(3)` を使っているため、Ubuntu 環境では次の理由で
**glibc の `gcc -static` より手間が増える**ことが多い。

- `getpass(3)` が musl 環境で利用できない／挙動が異なる可能性がある
- `crypt(3)`（SHA-512 `$6$` など）を提供する `libcrypt`/`libxcrypt` を **musl 向け**に用意する必要がある
	- Ubuntu の標準パッケージ構成だと「glibc 向けの libcrypt はあるが、musl 向けが無い」になりがち

結論：本書の範囲では、まず `gcc -static ... -lcrypt`（必要なら `-lxcrypt`）でのビルドを推奨する。
どうしても musl で作りたい場合は、パスワード入力を `getpass()` 以外（例：`/dev/tty` を開いて `fgets()`）へ差し替え、
かつ musl 向け `libxcrypt` を準備してリンクする方針になる。

## 3. UmuOS（192.168.0.202）へ転送して `/umu_bin/su` に配置する

`/umu_bin/su` に置くのは「BusyBox の `su` と衝突させない」ため。
呼び出しは `/umu_bin/su` を明示するか、`PATH` を `/umu_bin` 優先にして `su` をこちらへ向ける。

### 3.1 ゲスト側（受信）

ゲストで root になり、待ち受け（まずは `/tmp` に受ける）：

```sh
# IPv4 を強制（nc が IPv6 で待つ環境対策）
nc -4 -l -p 12345 > /tmp/umu_su
```

※ `-4` が未対応の場合は `nc --help` を確認し、次のように待受IPを明示する（実装差あり）。

```sh
nc -l -p 12345 -s 0.0.0.0 > /tmp/umu_su
```

### 3.2 Ubuntu 側（送信）

Ubuntu からゲストへ送る：

```sh
nc -4 192.168.0.202 12345 < umu_su
```

転送後、ゲスト側で配置と権限を設定：

```sh
mv -f /tmp/umu_su /umu_bin/su
chown root:root /umu_bin/su
chmod 4755 /umu_bin/su
ls -l /umu_bin/su
```

期待：`-rwsr-xr-x` のように `s` が付く。

## 4. 動作確認（tama → root）

`tama` でログインして確認：

```sh
id
/umu_bin/su
id
```

期待：root パスワード入力後に `uid=0(root)` になる。

`PATH` を `/umu_bin` 優先にしている場合は `su` だけでも動く（`which su` で確認）。

## トラブルシュート

- 実行しても root にならない（`euid!=0` と出る）
	- `/umu_bin/su` の所有者が `root:root` か確認
	- `chmod 4755 /umu_bin/su` になっているか確認
	- `mount` を見て `/` が `nosuid` でないか確認
- `su: Authentication failure`
	- `/etc/shadow` に root の `$6$...` が入っているか確認
	- root のパスワードがロックされていないか（`!` や `*`）確認
	- キーボード入力が想定どおりか（telnet 経由で誤入力していないか）
- `Permission denied`
	- `/umu_bin` や `/umu_bin/su` のパーミッションを確認
	- ファイルが壊れていないか（転送失敗）を疑う
- `nc` のオプションが通らない
	- BusyBox の `nc` 実装差があるので `nc --help` で確認し、待受/接続のオプションを合わせる
- `crypt: ...` で失敗する
	- Ubuntu 側のビルドで `-lcrypt` が正しくリンクできているか確認
	- ゲスト側が動的リンクを解決できないため、静的リンクになっているか（`file umu_su`）確認