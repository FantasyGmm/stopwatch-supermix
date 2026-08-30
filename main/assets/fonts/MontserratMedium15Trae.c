/*******************************************************************************
 * Size: 15 px
 * Bpp: 2
 * Opts: --font components/lvgl/scripts/built_in_font/Montserrat-Medium.ttf --size 15 --bpp 2 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name MontserratMedium15Trae --range 0x20-0x7E -o main/assets/fonts/MontserratMedium15Trae.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef MONTSERRATMEDIUM15TRAE
#define MONTSERRATMEDIUM15TRAE 1
#endif

#if MONTSERRATMEDIUM15TRAE

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0xff, 0xba, 0xaa, 0xa0, 0xaf,

    /* U+0022 "\"" */
    0x31, 0xcc, 0x73, 0x1c, 0xc7, 0x0, 0x0,

    /* U+0023 "#" */
    0x2, 0x43, 0x40, 0xd, 0xc, 0x3, 0xff, 0xff,
    0x0, 0xc1, 0xc0, 0x3, 0x7, 0x0, 0x18, 0x18,
    0xb, 0xff, 0xfe, 0x2, 0x82, 0x40, 0x9, 0xd,
    0x0, 0x34, 0x30, 0x0,

    /* U+0024 "$" */
    0x0, 0x40, 0x0, 0x20, 0x0, 0x8, 0x0, 0x2f,
    0xf4, 0x2d, 0xdb, 0xd, 0x20, 0x3, 0x88, 0x0,
    0x7f, 0x80, 0x1, 0xfe, 0x0, 0x21, 0xe0, 0x8,
    0x38, 0xe7, 0x6d, 0x1b, 0xfd, 0x0, 0x20, 0x0,
    0x4, 0x0,

    /* U+0025 "%" */
    0x1f, 0x40, 0x30, 0xc, 0x30, 0x24, 0x6, 0x9,
    0x18, 0x0, 0xc3, 0xc, 0x0, 0x1f, 0x4d, 0xb8,
    0x0, 0xa, 0x61, 0x80, 0x7, 0x20, 0x30, 0x3,
    0x8, 0xc, 0x3, 0x41, 0x87, 0x1, 0x80, 0x2e,
    0x0,

    /* U+0026 "&" */
    0x3, 0xf8, 0x0, 0x38, 0x34, 0x0, 0xd0, 0xd0,
    0x2, 0xde, 0x0, 0x3, 0xe0, 0x0, 0x76, 0xc2,
    0x43, 0x42, 0xdd, 0x1c, 0x1, 0xf0, 0x3d, 0x1f,
    0xd0, 0x2f, 0xe1, 0x80, 0x0, 0x0, 0x0,

    /* U+0027 "'" */
    0x30, 0xc3, 0xc, 0x0,

    /* U+0028 "(" */
    0xd, 0x2c, 0x38, 0x30, 0x70, 0x70, 0xb0, 0xb0,
    0x70, 0x70, 0x30, 0x38, 0x2c, 0xd,

    /* U+0029 ")" */
    0x70, 0x38, 0x1c, 0xc, 0xd, 0xe, 0xe, 0xe,
    0xe, 0xd, 0xc, 0x1c, 0x38, 0x70,

    /* U+002A "*" */
    0x5, 0x6, 0xa9, 0x1f, 0x87, 0xfd, 0x5, 0x0,
    0x50,

    /* U+002B "+" */
    0x2, 0x0, 0x1c, 0x0, 0x70, 0x3f, 0xfe, 0x7,
    0x0, 0x1c, 0x0, 0x70, 0x0,

    /* U+002C "," */
    0x0, 0xe3, 0x8c, 0x30,

    /* U+002D "-" */
    0x0, 0xf, 0xf0, 0x0,

    /* U+002E "." */
    0x0, 0xe3, 0x40,

    /* U+002F "/" */
    0x0, 0x28, 0x0, 0xd0, 0x7, 0x0, 0x28, 0x0,
    0xc0, 0x7, 0x0, 0x38, 0x0, 0xc0, 0x7, 0x0,
    0x34, 0x0, 0xc0, 0xb, 0x0, 0x34, 0x0, 0xc0,
    0x0,

    /* U+0030 "0" */
    0x2, 0xf8, 0x0, 0xf5, 0xf0, 0x3c, 0x3, 0xc3,
    0x40, 0x1c, 0x34, 0x1, 0xc3, 0x40, 0x1c, 0x34,
    0x1, 0xc3, 0xc0, 0x3c, 0xf, 0x5f, 0x0, 0x2f,
    0x80,

    /* U+0031 "1" */
    0xff, 0x5b, 0xb, 0xb, 0xb, 0xb, 0xb, 0xb,
    0xb, 0xb,

    /* U+0032 "2" */
    0x1f, 0xf4, 0x2e, 0x5f, 0x40, 0x0, 0xe0, 0x0,
    0x38, 0x0, 0x2c, 0x0, 0x2d, 0x0, 0x2c, 0x0,
    0x2c, 0x0, 0x3d, 0x55, 0x1f, 0xff, 0xc0,

    /* U+0033 "3" */
    0x7f, 0xfd, 0x15, 0x7c, 0x0, 0x70, 0x1, 0xd0,
    0x3, 0xe4, 0x1, 0x6d, 0x0, 0xb, 0x0, 0xf,
    0xb9, 0x6d, 0x2f, 0xf4,

    /* U+0034 "4" */
    0x0, 0x2c, 0x0, 0x7, 0x40, 0x1, 0xd0, 0x0,
    0x38, 0x0, 0xe, 0xb, 0x3, 0xc0, 0xb0, 0x7f,
    0xff, 0xf0, 0x0, 0xb0, 0x0, 0xb, 0x0, 0x0,
    0xb0,

    /* U+0035 "5" */
    0x1f, 0xfe, 0xb, 0x55, 0x2, 0x80, 0x0, 0xe0,
    0x0, 0x3f, 0xf8, 0x1, 0x5b, 0x80, 0x0, 0xb0,
    0x0, 0x2c, 0xb9, 0x6e, 0x7, 0xfd, 0x0,

    /* U+0036 "6" */
    0x2, 0xfe, 0x3, 0xd5, 0x82, 0xc0, 0x0, 0xd0,
    0x0, 0x36, 0xfd, 0x1f, 0x92, 0xd3, 0x80, 0x2c,
    0xe0, 0xb, 0x1e, 0x5b, 0x41, 0xbf, 0x40,

    /* U+0037 "7" */
    0xbf, 0xff, 0x6d, 0x56, 0xcb, 0x0, 0xe0, 0x0,
    0x70, 0x0, 0x38, 0x0, 0x1d, 0x0, 0xf, 0x0,
    0x7, 0x40, 0x3, 0xc0, 0x0, 0xd0, 0x0,

    /* U+0038 "8" */
    0xb, 0xfd, 0xb, 0x46, 0xd3, 0x80, 0x38, 0xb0,
    0x1d, 0xf, 0xfe, 0xf, 0x42, 0xd7, 0x40, 0x2d,
    0xd0, 0xb, 0x3d, 0x5b, 0x42, 0xff, 0x40,

    /* U+0039 "9" */
    0xb, 0xf4, 0xf, 0x47, 0x87, 0x0, 0x72, 0xc0,
    0x1d, 0x38, 0x1f, 0x87, 0xfd, 0xd0, 0x0, 0x34,
    0x0, 0x2c, 0x15, 0x7d, 0xb, 0xf8, 0x0,

    /* U+003A ":" */
    0x34, 0xe0, 0x0, 0x0, 0x3, 0x8d,

    /* U+003B ";" */
    0x34, 0xe0, 0x0, 0x0, 0x3, 0x4e, 0x34, 0xc0,
    0x0,

    /* U+003C "<" */
    0x0, 0x4, 0x2, 0xe1, 0xf8, 0x3d, 0x0, 0xbd,
    0x0, 0x1f, 0x80, 0x2, 0x80, 0x0,

    /* U+003D "=" */
    0xff, 0xf8, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff,
    0xf8, 0x0, 0x0,

    /* U+003E ">" */
    0x40, 0x3, 0xe0, 0x1, 0xb9, 0x0, 0x1e, 0x2,
    0xf5, 0xf9, 0xd, 0x0, 0x0, 0x0,

    /* U+003F "?" */
    0x1f, 0xf4, 0xb9, 0x6d, 0x0, 0xe, 0x0, 0xd,
    0x0, 0x78, 0x0, 0xd0, 0x1, 0x80, 0x0, 0x0,
    0x2, 0xc0, 0x2, 0xc0,

    /* U+0040 "@" */
    0x0, 0x6f, 0xf8, 0x0, 0xb, 0x40, 0x2d, 0x0,
    0xe1, 0xfd, 0xee, 0xa, 0x1d, 0x1f, 0x8c, 0x30,
    0xe0, 0xe, 0x28, 0xc3, 0x0, 0x38, 0x67, 0xc,
    0x0, 0xe1, 0xcc, 0x38, 0x3, 0x86, 0x30, 0x74,
    0x7f, 0x34, 0xa0, 0x7f, 0x4f, 0x80, 0xe0, 0x0,
    0x0, 0x0, 0xf4, 0x5, 0x0, 0x0, 0x6f, 0xf4,
    0x0,

    /* U+0041 "A" */
    0x0, 0x2d, 0x0, 0x0, 0x3f, 0x0, 0x0, 0x73,
    0x40, 0x0, 0xd1, 0xc0, 0x1, 0xc0, 0xd0, 0x3,
    0x40, 0xb0, 0x7, 0xff, 0xf4, 0xe, 0x0, 0x2c,
    0x1c, 0x0, 0xd, 0x38, 0x0, 0xf,

    /* U+0042 "B" */
    0x7f, 0xfe, 0x7, 0x40, 0x7c, 0x70, 0x2, 0xc7,
    0x0, 0x38, 0x7f, 0xff, 0x7, 0x40, 0x7c, 0x70,
    0x0, 0xe7, 0x0, 0xe, 0x74, 0x6, 0xc7, 0xff,
    0xe4,

    /* U+0043 "C" */
    0x1, 0xbf, 0x80, 0x2e, 0x57, 0xc2, 0xd0, 0x0,
    0xe, 0x0, 0x0, 0x34, 0x0, 0x0, 0xd0, 0x0,
    0x3, 0x80, 0x0, 0xb, 0x40, 0x0, 0xb, 0x95,
    0xf0, 0x6, 0xfe, 0x0,

    /* U+0044 "D" */
    0x7f, 0xfe, 0x1, 0xd5, 0x6f, 0x7, 0x0, 0xf,
    0x1c, 0x0, 0xd, 0x70, 0x0, 0x39, 0xc0, 0x0,
    0xe7, 0x0, 0x3, 0x5c, 0x0, 0x3c, 0x75, 0x5b,
    0xc1, 0xff, 0xf8, 0x0,

    /* U+0045 "E" */
    0x7f, 0xff, 0x1d, 0x55, 0x47, 0x0, 0x1, 0xc0,
    0x0, 0x7f, 0xfd, 0x1d, 0x0, 0x7, 0x0, 0x1,
    0xc0, 0x0, 0x75, 0x55, 0x1f, 0xff, 0xc0,

    /* U+0046 "F" */
    0x7f, 0xff, 0x75, 0x55, 0x70, 0x0, 0x70, 0x0,
    0x70, 0x0, 0x7f, 0xfd, 0x74, 0x0, 0x70, 0x0,
    0x70, 0x0, 0x70, 0x0,

    /* U+0047 "G" */
    0x1, 0xbf, 0x80, 0x2e, 0x57, 0xc2, 0xd0, 0x0,
    0xe, 0x0, 0x0, 0x34, 0x0, 0x0, 0xd0, 0x0,
    0xd3, 0x80, 0x3, 0x4b, 0x40, 0xd, 0xb, 0x95,
    0xf0, 0x6, 0xfe, 0x0,

    /* U+0048 "H" */
    0x70, 0x0, 0xe7, 0x0, 0xe, 0x70, 0x0, 0xe7,
    0x0, 0xe, 0x7f, 0xff, 0xe7, 0x55, 0x5e, 0x70,
    0x0, 0xe7, 0x0, 0xe, 0x70, 0x0, 0xe7, 0x0,
    0xe,

    /* U+0049 "I" */
    0x71, 0xc7, 0x1c, 0x71, 0xc7, 0x1c, 0x71, 0xc0,

    /* U+004A "J" */
    0xf, 0xfc, 0x5, 0x5c, 0x0, 0x1c, 0x0, 0x1c,
    0x0, 0x1c, 0x0, 0x1c, 0x0, 0x1c, 0x0, 0x1c,
    0x39, 0x7c, 0xb, 0xe0,

    /* U+004B "K" */
    0x70, 0x2, 0xc7, 0x0, 0xb0, 0x70, 0x2c, 0x7,
    0xf, 0x0, 0x73, 0xc0, 0x7, 0xff, 0x0, 0x7c,
    0x38, 0x7, 0x0, 0xe0, 0x70, 0x7, 0x47, 0x0,
    0x1d,

    /* U+004C "L" */
    0x70, 0x0, 0x70, 0x0, 0x70, 0x0, 0x70, 0x0,
    0x70, 0x0, 0x70, 0x0, 0x70, 0x0, 0x70, 0x0,
    0x75, 0x54, 0x7f, 0xff,

    /* U+004D "M" */
    0x70, 0x0, 0xe, 0x78, 0x0, 0x1e, 0x7d, 0x0,
    0x3e, 0x7b, 0x0, 0xba, 0x73, 0x81, 0xca, 0x71,
    0xc3, 0x8a, 0x70, 0xbb, 0xa, 0x70, 0x3d, 0xa,
    0x70, 0x18, 0xa, 0x70, 0x0, 0xa,

    /* U+004E "N" */
    0x74, 0x0, 0xe7, 0xc0, 0xe, 0x7f, 0x0, 0xe7,
    0x78, 0xe, 0x71, 0xe0, 0xe7, 0x7, 0x4e, 0x70,
    0x2d, 0xe7, 0x0, 0xbe, 0x70, 0x3, 0xe7, 0x0,
    0xe,

    /* U+004F "O" */
    0x1, 0xbf, 0x80, 0xb, 0x96, 0xf4, 0x2d, 0x0,
    0x2c, 0x38, 0x0, 0xe, 0x34, 0x0, 0xb, 0x34,
    0x0, 0xb, 0x38, 0x0, 0xe, 0x2d, 0x0, 0x2c,
    0xb, 0x95, 0xf4, 0x1, 0xbf, 0x80,

    /* U+0050 "P" */
    0x7f, 0xf8, 0x7, 0x55, 0xf4, 0x70, 0x3, 0xc7,
    0x0, 0x2c, 0x70, 0x2, 0xc7, 0x0, 0xb8, 0x7f,
    0xfd, 0x7, 0x40, 0x0, 0x70, 0x0, 0x7, 0x0,
    0x0,

    /* U+0051 "Q" */
    0x1, 0xbf, 0x80, 0x2, 0xe5, 0xbd, 0x2, 0xd0,
    0x2, 0xc0, 0xe0, 0x0, 0x38, 0x34, 0x0, 0xb,
    0xd, 0x0, 0x2, 0xc3, 0x80, 0x0, 0xe0, 0xb4,
    0x0, 0xb0, 0xb, 0x95, 0xf4, 0x0, 0x7f, 0xe0,
    0x0, 0x0, 0xb4, 0x20, 0x0, 0x7, 0xf8, 0x0,
    0x0, 0x0, 0x0,

    /* U+0052 "R" */
    0x7f, 0xf8, 0x7, 0x55, 0xf4, 0x70, 0x3, 0xc7,
    0x0, 0x2c, 0x70, 0x2, 0xc7, 0x0, 0x74, 0x7f,
    0xfd, 0x7, 0x41, 0xc0, 0x70, 0xb, 0x7, 0x0,
    0x3c,

    /* U+0053 "S" */
    0xb, 0xfd, 0xb, 0x56, 0xc3, 0x40, 0x0, 0xe0,
    0x0, 0x1f, 0x90, 0x0, 0x6f, 0x80, 0x0, 0x38,
    0x0, 0xe, 0x39, 0x5b, 0x46, 0xfe, 0x40,

    /* U+0054 "T" */
    0xff, 0xff, 0xd5, 0xb5, 0x40, 0x1c, 0x0, 0x7,
    0x0, 0x1, 0xc0, 0x0, 0x70, 0x0, 0x1c, 0x0,
    0x7, 0x0, 0x1, 0xc0, 0x0, 0x70, 0x0,

    /* U+0055 "U" */
    0xb0, 0x0, 0xdb, 0x0, 0xd, 0xb0, 0x0, 0xdb,
    0x0, 0xd, 0xb0, 0x0, 0xdb, 0x0, 0xd, 0x70,
    0x0, 0xd3, 0x80, 0x2c, 0x2e, 0x5b, 0x40, 0x7f,
    0xd0,

    /* U+0056 "V" */
    0x3c, 0x0, 0xd, 0x1d, 0x0, 0x2c, 0xf, 0x0,
    0x38, 0x7, 0x0, 0x70, 0x3, 0x80, 0xe0, 0x1,
    0xc1, 0xc0, 0x0, 0xe3, 0x80, 0x0, 0xb7, 0x0,
    0x0, 0x3f, 0x0, 0x0, 0x2d, 0x0,

    /* U+0057 "W" */
    0x70, 0x1, 0xd0, 0x3, 0xe, 0x0, 0xb8, 0x2,
    0xc2, 0xc0, 0x3b, 0x0, 0xe0, 0x70, 0x1c, 0xd0,
    0x70, 0xe, 0xe, 0x2c, 0x2c, 0x2, 0xc3, 0x7,
    0xd, 0x0, 0x36, 0xc0, 0xd7, 0x0, 0xe, 0xd0,
    0x2e, 0xc0, 0x2, 0xf0, 0x3, 0xd0, 0x0, 0x3c,
    0x0, 0xf0, 0x0,

    /* U+0058 "X" */
    0x38, 0x1, 0xd1, 0xd0, 0x34, 0xb, 0xe, 0x0,
    0x3e, 0xc0, 0x0, 0xf0, 0x0, 0x1f, 0x40, 0x3,
    0x9c, 0x0, 0xf0, 0xb0, 0x2c, 0x3, 0x87, 0x40,
    0xe,

    /* U+0059 "Y" */
    0x2c, 0x0, 0x34, 0x38, 0x2, 0xc0, 0x70, 0x1c,
    0x0, 0xf0, 0xe0, 0x0, 0xdb, 0x0, 0x2, 0xf4,
    0x0, 0x3, 0x80, 0x0, 0xe, 0x0, 0x0, 0x38,
    0x0, 0x0, 0xe0, 0x0,

    /* U+005A "Z" */
    0x7f, 0xff, 0xc1, 0x55, 0x7c, 0x0, 0xf, 0x0,
    0x2, 0xc0, 0x0, 0xb4, 0x0, 0x1d, 0x0, 0x3,
    0x80, 0x0, 0xe0, 0x0, 0x3d, 0x55, 0x47, 0xff,
    0xfd,

    /* U+005B "[" */
    0x7e, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
    0x70, 0x70, 0x70, 0x70, 0x70, 0x7e,

    /* U+005C "\\" */
    0x70, 0x0, 0xd0, 0x2, 0x80, 0x7, 0x0, 0xd,
    0x0, 0x28, 0x0, 0x70, 0x0, 0xd0, 0x2, 0x80,
    0x3, 0x0, 0xd, 0x0, 0x2c, 0x0, 0x30, 0x0,
    0xe0,

    /* U+005D "]" */
    0xbd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd,
    0xd, 0xd, 0xd, 0xd, 0xd, 0xbd,

    /* U+005E "^" */
    0x7, 0x0, 0x3a, 0x1, 0xcc, 0xd, 0x28, 0x30,
    0x32, 0x40, 0x90,

    /* U+005F "_" */
    0xff, 0xfd,

    /* U+0060 "`" */
    0x2c, 0x1, 0xc0,

    /* U+0061 "a" */
    0xb, 0xf4, 0x29, 0x6c, 0x0, 0xe, 0xb, 0xfe,
    0x38, 0xe, 0x30, 0xe, 0x38, 0x2e, 0xf, 0xea,

    /* U+0062 "b" */
    0xb0, 0x0, 0x2c, 0x0, 0xb, 0x0, 0x2, 0xdf,
    0xe0, 0xbd, 0x5e, 0x2c, 0x1, 0xdb, 0x0, 0x3a,
    0xc0, 0xe, 0xb0, 0x7, 0x6f, 0x57, 0x8b, 0x7f,
    0x80,

    /* U+0063 "c" */
    0x7, 0xf8, 0x7, 0x97, 0xc3, 0x80, 0x1, 0xc0,
    0x0, 0x70, 0x0, 0xe, 0x0, 0x1, 0xe5, 0xf0,
    0x1f, 0xe0,

    /* U+0064 "d" */
    0x0, 0x2, 0xc0, 0x0, 0xb0, 0x0, 0x2c, 0x1f,
    0xdb, 0x2e, 0x5f, 0xce, 0x0, 0xf7, 0x0, 0x2d,
    0xc0, 0xb, 0x34, 0x3, 0xcb, 0x87, 0xf0, 0x7f,
    0xac,

    /* U+0065 "e" */
    0x7, 0xf8, 0xb, 0x57, 0xc3, 0x40, 0x35, 0xff,
    0xfe, 0x74, 0x0, 0xe, 0x0, 0x1, 0xe5, 0xb0,
    0x1f, 0xe0,

    /* U+0066 "f" */
    0x7, 0xe0, 0xd0, 0x1c, 0xf, 0xfd, 0x2c, 0x1,
    0xc0, 0x1c, 0x1, 0xc0, 0x1c, 0x1, 0xc0, 0x1c,
    0x0,

    /* U+0067 "g" */
    0x7, 0xf9, 0xcb, 0x97, 0xf3, 0x80, 0x3d, 0xc0,
    0xb, 0x70, 0x2, 0xce, 0x0, 0xf2, 0xe5, 0xfc,
    0x1f, 0xeb, 0x0, 0x2, 0xcb, 0x56, 0xd0, 0xbf,
    0xd0,

    /* U+0068 "h" */
    0xb0, 0x0, 0xb0, 0x0, 0xb0, 0x0, 0xb7, 0xf4,
    0xbd, 0x6d, 0xb0, 0xf, 0xb0, 0xb, 0xb0, 0xb,
    0xb0, 0xb, 0xb0, 0xb, 0xb0, 0xb,

    /* U+0069 "i" */
    0xb2, 0x80, 0x2c, 0xb2, 0xcb, 0x2c, 0xb2, 0xcb,
    0x0,

    /* U+006A "j" */
    0x2, 0xc0, 0x28, 0x0, 0x0, 0x2c, 0x2, 0xc0,
    0x2c, 0x2, 0xc0, 0x2c, 0x2, 0xc0, 0x2c, 0x2,
    0xc0, 0x2c, 0x17, 0x83, 0xe0,

    /* U+006B "k" */
    0xb0, 0x0, 0x2c, 0x0, 0xb, 0x0, 0x2, 0xc0,
    0x74, 0xb0, 0x74, 0x2c, 0xb4, 0xb, 0xbc, 0x2,
    0xfb, 0x40, 0xb0, 0xb4, 0x2c, 0xf, 0xb, 0x0,
    0xf0,

    /* U+006C "l" */
    0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xb0,

    /* U+006D "m" */
    0xb7, 0xf4, 0xbf, 0xb, 0xd6, 0xfd, 0x7c, 0xb0,
    0xf, 0x0, 0xdb, 0x0, 0xe0, 0xe, 0xb0, 0xe,
    0x0, 0xeb, 0x0, 0xe0, 0xe, 0xb0, 0xe, 0x0,
    0xeb, 0x0, 0xe0, 0xe,

    /* U+006E "n" */
    0xb7, 0xf4, 0xbd, 0x6d, 0xb0, 0xb, 0xb0, 0xb,
    0xb0, 0xb, 0xb0, 0xb, 0xb0, 0xb, 0xb0, 0xb,

    /* U+006F "o" */
    0x7, 0xf8, 0x7, 0x97, 0xc3, 0x80, 0x39, 0xc0,
    0xb, 0x70, 0x2, 0xce, 0x0, 0xe1, 0xe5, 0xf0,
    0x1f, 0xe0,

    /* U+0070 "p" */
    0xb7, 0xf8, 0x2f, 0x47, 0x8b, 0x0, 0x76, 0xc0,
    0xe, 0xb0, 0x3, 0xac, 0x1, 0xdb, 0xd5, 0xe2,
    0xdf, 0xe0, 0xb0, 0x0, 0x2c, 0x0, 0xb, 0x0,
    0x0,

    /* U+0071 "q" */
    0x7, 0xfa, 0xcb, 0x97, 0xf3, 0x80, 0x3d, 0xc0,
    0xb, 0x70, 0x2, 0xce, 0x0, 0xf2, 0xe5, 0xfc,
    0x1f, 0xdb, 0x0, 0x2, 0xc0, 0x0, 0xb0, 0x0,
    0x2c,

    /* U+0072 "r" */
    0xb7, 0xaf, 0x9b, 0x2, 0xc0, 0xb0, 0x2c, 0xb,
    0x2, 0xc0,

    /* U+0073 "s" */
    0x1f, 0xf4, 0x39, 0x64, 0x70, 0x0, 0x3e, 0x40,
    0x6, 0xf8, 0x0, 0x1c, 0x75, 0x7c, 0x2f, 0xe0,

    /* U+0074 "t" */
    0x1c, 0x1, 0xc0, 0xff, 0xd2, 0xc0, 0x1c, 0x1,
    0xc0, 0x1c, 0x1, 0xc0, 0xe, 0x50, 0x7e,

    /* U+0075 "u" */
    0xa0, 0xb, 0xa0, 0xb, 0xa0, 0xb, 0xa0, 0xb,
    0xb0, 0xb, 0xb0, 0xf, 0x39, 0x7f, 0x1f, 0xeb,

    /* U+0076 "v" */
    0x38, 0x0, 0xc1, 0xc0, 0x2c, 0xe, 0x3, 0x40,
    0xb0, 0xb0, 0x3, 0x4d, 0x0, 0x2d, 0xc0, 0x0,
    0xf8, 0x0, 0xf, 0x0,

    /* U+0077 "w" */
    0xa0, 0xe, 0x0, 0xc7, 0x1, 0xf0, 0x1c, 0x34,
    0x3b, 0x43, 0x42, 0x83, 0x28, 0x30, 0xc, 0xb1,
    0xca, 0x0, 0xdd, 0xd, 0xd0, 0xb, 0xc0, 0xbc,
    0x0, 0x38, 0x3, 0x80,

    /* U+0078 "x" */
    0x74, 0xe, 0x7, 0xe, 0x0, 0xbb, 0x0, 0xf,
    0x0, 0x3, 0xd0, 0x3, 0x9c, 0x2, 0xc2, 0xc1,
    0xc0, 0x38,

    /* U+0079 "y" */
    0x38, 0x0, 0xc1, 0xc0, 0x28, 0xe, 0x3, 0x40,
    0x70, 0xb0, 0x3, 0x4d, 0x0, 0x2d, 0xc0, 0x0,
    0xf8, 0x0, 0xb, 0x0, 0x0, 0xe0, 0x2, 0x6c,
    0x0, 0x2f, 0x40, 0x0,

    /* U+007A "z" */
    0x7f, 0xfc, 0x0, 0x78, 0x0, 0xb0, 0x2, 0xc0,
    0x7, 0x40, 0xd, 0x0, 0x3c, 0x0, 0x7f, 0xfd,

    /* U+007B "{" */
    0x7, 0xc3, 0xc0, 0xe0, 0x38, 0xe, 0x3, 0x43,
    0xc0, 0x74, 0xd, 0x3, 0x80, 0xe0, 0x38, 0xf,
    0x0, 0xf0,

    /* U+007C "|" */
    0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,

    /* U+007D "}" */
    0xb4, 0xb, 0x0, 0xc0, 0x30, 0xc, 0x3, 0x40,
    0xf4, 0x34, 0xc, 0x3, 0x0, 0xc0, 0x30, 0x2c,
    0x2d, 0x0,

    /* U+007E "~" */
    0x1f, 0x46, 0x30, 0xbd
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 65, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 64, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 5, .adv_w = 94, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 12, .adv_w = 169, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 40, .adv_w = 149, .box_w = 9, .box_h = 15, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 74, .adv_w = 202, .box_w = 13, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 165, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 138, .adv_w = 50, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 142, .adv_w = 81, .box_w = 4, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 156, .adv_w = 81, .box_w = 4, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 170, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 179, .adv_w = 140, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 192, .adv_w = 54, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 196, .adv_w = 92, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 200, .adv_w = 54, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 203, .adv_w = 84, .box_w = 7, .box_h = 14, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 228, .adv_w = 160, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 253, .adv_w = 89, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 263, .adv_w = 138, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 286, .adv_w = 137, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 161, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 331, .adv_w = 138, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 354, .adv_w = 148, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 377, .adv_w = 144, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 400, .adv_w = 155, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 423, .adv_w = 148, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 446, .adv_w = 54, .box_w = 3, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 452, .adv_w = 54, .box_w = 3, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 461, .adv_w = 140, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 475, .adv_w = 140, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 486, .adv_w = 140, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 500, .adv_w = 138, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 520, .adv_w = 248, .box_w = 15, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 569, .adv_w = 176, .box_w = 12, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 599, .adv_w = 182, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 624, .adv_w = 174, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 652, .adv_w = 198, .box_w = 11, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 680, .adv_w = 161, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 703, .adv_w = 152, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 723, .adv_w = 185, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 751, .adv_w = 195, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 776, .adv_w = 74, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 784, .adv_w = 123, .box_w = 8, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 804, .adv_w = 173, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 829, .adv_w = 143, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 849, .adv_w = 229, .box_w = 12, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 879, .adv_w = 195, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 904, .adv_w = 202, .box_w = 12, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 934, .adv_w = 173, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 959, .adv_w = 202, .box_w = 13, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1002, .adv_w = 174, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1027, .adv_w = 149, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1050, .adv_w = 141, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1073, .adv_w = 190, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1098, .adv_w = 171, .box_w = 12, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1128, .adv_w = 270, .box_w = 17, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1171, .adv_w = 162, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1196, .adv_w = 155, .box_w = 11, .box_h = 10, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1224, .adv_w = 158, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1249, .adv_w = 80, .box_w = 4, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1263, .adv_w = 84, .box_w = 7, .box_h = 14, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 1288, .adv_w = 80, .box_w = 4, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1302, .adv_w = 140, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 1313, .adv_w = 120, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1315, .adv_w = 144, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 9},
    {.bitmap_index = 1318, .adv_w = 144, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1334, .adv_w = 164, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1359, .adv_w = 137, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1377, .adv_w = 164, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1402, .adv_w = 147, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1420, .adv_w = 85, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1437, .adv_w = 166, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1462, .adv_w = 163, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1484, .adv_w = 67, .box_w = 3, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1493, .adv_w = 68, .box_w = 6, .box_h = 14, .ofs_x = -2, .ofs_y = -3},
    {.bitmap_index = 1514, .adv_w = 148, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1539, .adv_w = 67, .box_w = 2, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1545, .adv_w = 254, .box_w = 14, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1573, .adv_w = 163, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1589, .adv_w = 152, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1607, .adv_w = 164, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1632, .adv_w = 164, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1657, .adv_w = 98, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1667, .adv_w = 120, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1683, .adv_w = 99, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1698, .adv_w = 162, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1714, .adv_w = 134, .box_w = 10, .box_h = 8, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1734, .adv_w = 216, .box_w = 14, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1762, .adv_w = 132, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1780, .adv_w = 134, .box_w = 10, .box_h = 11, .ofs_x = -1, .ofs_y = -3},
    {.bitmap_index = 1808, .adv_w = 125, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1824, .adv_w = 84, .box_w = 5, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1842, .adv_w = 72, .box_w = 2, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1849, .adv_w = 84, .box_w = 5, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1867, .adv_w = 140, .box_w = 8, .box_h = 2, .ofs_x = 0, .ofs_y = 4}
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
    58, 59, 58, 60, 41, 0, 0, 9
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
    36, 0, 37, 38, 39, 39, 39, 0,
    39, 38, 40, 41, 38, 38, 42, 42,
    39, 42, 39, 42, 43, 44, 45, 46,
    46, 47, 46, 48, 0, 0, 35, 9
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    0, 1, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 2, 0, 0, 0,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 11, 0, 6, -5, 0, 0, 0,
    0, -13, -14, 2, 11, 5, 4, -10,
    2, 12, 1, 10, 2, 8, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 14, 2, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -7, 0, 0, 0, 0, 0, -5,
    4, 5, 0, 0, -2, 0, -2, 2,
    0, -2, 0, -2, -1, -5, 0, 0,
    0, 0, -2, 0, 0, -3, -4, 0,
    0, -2, 0, -5, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -2, -2, 0,
    0, -6, 0, -29, 0, 0, -5, 0,
    5, 7, 0, 0, -5, 2, 2, 8,
    5, -4, 5, 0, 0, -14, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -9, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -3, -12, 0, -10, -2, 0, 0, 0,
    0, 0, 9, 0, -7, -2, -1, 1,
    0, -4, 0, 0, -2, -18, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -19, -2, 9, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 8, 0, 2, 0, 0, -5,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 9, 2, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -9, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    2, 5, 2, 7, -2, 0, 0, 5,
    -2, -8, -33, 2, 6, 5, 0, -3,
    0, 9, 0, 8, 0, 8, 0, -22,
    0, -3, 7, 0, 8, -2, 5, 2,
    0, 0, 1, -2, 0, 0, -4, 19,
    0, 19, 0, 7, 0, 10, 3, 4,
    0, 0, 0, -9, 0, 0, 0, 0,
    1, -2, 0, 2, -4, -3, -5, 2,
    0, -2, 0, 0, 0, -10, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -16, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, -13, 0, -15, 0, 0, 0, 0,
    -2, 0, 24, -3, -3, 2, 2, -2,
    0, -3, 2, 0, 0, -13, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -23, 0, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 14, 0, 0, -9, 0, 8, 0,
    -16, -23, -16, -5, 7, 0, 0, -16,
    0, 3, -6, 0, -4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 6, 7, -29, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 0, 0, 0, 0, 0, 2,
    2, -3, -5, 0, -1, -1, -2, 0,
    0, -2, 0, 0, 0, -5, 0, -2,
    0, -6, -5, 0, -6, -8, -8, -5,
    0, -5, 0, -5, 0, 0, 0, 0,
    -2, 0, 0, 2, 0, 2, -2, 0,
    0, 0, 0, 2, -2, 0, 0, 0,
    -2, 2, 2, -1, 0, 0, 0, -5,
    0, -1, 0, 0, 0, 0, 0, 1,
    0, 3, -2, 0, -3, 0, -4, 0,
    0, -2, 0, 7, 0, 0, -2, 0,
    0, 0, 0, 0, -1, 1, -2, -2,
    0, -2, 0, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, -1, 0,
    -2, -3, 0, 0, 0, 0, 0, 1,
    0, 0, -2, 0, -2, -2, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, -2, -3, 0,
    0, -7, -2, -7, 5, 0, 0, -5,
    2, 5, 6, 0, -6, -1, -3, 0,
    -1, -11, 2, -2, 2, -13, 2, 0,
    0, 1, -12, 0, -13, -2, -21, -2,
    0, -12, 0, 5, 7, 0, 3, 0,
    0, 0, 0, 0, 0, -4, -3, 0,
    0, 0, 0, -2, 0, 0, 0, -2,
    0, 0, 0, 0, 0, -1, -1, 0,
    -1, -3, 0, 0, 0, 0, 0, 0,
    0, -2, -2, 0, -2, -3, -2, 0,
    0, -2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -2, -2, 0,
    0, -2, 0, -5, 2, 0, 0, -3,
    1, 2, 2, 0, 0, 0, 0, 0,
    0, -2, 0, 0, 0, 0, 0, 2,
    0, 0, -2, 0, -2, -2, -3, 0,
    0, 0, 0, 0, 0, 0, 2, 0,
    -2, 0, 0, 0, 0, -3, -4, 0,
    0, 7, -2, 1, -8, 0, 0, 6,
    -12, -12, -10, -5, 2, 0, -2, -16,
    -4, 0, -4, 0, -5, 4, -4, -15,
    0, -6, 0, 0, 1, -1, 2, -2,
    0, 2, 0, -7, -9, 0, -12, -6,
    -5, -6, -7, -3, -6, 0, -5, -6,
    0, 1, 0, -2, 0, 0, 0, 2,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -2, 0, -1,
    0, -1, -2, 0, -4, -5, -5, -1,
    0, -7, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, 0, 1, -1, 0,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 12, 0, 0, 0, 0, 0,
    0, 2, 0, 0, 0, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -4, 0, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, -5, 0, 0, 0,
    0, -12, -7, 0, 0, 0, -4, -12,
    0, 0, -2, 2, 0, -6, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -4, 0, 0, -5, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -4, 0, 0, 0, 0, 3, 0,
    2, -5, -5, 0, -2, -2, -3, 0,
    0, 0, 0, 0, 0, -7, 0, -2,
    0, -4, -2, 0, -5, -6, -7, -2,
    0, -5, 0, -7, 0, 0, 0, 0,
    19, 0, 0, 1, 0, 0, -3, 0,
    0, -10, 0, 0, 0, 0, 0, -22,
    -4, 8, 7, -2, -10, 0, 2, -4,
    0, -12, -1, -3, 2, -17, -2, 3,
    0, 4, -8, -4, -9, -8, -10, 0,
    0, -14, 0, 14, 0, 0, -1, 0,
    0, 0, -1, -1, -2, -6, -8, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, 0, -1, -2, -4, 0,
    0, -5, 0, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, -5, 0, 0, 5,
    -1, 3, 0, -5, 2, -2, -1, -6,
    -2, 0, -3, -2, -2, 0, -4, -4,
    0, 0, -2, -1, -2, -4, -3, 0,
    0, -2, 0, 2, -2, 0, -5, 0,
    0, 0, -5, 0, -4, 0, -4, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    -5, 2, 0, -3, 0, -2, -3, -7,
    -2, -2, -2, -1, -2, -3, -1, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, 0, 0, 3, -2, 0, -2, 0,
    0, 0, -2, -3, -2, -2, -3, -2,
    2, 10, -1, 0, -6, 0, -2, 5,
    0, -2, -10, -3, 4, 0, 0, -11,
    -4, 2, -4, 2, 0, -2, -2, -8,
    0, -4, 1, 0, 0, -4, 0, 0,
    0, 2, 2, -5, -5, 0, -4, -2,
    -4, -2, -2, 0, -4, 1, -5, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, -2, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -4,
    0, 0, -3, 0, 0, -2, -2, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, -2, 0,
    0, 0, -4, 0, -5, 0, 0, 0,
    -8, 0, 2, -5, 5, 0, -2, -11,
    0, 0, -5, -2, 0, -10, -6, -7,
    0, 0, -10, -2, -10, -9, -12, 0,
    -6, 0, 2, 16, -3, 0, -6, -2,
    -1, -2, -4, -6, -4, -9, -10, -6,
    0, 0, -2, 0, 1, 0, 0, -17,
    -2, 7, 5, -5, -9, 0, 1, -7,
    0, -12, -2, -2, 5, -22, -3, 1,
    0, 0, -16, -3, -12, -2, -18, 0,
    0, -17, 0, 14, 1, 0, -2, 0,
    0, 0, 0, -1, -2, -9, -2, 0,
    0, 0, 0, 0, -8, 0, -2, 0,
    -1, -7, -11, 0, 0, -1, -4, -7,
    -2, 0, -2, 0, 0, 0, 0, -11,
    -2, -8, -8, -2, -4, -6, -2, -4,
    0, -5, -2, -8, -4, 0, -3, -5,
    -2, -5, 0, 1, 0, -2, -8, 0,
    0, -4, 0, 0, 0, 0, 3, 0,
    2, -5, 10, 0, -2, -2, -3, 0,
    0, 0, 0, 0, 0, -7, 0, -2,
    0, -4, -2, 0, -5, -6, -7, -2,
    0, -5, 2, 10, 0, 0, 0, 0,
    19, 0, 0, 1, 0, 0, -3, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, -5,
    0, 0, 0, 0, 0, -1, 0, 0,
    0, -2, -2, 0, 0, -5, -2, 0,
    0, -5, 0, 4, -1, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 0,
    5, 2, -2, 0, -8, -4, 0, 7,
    -8, -8, -5, -5, 10, 4, 2, -21,
    -2, 5, -2, 0, -2, 3, -2, -8,
    0, -2, 2, -3, -2, -7, -2, 0,
    0, 7, 5, 0, -7, 0, -13, -3,
    7, -3, -9, 1, -3, -8, -8, -2,
    2, 0, -4, 0, -6, 0, 2, 8,
    -6, -9, -10, -6, 7, 0, 1, -18,
    -2, 2, -4, -2, -6, 0, -5, -9,
    -4, -4, -2, 0, 0, -6, -5, -2,
    0, 7, 6, -2, -13, 0, -13, -3,
    0, -8, -14, -1, -8, -4, -8, -7,
    0, 0, -3, 0, -5, -2, 0, -2,
    -4, 0, 4, -8, 2, 0, 0, -13,
    0, -2, -5, -4, -2, -7, -6, -8,
    -6, 0, -7, -2, -6, -5, -7, -2,
    0, 0, 1, 11, -4, 0, -7, -2,
    0, -2, -5, -6, -6, -7, -9, -3,
    5, 0, -4, 0, -12, -3, 1, 5,
    -8, -9, -5, -8, 8, -2, 1, -22,
    -4, 5, -5, -4, -9, 0, -7, -10,
    -3, -2, -2, -2, -5, -7, -1, 0,
    0, 7, 7, -2, -16, 0, -14, -6,
    6, -9, -16, -5, -8, -10, -12, -8,
    0, 0, 0, 0, -3, 0, 0, 2,
    -3, 5, 2, -5, 5, 0, 0, -7,
    -1, 0, -1, 0, 1, 1, -2, 0,
    0, 0, 0, 0, 0, -2, 0, 0,
    0, 0, 2, 7, 0, 0, -3, 0,
    0, 0, 0, -2, -2, -3, 0, 0,
    1, 2, 0, 0, 0, 0, 2, 0,
    -2, 0, 9, 0, 4, 1, 1, -3,
    0, 5, 0, 0, 0, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 7, 0, 7, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -14, 0, -2, 4, 0, 7, 0,
    0, 24, 3, -5, -5, 2, 2, -2,
    1, -12, 0, 0, 12, -14, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -16, 9, 34, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 0, 0, -5, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, -6, 0, 0, 1, 0,
    0, 2, 31, -5, -2, 8, 6, -6,
    2, 0, 0, 2, 2, -3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -31, 7, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -7, 0, 0, 0, -6,
    0, 0, 0, 0, -5, -1, 0, 0,
    0, -5, 0, -3, 0, -11, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -16, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, -2, 0, 0,
    0, -4, 0, -6, 0, 0, 0, -4,
    2, -3, 0, 0, -6, -2, -6, 0,
    0, -6, 0, -2, 0, -11, 0, -3,
    0, 0, -19, -5, -10, -3, -9, 0,
    0, -16, 0, -6, -1, 0, 0, 0,
    0, 0, 0, 0, 0, -4, -4, -2,
    0, 0, 0, 0, -5, 0, -5, 3,
    -3, 5, 0, -2, -6, -2, -4, -5,
    0, -3, -1, -2, 2, -6, -1, 0,
    0, 0, -21, -2, -3, 0, -5, 0,
    -2, -11, -2, 0, 0, -2, -2, 0,
    0, 0, 0, 2, 0, -2, -4, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 3, 0, 0, 0, 0,
    0, -5, 0, -2, 0, 0, 0, -5,
    2, 0, 0, 0, -6, -2, -5, 0,
    0, -7, 0, -2, 0, -11, 0, 0,
    0, 0, -23, 0, -5, -9, -12, 0,
    0, -16, 0, -2, -4, 0, 0, 0,
    0, 0, 0, 0, 0, -2, -4, -1,
    1, 0, 0, 4, -3, 0, 7, 12,
    -2, -2, -7, 3, 12, 4, 5, -6,
    3, 10, 3, 7, 5, 6, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 15, 11, -4, -2, 0, -2, 19,
    10, 19, 0, 0, 0, 2, 0, 0,
    0, 0, -4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 3, 0, 0,
    0, 0, -20, -3, -2, -10, -12, 0,
    0, -16, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 3, 0, 0,
    0, 0, -20, -3, -2, -10, -12, 0,
    0, -10, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    -6, 2, 0, -2, 2, 4, 2, -7,
    0, 0, -2, 2, 0, 2, 0, 0,
    0, 0, -6, 0, -2, -2, -5, 0,
    -2, -10, 0, 15, -2, 0, -5, -2,
    0, -2, -4, 0, -2, -7, -5, -3,
    0, 0, -4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 3, 0, 0,
    0, 0, -20, -3, -2, -10, -12, 0,
    0, -16, 0, 0, 0, 0, 0, 0,
    12, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 0, -8, -3, -2, 7,
    -2, -2, -10, 1, -1, 1, -2, -6,
    0, 5, 0, 2, 1, 2, -6, -10,
    -3, 0, -9, -5, -6, -10, -9, 0,
    -4, -5, -3, -3, -2, -2, -3, -2,
    0, -2, -1, 4, 0, 4, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, -6, 0, -1, 0, -4, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -14, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -2, -2, 0,
    0, 0, 0, 0, -2, 0, 0, -4,
    -2, 2, 0, -4, -5, -2, 0, -7,
    -2, -5, -2, -3, 0, -4, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -16, 0, 8, 0, 0, -4, 0,
    0, 0, 0, -3, 0, -2, 0, 0,
    0, 0, -2, 0, -6, 0, 0, 10,
    -3, -8, -7, 2, 3, 3, 0, -7,
    2, 4, 2, 7, 2, 8, -2, -6,
    0, 0, -10, 0, 0, -7, -6, 0,
    0, -5, 0, -3, -4, 0, -4, 0,
    -4, 0, -2, 4, 0, -2, -7, -2,
    0, 0, -2, 0, -5, 0, 0, 3,
    -6, 0, 2, -2, 2, 0, 0, -8,
    0, -2, -1, 0, -2, 3, -2, 0,
    0, 0, -10, -3, -5, 0, -7, 0,
    0, -11, 0, 9, -2, 0, -4, 0,
    1, 0, -2, 0, -2, -7, 0, -2,
    0, 0, 0, 0, -2, 0, 0, 2,
    -3, 1, 0, 0, -3, -2, 0, -3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -15, 0, 5, 0, 0, -2, 0,
    0, 0, 0, 0, 0, -2, -2, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 60,
    .right_class_cnt     = 48,
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
    .cmap_num = 1,
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
const lv_font_t MontserratMedium15Trae = {
#else
lv_font_t MontserratMedium15Trae = {
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



#endif /*#if MONTSERRATMEDIUM15TRAE*/
