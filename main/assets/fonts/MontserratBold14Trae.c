/*******************************************************************************
 * Size: 14 px
 * Bpp: 2
 * Opts: --font components/lvgl/tests/src/test_files/fonts/Montserrat-Bold.ttf --size 14 --bpp 2 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name MontserratBold14Trae --range 0x20-0x7E --symbols • -o main/assets/fonts/MontserratBold14Trae.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef MONTSERRATBOLD14TRAE
#define MONTSERRATBOLD14TRAE 1
#endif

#if MONTSERRATBOLD14TRAE

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x3d, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x28, 0x18,
    0x7d, 0x3c,

    /* U+0022 "\"" */
    0x79, 0xd7, 0x5d, 0x35, 0xd3, 0x5d, 0x0, 0x0,

    /* U+0023 "#" */
    0x3, 0x87, 0x0, 0x34, 0xb0, 0x7f, 0xff, 0xf2,
    0xfe, 0xf9, 0xb, 0xe, 0x0, 0xb0, 0xe0, 0xbf,
    0xff, 0xd6, 0xfb, 0xf8, 0xe, 0x1c, 0x0, 0xd1,
    0xc0,

    /* U+0024 "$" */
    0x0, 0xd0, 0x0, 0x34, 0x0, 0xbf, 0x90, 0xff,
    0xfc, 0x7d, 0xd5, 0x1f, 0x74, 0x3, 0xff, 0x40,
    0x1f, 0xfc, 0x0, 0xef, 0x49, 0x37, 0xd7, 0xff,
    0xf0, 0x7f, 0xe0, 0x0, 0xd0, 0x0, 0x20, 0x0,

    /* U+0025 "%" */
    0x2f, 0x40, 0xb0, 0x72, 0xc1, 0xd0, 0xb0, 0xc3,
    0x80, 0x71, 0xcb, 0x0, 0x3f, 0x5d, 0xb8, 0x0,
    0x3a, 0xde, 0x0, 0xb3, 0x4b, 0x1, 0xd3, 0x4b,
    0x3, 0x82, 0xce, 0xb, 0x0, 0xb8,

    /* U+0026 "&" */
    0x7, 0xf8, 0x0, 0xfb, 0xd0, 0x1e, 0x1d, 0x0,
    0xfb, 0xc0, 0xb, 0xf0, 0x2, 0xff, 0x5d, 0x7c,
    0x3f, 0xd7, 0xc0, 0xfc, 0x3f, 0xff, 0xe0, 0xbf,
    0x88,

    /* U+0027 "'" */
    0x79, 0xd3, 0x4d, 0x0,

    /* U+0028 "(" */
    0x1e, 0x3c, 0x7c, 0xb8, 0xf4, 0xf0, 0xf0, 0xf0,
    0xf0, 0xf4, 0xb8, 0x7c, 0x3c, 0x1e,

    /* U+0029 ")" */
    0xb4, 0x3c, 0x3d, 0x2e, 0x1f, 0xf, 0xf, 0xf,
    0xf, 0x1f, 0x2e, 0x3d, 0x3c, 0xb4,

    /* U+002A "*" */
    0xa, 0xb, 0xfa, 0x3f, 0xc7, 0xfd, 0x6a, 0x90,
    0x60,

    /* U+002B "+" */
    0x1, 0x80, 0x2, 0xc0, 0x2, 0xc0, 0x3f, 0xfd,
    0x3f, 0xfe, 0x2, 0xc0, 0x2, 0xc0, 0x1, 0x80,

    /* U+002C "," */
    0x28, 0x7c, 0x3c, 0x38, 0x30,

    /* U+002D "-" */
    0x3f, 0x9f, 0xe0,

    /* U+002E "." */
    0x38, 0x7c, 0x3c,

    /* U+002F "/" */
    0x0, 0x3c, 0x0, 0x78, 0x0, 0xb4, 0x0, 0xf0,
    0x1, 0xe0, 0x2, 0xd0, 0x3, 0xc0, 0x7, 0x80,
    0xb, 0x40, 0xf, 0x0, 0x1e, 0x0, 0x2d, 0x0,
    0x3c, 0x0, 0x78, 0x0,

    /* U+0030 "0" */
    0x7, 0xf8, 0x7, 0xff, 0xc3, 0xe0, 0xf5, 0xf0,
    0x2f, 0x7c, 0x7, 0xdf, 0x1, 0xf7, 0xc0, 0xbc,
    0xf8, 0x3d, 0x1f, 0xff, 0x1, 0xfe, 0x0,

    /* U+0031 "1" */
    0xff, 0x7f, 0xd0, 0xf4, 0x3d, 0xf, 0x43, 0xd0,
    0xf4, 0x3d, 0xf, 0x43, 0xd0,

    /* U+0032 "2" */
    0x1f, 0xe0, 0xbf, 0xfc, 0x34, 0x3d, 0x0, 0x3d,
    0x0, 0x7c, 0x1, 0xf4, 0x7, 0xd0, 0x2f, 0x40,
    0x7f, 0xff, 0xbf, 0xff,

    /* U+0033 "3" */
    0x2f, 0xff, 0x47, 0xff, 0xd0, 0x3, 0xe0, 0x2,
    0xe0, 0x1, 0xfd, 0x0, 0x7f, 0xd0, 0x0, 0xb8,
    0x50, 0x3e, 0x3f, 0xff, 0x2, 0xfe, 0x0,

    /* U+0034 "4" */
    0x0, 0x7c, 0x0, 0xf, 0x40, 0x3, 0xe0, 0x0,
    0xbc, 0x0, 0xf, 0xf, 0x3, 0xe0, 0xf0, 0xbf,
    0xff, 0xeb, 0xff, 0xfd, 0x0, 0x1f, 0x0, 0x1,
    0xf0,

    /* U+0035 "5" */
    0x2f, 0xfd, 0x3f, 0xfd, 0x3c, 0x0, 0x3f, 0xe4,
    0x3f, 0xfd, 0x0, 0x3f, 0x0, 0x1f, 0x30, 0x3f,
    0xbf, 0xfd, 0x2f, 0xe4,

    /* U+0036 "6" */
    0x2, 0xfe, 0x3, 0xff, 0xc3, 0xe0, 0x0, 0xf6,
    0xa0, 0x7f, 0xff, 0x1f, 0x83, 0xd7, 0xc0, 0xb8,
    0xf4, 0x3d, 0x1f, 0xff, 0x1, 0xfe, 0x0,

    /* U+0037 "7" */
    0xbf, 0xff, 0x2f, 0xff, 0xcb, 0x42, 0xf1, 0x80,
    0xf4, 0x0, 0xbc, 0x0, 0x3e, 0x0, 0x1f, 0x0,
    0xf, 0x80, 0x3, 0xc0, 0x2, 0xf0, 0x0,

    /* U+0038 "8" */
    0xb, 0xf8, 0xf, 0xff, 0xc3, 0xc0, 0xf4, 0xf9,
    0x7c, 0x2f, 0xff, 0xf, 0xeb, 0xd7, 0xc0, 0xb9,
    0xf0, 0x3e, 0x3f, 0xff, 0x2, 0xfe, 0x0,

    /* U+0039 "9" */
    0xb, 0xf4, 0xf, 0xff, 0x4b, 0x81, 0xf2, 0xe0,
    0x7c, 0x7f, 0xff, 0x47, 0xf7, 0xd0, 0x0, 0xf0,
    0x0, 0xfc, 0x3f, 0xfc, 0xb, 0xf8, 0x0,

    /* U+003A ":" */
    0x3c, 0x7c, 0x28, 0x0, 0x0, 0x38, 0x7c, 0x3c,

    /* U+003B ";" */
    0x3c, 0x7c, 0x28, 0x0, 0x0, 0x28, 0x7c, 0x3c,
    0x38, 0x30,

    /* U+003C "<" */
    0x0, 0x5, 0x0, 0xbe, 0x1b, 0xf4, 0x3e, 0x0,
    0x3f, 0x80, 0x7, 0xfc, 0x0, 0x6e, 0x0, 0x0,

    /* U+003D "=" */
    0x3f, 0xfe, 0x3f, 0xfd, 0x0, 0x0, 0x0, 0x0,
    0x3f, 0xfe, 0x3f, 0xfd,

    /* U+003E ">" */
    0x10, 0x0, 0x3e, 0x40, 0x1b, 0xf4, 0x0, 0x7e,
    0x1, 0xbd, 0x2f, 0xe0, 0x3d, 0x0, 0x0, 0x0,

    /* U+003F "?" */
    0x7, 0xfd, 0xb, 0xff, 0xc1, 0x90, 0xf4, 0x0,
    0x3d, 0x0, 0x3e, 0x0, 0x2e, 0x0, 0x5, 0x0,
    0x1, 0x80, 0x0, 0xf4, 0x0, 0x3c, 0x0,

    /* U+0040 "@" */
    0x0, 0x7f, 0xe4, 0x0, 0x3e, 0x5b, 0xe0, 0xf,
    0x0, 0x7, 0x83, 0xc2, 0xf7, 0xdd, 0x34, 0xfe,
    0xfc, 0xe7, 0x1e, 0x7, 0xcb, 0x71, 0xe0, 0x3c,
    0x77, 0x1f, 0x7, 0xcb, 0x34, 0xfe, 0xfe, 0xd2,
    0xc2, 0xf5, 0xf8, 0xf, 0x0, 0x0, 0x0, 0x3e,
    0x5b, 0x0, 0x0, 0x7f, 0xe0, 0x0,

    /* U+0041 "A" */
    0x0, 0x3e, 0x0, 0x0, 0x7f, 0x0, 0x0, 0xff,
    0x80, 0x1, 0xf3, 0xc0, 0x2, 0xd2, 0xd0, 0x3,
    0xc1, 0xf0, 0xb, 0xff, 0xf4, 0xf, 0xff, 0xfc,
    0x1f, 0x0, 0x3c, 0x3d, 0x0, 0x2e,

    /* U+0042 "B" */
    0xff, 0xfd, 0xf, 0xff, 0xf4, 0xf4, 0xf, 0x8f,
    0x40, 0xf4, 0xff, 0xff, 0xf, 0xeb, 0xf8, 0xf4,
    0x7, 0xcf, 0x40, 0x7c, 0xff, 0xff, 0x8f, 0xff,
    0xe0,

    /* U+0043 "C" */
    0x1, 0xfe, 0x40, 0xff, 0xfe, 0x3f, 0x41, 0xc3,
    0xd0, 0x0, 0x7c, 0x0, 0x7, 0xc0, 0x0, 0x3d,
    0x0, 0x3, 0xf4, 0x1c, 0xf, 0xff, 0xe0, 0x2f,
    0xe4,

    /* U+0044 "D" */
    0xff, 0xf9, 0x3, 0xff, 0xfe, 0xf, 0x40, 0xbd,
    0x3d, 0x0, 0xbc, 0xf4, 0x1, 0xf3, 0xd0, 0x7,
    0xcf, 0x40, 0x2f, 0x3d, 0x2, 0xf4, 0xff, 0xff,
    0x83, 0xff, 0xe4, 0x0,

    /* U+0045 "E" */
    0xff, 0xfe, 0xff, 0xfe, 0xf4, 0x0, 0xf4, 0x0,
    0xff, 0xfc, 0xff, 0xfc, 0xf4, 0x0, 0xf4, 0x0,
    0xff, 0xfe, 0xff, 0xff,

    /* U+0046 "F" */
    0xff, 0xfe, 0xff, 0xfe, 0xf4, 0x0, 0xf4, 0x0,
    0xff, 0xfc, 0xff, 0xfc, 0xf4, 0x0, 0xf4, 0x0,
    0xf4, 0x0, 0xf4, 0x0,

    /* U+0047 "G" */
    0x1, 0xff, 0x40, 0x3f, 0xff, 0x83, 0xf4, 0x1d,
    0xf, 0x40, 0x0, 0x7c, 0x0, 0x1, 0xf0, 0x3,
    0xc3, 0xd0, 0xf, 0xf, 0xd0, 0xbc, 0xf, 0xff,
    0xf0, 0xb, 0xfd, 0x0,

    /* U+0048 "H" */
    0xf4, 0x3, 0xcf, 0x40, 0x3c, 0xf4, 0x3, 0xcf,
    0x40, 0x3c, 0xff, 0xff, 0xcf, 0xff, 0xfc, 0xf4,
    0x3, 0xcf, 0x40, 0x3c, 0xf4, 0x3, 0xcf, 0x40,
    0x3c,

    /* U+0049 "I" */
    0xf7, 0xdf, 0x7d, 0xf7, 0xdf, 0x7d, 0xf7, 0xd0,

    /* U+004A "J" */
    0xf, 0xfe, 0xf, 0xfe, 0x0, 0x3e, 0x0, 0x3e,
    0x0, 0x3e, 0x0, 0x3e, 0x0, 0x3d, 0x18, 0x3d,
    0x3f, 0xfc, 0xb, 0xe0,

    /* U+004B "K" */
    0xf4, 0xb, 0xcf, 0x42, 0xf0, 0xf4, 0xbc, 0xf,
    0x6f, 0x0, 0xff, 0xd0, 0xf, 0xff, 0x0, 0xfd,
    0xfc, 0xf, 0x83, 0xe0, 0xf4, 0x1f, 0x4f, 0x40,
    0xbc,

    /* U+004C "L" */
    0xf4, 0x0, 0xf4, 0x0, 0xf4, 0x0, 0xf4, 0x0,
    0xf4, 0x0, 0xf4, 0x0, 0xf4, 0x0, 0xf4, 0x0,
    0xff, 0xfd, 0xff, 0xfd,

    /* U+004D "M" */
    0xf4, 0x0, 0x3c, 0xfc, 0x0, 0xbc, 0xfe, 0x0,
    0xfc, 0xff, 0x2, 0xfc, 0xfb, 0x87, 0xbc, 0xf7,
    0xdf, 0x3c, 0xf5, 0xfe, 0x3c, 0xf4, 0xbc, 0x3c,
    0xf4, 0x34, 0x3c, 0xf4, 0x0, 0x3c,

    /* U+004E "N" */
    0xf4, 0x3, 0xcf, 0xc0, 0x3c, 0xff, 0x3, 0xcf,
    0xfc, 0x3c, 0xfb, 0xe3, 0xcf, 0x5f, 0x7c, 0xf4,
    0x7f, 0xcf, 0x42, 0xfc, 0xf4, 0xf, 0xcf, 0x40,
    0x3c,

    /* U+004F "O" */
    0x1, 0xff, 0x40, 0xf, 0xff, 0xe0, 0x3f, 0x41,
    0xf8, 0x3d, 0x0, 0x7c, 0x7c, 0x0, 0x3c, 0x7c,
    0x0, 0x3c, 0x3d, 0x0, 0x7c, 0x3f, 0x41, 0xf8,
    0xf, 0xff, 0xe0, 0x2, 0xff, 0x40,

    /* U+0050 "P" */
    0xff, 0xf8, 0x3f, 0xff, 0xcf, 0x40, 0xf7, 0xd0,
    0x2e, 0xf4, 0xf, 0x7f, 0xff, 0xcf, 0xff, 0x83,
    0xd0, 0x0, 0xf4, 0x0, 0x3d, 0x0, 0x0,

    /* U+0051 "Q" */
    0x1, 0xff, 0x40, 0xf, 0xff, 0xe0, 0x3f, 0x41,
    0xf8, 0x3d, 0x0, 0x7c, 0x7c, 0x0, 0x3c, 0x7c,
    0x0, 0x3c, 0x3d, 0x0, 0x7c, 0x3f, 0x41, 0xf8,
    0xf, 0xff, 0xe0, 0x2, 0xff, 0x40, 0x0, 0x1f,
    0xfd, 0x0, 0x2, 0xf8,

    /* U+0052 "R" */
    0xff, 0xf8, 0x3f, 0xff, 0xcf, 0x40, 0xf7, 0xd0,
    0x2e, 0xf4, 0xf, 0x7f, 0xff, 0xcf, 0xff, 0xc3,
    0xd0, 0xf0, 0xf4, 0x2f, 0x3d, 0x3, 0xe0,

    /* U+0053 "S" */
    0xb, 0xf9, 0xf, 0xff, 0xc7, 0xc0, 0x51, 0xf4,
    0x0, 0x3f, 0xe4, 0x1, 0xbf, 0xc0, 0x1, 0xf4,
    0x90, 0x3d, 0x7f, 0xff, 0x7, 0xfe, 0x0,

    /* U+0054 "T" */
    0xff, 0xff, 0xbf, 0xff, 0xe0, 0x3d, 0x0, 0xf,
    0x40, 0x3, 0xd0, 0x0, 0xf4, 0x0, 0x3d, 0x0,
    0xf, 0x40, 0x3, 0xd0, 0x0, 0xf4, 0x0,

    /* U+0055 "U" */
    0xf4, 0x7, 0xfd, 0x1, 0xff, 0x40, 0x7f, 0xd0,
    0x1f, 0xf4, 0x7, 0xfd, 0x1, 0xff, 0x80, 0x7e,
    0xf4, 0x7e, 0x3f, 0xff, 0x2, 0xfe, 0x0,

    /* U+0056 "V" */
    0x3e, 0x0, 0x3d, 0x1f, 0x0, 0x7c, 0xf, 0x40,
    0xb8, 0xb, 0xc0, 0xf0, 0x3, 0xc2, 0xe0, 0x3,
    0xe3, 0xc0, 0x1, 0xf7, 0xc0, 0x0, 0xff, 0x40,
    0x0, 0xbf, 0x0, 0x0, 0x3e, 0x0,

    /* U+0057 "W" */
    0xbc, 0x3, 0xd0, 0x1f, 0x3c, 0x7, 0xf0, 0x2d,
    0x3d, 0xb, 0xf0, 0x3c, 0x2f, 0xf, 0xb4, 0x7c,
    0x1f, 0x1e, 0x78, 0xb8, 0xf, 0x6d, 0x3c, 0xf0,
    0xb, 0xbc, 0x3e, 0xf0, 0x7, 0xf8, 0x1f, 0xe0,
    0x3, 0xf4, 0xf, 0xc0, 0x2, 0xf0, 0xf, 0xc0,

    /* U+0058 "X" */
    0x7c, 0x3, 0xd2, 0xf0, 0xf8, 0xf, 0x9f, 0x0,
    0x7f, 0xd0, 0x2, 0xf8, 0x0, 0x3f, 0xc0, 0x7,
    0xfd, 0x0, 0xf5, 0xf0, 0x3e, 0xb, 0xcb, 0xc0,
    0x3e,

    /* U+0059 "Y" */
    0x3e, 0x0, 0xf4, 0x7c, 0x7, 0xc0, 0xb8, 0x3c,
    0x0, 0xf6, 0xe0, 0x2, 0xff, 0x0, 0x3, 0xf4,
    0x0, 0xb, 0xc0, 0x0, 0x1f, 0x0, 0x0, 0x7c,
    0x0, 0x1, 0xf0, 0x0,

    /* U+005A "Z" */
    0x7f, 0xff, 0xc7, 0xff, 0xfc, 0x0, 0x2f, 0x0,
    0x7, 0xd0, 0x0, 0xf4, 0x0, 0x3e, 0x0, 0xf,
    0xc0, 0x2, 0xf0, 0x0, 0x7f, 0xff, 0xc7, 0xff,
    0xfc,

    /* U+005B "[" */
    0xff, 0xfe, 0xf4, 0xf4, 0xf4, 0xf4, 0xf4, 0xf4,
    0xf4, 0xf4, 0xf4, 0xf4, 0xfe, 0xff,

    /* U+005C "\\" */
    0x78, 0x0, 0xf0, 0x3, 0xc0, 0x7, 0x40, 0xf,
    0x0, 0x3c, 0x0, 0x74, 0x0, 0xf0, 0x3, 0xc0,
    0x7, 0x40, 0xf, 0x0, 0x3c, 0x0, 0x74, 0x0,
    0xf0,

    /* U+005D "]" */
    0xff, 0xbf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
    0xf, 0xf, 0xf, 0xf, 0xbf, 0xff,

    /* U+005E "^" */
    0x3, 0xd0, 0x7, 0xe0, 0xb, 0x70, 0xd, 0x38,
    0x2c, 0x2c, 0x38, 0x1d,

    /* U+005F "_" */
    0x0, 0x3, 0xff, 0xf5, 0x55, 0x40,

    /* U+0060 "`" */
    0x3c, 0x3, 0xc0,

    /* U+0061 "a" */
    0x1f, 0xf4, 0x3f, 0xfc, 0x0, 0x3e, 0x1f, 0xfe,
    0x7d, 0x6e, 0x78, 0x2e, 0x3e, 0xbe, 0x1f, 0xee,

    /* U+0062 "b" */
    0xf0, 0x0, 0x3c, 0x0, 0xf, 0x0, 0x3, 0xdf,
    0xd0, 0xff, 0xfd, 0x3f, 0xb, 0xcf, 0x40, 0xf3,
    0xd0, 0x3c, 0xfc, 0x2f, 0x3f, 0xff, 0x4f, 0x7f,
    0x40,

    /* U+0063 "c" */
    0x7, 0xf8, 0xb, 0xff, 0x87, 0xd0, 0x91, 0xf0,
    0x0, 0x7c, 0x0, 0xf, 0x42, 0x42, 0xff, 0xe0,
    0x1f, 0xe0,

    /* U+0064 "d" */
    0x0, 0xb, 0x80, 0x2, 0xe0, 0x0, 0xb8, 0x2f,
    0xae, 0x2f, 0xff, 0x9f, 0x43, 0xe7, 0xc0, 0xf9,
    0xf0, 0x3e, 0x7d, 0xf, 0x8b, 0xff, 0xe0, 0xbe,
    0xb8,

    /* U+0065 "e" */
    0x7, 0xf4, 0xb, 0xff, 0x87, 0xc0, 0xf1, 0xff,
    0xfd, 0x7d, 0x55, 0xf, 0x42, 0x2, 0xff, 0xe0,
    0x1f, 0xe0,

    /* U+0066 "f" */
    0x7, 0xe1, 0xfd, 0x2e, 0xf, 0xfd, 0xbf, 0xc2,
    0xd0, 0x2d, 0x2, 0xd0, 0x2d, 0x2, 0xd0, 0x2d,
    0x0,

    /* U+0067 "g" */
    0xb, 0xe7, 0xcf, 0xff, 0xf7, 0xc0, 0xfd, 0xe0,
    0x2f, 0x7c, 0xf, 0xcf, 0x43, 0xf2, 0xff, 0xfc,
    0x1f, 0xae, 0x10, 0xf, 0x8f, 0xff, 0xc1, 0xbf,
    0x80,

    /* U+0068 "h" */
    0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf7, 0xf4,
    0xff, 0xfc, 0xfc, 0x3e, 0xf4, 0x2e, 0xf0, 0x1e,
    0xf0, 0x1e, 0xf0, 0x1e, 0xf0, 0x1e,

    /* U+0069 "i" */
    0x14, 0x3d, 0x3c, 0x0, 0x3c, 0x3c, 0x3c, 0x3c,
    0x3c, 0x3c, 0x3c, 0x3c,

    /* U+006A "j" */
    0x1, 0x40, 0x3d, 0x3, 0xd0, 0x0, 0x3, 0xd0,
    0x3d, 0x3, 0xd0, 0x3d, 0x3, 0xd0, 0x3d, 0x3,
    0xd0, 0x3d, 0x3, 0xc3, 0xfc, 0x3f, 0x0,

    /* U+006B "k" */
    0xf0, 0x0, 0x3c, 0x0, 0xf, 0x0, 0x3, 0xc0,
    0xf8, 0xf0, 0xf8, 0x3d, 0xf8, 0xf, 0xfc, 0x3,
    0xff, 0x80, 0xfd, 0xf4, 0x3c, 0x2f, 0xf, 0x2,
    0xf0,

    /* U+006C "l" */
    0xf3, 0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xf3, 0xcf,
    0x0,

    /* U+006D "m" */
    0xf7, 0xf1, 0xfd, 0x3f, 0xff, 0xff, 0xdf, 0x83,
    0xf0, 0xfb, 0xd0, 0xf8, 0x1e, 0xf0, 0x2d, 0x7,
    0xbc, 0xb, 0x41, 0xef, 0x2, 0xd0, 0x7b, 0xc0,
    0xb4, 0x1e,

    /* U+006E "n" */
    0xf7, 0xf4, 0xff, 0xfc, 0xfc, 0x3e, 0xf4, 0x2e,
    0xf0, 0x1e, 0xf0, 0x1e, 0xf0, 0x1e, 0xf0, 0x1e,

    /* U+006F "o" */
    0x7, 0xf8, 0xb, 0xff, 0xc7, 0xd0, 0xf5, 0xf0,
    0x2e, 0x7c, 0xb, 0x8f, 0x43, 0xd2, 0xff, 0xf0,
    0x1f, 0xe0,

    /* U+0070 "p" */
    0xf7, 0xf4, 0x3f, 0xff, 0x4f, 0xc2, 0xf3, 0xd0,
    0x3c, 0xf4, 0xf, 0x3f, 0xb, 0xcf, 0xff, 0xd3,
    0xdf, 0xd0, 0xf0, 0x0, 0x3c, 0x0, 0xf, 0x0,
    0x0,

    /* U+0071 "q" */
    0xb, 0xeb, 0x8f, 0xff, 0xe7, 0xd0, 0xf9, 0xf0,
    0x3e, 0x7c, 0xf, 0x9f, 0x43, 0xe3, 0xff, 0xf8,
    0x2f, 0xae, 0x0, 0xb, 0x80, 0x2, 0xe0, 0x0,
    0xb8,

    /* U+0072 "r" */
    0xf7, 0xbf, 0xef, 0xc3, 0xd0, 0xf0, 0x3c, 0xf,
    0x3, 0xc0,

    /* U+0073 "s" */
    0x1f, 0xf4, 0x7f, 0xf4, 0x78, 0x0, 0x7f, 0x90,
    0x1b, 0xfc, 0x10, 0x3c, 0x7f, 0xfc, 0x2f, 0xe0,

    /* U+0074 "t" */
    0x2d, 0x2, 0xd0, 0xff, 0xdb, 0xfc, 0x2d, 0x2,
    0xd0, 0x2d, 0x2, 0xe0, 0x1f, 0xe0, 0xbe,

    /* U+0075 "u" */
    0x3c, 0xb, 0x8f, 0x2, 0xe3, 0xc0, 0xb8, 0xf0,
    0x2e, 0x3c, 0xb, 0x8f, 0x87, 0xe2, 0xff, 0xf8,
    0x2f, 0xae,

    /* U+0076 "v" */
    0x3d, 0x3, 0xd2, 0xe0, 0x3c, 0xf, 0xb, 0x40,
    0xb4, 0xf0, 0x7, 0xde, 0x0, 0x3f, 0xd0, 0x2,
    0xfc, 0x0, 0xf, 0x80,

    /* U+0077 "w" */
    0xf0, 0x1e, 0x3, 0xc7, 0x83, 0xf0, 0x78, 0x3c,
    0x3f, 0x4b, 0x43, 0xc7, 0xb8, 0xf0, 0x1e, 0xf3,
    0xde, 0x0, 0xfe, 0x2f, 0xc0, 0xf, 0xd0, 0xfc,
    0x0, 0x7c, 0xf, 0x80,

    /* U+0078 "x" */
    0x7c, 0x2f, 0xf, 0x8f, 0x0, 0xff, 0x40, 0x1f,
    0xc0, 0x7, 0xf0, 0x3, 0xfd, 0x3, 0xd3, 0xc2,
    0xf0, 0xbc,

    /* U+0079 "y" */
    0x3d, 0x3, 0xd2, 0xe0, 0x3c, 0xf, 0xb, 0x40,
    0xb8, 0xf0, 0x7, 0xde, 0x0, 0x3f, 0xd0, 0x1,
    0xfc, 0x0, 0xf, 0x40, 0x0, 0xf0, 0x3, 0xfe,
    0x0, 0x2f, 0x80, 0x0,

    /* U+007A "z" */
    0x7f, 0xfc, 0x7f, 0xfc, 0x1, 0xf0, 0x3, 0xd0,
    0xf, 0x80, 0x2f, 0x0, 0x7f, 0xfc, 0xbf, 0xfc,

    /* U+007B "{" */
    0x7, 0xd0, 0xfc, 0xf, 0x0, 0xf0, 0xf, 0x0,
    0xf0, 0x3e, 0x7, 0xe0, 0x1f, 0x0, 0xf0, 0xf,
    0x0, 0xf0, 0xf, 0xc0, 0x7d,

    /* U+007C "|" */
    0xf3, 0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xf3, 0xcf,
    0x3c, 0xf3, 0xc0,

    /* U+007D "}" */
    0xf8, 0x2f, 0x42, 0xe0, 0xb8, 0x2e, 0xb, 0x80,
    0xf8, 0x3f, 0x1f, 0xb, 0x82, 0xe0, 0xb8, 0xbd,
    0x3e, 0x0,

    /* U+007E "~" */
    0x1f, 0xe, 0x3f, 0xed, 0x30, 0xb8,

    /* U+2022 "•" */
    0x19, 0xf, 0xc3, 0xf0, 0xb8
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 63, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 65, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 10, .adv_w = 98, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 18, .adv_w = 161, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 43, .adv_w = 143, .box_w = 9, .box_h = 14, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 75, .adv_w = 196, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 105, .adv_w = 163, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 130, .adv_w = 52, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 134, .adv_w = 80, .box_w = 4, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 148, .adv_w = 80, .box_w = 4, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 162, .adv_w = 97, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 171, .adv_w = 134, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 187, .adv_w = 59, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 192, .adv_w = 86, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 195, .adv_w = 59, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 198, .adv_w = 88, .box_w = 8, .box_h = 14, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 226, .adv_w = 152, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 88, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 262, .adv_w = 132, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 282, .adv_w = 133, .box_w = 9, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 305, .adv_w = 154, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 330, .adv_w = 133, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 350, .adv_w = 143, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 373, .adv_w = 139, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 396, .adv_w = 148, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 419, .adv_w = 143, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 442, .adv_w = 59, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 450, .adv_w = 59, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 460, .adv_w = 134, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 476, .adv_w = 134, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 488, .adv_w = 134, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 504, .adv_w = 132, .box_w = 9, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 232, .box_w = 14, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 573, .adv_w = 172, .box_w = 12, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 603, .adv_w = 171, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 628, .adv_w = 164, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 653, .adv_w = 185, .box_w = 11, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 681, .adv_w = 150, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 701, .adv_w = 143, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 721, .adv_w = 173, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 749, .adv_w = 181, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 774, .adv_w = 73, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 782, .adv_w = 121, .box_w = 8, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 802, .adv_w = 166, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 827, .adv_w = 135, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 847, .adv_w = 214, .box_w = 12, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 877, .adv_w = 181, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 902, .adv_w = 189, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 932, .adv_w = 164, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 955, .adv_w = 189, .box_w = 12, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 991, .adv_w = 165, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1014, .adv_w = 143, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1037, .adv_w = 138, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1060, .adv_w = 177, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1083, .adv_w = 167, .box_w = 12, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1113, .adv_w = 261, .box_w = 16, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1153, .adv_w = 160, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1178, .adv_w = 151, .box_w = 11, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1206, .adv_w = 150, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1231, .adv_w = 82, .box_w = 4, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1245, .adv_w = 88, .box_w = 7, .box_h = 14, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 1270, .adv_w = 82, .box_w = 4, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1284, .adv_w = 134, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1296, .adv_w = 112, .box_w = 7, .box_h = 3, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1302, .adv_w = 134, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 9},
    {.bitmap_index = 1305, .adv_w = 138, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1321, .adv_w = 155, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1346, .adv_w = 132, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1364, .adv_w = 155, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1389, .adv_w = 141, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1407, .adv_w = 87, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1424, .adv_w = 157, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1449, .adv_w = 155, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1471, .adv_w = 67, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1483, .adv_w = 69, .box_w = 6, .box_h = 15, .ofs_x = -2, .ofs_y = -3},
    {.bitmap_index = 1506, .adv_w = 148, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1531, .adv_w = 67, .box_w = 3, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1540, .adv_w = 235, .box_w = 13, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1566, .adv_w = 155, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1582, .adv_w = 147, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1600, .adv_w = 155, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1625, .adv_w = 155, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1650, .adv_w = 97, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1660, .adv_w = 119, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1676, .adv_w = 97, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1691, .adv_w = 154, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1709, .adv_w = 134, .box_w = 10, .box_h = 8, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1729, .adv_w = 210, .box_w = 14, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1757, .adv_w = 133, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1775, .adv_w = 134, .box_w = 10, .box_h = 11, .ofs_x = -1, .ofs_y = -3},
    {.bitmap_index = 1803, .adv_w = 122, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1819, .adv_w = 88, .box_w = 6, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1840, .adv_w = 69, .box_w = 3, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1851, .adv_w = 88, .box_w = 5, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1869, .adv_w = 134, .box_w = 8, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 1875, .adv_w = 80, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 8226, .range_length = 1, .glyph_id_start = 96,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 0, 1, 2, 0, 3, 4, 5,
    2, 6, 7, 8, 9, 10, 9, 10,
    11, 12, 0, 13, 14, 15, 16, 17,
    18, 19, 12, 20, 20, 0, 0, 0,
    21, 22, 23, 24, 25, 22, 26, 27,
    28, 29, 29, 30, 31, 32, 29, 29,
    22, 33, 34, 35, 3, 36, 30, 37,
    37, 38, 39, 40, 41, 42, 43, 0,
    44, 0, 45, 46, 47, 48, 49, 50,
    51, 45, 52, 52, 53, 48, 45, 45,
    46, 46, 54, 55, 56, 57, 51, 58,
    58, 59, 58, 60, 41, 0, 0, 9,
    9
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 0, 1, 2, 0, 3, 4, 5,
    2, 6, 7, 8, 9, 10, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 12,
    18, 19, 20, 21, 21, 0, 0, 0,
    22, 23, 24, 25, 23, 25, 25, 25,
    23, 25, 25, 26, 25, 25, 25, 25,
    23, 25, 23, 25, 3, 27, 28, 29,
    29, 30, 31, 32, 33, 34, 35, 0,
    36, 0, 37, 38, 39, 39, 39, 40,
    39, 38, 41, 42, 38, 38, 43, 43,
    39, 43, 39, 43, 44, 45, 46, 47,
    47, 48, 49, 50, 0, 0, 35, 9,
    9
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    0, 1, 0, 0, 0, 0, 0, 2,
    0, 1, 0, 0, 2, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0,
    0, 5, 4, 0, 2, 0, 4, 0,
    0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 10, 0, 5, -4, 0,
    0, 4, 0, -12, -13, 1, 10, 4,
    3, -9, 1, 9, 0, 8, 2, 6,
    0, -9, 0, 0, 3, 0, 0, 0,
    0, 0, 0, 13, 3, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -7, 0, 0,
    0, 0, 0, -4, 3, 4, 0, 0,
    -2, 0, -1, 2, 0, -2, 0, -2,
    -1, -4, 0, 0, 0, 0, -2, 0,
    0, -3, -3, 0, 0, -2, 0, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, -2, -2, 0, 0, -5,
    0, -28, 0, 0, -4, -11, 4, 7,
    0, 0, -4, 2, 2, 7, 4, -3,
    4, 0, 0, -12, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -8,
    0, 0, 2, 0, 6, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -3, -9, 0, -9, -1, 0, 0, -7,
    0, -1, 7, 0, -7, -3, -1, 1,
    0, -3, 0, 0, -2, -15, 0, 2,
    0, 6, -5, 0, -4, 0, -9, 2,
    0, -18, -3, 5, 2, 0, 0, 0,
    0, 0, 0, 0, 2, 0, -4, -1,
    -4, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 8, 0, 2, 0,
    0, -4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 10, 3, 1, 0, 0,
    0, 0, 0, 23, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -8, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 2, 4,
    2, 7, -2, 0, 0, 4, -2, -8,
    -30, 1, 5, 4, 0, -3, 0, 5,
    0, 6, 0, 6, 0, -22, 0, -3,
    7, 0, 7, -2, 4, 2, 0, 0,
    1, -2, 0, 0, -3, 0, 0, 0,
    18, 0, 7, 0, 8, 4, 8, 3,
    0, 0, -3, -8, 0, 0, 0, -2,
    1, -2, 0, 1, -5, -4, -4, 1,
    0, -2, 0, 0, 0, -9, 1, -5,
    0, -4, -8, 0, -6, -5, -7, 0,
    0, -15, 0, 0, 0, 0, 2, 0,
    0, 0, 0, 0, 2, 0, -4, -6,
    -4, -3, 1, -12, 2, -15, 0, 0,
    0, -8, -2, 0, 20, -3, -4, 2,
    2, 0, 0, -4, 2, 0, 0, -13,
    -4, 7, 0, 12, -7, -2, -9, 0,
    -9, 3, 0, -21, 0, 2, 2, 0,
    -2, 0, 0, 2, 0, 0, -1, -2,
    -7, 0, -7, 0, 0, 13, -4, 0,
    -8, 0, 8, 0, -16, -21, -16, -4,
    7, 0, 0, -14, 0, 2, -6, 0,
    -3, 0, -4, -9, 0, -2, 7, 0,
    7, 0, 7, 0, 0, 3, 7, -26,
    -14, 0, -14, 0, 3, 1, -14, -14,
    -5, -14, -7, -11, -7, -14, 0, 1,
    0, 0, 0, 0, 0, 1, 1, -3,
    -4, 0, -1, -1, -2, 0, 0, -1,
    0, 0, 0, -4, 0, -2, 0, -6,
    -4, 0, -6, -8, -8, -3, 0, -4,
    0, -4, 0, 0, 0, 0, 0, -2,
    0, 0, 2, 0, 1, -2, 1, 0,
    0, 0, 0, 2, -1, 0, 0, 0,
    -1, 2, 2, 0, 0, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 1,
    0, 4, -1, 0, -3, 0, -4, 0,
    0, -1, 0, 7, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    0, -1, 0, -2, 0, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    -1, 0, -2, -2, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, -2, -2,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    -2, -4, -2, 0, 0, -7, -1, -7,
    4, 0, 0, -4, 2, 4, 5, 0,
    -6, 0, -2, 0, 0, -9, 2, -1,
    2, -13, 2, 0, 0, 1, -12, 0,
    -13, -2, -20, -1, 0, -11, 0, 4,
    6, 0, 4, 0, 0, 0, 0, 0,
    0, 0, -5, -4, -5, 0, 0, 0,
    0, -2, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -1, -1, 0, -1, -3,
    0, 0, 0, 0, 0, 0, 0, -2,
    -2, 0, -2, -3, -2, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, -1, 0, -4, 2, 0, 0, -2,
    1, 2, 2, 0, 0, 0, 0, 0,
    0, -1, 0, 0, 0, 0, 0, 1,
    0, 0, -2, 0, -2, -1, -2, 0,
    0, 0, 0, 0, 0, 0, 2, 0,
    0, -2, 0, 0, 0, 0, -3, -3,
    -3, 0, 0, 7, -1, 1, -6, 0,
    0, 5, -11, -11, -9, -4, 2, 0,
    -2, -15, -4, 0, -4, 0, -4, 3,
    -4, -13, 0, -5, 0, 0, 1, 0,
    2, -1, 0, 2, 2, -7, -8, 0,
    -11, 0, 0, -5, -4, -7, -2, -5,
    1, -2, 1, -5, 0, 0, 0, -2,
    0, 0, 0, 1, 0, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, -1, 0, 0, -2, 0,
    -4, -5, -5, 0, 0, -7, 0, 0,
    0, 0, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 2,
    0, 0, 0, 0, 0, 0, 0, 0,
    12, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, -2, 0, 2, 0, 8,
    -2, 0, -6, -2, -9, 0, 0, -5,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 3, 0, 1, -2, 1, 0,
    -1, 0, 0, 0, -2, 0, 0, 2,
    0, -11, -7, 0, 0, 0, -3, -11,
    0, 0, -2, 2, 0, -4, -1, -10,
    0, -7, 0, 0, -4, -5, -4, -2,
    -2, 0, 0, -3, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 2, 0,
    2, 0, 0, -4, 0, 0, 0, 0,
    2, 0, 1, -4, -4, 0, -2, -2,
    -3, 0, 0, 0, 0, 0, 0, -7,
    0, -2, 0, -3, -2, 0, -5, -6,
    -7, -2, 0, -4, 2, -7, 0, 0,
    0, 0, 0, 18, 0, 0, 1, 0,
    0, -4, 0, 0, 0, -9, 0, 0,
    0, 0, 0, -22, -5, 7, 7, -2,
    -10, 0, 2, -3, 0, -11, -1, -3,
    2, -16, -2, 5, 0, 3, -8, -3,
    -9, -8, -10, 0, 0, -13, 0, 12,
    0, 0, -1, 0, 0, 0, 0, -1,
    -1, -2, -6, -8, -6, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, -1, -2, -3, 0, 0, -4,
    0, -2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, -4, 0, 0, 4,
    -1, 3, 0, -4, 2, -1, 0, -3,
    -2, 0, -3, -2, -2, 0, -3, -3,
    0, 0, -2, -1, -1, -3, -2, 0,
    0, -2, 0, 2, -1, 0, -4, 0,
    0, 0, 0, -4, 0, -3, 0, -3,
    0, -3, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 2, 0, -2, 0, -1,
    -2, -4, -1, -1, -1, 0, -1, -2,
    0, 0, 0, 0, 0, 0, -2, -2,
    -2, 0, 0, 0, 0, 2, -1, 0,
    -1, 0, 0, 0, 0, -1, -2, -1,
    -1, -2, -1, -1, 3, 9, 0, 0,
    -5, 0, -1, 4, 0, -2, -9, -3,
    3, 1, 0, -9, -3, 2, -3, 2,
    0, 1, -2, -6, 0, -3, 1, 0,
    0, -3, 0, 0, 0, 2, 2, -4,
    -3, 0, -3, 0, 0, -3, -2, -2,
    0, -3, 1, -3, 1, -3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -3, 0, 0, -3, 0, 0, -2,
    -2, 0, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, -1, 0, 0, 0, 0, -3, 0,
    -4, 0, 0, 0, -8, 0, 1, -5,
    4, 0, -1, -10, 0, 0, -5, -2,
    0, -9, -6, -7, 0, 0, -11, -2,
    -9, -9, -11, 0, -3, 0, 3, 14,
    -3, 0, -5, 0, 0, 0, -2, -3,
    -5, -4, -9, -10, -9, -6, 0, 0,
    -1, 0, 1, 0, 0, -16, 0, 7,
    4, -4, -8, 0, 1, -4, 0, -11,
    -1, -2, 4, -19, -3, 1, 0, 0,
    -15, -2, -11, -2, -16, 0, 0, -16,
    0, 11, 1, 0, -1, 0, 0, 0,
    0, 0, -1, -1, -8, -1, -8, 0,
    0, 0, 0, 0, -6, 0, -1, 0,
    -1, -6, -10, 0, 0, -1, -3, -7,
    -2, 0, -2, 0, 0, 0, 0, -10,
    -2, -7, -6, -3, -4, -6, -2, -3,
    0, -4, -1, -8, -3, 0, -2, 0,
    0, -2, -2, 0, 1, 0, -1, -7,
    -1, 0, 0, -4, 0, 0, 0, 0,
    2, 0, 1, -4, 11, 0, -2, -2,
    -3, 0, 0, 0, 0, 0, 0, -7,
    0, -2, 0, -3, -2, 0, -5, -6,
    -7, -2, 0, -4, 3, 9, 0, 0,
    0, 0, 0, 18, 0, 0, 1, 0,
    0, -4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, -4, 0, 0, 0, 0,
    0, -1, 0, 0, 0, -2, -2, 0,
    0, -4, -2, 0, 0, -4, 0, 3,
    -1, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 4, 3,
    -1, 0, -6, -2, 0, 7, -8, -7,
    -4, -4, 9, 5, 2, -19, -2, 4,
    -2, 0, -2, 4, -2, -8, 0, -2,
    2, -3, -3, -7, -3, 0, 0, 7,
    4, 0, -6, 0, -12, 0, 0, 10,
    -3, -8, 1, -3, -7, -7, -7, -2,
    2, 0, -3, 0, -5, 0, 3, 7,
    -6, -9, -9, -6, 7, 0, 1, -17,
    -2, 2, -4, -2, -6, 0, -5, -9,
    -3, -3, -3, 0, 0, -6, -6, -2,
    0, 7, 6, -2, -12, 0, -12, 0,
    -1, -4, -8, -14, -1, -7, -4, -7,
    -4, -7, 0, 0, -3, 0, -4, -1,
    0, -2, -5, 0, 3, -8, 2, 0,
    0, -13, 0, -2, -5, -4, -2, -7,
    -6, -8, -5, 0, -7, -2, -6, -2,
    -7, -2, 0, 0, 1, 10, -4, 0,
    -7, 0, 0, 0, -2, -4, -6, -6,
    -7, -10, -7, -4, 4, 0, -3, 0,
    -11, -2, 2, 4, -7, -9, -4, -8,
    8, -2, 1, -22, -5, 4, -5, -4,
    -9, 0, -7, -10, -2, -2, -3, -2,
    -6, -7, -1, 0, 0, 7, 7, -1,
    -15, 0, -13, 0, -2, 8, -8, -16,
    -4, -8, -10, -11, -10, -8, 0, 0,
    0, 0, -2, 0, 0, 2, -2, 4,
    1, -3, 4, 0, 0, -4, 0, 0,
    0, 0, 1, 1, -2, 0, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    3, 7, 0, 0, -2, 0, 0, 0,
    0, 0, -1, -1, -2, 0, -2, 0,
    1, 3, 0, 0, 0, 0, 3, 1,
    -1, 0, 10, 0, 5, 1, 1, -4,
    0, 4, 0, 0, 0, 3, 0, 0,
    0, 0, 4, 0, 6, 1, 7, 0,
    0, 7, 0, 7, -1, 0, 0, 0,
    0, 22, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -13, 0, -2, 3, 0,
    7, -30, 0, 20, 2, -4, -4, 2,
    2, -1, 1, -11, 0, 0, 12, -13,
    -4, 7, 0, 7, -4, -2, -9, 4,
    -4, 0, 0, -16, 10, 31, 0, 0,
    0, 0, 0, 27, 0, 0, 0, 0,
    4, 0, 4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, -2, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    4, -5, 0, 0, 1, -2, 0, 2,
    27, -4, -2, 6, 5, -5, 2, 0,
    0, 2, 2, -4, -7, 12, 7, 17,
    0, -2, -2, 10, -1, 4, 0, -29,
    7, 0, -2, 0, -5, 0, 0, 25,
    0, 2, -4, -5, -4, 8, 4, 3,
    0, 0, 0, -6, 0, 0, 0, -5,
    0, 0, 0, 0, -4, -1, 0, 0,
    0, -4, 0, -2, 0, -10, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -14, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    -2, 0, 0, -3, 0, -5, 0, 0,
    0, -3, 2, -2, 0, 0, -5, -2,
    -5, 0, 0, -5, 0, -2, 0, -10,
    0, -4, 0, 0, -15, -2, -9, -4,
    -9, 0, 0, -14, 0, -5, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -3, -4, -3, -2, 0, 0, 0, 0,
    -4, 0, -5, 4, -4, 4, 0, -1,
    -5, -1, -3, -2, 0, -2, -1, -1,
    2, -5, -1, 0, 0, 0, -16, -3,
    -6, 0, -9, 0, -1, -10, -1, 0,
    0, -1, -2, 0, -1, 0, 0, 0,
    2, 0, -2, -3, -2, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 9, 0, 0, 0, 0, 0,
    0, -4, 0, -1, 0, 0, 0, -4,
    2, 0, 0, 0, -5, -2, -4, 0,
    0, -6, 0, -2, 0, -10, 0, 0,
    0, 0, -21, 0, -4, -9, -11, 0,
    0, -14, 0, -2, -3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, -3,
    -2, -1, 1, 0, 0, 3, -4, 0,
    9, 9, -2, -2, -7, 2, 9, 3,
    4, -5, 2, 8, 2, 6, 4, 5,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 14, 10, -5, -2, 2,
    -2, 2, 2, 10, 1, 0, 0, 0,
    2, 0, 2, 0, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 6, 0, 0, 0, 0, -14, -2,
    -3, -7, -9, 0, 0, -14, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 6, 0, 0, 0, 0,
    -14, -2, -3, -7, -9, 0, 0, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -3, 0, 0, 0,
    -6, 2, 0, -2, 3, 5, 2, -7,
    0, 1, -2, 2, 0, 3, 0, 0,
    0, 0, -2, 0, -1, -1, -4, 0,
    -1, -9, 0, 13, -2, 0, -5, 0,
    0, 0, -1, -4, 0, -2, -7, -4,
    -7, -3, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0, 6,
    0, 0, 0, 0, -14, -2, -3, -7,
    -9, 0, 0, -14, 0, 0, 0, 0,
    0, 0, 0, 11, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    -6, -2, -1, 7, -2, -2, -9, 1,
    2, 1, -1, -5, 0, 5, 0, 3,
    1, 3, -4, -10, -2, 0, -5, -2,
    -5, -9, -8, 0, -2, -4, -2, -4,
    -9, -1, -2, 0, -1, 0, -1, 0,
    3, 0, 3, -1, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, -2, -2, 0, 0, -5,
    0, -1, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -13,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, 0, 0, 0, -2, 0, 0, -3,
    -2, 2, 0, -3, -3, -1, 0, -4,
    -1, -4, -2, -2, 0, -3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -14, 0, 6, 0, 0, -4, 0,
    0, 0, 0, 0, -3, 0, -2, 0,
    -2, 0, 0, 0, -1, 0, -6, 0,
    0, 8, -4, -7, -8, 1, 4, 4,
    1, -7, 1, 3, 1, 7, 1, 8,
    -1, -6, 0, 0, -4, 0, 0, -7,
    -5, 0, 0, -4, 0, -4, -4, 0,
    -3, 0, 0, -3, 0, -2, 3, 0,
    -3, -7, -3, -2, 0, 0, -1, 0,
    -4, 0, 0, 4, -6, 0, 2, -2,
    3, 2, 0, -8, 0, -1, -1, 0,
    -2, 4, -3, 0, 0, 0, -5, -2,
    -5, 0, -7, 0, 0, -10, 0, 8,
    -2, 0, -4, 0, 0, 6, 0, -2,
    0, -2, -7, 0, -7, -2, 0, 0,
    0, 0, -1, 0, 0, 2, -3, 1,
    0, 0, -2, -1, 0, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -13,
    0, 4, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 60,
    .right_class_cnt     = 50,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 2,
    .bpp = 2,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t MontserratBold14Trae = {
#else
lv_font_t MontserratBold14Trae = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if MONTSERRATBOLD14TRAE*/
