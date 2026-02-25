# top

UmuOS向けの簡易 `top`。
`/proc` から CPU/メモリ/負荷 と、プロセス一覧を取得して、一定間隔で画面を更新表示する。

> UmuOS では `/umu_bin` が `PATH` 先頭になる想定のため、`/umu_bin/top` を置くことで利用できる。

## 使い方

起動:

```sh
top
```

終了:

- `q` または `Q`

## 表示内容（概要）

- 先頭に
  - load average（/proc/loadavg）
  - uptime（/proc/uptime）
  - Tasks（/proc から集計）
  - CPU使用率（/proc/stat 差分の近似）
  - Mem/Swap（/proc/meminfo）
- 続いてプロセス一覧（CPU使用率が高い順。同率はPID昇順）

## 数値の見方

### load average

`load average: 1, 5, 15` は、直近 **1分/5分/15分** の「実行待ち（または実行中）」タスク数の平均。

- だいたい「CPUコア数」に近い値が目安
  - 例: 1コアで `load average` が 1.00 前後なら混雑は少なめ
  - それより大きいと「CPU待ち」が増えている可能性

### Tasks

`Tasks: ...` は `/proc` を走査して状態を集計したもの。

- running: 実行中（R）
- sleeping: スリープ中（S/D）
- stopped: 停止中（T）
- zombie: ゾンビ（Z）

### %Cpu(s)

`%Cpu(s): ...` は `/proc/stat` の差分から計算したCPU使用率（近似）。

- us: ユーザー空間
- sy: カーネル
- ni: nice（優先度変更されたタスク）
- id: idle（アイドル）

※この `top` は最小実装のため `wa/hi/si/st` は 0.0 表示（未計測）。

### Mem / Swap

`MiB Mem` と `MiB Swap` は `/proc/meminfo` 由来。

- total: 合計
- free: 未使用
- used: 使用中（概算）
- buff/cache: バッファ/キャッシュ（概算）
- avail Mem: 利用可能（概算。キャッシュ等を考慮した目安）

### プロセス一覧の列

`PID USER PR NI VIRT RES SHR S %CPU %MEM TIME+ COMMAND`

- PID: プロセスID
- USER: 所有ユーザー（UID→名前。分からない場合は数値UID）
- PR/NI: 優先度/ nice 値（近似）
- VIRT: 仮想メモリサイズ（バイト由来を kB 表示）
- RES: 常駐メモリ（RSS。kB 表示）
- SHR: 共有メモリ（推定。kB 表示）
- S: 状態（R/S/D/T/Z など）
- %CPU: 直近サンプル間のCPU使用率（近似）
- %MEM: `RES / MemTotal` の割合（近似）
- TIME+: `(utime+stime)` の合計（分:秒の目安）
- COMMAND: コマンド名（`/proc/<pid>/stat` の comm。長い場合は省略）
  - カーネルスレッドは本家 `top` と同様に `[` `]` で囲って表示

## 備考

- 最小実装のため、`top` 本家の全操作（並び替え等）は未対応。
- 端末幅が狭いと `COMMAND` が折り返して見づらくなることがある。
