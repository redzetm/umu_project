# repro/0.1.1

このディレクトリは、UmuOS-0.1.1 を「何度でも再現できる固定点」にするための、再現（reproduction）用ツール一式を置く。

- 目的: 失敗コストを下げ、破壊的な検証を躊躇なく行えるようにする
- 非目的: 便利化そのもの／OS本体の仕様変更
- 方針: UmuOS-0.1.1 側の成果物は変更しない（ベース版を温存する）

## 生成物

このツールは、UmuOS-0.1.1 相当の成果物（例: ISO、disk.img、起動スクリプト）を再現する。

生成先は「親ディレクトリ」を指定し、その直下に `UmuOS-0.1.1/` を作る。

- デフォルト: `UMU_ROOT/work`（存在しない場合は `repro/0.1.1` 配下）
- 指定方法: 環境変数 `OUT_PARENT` または `--out-parent <dir>`

例（親ディレクトリが `~/umu_project/work` の場合）:

```
~/umu_project/work/
	UmuOS-0.1.1/
		disk/disk.img
		logs/
		run/
		...
```

運用方針として、作り直しのたびに生成先ディレクトリは削除し、クリーンに初期化する。

注意:

- `disk.img` は「mkfsでゼロから作る」のではなく、ベース版の `disk.img` をコピーしてから UUID 等を更新して作る（=ベースの初期状態を引き継ぐ）
- したがって、ベース版 `disk.img` にファイルが残っていれば、それは毎回“初期状態”として現れる

## 運用方法

結論: **UmuOS-0.1.1（ベース）は原則変更しない**。変更・検証は **`work/` 配下に複製した UmuOS-0.1.1** に対して行う。

理由:

- 固定点（ベース）を温存できるため、いつでも同じ出発点へ戻れる
- `work/` 側だけを壊してよいので、破壊的な検証の心理的コストが下がる
- 起動スクリプトも `work/<...>/UmuOS-0.1.1/umuOSstart.sh` を使うため、誤ってベースの `disk.img` を起動してしまう事故を避けやすい

推奨ルール:

- `OUT_PARENT` は実験ごとに分ける（例: `~/umu_project/work/20260101_login_test`）
- このスクリプトは生成先を削除して作り直すため、重要なディレクトリを `OUT_PARENT` に指定しない
- シリアルコンソール（`-nographic`）運用では、日本語パスワードは入力が崩れることがあるため、まずはASCII（英数字）を推奨

## 必要コマンド（ホスト側）

少なくとも以下が必要。

- `debugfs`, `e2fsck`, `tune2fs`, `blkid`（e2fsprogs）
- `grub-mkrescue`
- `xorriso`
- `python3`

Ubuntu で足りない場合の例:

```bash
sudo apt update
sudo apt install -y e2fsprogs grub-pc-bin grub-efi-amd64-bin xorriso
```

## 入力

- root パスワード
- ユーザ名
- ユーザ パスワード

## 使い方

```bash
cd ~/umu_project/repro/0.1.1
./install_0.1.1.sh
```

生成先の確認プロンプトをスキップしたい場合（危険）:

```bash
./install_0.1.1.sh --yes
```

`work/` 配下でテストしたい場合（推奨）:

```bash
OUT_PARENT=~/umu_project/work ./install_0.1.1.sh
```

または引数で指定:

```bash
./install_0.1.1.sh --out-parent ~/umu_project/work
```

非対話（環境変数で固定）:

```bash
OUT_PARENT=~/umu_project/work/20260101_login_test \
USER_NAME=tama ROOT_PW=umur USER_PW=umut \
./install_0.1.1.sh --yes
```

`UMU_ROOT` を明示したい場合:

```bash
UMU_ROOT=~/umu_project ./install_0.1.1.sh
```

## files/

`files/` は、スクリプトから生成・投入するファイル（Cコード、設定ファイルなど）の素材を置く。

- 直接 `tee` で流し込む方式でもよい
- ただし、差分レビューしやすいように「材料としてのファイル」を残す方針を推奨
