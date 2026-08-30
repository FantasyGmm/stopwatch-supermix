/*******************************************************************************
 * Size: 12 px
 * Bpp: 2
 * Opts: --size 12 --bpp 2 --format lvgl --font components/lvgl/tests/src/test_files/fonts/Montserrat-Bold.ttf --range 0x20-0x7E --symbols • --no-compress --lv-include lvgl.h --lv-font-name MontserratBold12Trae -o main/assets/fonts/MontserratBold12Trae.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef MONTSERRATBOLD12TRAE
#define MONTSERRATBOLD12TRAE 1
#endif

#if MONTSERRATBOLD12TRAE

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x7d, 0xf3, 0x8e, 0x38, 0xd0, 0x1f, 0x38,

    /* U+0022 "\"" */
    0x73, 0x9c, 0xe7, 0x38, 0x89,

    /* U+0023 "#" */
    0x7, 0x1c, 0x2, 0xc7, 0x7, 0xff, 0xf4, 0x7d,
    0xf4, 0xd, 0x38, 0x3, 0x4e, 0xf, 0xff, 0xf0,
    0xb5, 0xd4, 0x1c, 0x70, 0x0,

    /* U+0024 "$" */
    0x1, 0x40, 0x2, 0x80, 0x1f, 0xf4, 0x3f, 0xf8,
    0x7a, 0x80, 0x7f, 0x80, 0x2f, 0xf4, 0x3, 0xfc,
    0x12, 0xad, 0x7f, 0xfc, 0x2f, 0xe0, 0x2, 0x80,
    0x1, 0x40,

    /* U+0025 "%" */
    0x2e, 0x3, 0x81, 0xcd, 0x2c, 0xa, 0x24, 0xd0,
    0x28, 0xde, 0x0, 0x3e, 0x73, 0xd0, 0x3, 0x79,
    0xc0, 0x28, 0xd3, 0x1, 0xc3, 0x4c, 0xd, 0x3,
    0xd0,

    /* U+0026 "&" */
    0xb, 0xe0, 0x7, 0xad, 0x2, 0xc7, 0x40, 0x3f,
    0x80, 0x1f, 0xc1, 0x1e, 0x3f, 0xcb, 0x43, 0xf1,
    0xfa, 0xfc, 0x1f, 0xe2, 0x0,

    /* U+0027 "'" */
    0x71, 0xc7, 0x8,

    /* U+0028 "(" */
    0xf, 0x7, 0x82, 0xc0, 0xf0, 0x3c, 0xe, 0x3,
    0x80, 0xf0, 0x3c, 0xb, 0x1, 0xe0, 0x3c,

    /* U+0029 ")" */
    0xb0, 0x38, 0x3c, 0x2c, 0x2d, 0x1d, 0x1d, 0x2d,
    0x2c, 0x3c, 0x38, 0xb0,

    /* U+002A "*" */
    0xc, 0xb, 0xfc, 0x3f, 0xb, 0xfc, 0xc, 0x0,
    0x0,

    /* U+002B "+" */
    0x3, 0x40, 0xd, 0x2, 0xfe, 0x5f, 0xfd, 0x3,
    0x40, 0xd, 0x0,

    /* U+002C "," */
    0x12, 0xe7, 0x8d, 0x70,

    /* U+002D "-" */
    0x2a, 0x1f, 0xc0,

    /* U+002E "." */
    0x12, 0xe7, 0x40,

    /* U+002F "/" */
    0x0, 0xb0, 0x3, 0x80, 0x1d, 0x0, 0xb0, 0x3,
    0x80, 0x1d, 0x0, 0xb0, 0x3, 0xc0, 0xd, 0x0,
    0xb0, 0x3, 0xc0, 0xd, 0x0,

    /* U+0030 "0" */
    0xb, 0xe0, 0x2f, 0xfc, 0x3c, 0x2d, 0x78, 0x1e,
    0xb4, 0x1e, 0x78, 0x1e, 0x3c, 0x2d, 0x2f, 0xfc,
    0xb, 0xe0,

    /* U+0031 "1" */
    0xfe, 0xbe, 0xe, 0xe, 0xe, 0xe, 0xe, 0xe,
    0xe,

    /* U+0032 "2" */
    0x2f, 0xd2, 0xff, 0xc1, 0xb, 0x40, 0x2d, 0x1,
    0xf0, 0x1f, 0x1, 0xf0, 0x1f, 0xea, 0xbf, 0xfc,

    /* U+0033 "3" */
    0x2f, 0xfd, 0x1a, 0xbc, 0x0, 0xf4, 0x2, 0xf0,
    0x3, 0xfc, 0x0, 0x2e, 0x0, 0x1e, 0x3f, 0xfd,
    0x1b, 0xf4,

    /* U+0034 "4" */
    0x0, 0xf0, 0x0, 0xb4, 0x0, 0x78, 0x0, 0x3c,
    0x50, 0x3d, 0x3c, 0x1f, 0xaf, 0x8b, 0xff, 0xf0,
    0x0, 0xf0, 0x0, 0x3c, 0x0,

    /* U+0035 "5" */
    0x3f, 0xf4, 0xfa, 0x83, 0x80, 0xf, 0xa0, 0x3f,
    0xf4, 0x1, 0xe0, 0x7, 0xef, 0xfd, 0x2f, 0xd0,

    /* U+0036 "6" */
    0x7, 0xf8, 0x2f, 0xf8, 0x3c, 0x0, 0x7f, 0xf0,
    0xbe, 0xbc, 0x7c, 0x2d, 0x7c, 0x2d, 0x3e, 0xbc,
    0xb, 0xe0,

    /* U+0037 "7" */
    0xbf, 0xfc, 0xbe, 0xfc, 0xb0, 0xb8, 0x0, 0xf0,
    0x1, 0xf0, 0x2, 0xd0, 0x3, 0xc0, 0xb, 0x80,
    0xf, 0x0,

    /* U+0038 "8" */
    0xb, 0xe0, 0x3e, 0xbc, 0x38, 0x3c, 0x3c, 0x3c,
    0x3f, 0xf8, 0x7d, 0x7d, 0xb4, 0x2d, 0x7e, 0xbc,
    0x1f, 0xf4,

    /* U+0039 "9" */
    0x1f, 0xd0, 0x7e, 0xf4, 0xb0, 0x3c, 0xb4, 0x7c,
    0x3f, 0xfc, 0x6, 0x3c, 0x0, 0x7c, 0x3b, 0xf0,
    0x3f, 0x80,

    /* U+003A ":" */
    0x76, 0xe1, 0x0, 0x12, 0xe7, 0x40,

    /* U+003B ";" */
    0x76, 0xe1, 0x0, 0x12, 0xe7, 0x8d, 0x70,

    /* U+003C "<" */
    0x0, 0x10, 0x7, 0xd2, 0xf9, 0x1f, 0x0, 0x2f,
    0x90, 0x7, 0xd0, 0x1, 0x40,

    /* U+003D "=" */
    0x7f, 0xf4, 0xaa, 0x90, 0x0, 0x1f, 0xfd, 0x2a,
    0xa4,

    /* U+003E ">" */
    0x10, 0x0, 0xf8, 0x0, 0xbe, 0x0, 0x3d, 0xb,
    0xe0, 0xf8, 0x1, 0x0, 0x0,

    /* U+003F "?" */
    0xb, 0xf4, 0x2f, 0xfc, 0x4, 0x2d, 0x0, 0x3c,
    0x0, 0xf4, 0x1, 0xd0, 0x0, 0x40, 0x2, 0xe0,
    0x1, 0xd0,

    /* U+0040 "@" */
    0x1, 0xff, 0x80, 0xf, 0x95, 0xf4, 0x2c, 0xbe,
    0xec, 0x36, 0xe7, 0xda, 0x73, 0xc3, 0xd7, 0xb3,
    0x82, 0xd7, 0x73, 0xc3, 0xd7, 0x35, 0xeb, 0xee,
    0x2c, 0xbd, 0xb8, 0xf, 0x95, 0x40, 0x1, 0xff,
    0x80,

    /* U+0041 "A" */
    0x0, 0xbc, 0x0, 0x3, 0xf0, 0x0, 0x1e, 0xe0,
    0x0, 0xf3, 0xc0, 0x3, 0x87, 0x40, 0x2f, 0xaf,
    0x0, 0xff, 0xfd, 0x7, 0x40, 0x78, 0x3c, 0x0,
    0xf0,

    /* U+0042 "B" */
    0xff, 0xf4, 0xfa, 0xbc, 0xf0, 0x2c, 0xfa, 0xbc,
    0xff, 0xf8, 0xf0, 0x2d, 0xf0, 0x1e, 0xfa, 0xbd,
    0xff, 0xf8,

    /* U+0043 "C" */
    0x6, 0xfd, 0x7, 0xff, 0xc3, 0xd0, 0x11, 0xe0,
    0x0, 0xb4, 0x0, 0x1e, 0x0, 0x3, 0xd0, 0x10,
    0x7f, 0xfc, 0x2, 0xfd, 0x0,

    /* U+0044 "D" */
    0xff, 0xe0, 0x3e, 0xbf, 0x4f, 0x1, 0xf3, 0xc0,
    0x2d, 0xf0, 0xb, 0x7c, 0x2, 0xdf, 0x1, 0xf3,
    0xeb, 0xf4, 0xff, 0xe0, 0x0,

    /* U+0045 "E" */
    0xff, 0xf7, 0xea, 0x8f, 0x0, 0x3e, 0xa8, 0xff,
    0xf3, 0xc0, 0xf, 0x0, 0x3e, 0xa9, 0xff, 0xf8,

    /* U+0046 "F" */
    0xff, 0xf7, 0xea, 0x8f, 0x0, 0x3c, 0x0, 0xff,
    0xf3, 0xea, 0x8f, 0x0, 0x3c, 0x0, 0xf0, 0x0,

    /* U+0047 "G" */
    0x6, 0xfd, 0x7, 0xff, 0xd3, 0xd0, 0x21, 0xe0,
    0x0, 0xb4, 0x2, 0x5e, 0x1, 0xd3, 0xd0, 0x74,
    0x7f, 0xfe, 0x2, 0xfd, 0x0,

    /* U+0048 "H" */
    0xf0, 0x1e, 0xf0, 0x1e, 0xf0, 0x1e, 0xff, 0xfe,
    0xff, 0xfe, 0xf0, 0x1e, 0xf0, 0x1e, 0xf0, 0x1e,
    0xf0, 0x1e,

    /* U+0049 "I" */
    0xff, 0xff, 0xff, 0xff, 0xf0,

    /* U+004A "J" */
    0x1f, 0xf8, 0x2b, 0xe0, 0x7, 0x80, 0x1e, 0x0,
    0x78, 0x1, 0xe0, 0x7, 0x4f, 0xfc, 0x1f, 0xd0,

    /* U+004B "K" */
    0xf0, 0x3d, 0x3c, 0x2e, 0xf, 0x2e, 0x3, 0xdf,
    0x0, 0xff, 0xc0, 0x3f, 0xf4, 0xf, 0xf, 0x3,
    0xc1, 0xf0, 0xf0, 0x2e, 0x0,

    /* U+004C "L" */
    0xf0, 0x3, 0xc0, 0xf, 0x0, 0x3c, 0x0, 0xf0,
    0x3, 0xc0, 0xf, 0x0, 0x3e, 0xa8, 0xff, 0xf0,

    /* U+004D "M" */
    0xf0, 0x1, 0xdf, 0x40, 0x3d, 0xfc, 0xb, 0xdf,
    0xe0, 0xfd, 0xfb, 0x2e, 0xdf, 0x3f, 0x9d, 0xf2,
    0xf1, 0xdf, 0xd, 0x1d, 0xf0, 0x1, 0xd0,

    /* U+004E "N" */
    0xf0, 0xe, 0xf8, 0xe, 0xfe, 0xe, 0xff, 0x4e,
    0xf7, 0xde, 0xf2, 0xfe, 0xf0, 0xfe, 0xf0, 0x3e,
    0xf0, 0x1e,

    /* U+004F "O" */
    0x2, 0xfd, 0x1, 0xff, 0xf4, 0x3d, 0x7, 0xc7,
    0x80, 0x2e, 0xb4, 0x1, 0xe7, 0x80, 0x2e, 0x3d,
    0x3, 0xc1, 0xff, 0xf8, 0x6, 0xfd, 0x0,

    /* U+0050 "P" */
    0xff, 0xe0, 0xfa, 0xf8, 0xf0, 0x3c, 0xf0, 0x2c,
    0xf0, 0x3c, 0xff, 0xf8, 0xfa, 0x90, 0xf0, 0x0,
    0xf0, 0x0,

    /* U+0051 "Q" */
    0x2, 0xfd, 0x0, 0x7f, 0xfe, 0x3, 0xd0, 0x7c,
    0x1e, 0x0, 0xb8, 0xb4, 0x1, 0xe1, 0xe0, 0xb,
    0x83, 0xd0, 0x3c, 0x7, 0xff, 0xe0, 0x6, 0xfd,
    0x0, 0x0, 0xfb, 0xc0, 0x0, 0xbd, 0x0,

    /* U+0052 "R" */
    0xff, 0xe0, 0xfa, 0xf8, 0xf0, 0x3c, 0xf0, 0x2c,
    0xf0, 0x7c, 0xff, 0xf8, 0xfb, 0xf0, 0xf0, 0xb4,
    0xf0, 0x3c,

    /* U+0053 "S" */
    0x1f, 0xf4, 0x3f, 0xb8, 0x78, 0x0, 0x7d, 0x0,
    0x2f, 0xf4, 0x1, 0xbc, 0x10, 0x2d, 0x7e, 0xfc,
    0x2f, 0xe0,

    /* U+0054 "T" */
    0xff, 0xfd, 0xaf, 0xe9, 0x7, 0x80, 0x7, 0x80,
    0x7, 0x80, 0x7, 0x80, 0x7, 0x80, 0x7, 0x80,
    0x7, 0x80,

    /* U+0055 "U" */
    0x3c, 0x7, 0x8f, 0x1, 0xe3, 0xc0, 0x78, 0xf0,
    0x1e, 0x3c, 0x7, 0x8f, 0x1, 0xd3, 0xd0, 0xf4,
    0x7f, 0xfc, 0x7, 0xf8, 0x0,

    /* U+0056 "V" */
    0x3c, 0x0, 0xf0, 0xb8, 0x7, 0x40, 0xf0, 0x3c,
    0x2, 0xd1, 0xe0, 0x7, 0xcb, 0x40, 0xf, 0x7c,
    0x0, 0x1f, 0xd0, 0x0, 0x3f, 0x0, 0x0, 0xb8,
    0x0,

    /* U+0057 "W" */
    0xb4, 0xf, 0x1, 0xe7, 0x81, 0xf4, 0x2c, 0x3c,
    0x2f, 0x83, 0xc2, 0xd3, 0xfc, 0x38, 0x1e, 0x76,
    0xdb, 0x40, 0xfb, 0xe, 0xf0, 0xf, 0xf0, 0xfe,
    0x0, 0x7e, 0xb, 0xd0, 0x3, 0xc0, 0x7c, 0x0,

    /* U+0058 "X" */
    0x78, 0xf, 0xf, 0x4b, 0x40, 0xfb, 0x80, 0x1f,
    0xc0, 0x3, 0xf0, 0x2, 0xfc, 0x1, 0xf7, 0xc0,
    0xf0, 0xb8, 0xb8, 0xf, 0x0,

    /* U+0059 "Y" */
    0x3c, 0x3, 0xc1, 0xe0, 0x74, 0xf, 0x4f, 0x0,
    0x3e, 0xd0, 0x2, 0xfc, 0x0, 0xf, 0x0, 0x0,
    0xf0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0,

    /* U+005A "Z" */
    0x7f, 0xfe, 0x6a, 0xbd, 0x0, 0xb8, 0x1, 0xf0,
    0x3, 0xc0, 0xf, 0x40, 0x2e, 0x0, 0x7e, 0xaa,
    0xbf, 0xff,

    /* U+005B "[" */
    0xfd, 0xf8, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
    0xf0, 0xf0, 0xf8, 0xfd,

    /* U+005C "\\" */
    0x74, 0x0, 0xe0, 0x2, 0xc0, 0x7, 0x40, 0xe,
    0x0, 0x2c, 0x0, 0x74, 0x0, 0xe0, 0x2, 0xc0,
    0x7, 0x40, 0xe, 0x0, 0x2c,

    /* U+005D "]" */
    0xfd, 0x7d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
    0x1d, 0x1d, 0x7d, 0xfd,

    /* U+005E "^" */
    0x2, 0x0, 0x2f, 0x0, 0xec, 0x7, 0x38, 0x28,
    0x70, 0xd0, 0xd0,

    /* U+005F "_" */
    0xff, 0xf0, 0x0,

    /* U+0060 "`" */
    0x74, 0x1d,

    /* U+0061 "a" */
    0x2f, 0xd0, 0xeb, 0xc0, 0x7, 0x4b, 0xfe, 0x78,
    0x79, 0xe7, 0xe2, 0xfb, 0x80,

    /* U+0062 "b" */
    0x38, 0x0, 0x38, 0x0, 0x38, 0x0, 0x3e, 0xf4,
    0x3f, 0xbd, 0x3c, 0x1f, 0x3c, 0xf, 0x3c, 0x1e,
    0x3f, 0xbd, 0x3e, 0xf4,

    /* U+0063 "c" */
    0xb, 0xe0, 0xff, 0xe7, 0x81, 0x2d, 0x0, 0x78,
    0x10, 0xff, 0xe0, 0xbe, 0x0,

    /* U+0064 "d" */
    0x0, 0x1d, 0x0, 0x1d, 0x0, 0x1d, 0xf, 0xed,
    0x3f, 0xfd, 0x78, 0x3d, 0xb4, 0x3d, 0x78, 0x3d,
    0x3f, 0xfd, 0xf, 0xed,

    /* U+0065 "e" */
    0xb, 0xe0, 0x3e, 0xb8, 0x74, 0x3c, 0xbf, 0xfc,
    0x78, 0x0, 0x3e, 0xb4, 0xb, 0xe0,

    /* U+0066 "f" */
    0xb, 0xc2, 0xe4, 0x3c, 0xf, 0xf8, 0x7e, 0x43,
    0xc0, 0x3c, 0x3, 0xc0, 0x3c, 0x3, 0xc0,

    /* U+0067 "g" */
    0x1f, 0xee, 0x3e, 0xbe, 0xb4, 0x2e, 0xb4, 0x3e,
    0x3e, 0xbe, 0x1f, 0xee, 0x0, 0x2d, 0x3e, 0xbc,
    0x1f, 0xe0,

    /* U+0068 "h" */
    0x38, 0x0, 0x38, 0x0, 0x38, 0x0, 0x3e, 0xf4,
    0x3f, 0xfc, 0x3c, 0x2d, 0x3c, 0x1d, 0x38, 0x1d,
    0x38, 0x1d, 0x38, 0x1d,

    /* U+0069 "i" */
    0x15, 0xf2, 0x40, 0x38, 0xe3, 0x8e, 0x38, 0xe3,
    0x80,

    /* U+006A "j" */
    0x1, 0x40, 0x3c, 0x2, 0x40, 0x0, 0x3, 0xc0,
    0x3c, 0x3, 0xc0, 0x3c, 0x3, 0xc0, 0x3c, 0x3,
    0xc2, 0xf8, 0x3e, 0x0,

    /* U+006B "k" */
    0x38, 0x0, 0x38, 0x0, 0x38, 0x0, 0x38, 0x3d,
    0x38, 0xf4, 0x3f, 0xd0, 0x3f, 0xd0, 0x3e, 0xf0,
    0x3c, 0x7c, 0x38, 0x2e,

    /* U+006C "l" */
    0x38, 0xe3, 0x8e, 0x38, 0xe3, 0x8e, 0x38, 0xe0,

    /* U+006D "m" */
    0x3e, 0xf5, 0xf8, 0x3f, 0xff, 0xfd, 0x3c, 0x3e,
    0x1e, 0x3c, 0x2d, 0xf, 0x38, 0x2d, 0xf, 0x38,
    0x2d, 0xf, 0x38, 0x2d, 0xf,

    /* U+006E "n" */
    0x3a, 0xf4, 0x3f, 0xfc, 0x3c, 0x2d, 0x3c, 0x1d,
    0x38, 0x1d, 0x38, 0x1d, 0x38, 0x1d,

    /* U+006F "o" */
    0xb, 0xe0, 0x3f, 0xfc, 0x78, 0x2d, 0xb4, 0x1d,
    0x78, 0x2d, 0x3f, 0xfc, 0xb, 0xe0,

    /* U+0070 "p" */
    0x3e, 0xf4, 0x3f, 0xbd, 0x3c, 0x1e, 0x3c, 0xf,
    0x3c, 0x1f, 0x3f, 0xbd, 0x3e, 0xf4, 0x38, 0x0,
    0x38, 0x0,

    /* U+0071 "q" */
    0xf, 0xed, 0x3f, 0xfd, 0x78, 0x3d, 0xb4, 0x3d,
    0x78, 0x3d, 0x3f, 0xfd, 0xf, 0xed, 0x0, 0x1d,
    0x0, 0x1d,

    /* U+0072 "r" */
    0x3a, 0xcf, 0xe3, 0xc0, 0xf0, 0x38, 0xe, 0x3,
    0x80,

    /* U+0073 "s" */
    0x2f, 0xd1, 0xea, 0x4b, 0x80, 0xf, 0xf4, 0x1,
    0xf1, 0xeb, 0xc7, 0xfc, 0x0,

    /* U+0074 "t" */
    0x14, 0x3, 0xc0, 0xff, 0x87, 0xe4, 0x3c, 0x3,
    0xc0, 0x3c, 0x2, 0xe8, 0xf, 0xc0,

    /* U+0075 "u" */
    0x38, 0x2d, 0x38, 0x2d, 0x38, 0x2d, 0x38, 0x2d,
    0x3c, 0x3d, 0x3f, 0xfd, 0xf, 0xed,

    /* U+0076 "v" */
    0x3c, 0xb, 0xb, 0x43, 0x80, 0xf2, 0xd0, 0x2c,
    0xf0, 0x7, 0xf8, 0x0, 0xfc, 0x0, 0x2e, 0x0,

    /* U+0077 "w" */
    0xf0, 0x78, 0x2c, 0x74, 0xbc, 0x38, 0x38, 0xfd,
    0x74, 0x3d, 0xde, 0xb0, 0x1f, 0xcf, 0xe0, 0xf,
    0x87, 0xd0, 0xf, 0x43, 0xc0,

    /* U+0078 "x" */
    0xb8, 0x78, 0x3d, 0xf0, 0xf, 0xd0, 0xf, 0xc0,
    0xf, 0xd0, 0x3c, 0xf0, 0xb4, 0x78,

    /* U+0079 "y" */
    0x3c, 0xb, 0x7, 0x43, 0x80, 0xf2, 0xc0, 0x2d,
    0xf0, 0x3, 0xf4, 0x0, 0xfc, 0x0, 0x2e, 0x0,
    0xaf, 0x0, 0x2f, 0x40, 0x0,

    /* U+007A "z" */
    0xbf, 0xf1, 0xaf, 0xc0, 0x3c, 0x3, 0xd0, 0x2e,
    0x1, 0xfa, 0x8b, 0xff, 0x0,

    /* U+007B "{" */
    0xb, 0x87, 0xd1, 0xd0, 0x74, 0x2d, 0x1f, 0x3,
    0xd0, 0x74, 0x1d, 0x7, 0x41, 0xf4, 0x2e,

    /* U+007C "|" */
    0xee, 0xee, 0xee, 0xee, 0xee, 0xee,

    /* U+007D "}" */
    0xf4, 0x2f, 0x3, 0xc0, 0xb0, 0x2c, 0x7, 0xc2,
    0xe0, 0xb0, 0x2c, 0xf, 0xb, 0xc3, 0xd0,

    /* U+007E "~" */
    0x0, 0x0, 0xf8, 0xe7, 0x7f, 0x4, 0x0,

    /* U+2022 "•" */
    0x0, 0x3d, 0x7e, 0x3c
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 54, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 55, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 7, .adv_w = 84, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 12, .adv_w = 138, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 33, .adv_w = 122, .box_w = 8, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 59, .adv_w = 168, .box_w = 11, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 140, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 105, .adv_w = 44, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 108, .adv_w = 69, .box_w = 5, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 123, .adv_w = 69, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 135, .adv_w = 83, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 144, .adv_w = 115, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 155, .adv_w = 50, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 159, .adv_w = 74, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 162, .adv_w = 50, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 165, .adv_w = 75, .box_w = 7, .box_h = 12, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 186, .adv_w = 130, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 75, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 213, .adv_w = 113, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 229, .adv_w = 114, .box_w = 8, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 247, .adv_w = 132, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 268, .adv_w = 114, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 284, .adv_w = 122, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 302, .adv_w = 119, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 320, .adv_w = 127, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 338, .adv_w = 122, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 356, .adv_w = 50, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 362, .adv_w = 50, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 369, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 382, .adv_w = 115, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 391, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 404, .adv_w = 113, .box_w = 8, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 422, .adv_w = 199, .box_w = 12, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 455, .adv_w = 147, .box_w = 11, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 480, .adv_w = 147, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 498, .adv_w = 141, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 519, .adv_w = 159, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 540, .adv_w = 129, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 556, .adv_w = 123, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 572, .adv_w = 148, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 593, .adv_w = 155, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 611, .adv_w = 63, .box_w = 2, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 616, .adv_w = 104, .box_w = 7, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 632, .adv_w = 142, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 653, .adv_w = 116, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 669, .adv_w = 183, .box_w = 10, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 692, .adv_w = 155, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 710, .adv_w = 162, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 733, .adv_w = 141, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 751, .adv_w = 162, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 782, .adv_w = 141, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 800, .adv_w = 122, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 818, .adv_w = 119, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 836, .adv_w = 151, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 857, .adv_w = 143, .box_w = 11, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 882, .adv_w = 223, .box_w = 14, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 914, .adv_w = 137, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 935, .adv_w = 130, .box_w = 10, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 958, .adv_w = 129, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 976, .adv_w = 71, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 988, .adv_w = 75, .box_w = 7, .box_h = 12, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 1009, .adv_w = 71, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1021, .adv_w = 115, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1032, .adv_w = 96, .box_w = 6, .box_h = 2, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1035, .adv_w = 115, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 1037, .adv_w = 118, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1050, .adv_w = 132, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1070, .adv_w = 113, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1083, .adv_w = 133, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1103, .adv_w = 121, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1117, .adv_w = 74, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1132, .adv_w = 134, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1150, .adv_w = 133, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1170, .adv_w = 58, .box_w = 3, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1179, .adv_w = 59, .box_w = 6, .box_h = 13, .ofs_x = -2, .ofs_y = -2},
    {.bitmap_index = 1199, .adv_w = 127, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1219, .adv_w = 58, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1227, .adv_w = 201, .box_w = 12, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1248, .adv_w = 133, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1262, .adv_w = 126, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1276, .adv_w = 132, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1294, .adv_w = 132, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1312, .adv_w = 83, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1321, .adv_w = 102, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1334, .adv_w = 84, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1348, .adv_w = 132, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1362, .adv_w = 115, .box_w = 9, .box_h = 7, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1378, .adv_w = 180, .box_w = 12, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1399, .adv_w = 114, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1413, .adv_w = 115, .box_w = 9, .box_h = 9, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 1434, .adv_w = 104, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1447, .adv_w = 75, .box_w = 5, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1462, .adv_w = 59, .box_w = 2, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1468, .adv_w = 75, .box_w = 5, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1483, .adv_w = 115, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1490, .adv_w = 69, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 2}
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
    0, 4, 4, 0, 2, 0, 4, 0,
    0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 9, 0, 5, -3, 0,
    0, 4, 0, -11, -12, 1, 8, 3,
    3, -8, 1, 8, 0, 7, 2, 5,
    0, -8, 0, 0, 2, 0, 0, 0,
    0, 0, 0, 12, 2, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -6, 0, 0,
    0, 0, 0, -4, 3, 4, 0, 0,
    -2, 0, -1, 2, 0, -2, 0, -2,
    -1, -4, 0, 0, 0, 0, -2, 0,
    0, -2, -3, 0, 0, -2, 0, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, -2, -2, 0, 0, -5,
    0, -24, 0, 0, -4, -10, 4, 6,
    0, 0, -4, 2, 2, 6, 4, -3,
    4, 0, 0, -10, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -7,
    0, 0, 1, 0, 5, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, -8, 0, -8, -1, 0, 0, -6,
    0, -1, 6, 0, -6, -2, -1, 1,
    0, -3, 0, 0, -2, -12, 0, 1,
    0, 5, -5, 0, -4, 0, -8, 1,
    0, -15, -2, 4, 2, 0, 0, 0,
    0, 0, 0, 0, 2, 0, -3, -1,
    -3, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 7, 0, 2, 0,
    0, -4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 8, 2, 1, 0, 0,
    0, 0, 0, 20, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -7, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 2, 4,
    2, 6, -2, 0, 0, 4, -2, -7,
    -25, 1, 5, 4, 0, -2, 0, 5,
    0, 5, 0, 5, 0, -18, 0, -2,
    6, 0, 6, -2, 4, 2, 0, 0,
    1, -2, 0, 0, -3, 0, 0, 0,
    15, 0, 6, 0, 7, 3, 7, 3,
    0, 0, -3, -7, 0, 0, 0, -2,
    1, -2, 0, 1, -4, -3, -4, 1,
    0, -2, 0, 0, 0, -8, 1, -4,
    0, -4, -7, 0, -5, -4, -6, 0,
    0, -12, 0, 0, 0, 0, 2, 0,
    0, 0, 0, 0, 2, 0, -3, -5,
    -3, -2, 1, -11, 2, -13, 0, 0,
    0, -7, -2, 0, 17, -2, -3, 2,
    2, 0, 0, -3, 2, 0, 0, -11,
    -4, 6, 0, 10, -6, -1, -7, 0,
    -7, 3, 0, -18, 0, 2, 2, 0,
    -1, 0, 0, 2, 0, 0, -1, -1,
    -6, 0, -6, 0, 0, 12, -4, 0,
    -7, 0, 7, 0, -14, -18, -14, -4,
    6, 0, 0, -12, 0, 1, -5, 0,
    -3, 0, -4, -8, 0, -2, 6, 0,
    6, 0, 6, 0, 0, 3, 6, -22,
    -12, 0, -12, 0, 2, 1, -12, -12,
    -5, -12, -6, -10, -6, -12, 0, 1,
    0, 0, 0, 0, 0, 1, 1, -2,
    -4, 0, -1, -1, -2, 0, 0, -1,
    0, 0, 0, -4, 0, -2, 0, -5,
    -4, 0, -5, -7, -7, -3, 0, -4,
    0, -4, 0, 0, 0, 0, 0, -2,
    0, 0, 2, 0, 1, -2, 1, 0,
    0, 0, 0, 2, -1, 0, 0, 0,
    -1, 2, 2, 0, 0, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 1,
    0, 3, -1, 0, -2, 0, -3, 0,
    0, -1, 0, 6, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    0, -1, 0, -2, 0, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    -1, 0, -2, -2, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, -2, -2,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    -2, -3, -2, 0, 0, -6, -1, -6,
    4, 0, 0, -4, 2, 4, 5, 0,
    -5, 0, -1, 0, 0, -8, 2, -1,
    2, -11, 2, 0, 0, 1, -10, 0,
    -11, -2, -17, -1, 0, -10, 0, 4,
    5, 0, 3, 0, 0, 0, 0, 0,
    0, 0, -4, -3, -4, 0, 0, 0,
    0, -2, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -1, -1, 0, -1, -2,
    0, 0, 0, 0, 0, 0, 0, -2,
    -2, 0, -2, -2, -1, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, -1, 0, -4, 2, 0, 0, -1,
    1, 2, 2, 0, 0, 0, 0, 0,
    0, -1, 0, 0, 0, 0, 0, 1,
    0, 0, -2, 0, -2, -1, -1, 0,
    0, 0, 0, 0, 0, 0, 1, 0,
    0, -2, 0, 0, 0, 0, -3, -3,
    -3, 0, 0, 6, -1, 1, -5, 0,
    0, 5, -10, -9, -7, -4, 2, 0,
    -1, -12, -3, 0, -3, 0, -4, 3,
    -3, -12, 0, -5, 0, 0, 1, 0,
    2, -1, 0, 2, 2, -6, -7, 0,
    -10, 0, 0, -4, -4, -6, -1, -5,
    1, -2, 1, -4, 0, 0, 0, -2,
    0, 0, 0, 1, 0, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, -1, 0, 0, -2, 0,
    -3, -4, -4, 0, 0, -6, 0, 0,
    0, 0, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 2,
    0, 0, 0, 0, 0, 0, 0, 0,
    10, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, -2, 0, 2, 0, 7,
    -2, 0, -5, -2, -7, 0, 0, -4,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 0, 1, -2, 1, 0,
    -1, 0, 0, 0, -2, 0, 0, 1,
    0, -10, -6, 0, 0, 0, -3, -10,
    0, 0, -2, 2, 0, -3, -1, -9,
    0, -6, 0, 0, -3, -4, -3, -2,
    -2, 0, 0, -3, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 2, 0,
    2, 0, 0, -3, 0, 0, 0, 0,
    1, 0, 1, -4, -4, 0, -2, -2,
    -2, 0, 0, 0, 0, 0, 0, -6,
    0, -2, 0, -3, -2, 0, -4, -5,
    -6, -1, 0, -4, 2, -6, 0, 0,
    0, 0, 0, 15, 0, 0, 1, 0,
    0, -3, 0, 0, 0, -8, 0, 0,
    0, 0, 0, -18, -4, 6, 6, -2,
    -8, 0, 2, -3, 0, -10, -1, -2,
    2, -13, -2, 4, 0, 3, -7, -3,
    -7, -7, -8, 0, 0, -12, 0, 10,
    0, 0, -1, 0, 0, 0, 0, -1,
    -1, -2, -5, -7, -5, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, -1, -2, -3, 0, 0, -4,
    0, -2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, -4, 0, 0, 4,
    -1, 2, 0, -4, 2, -1, 0, -3,
    -2, 0, -2, -2, -2, 0, -3, -3,
    0, 0, -1, -1, -1, -3, -1, 0,
    0, -2, 0, 2, -1, 0, -4, 0,
    0, 0, 0, -4, 0, -3, 0, -3,
    0, -3, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 2, 0, -2, 0, -1,
    -1, -3, -1, -1, -1, 0, -1, -1,
    0, 0, 0, 0, 0, 0, -2, -1,
    -1, 0, 0, 0, 0, 1, -1, 0,
    -1, 0, 0, 0, 0, -1, -1, -1,
    -1, -1, -1, -1, 2, 8, 0, 0,
    -4, 0, -1, 4, 0, -2, -7, -2,
    3, 1, 0, -8, -3, 2, -3, 2,
    0, 1, -1, -6, 0, -3, 1, 0,
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
    0, 0, 0, -1, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -3, 0, 0, -2, 0, 0, -2,
    -2, 0, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, -1, 0, 0, 0, 0, -3, 0,
    -4, 0, 0, 0, -7, 0, 1, -4,
    4, 0, -1, -8, 0, 0, -4, -2,
    0, -8, -5, -6, 0, 0, -9, -2,
    -8, -8, -10, 0, -3, 0, 2, 12,
    -2, 0, -4, 0, 0, 0, -2, -3,
    -5, -3, -7, -9, -7, -5, 0, 0,
    -1, 0, 1, 0, 0, -13, 0, 6,
    3, -3, -7, 0, 1, -3, 0, -10,
    -1, -2, 4, -17, -2, 1, 0, 0,
    -12, -2, -9, -2, -14, 0, 0, -13,
    0, 10, 1, 0, -1, 0, 0, 0,
    0, 0, -1, -1, -7, -1, -7, 0,
    0, 0, 0, 0, -6, 0, -1, 0,
    -1, -5, -8, 0, 0, -1, -3, -6,
    -2, 0, -2, 0, 0, 0, 0, -9,
    -2, -6, -6, -2, -3, -5, -2, -3,
    0, -4, -1, -7, -3, 0, -2, 0,
    0, -2, -2, 0, 1, 0, -1, -6,
    -1, 0, 0, -3, 0, 0, 0, 0,
    1, 0, 1, -4, 10, 0, -2, -2,
    -2, 0, 0, 0, 0, 0, 0, -6,
    0, -2, 0, -3, -2, 0, -4, -5,
    -6, -1, 0, -4, 2, 8, 0, 0,
    0, 0, 0, 15, 0, 0, 1, 0,
    0, -3, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, -4, 0, 0, 0, 0,
    0, -1, 0, 0, 0, -2, -2, 0,
    0, -4, -2, 0, 0, -4, 0, 3,
    -1, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 4, 2,
    -1, 0, -5, -2, 0, 6, -7, -6,
    -4, -4, 8, 4, 2, -16, -2, 4,
    -2, 0, -2, 4, -2, -7, 0, -2,
    2, -2, -2, -6, -2, 0, 0, 6,
    4, 0, -5, 0, -11, 0, 0, 8,
    -2, -7, 1, -2, -6, -6, -6, -2,
    2, 0, -3, 0, -5, 0, 2, 6,
    -5, -7, -8, -5, 6, 0, 1, -15,
    -2, 2, -3, -2, -5, 0, -4, -7,
    -3, -3, -2, 0, 0, -5, -5, -2,
    0, 6, 5, -2, -11, 0, -11, 0,
    -1, -4, -7, -12, -1, -6, -3, -6,
    -3, -6, 0, 0, -2, 0, -4, -1,
    0, -2, -4, 0, 3, -7, 2, 0,
    0, -11, 0, -2, -4, -3, -2, -6,
    -5, -7, -4, 0, -6, -2, -5, -2,
    -6, -2, 0, 0, 1, 8, -3, 0,
    -6, 0, 0, 0, -2, -4, -5, -5,
    -6, -8, -6, -3, 4, 0, -3, 0,
    -10, -1, 2, 4, -6, -7, -4, -7,
    7, -2, 1, -18, -4, 4, -4, -3,
    -7, 0, -6, -8, -1, -2, -2, -2,
    -5, -6, -1, 0, 0, 6, 6, -1,
    -12, 0, -12, 0, -2, 7, -7, -14,
    -4, -7, -8, -10, -8, -7, 0, 0,
    0, 0, -1, 0, 0, 2, -1, 4,
    1, -3, 4, 0, 0, -3, 0, 0,
    0, 0, 1, 1, -1, 0, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    2, 6, 0, 0, -1, 0, 0, 0,
    0, 0, -1, -1, -1, 0, -1, 0,
    1, 2, 0, 0, 0, 0, 2, 1,
    -1, 0, 8, 0, 4, 1, 1, -3,
    0, 4, 0, 0, 0, 2, 0, 0,
    0, 0, 4, 0, 5, 1, 6, 0,
    0, 6, 0, 6, -1, 0, 0, 0,
    0, 19, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -12, 0, -2, 3, 0,
    6, -25, 0, 17, 1, -4, -4, 2,
    2, -1, 1, -10, 0, 0, 10, -12,
    -4, 6, 0, 6, -4, -2, -8, 4,
    -4, 0, 0, -14, 8, 27, 0, 0,
    0, 0, 0, 23, 0, 0, 0, 0,
    4, 0, 4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, -2, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    4, -5, 0, 0, 1, -2, 0, 2,
    23, -4, -1, 5, 5, -5, 2, 0,
    0, 2, 2, -3, -6, 10, 6, 15,
    0, -2, -2, 8, -1, 4, 0, -25,
    6, 0, -2, 0, -5, 0, 0, 22,
    0, 2, -4, -5, -3, 7, 4, 3,
    0, 0, 0, -5, 0, 0, 0, -5,
    0, 0, 0, 0, -4, -1, 0, 0,
    0, -4, 0, -1, 0, -8, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -12, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    -2, 0, 0, -3, 0, -5, 0, 0,
    0, -3, 2, -1, 0, 0, -5, -2,
    -4, 0, 0, -5, 0, -2, 0, -8,
    0, -4, 0, 0, -13, -2, -8, -4,
    -8, 0, 0, -12, 0, -5, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -3, -3, -3, -1, 0, 0, 0, 0,
    -3, 0, -4, 3, -4, 4, 0, -1,
    -4, -1, -3, -2, 0, -1, -1, -1,
    2, -5, -1, 0, 0, 0, -14, -2,
    -5, 0, -7, 0, -1, -8, -1, 0,
    0, -1, -1, 0, -1, 0, 0, 0,
    2, 0, -2, -3, -2, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 7, 0, 0, 0, 0, 0,
    0, -3, 0, -1, 0, 0, 0, -4,
    2, 0, 0, 0, -5, -2, -4, 0,
    0, -5, 0, -2, 0, -8, 0, 0,
    0, 0, -18, 0, -4, -7, -10, 0,
    0, -12, 0, -2, -3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, -3,
    -2, -1, 1, 0, 0, 3, -3, 0,
    8, 8, -2, -2, -6, 1, 8, 3,
    3, -5, 1, 7, 1, 5, 3, 5,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 12, 8, -4, -2, 2,
    -1, 2, 2, 9, 1, 0, 0, 0,
    2, 0, 2, 0, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 5, 0, 0, 0, 0, -12, -1,
    -2, -6, -8, 0, 0, -12, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 5, 0, 0, 0, 0,
    -12, -1, -2, -6, -8, 0, 0, -3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    -5, 2, 0, -2, 2, 4, 2, -6,
    0, 1, -2, 2, 0, 2, 0, 0,
    0, 0, -2, 0, -1, -1, -4, 0,
    -1, -8, 0, 11, -2, 0, -4, 0,
    0, 0, -1, -3, 0, -2, -6, -4,
    -6, -2, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0, 5,
    0, 0, 0, 0, -12, -1, -2, -6,
    -8, 0, 0, -12, 0, 0, 0, 0,
    0, 0, 0, 10, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    -5, -1, -1, 6, -2, -2, -8, 1,
    2, 1, -1, -4, 0, 4, 0, 2,
    1, 2, -4, -9, -1, 0, -4, -2,
    -5, -7, -7, 0, -2, -4, -1, -3,
    -7, -1, -2, 0, -1, 0, -1, 0,
    3, 0, 3, -1, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, -2, -2, 0, 0, -5,
    0, -1, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -12,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, 0, 0, 0, -1, 0, 0, -3,
    -2, 2, 0, -3, -3, -1, 0, -4,
    -1, -3, -2, -1, 0, -3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -12, 0, 5, 0, 0, -3, 0,
    0, 0, 0, 0, -2, 0, -2, 0,
    -2, 0, 0, 0, -1, 0, -5, 0,
    0, 7, -3, -6, -7, 1, 4, 4,
    1, -6, 1, 3, 1, 6, 1, 7,
    -1, -5, 0, 0, -3, 0, 0, -6,
    -5, 0, 0, -4, 0, -3, -3, 0,
    -3, 0, 0, -3, 0, -2, 3, 0,
    -2, -6, -2, -2, 0, 0, -1, 0,
    -4, 0, 0, 3, -5, 0, 2, -2,
    2, 2, 0, -7, 0, -1, -1, 0,
    -2, 4, -2, 0, 0, 0, -4, -1,
    -4, 0, -6, 0, 0, -8, 0, 7,
    -2, 0, -3, 0, 0, 5, 0, -2,
    0, -2, -6, 0, -6, -2, 0, 0,
    0, 0, -1, 0, 0, 2, -2, 1,
    0, 0, -1, -1, 0, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -11,
    0, 3, 0, 0, -1, 0, 0, 0,
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
const lv_font_t MontserratBold12Trae = {
#else
lv_font_t MontserratBold12Trae = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if MONTSERRATBOLD12TRAE*/
