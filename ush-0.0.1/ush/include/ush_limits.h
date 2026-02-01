#pragma once

/*
 * ush_limits.h
 *
 * MVPの固定制限値。
 * 仕様書/詳細設計書に合わせて、実装ではここを参照する。
 */

enum {
    /* 1行の最大長（bytes）。getline()で取得した行に対して長さチェックする。 */
    USH_MAX_LINE_LEN = 8192,

    /* argv の最大要素数（NULL終端は別枠）。 */
    USH_MAX_ARGS = 128,

    /* 1トークンの最大長（bytes）。 */
    USH_MAX_TOKEN_LEN = 1024,
};
