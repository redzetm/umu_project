#pragma once

/*
 * ush.h
 *
 * ush 全体の状態をまとめる。
 * グローバル変数を増やさず、main から各モジュールへ明示的に渡す。
 */

typedef struct {
    /* 直前のコマンド終了ステータス。初期値は0。 */
    int last_status;
} ush_state_t;
