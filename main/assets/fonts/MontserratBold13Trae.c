/*******************************************************************************
 * Size: 13 px
 * Bpp: 2
 * Opts: --font components/lvgl/tests/src/test_files/fonts/Montserrat-Bold.ttf --size 13 --bpp 2 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name MontserratBold13Trae --range 0x20-0x7E --symbols • -o main/assets/fonts/MontserratBold13Trae.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef MONTSERRATBOLD13TRAE
#define MONTSERRATBOLD13TRAE 1
#endif

#if MONTSERRATBOLD13TRAE

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x7c, 0x3c, 0x3c, 0x3c, 0x3c, 0x38, 0x0, 0x7c,
    0x3c,

    /* U+0022 "\"" */
    0x76, 0xc7, 0x6c, 0x76, 0xc2, 0x18,

    /* U+0023 "#" */
    0x3, 0xe, 0x0, 0x70, 0xd0, 0x7f, 0xff, 0xc1,
    0xf6, 0xe4, 0xa, 0x1c, 0x0, 0xe2, 0xc0, 0xbf,
    0xff, 0x81, 0xe7, 0xd0, 0xc, 0x38, 0x0,

    /* U+0024 "$" */
    0x0, 0x40, 0x1, 0xc0, 0xb, 0xf8, 0x3f, 0xfc,
    0x79, 0xc0, 0x7f, 0xc0, 0x1f, 0xf8, 0x2, 0xfe,
    0x11, 0xdf, 0x7f, 0xfe, 0x2f, 0xf4, 0x1, 0xc0,
    0x0, 0x40,

    /* U+0025 "%" */
    0x2f, 0x1, 0xc0, 0x72, 0x83, 0x80, 0xa1, 0xcb,
    0x0, 0x72, 0x9c, 0x0, 0x3f, 0x75, 0xf0, 0x0,
    0xf7, 0x6c, 0x1, 0xc7, 0x1c, 0x3, 0x47, 0x1c,
    0xe, 0x2, 0xf0,

    /* U+0026 "&" */
    0xb, 0xf0, 0x1, 0xf7, 0xc0, 0x1e, 0x3c, 0x0,
    0xff, 0x40, 0x1f, 0xe1, 0x43, 0xcf, 0xb8, 0x78,
    0x2f, 0x47, 0xeb, 0xfc, 0x1f, 0xf5, 0x80,

    /* U+0027 "'" */
    0x75, 0xd7, 0x48,

    /* U+0028 "(" */
    0xb, 0x43, 0xc2, 0xd0, 0xf0, 0x3c, 0xf, 0x3,
    0xc0, 0xf0, 0x3c, 0xb, 0x40, 0xf0, 0x2d,

    /* U+0029 ")" */
    0xb4, 0x3c, 0x3c, 0x2d, 0x1e, 0x1e, 0x1e, 0x1e,
    0x2d, 0x3c, 0x3c, 0xb4,

    /* U+002A "*" */
    0xd, 0xb, 0xfc, 0x3f, 0x8b, 0xfc, 0xd, 0x0,
    0x0,

    /* U+002B "+" */
    0x3, 0x80, 0xe, 0x2, 0xbe, 0x8f, 0xff, 0x3,
    0x80, 0xe, 0x0,

    /* U+002C "," */
    0x11, 0xf7, 0xcd, 0x70,

    /* U+002D "-" */
    0x2a, 0x1f, 0xd0,

    /* U+002E "." */
    0x11, 0xf7, 0x80,

    /* U+002F "/" */
    0x0, 0x74, 0x2, 0xc0, 0xf, 0x0, 0x74, 0x2,
    0xc0, 0xe, 0x0, 0x74, 0x3, 0xc0, 0xe, 0x0,
    0xb4, 0x3, 0xc0, 0xe, 0x0,

    /* U+0030 "0" */
    0x7, 0xf4, 0xb, 0xff, 0x43, 0xc1, 0xf1, 0xe0,
    0x3c, 0x78, 0xf, 0x5e, 0x3, 0xc3, 0xc1, 0xf0,
    0xbf, 0xf4, 0x7, 0xf4, 0x0,

    /* U+0031 "1" */
    0xff, 0x2f, 0xc0, 0xf0, 0x3c, 0xf, 0x3, 0xc0,
    0xf0, 0x3c, 0xf, 0x0,

    /* U+0032 "2" */
    0x2f, 0xe0, 0xbf, 0xf8, 0x10, 0x3c, 0x0, 0x7c,
    0x0, 0xf4, 0x7, 0xd0, 0x1f, 0x40, 0x7f, 0xa8,
    0xbf, 0xfd,

    /* U+0033 "3" */
    0x2f, 0xff, 0x6, 0xaf, 0x80, 0xb, 0x80, 0x7,
    0xd0, 0x2, 0xfe, 0x0, 0x3, 0xc0, 0x0, 0xf0,
    0xbf, 0xfc, 0x1b, 0xf8, 0x0,

    /* U+0034 "4" */
    0x0, 0xb8, 0x0, 0x7c, 0x0, 0x3c, 0x0, 0x3d,
    0x10, 0x2e, 0x2d, 0x1f, 0xaf, 0xeb, 0xff, 0xfc,
    0x0, 0xb4, 0x0, 0x2d, 0x0,

    /* U+0035 "5" */
    0x3f, 0xfc, 0x3e, 0xa8, 0x3c, 0x0, 0x3e, 0x90,
    0x3f, 0xfc, 0x0, 0x3d, 0x10, 0x3d, 0xbf, 0xfc,
    0x2f, 0xe0,

    /* U+0036 "6" */
    0x6, 0xfd, 0x7, 0xfb, 0x3, 0xd0, 0x1, 0xfb,
    0xe0, 0x7f, 0xbe, 0x1f, 0x3, 0xc3, 0xc0, 0xf0,
    0xbe, 0xf8, 0xb, 0xf4, 0x0,

    /* U+0037 "7" */
    0xbf, 0xfe, 0xbe, 0xbe, 0xb0, 0x3c, 0x0, 0xb8,
    0x0, 0xf0, 0x1, 0xf0, 0x3, 0xd0, 0x7, 0xc0,
    0xf, 0x40,

    /* U+0038 "8" */
    0xb, 0xf4, 0xf, 0xef, 0x43, 0xc1, 0xf0, 0xf4,
    0xb8, 0x2f, 0xfc, 0x1f, 0x9b, 0xcb, 0x80, 0xf0,
    0xfa, 0xfc, 0x1b, 0xf8, 0x0,

    /* U+0039 "9" */
    0x1f, 0xe0, 0x7e, 0xbc, 0xb4, 0x2d, 0xb8, 0x3e,
    0x3f, 0xff, 0x6, 0x6e, 0x0, 0x3d, 0x3f, 0xf8,
    0x2f, 0xd0,

    /* U+003A ":" */
    0x79, 0xf1, 0x0, 0x11, 0xf7, 0x80,

    /* U+003B ";" */
    0x79, 0xf1, 0x0, 0x11, 0xf7, 0xcd, 0x70,

    /* U+003C "<" */
    0x0, 0x4, 0x6, 0xf2, 0xfd, 0xf, 0x40, 0x2f,
    0xd0, 0x6, 0xf0, 0x0, 0x40,

    /* U+003D "=" */
    0x3f, 0xfc, 0xaa, 0xa0, 0x0, 0xf, 0xff, 0x2a,
    0xa8,

    /* U+003E ">" */
    0x10, 0x0, 0xf9, 0x0, 0xbf, 0x80, 0x2f, 0xb,
    0xf8, 0xf9, 0x1, 0x0, 0x0,

    /* U+003F "?" */
    0xb, 0xf8, 0x2f, 0xbf, 0x4, 0x1f, 0x0, 0x2e,
    0x0, 0xb8, 0x0, 0xe0, 0x0, 0x0, 0x1, 0xf0,
    0x0, 0xe0,

    /* U+0040 "@" */
    0x1, 0xbf, 0xd0, 0x2, 0xe5, 0x6f, 0x2, 0xd7,
    0xff, 0xb0, 0xd3, 0xdb, 0xce, 0x72, 0xd0, 0xf1,
    0xdc, 0xb0, 0x3c, 0x77, 0x2d, 0xf, 0x1c, 0xd3,
    0xdb, 0xee, 0x2d, 0x7e, 0x3e, 0x2, 0xe5, 0x50,
    0x0, 0x1b, 0xfd, 0x0,

    /* U+0041 "A" */
    0x0, 0x7c, 0x0, 0x0, 0xbe, 0x0, 0x0, 0xff,
    0x0, 0x2, 0xd7, 0x80, 0x3, 0xc3, 0xc0, 0xb,
    0xeb, 0xe0, 0xf, 0xff, 0xf0, 0x1e, 0x0, 0xf4,
    0x3d, 0x0, 0x7c,

    /* U+0042 "B" */
    0xff, 0xf8, 0x3e, 0xaf, 0x8f, 0x0, 0xf3, 0xea,
    0xf8, 0xff, 0xfd, 0x3c, 0x3, 0xcf, 0x0, 0xf7,
    0xea, 0xbc, 0xff, 0xfd, 0x0,

    /* U+0043 "C" */
    0x2, 0xfe, 0x1, 0xff, 0xfc, 0x3e, 0x2, 0x7,
    0xc0, 0x0, 0x78, 0x0, 0x7, 0xc0, 0x0, 0x3e,
    0x1, 0x1, 0xff, 0xfc, 0x2, 0xfe, 0x0,

    /* U+0044 "D" */
    0xff, 0xf8, 0xf, 0xeb, 0xf0, 0xf0, 0xb, 0xcf,
    0x0, 0x3c, 0xf0, 0x3, 0xcf, 0x0, 0x3c, 0xf0,
    0xb, 0xcf, 0xeb, 0xf0, 0xff, 0xf8, 0x0,

    /* U+0045 "E" */
    0xff, 0xfc, 0xfa, 0xa8, 0xf0, 0x0, 0xfe, 0xa0,
    0xff, 0xf4, 0xf0, 0x0, 0xf0, 0x0, 0xfa, 0xa8,
    0xff, 0xfc,

    /* U+0046 "F" */
    0xff, 0xff, 0xea, 0xaf, 0x0, 0x3c, 0x0, 0xff,
    0xf7, 0xfa, 0x9f, 0x0, 0x3c, 0x0, 0xf0, 0x0,

    /* U+0047 "G" */
    0x2, 0xfe, 0x1, 0xff, 0xfc, 0x3e, 0x1, 0x47,
    0xc0, 0x0, 0x78, 0x1, 0x87, 0xc0, 0x3c, 0x3e,
    0x3, 0xc1, 0xff, 0xfc, 0x2, 0xfe, 0x0,

    /* U+0048 "H" */
    0xf0, 0xb, 0x7c, 0x2, 0xdf, 0x0, 0xb7, 0xff,
    0xfd, 0xff, 0xff, 0x7c, 0x2, 0xdf, 0x0, 0xb7,
    0xc0, 0x2d, 0xf0, 0xb, 0x40,

    /* U+0049 "I" */
    0xf3, 0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xf0,

    /* U+004A "J" */
    0x1f, 0xfc, 0xa, 0xbc, 0x0, 0x3c, 0x0, 0x3c,
    0x0, 0x3c, 0x0, 0x3c, 0x0, 0x3c, 0x3f, 0xf8,
    0x1f, 0xe0,

    /* U+004B "K" */
    0xf0, 0x1f, 0x3c, 0x1f, 0xf, 0xf, 0x3, 0xdf,
    0x40, 0xff, 0xc0, 0x3f, 0xfc, 0xf, 0x8b, 0xc3,
    0xc0, 0xf8, 0xf0, 0xf, 0x40,

    /* U+004C "L" */
    0xf0, 0x3, 0xc0, 0xf, 0x0, 0x3c, 0x0, 0xf0,
    0x3, 0xc0, 0xf, 0x0, 0x3e, 0xaa, 0xff, 0xfc,

    /* U+004D "M" */
    0xf0, 0x0, 0xb7, 0xe0, 0x7, 0xdf, 0xd0, 0x3f,
    0x7f, 0xc2, 0xfd, 0xfb, 0x8f, 0xb7, 0xcf, 0xf2,
    0xdf, 0xf, 0x8b, 0x7c, 0x2c, 0x2d, 0xf0, 0x0,
    0xb4,

    /* U+004E "N" */
    0xf4, 0xb, 0x7f, 0x2, 0xdf, 0xf0, 0xb7, 0xfe,
    0x2d, 0xf7, 0xeb, 0x7c, 0x3f, 0xdf, 0x7, 0xf7,
    0xc0, 0xbd, 0xf0, 0xb, 0x40,

    /* U+004F "O" */
    0x2, 0xfe, 0x0, 0x7f, 0xff, 0x43, 0xe0, 0x2f,
    0x1f, 0x0, 0x3d, 0x78, 0x0, 0xb5, 0xf0, 0x3,
    0xd3, 0xe0, 0x2f, 0x7, 0xff, 0xf4, 0x2, 0xfe,
    0x0,

    /* U+0050 "P" */
    0xff, 0xf4, 0xfe, 0xfd, 0xf0, 0x1f, 0xf0, 0xf,
    0xf0, 0x2f, 0xff, 0xfc, 0xfe, 0x90, 0xf0, 0x0,
    0xf0, 0x0,

    /* U+0051 "Q" */
    0x2, 0xfe, 0x0, 0x7f, 0xff, 0x43, 0xe0, 0x2f,
    0x1f, 0x0, 0x3d, 0x78, 0x0, 0xb5, 0xf0, 0x3,
    0xd3, 0xe0, 0x2f, 0x7, 0xff, 0xf4, 0x2, 0xfe,
    0x0, 0x0, 0xbe, 0xe0, 0x0, 0x7f, 0x40,

    /* U+0052 "R" */
    0xff, 0xf4, 0x3f, 0xbf, 0x4f, 0x1, 0xf3, 0xc0,
    0x3c, 0xf0, 0x2e, 0x3f, 0xff, 0xf, 0xef, 0x43,
    0xc0, 0xf0, 0xf0, 0x2f, 0x0,

    /* U+0053 "S" */
    0xb, 0xf8, 0x3f, 0xbc, 0x78, 0x0, 0x7e, 0x40,
    0x1f, 0xf8, 0x0, 0x7e, 0x10, 0x1f, 0x7f, 0xbe,
    0x2f, 0xf4,

    /* U+0054 "T" */
    0xff, 0xff, 0xab, 0xea, 0x3, 0xc0, 0x3, 0xc0,
    0x3, 0xc0, 0x3, 0xc0, 0x3, 0xc0, 0x3, 0xc0,
    0x3, 0xc0,

    /* U+0055 "U" */
    0xf0, 0xf, 0x7c, 0x3, 0xdf, 0x0, 0xf7, 0xc0,
    0x3d, 0xf0, 0xf, 0x3c, 0x3, 0xcf, 0x81, 0xf1,
    0xff, 0xf8, 0x1b, 0xf4, 0x0,

    /* U+0056 "V" */
    0x3d, 0x0, 0x78, 0x7c, 0x3, 0xc0, 0xf0, 0x1e,
    0x2, 0xe0, 0xf4, 0x3, 0xc3, 0xc0, 0xb, 0xad,
    0x0, 0x1f, 0xf0, 0x0, 0x3f, 0x80, 0x0, 0x7c,
    0x0,

    /* U+0057 "W" */
    0xb8, 0xb, 0xc0, 0x78, 0xf0, 0x3f, 0x2, 0xd3,
    0xd0, 0xfd, 0xf, 0xb, 0x8b, 0xbc, 0x78, 0xf,
    0x3c, 0xf2, 0xd0, 0x3d, 0xe2, 0xdf, 0x0, 0xbf,
    0x43, 0xf8, 0x0, 0xfc, 0xf, 0xd0, 0x3, 0xe0,
    0x2f, 0x0,

    /* U+0058 "X" */
    0x7c, 0xb, 0x82, 0xe1, 0xf0, 0xf, 0x7c, 0x0,
    0x7f, 0x40, 0x3, 0xf0, 0x0, 0x7f, 0x80, 0xf,
    0x7d, 0x3, 0xd0, 0xf0, 0xbc, 0xb, 0xc0,

    /* U+0059 "Y" */
    0x3d, 0x1, 0xe1, 0xf0, 0x3c, 0xb, 0x8b, 0x40,
    0x3d, 0xf0, 0x1, 0xfd, 0x0, 0xf, 0x80, 0x0,
    0xb4, 0x0, 0xb, 0x40, 0x0, 0xb4, 0x0,

    /* U+005A "Z" */
    0x7f, 0xff, 0x5a, 0xaf, 0xc0, 0x3, 0xd0, 0x3,
    0xd0, 0x3, 0xe0, 0x2, 0xf0, 0x2, 0xf0, 0x1,
    0xfe, 0xa9, 0xbf, 0xff, 0x40,

    /* U+005B "[" */
    0xfe, 0xf9, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
    0xf0, 0xf0, 0xf9, 0xfe,

    /* U+005C "\\" */
    0x74, 0x0, 0xf0, 0x2, 0xc0, 0x7, 0x40, 0xf,
    0x0, 0x2c, 0x0, 0x38, 0x0, 0xf0, 0x2, 0xd0,
    0x3, 0x80, 0xf, 0x0, 0x1d,

    /* U+005D "]" */
    0xfe, 0x6e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
    0x1e, 0x1e, 0x6e, 0xfe,

    /* U+005E "^" */
    0x1, 0x40, 0x7, 0xc0, 0xf, 0xe0, 0xd, 0xb0,
    0x2c, 0x34, 0x38, 0x2c,

    /* U+005F "_" */
    0xff, 0xf4, 0x0, 0x0,

    /* U+0060 "`" */
    0x78, 0x3, 0x80,

    /* U+0061 "a" */
    0x2f, 0xe0, 0x3e, 0xf8, 0x0, 0x3c, 0x2f, 0xfc,
    0x78, 0x3c, 0x7d, 0xbc, 0x2f, 0xbc,

    /* U+0062 "b" */
    0x3c, 0x0, 0xf, 0x0, 0x3, 0xc0, 0x0, 0xfb,
    0xe0, 0x3f, 0xbf, 0xf, 0x43, 0xd3, 0xc0, 0xb8,
    0xf4, 0x3d, 0x3f, 0xbf, 0xf, 0xbe, 0x0,

    /* U+0063 "c" */
    0xb, 0xf4, 0x3f, 0xbc, 0x7c, 0x0, 0xb8, 0x0,
    0x7c, 0x0, 0x3f, 0xbc, 0xb, 0xf4,

    /* U+0064 "d" */
    0x0, 0xf, 0x0, 0x3, 0xc0, 0x0, 0xf0, 0x2f,
    0xbc, 0x3f, 0xbf, 0x1f, 0x7, 0xcb, 0x80, 0xf1,
    0xf0, 0x7c, 0x3f, 0xbf, 0x2, 0xfb, 0xc0,

    /* U+0065 "e" */
    0xb, 0xe0, 0x3e, 0xbc, 0x78, 0x1e, 0xbf, 0xff,
    0x7c, 0x0, 0x3f, 0xbc, 0xb, 0xf4,

    /* U+0066 "f" */
    0xb, 0xc2, 0xf8, 0x3c, 0xf, 0xfc, 0x7f, 0x83,
    0xc0, 0x3c, 0x3, 0xc0, 0x3c, 0x3, 0xc0,

    /* U+0067 "g" */
    0xb, 0xef, 0xf, 0xaf, 0xcb, 0x80, 0xf2, 0xe0,
    0x3c, 0x3f, 0xbf, 0x2, 0xfb, 0xc0, 0x0, 0xf0,
    0xfa, 0xf8, 0x1f, 0xf4, 0x0,

    /* U+0068 "h" */
    0x3c, 0x0, 0xf, 0x0, 0x3, 0xc0, 0x0, 0xfb,
    0xe0, 0x3f, 0xfe, 0xf, 0x43, 0xc3, 0xc0, 0xf0,
    0xf0, 0x3c, 0x3c, 0xf, 0xf, 0x3, 0xc0,

    /* U+0069 "i" */
    0x14, 0x7c, 0x28, 0x0, 0x3c, 0x3c, 0x3c, 0x3c,
    0x3c, 0x3c, 0x3c,

    /* U+006A "j" */
    0x1, 0x40, 0x3c, 0x2, 0x80, 0x0, 0x3, 0xc0,
    0x3c, 0x3, 0xc0, 0x3c, 0x3, 0xc0, 0x3c, 0x3,
    0xc2, 0xbc, 0x3f, 0x0,

    /* U+006B "k" */
    0x3c, 0x0, 0xf, 0x0, 0x3, 0xc0, 0x0, 0xf0,
    0xbc, 0x3c, 0xbc, 0xf, 0xbc, 0x3, 0xff, 0x0,
    0xfb, 0xe0, 0x3c, 0x3d, 0xf, 0x7, 0xc0,

    /* U+006C "l" */
    0x3c, 0xf3, 0xcf, 0x3c, 0xf3, 0xcf, 0x3c, 0xf0,

    /* U+006D "m" */
    0x3e, 0xf8, 0xbe, 0xf, 0xff, 0xff, 0xd3, 0xd1,
    0xf0, 0x78, 0xf0, 0x3c, 0x1e, 0x3c, 0xf, 0x7,
    0xcf, 0x3, 0xc1, 0xf3, 0xc0, 0xf0, 0x7c,

    /* U+006E "n" */
    0x3e, 0xf8, 0xf, 0xff, 0x83, 0xd0, 0xf0, 0xf0,
    0x3c, 0x3c, 0xf, 0xf, 0x3, 0xc3, 0xc0, 0xf0,

    /* U+006F "o" */
    0xb, 0xf4, 0xf, 0xef, 0x47, 0xc1, 0xf2, 0xe0,
    0x3c, 0x7c, 0x1f, 0xf, 0xef, 0x40, 0xbf, 0x40,

    /* U+0070 "p" */
    0x3e, 0xf8, 0xf, 0xef, 0xc3, 0xd0, 0xf4, 0xf0,
    0x2e, 0x3d, 0xf, 0x4f, 0xef, 0xc3, 0xef, 0x80,
    0xf0, 0x0, 0x3c, 0x0, 0x0,

    /* U+0071 "q" */
    0xb, 0xef, 0xf, 0xef, 0xc7, 0xc1, 0xf2, 0xe0,
    0x3c, 0x7c, 0x1f, 0xf, 0xef, 0xc0, 0xbe, 0xf0,
    0x0, 0x3c, 0x0, 0xf, 0x0,

    /* U+0072 "r" */
    0x3e, 0xd3, 0xfc, 0x3d, 0x3, 0xc0, 0x3c, 0x3,
    0xc0, 0x3c, 0x0,

    /* U+0073 "s" */
    0x1f, 0xe1, 0xfa, 0xc7, 0x80, 0xf, 0xf8, 0x1,
    0xb9, 0xeb, 0xd6, 0xfd, 0x0,

    /* U+0074 "t" */
    0x18, 0x3, 0xc0, 0xff, 0xc7, 0xf8, 0x3c, 0x3,
    0xc0, 0x3d, 0x2, 0xf8, 0xb, 0xc0,

    /* U+0075 "u" */
    0x3c, 0xf, 0x3c, 0xf, 0x3c, 0xf, 0x3c, 0xf,
    0x3c, 0x1f, 0x3f, 0xff, 0xb, 0xef,

    /* U+0076 "v" */
    0x3c, 0x7, 0x87, 0x82, 0xc0, 0xf0, 0xf0, 0x2d,
    0xb4, 0x3, 0xfc, 0x0, 0xfe, 0x0, 0x1f, 0x0,

    /* U+0077 "w" */
    0xf0, 0x3c, 0xf, 0x1d, 0x1f, 0x83, 0x83, 0xcb,
    0xf2, 0xd0, 0xb3, 0xec, 0xf0, 0x1f, 0xd3, 0xf8,
    0x3, 0xf0, 0xfc, 0x0, 0xb8, 0x2f, 0x0,

    /* U+0078 "x" */
    0x7c, 0x3c, 0x2e, 0xb4, 0xf, 0xe0, 0xb, 0xc0,
    0xf, 0xf0, 0x3d, 0xb8, 0xb8, 0x3d,

    /* U+0079 "y" */
    0x3c, 0x7, 0x87, 0x83, 0xc0, 0xf0, 0xe0, 0x2e,
    0xb4, 0x3, 0xfc, 0x0, 0xbd, 0x0, 0xf, 0x0,
    0xaf, 0x40, 0x2f, 0x40, 0x0,

    /* U+007A "z" */
    0x7f, 0xf8, 0xab, 0xd0, 0x3d, 0x2, 0xe0, 0x1f,
    0x1, 0xfa, 0x9b, 0xff, 0x80,

    /* U+007B "{" */
    0xb, 0xc3, 0xe1, 0xe0, 0x78, 0x1e, 0x1f, 0x43,
    0xe0, 0x78, 0x1e, 0x7, 0xc0, 0xf8, 0x1f,

    /* U+007C "|" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+007D "}" */
    0xf8, 0x2f, 0x42, 0xd0, 0xb4, 0x2d, 0x3, 0xd1,
    0xf0, 0xb4, 0x2d, 0xb, 0x4b, 0xc3, 0xe0,

    /* U+007E "~" */
    0x0, 0x0, 0x2f, 0x5c, 0x36, 0xf8, 0x10, 0x0,

    /* U+2022 "•" */
    0x0, 0x3e, 0x3f, 0x2d
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 59, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 60, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 9, .adv_w = 91, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 15, .adv_w = 150, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 38, .adv_w = 133, .box_w = 8, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 64, .adv_w = 182, .box_w = 12, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 91, .adv_w = 151, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 114, .adv_w = 48, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 117, .adv_w = 74, .box_w = 5, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 132, .adv_w = 74, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 144, .adv_w = 90, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 153, .adv_w = 125, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 164, .adv_w = 54, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 168, .adv_w = 80, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 171, .adv_w = 54, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 174, .adv_w = 82, .box_w = 7, .box_h = 12, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 195, .adv_w = 141, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 216, .adv_w = 82, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 228, .adv_w = 123, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 246, .adv_w = 123, .box_w = 9, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 267, .adv_w = 143, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 124, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 132, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 327, .adv_w = 129, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 345, .adv_w = 137, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 366, .adv_w = 132, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 384, .adv_w = 54, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 390, .adv_w = 54, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 397, .adv_w = 125, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 410, .adv_w = 125, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 419, .adv_w = 125, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 432, .adv_w = 123, .box_w = 8, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 450, .adv_w = 215, .box_w = 13, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 486, .adv_w = 159, .box_w = 12, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 513, .adv_w = 159, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 534, .adv_w = 152, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 557, .adv_w = 172, .box_w = 10, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 580, .adv_w = 140, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 598, .adv_w = 133, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 614, .adv_w = 160, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 637, .adv_w = 168, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 658, .adv_w = 68, .box_w = 3, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 665, .adv_w = 113, .box_w = 8, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 683, .adv_w = 154, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 704, .adv_w = 126, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 720, .adv_w = 199, .box_w = 11, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 745, .adv_w = 168, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 766, .adv_w = 176, .box_w = 11, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 791, .adv_w = 152, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 809, .adv_w = 176, .box_w = 11, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 840, .adv_w = 153, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 861, .adv_w = 133, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 879, .adv_w = 129, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 897, .adv_w = 164, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 918, .adv_w = 155, .box_w = 11, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 943, .adv_w = 242, .box_w = 15, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 977, .adv_w = 149, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1000, .adv_w = 141, .box_w = 10, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1023, .adv_w = 140, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1044, .adv_w = 77, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1056, .adv_w = 82, .box_w = 7, .box_h = 12, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 1077, .adv_w = 77, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1089, .adv_w = 125, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 1101, .adv_w = 104, .box_w = 7, .box_h = 2, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1105, .adv_w = 125, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 1108, .adv_w = 128, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1122, .adv_w = 144, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1145, .adv_w = 123, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1159, .adv_w = 144, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1182, .adv_w = 131, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1196, .adv_w = 80, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1211, .adv_w = 146, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1232, .adv_w = 144, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1255, .adv_w = 63, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1266, .adv_w = 64, .box_w = 6, .box_h = 13, .ofs_x = -2, .ofs_y = -2},
    {.bitmap_index = 1286, .adv_w = 137, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1309, .adv_w = 63, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1317, .adv_w = 218, .box_w = 13, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1340, .adv_w = 144, .box_w = 9, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1356, .adv_w = 136, .box_w = 9, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1372, .adv_w = 144, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1393, .adv_w = 144, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1414, .adv_w = 90, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1425, .adv_w = 110, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1438, .adv_w = 90, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1452, .adv_w = 143, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1466, .adv_w = 124, .box_w = 9, .box_h = 7, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 1482, .adv_w = 195, .box_w = 13, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1505, .adv_w = 124, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1519, .adv_w = 124, .box_w = 9, .box_h = 9, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 1540, .adv_w = 113, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1553, .adv_w = 81, .box_w = 5, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1568, .adv_w = 64, .box_w = 2, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 1574, .adv_w = 81, .box_w = 5, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1589, .adv_w = 125, .box_w = 8, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 1597, .adv_w = 74, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 2}
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
    0, 0, 1, 9, 0, 5, -4, 0,
    0, 4, 0, -11, -12, 1, 9, 4,
    3, -8, 1, 8, 0, 8, 2, 6,
    0, -9, 0, 0, 3, 0, 0, 0,
    0, 0, 0, 12, 3, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -6, 0, 0,
    0, 0, 0, -4, 3, 4, 0, 0,
    -2, 0, -1, 2, 0, -2, 0, -2,
    -1, -4, 0, 0, 0, 0, -2, 0,
    0, -2, -3, 0, 0, -2, 0, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, -2, -2, 0, 0, -5,
    0, -26, 0, 0, -4, -10, 4, 6,
    0, 0, -4, 2, 2, 7, 4, -3,
    4, 0, 0, -11, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -7,
    0, 0, 1, 0, 5, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -3, -8, 0, -8, -1, 0, 0, -6,
    0, -1, 6, 0, -6, -3, -1, 1,
    0, -3, 0, 0, -2, -14, 0, 1,
    0, 6, -5, 0, -4, 0, -8, 1,
    0, -17, -3, 4, 2, 0, 0, 0,
    0, 0, 0, 0, 2, 0, -3, -1,
    -3, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 7, 0, 2, 0,
    0, -4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 9, 3, 1, 0, 0,
    0, 0, 0, 21, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -7, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 2, 4,
    2, 6, -2, 0, 0, 4, -2, -7,
    -27, 1, 5, 4, 0, -2, 0, 5,
    0, 6, 0, 6, 0, -20, 0, -3,
    6, 0, 7, -2, 4, 2, 0, 0,
    1, -2, 0, 0, -3, 0, 0, 0,
    17, 0, 6, 0, 8, 3, 8, 3,
    0, 0, -3, -7, 0, 0, 0, -2,
    1, -2, 0, 1, -4, -3, -4, 1,
    0, -2, 0, 0, 0, -8, 1, -5,
    0, -4, -7, 0, -5, -4, -7, 0,
    0, -14, 0, 0, 0, 0, 2, 0,
    0, 0, 0, 0, 2, 0, -3, -5,
    -3, -2, 1, -11, 2, -14, 0, 0,
    0, -7, -2, 0, 19, -3, -3, 2,
    2, 0, 0, -3, 2, 0, 0, -12,
    -4, 7, 0, 11, -7, -1, -8, 0,
    -8, 3, 0, -20, 0, 2, 2, 0,
    -1, 0, 0, 2, 0, 0, -1, -1,
    -7, 0, -7, 0, 0, 12, -4, 0,
    -7, 0, 7, 0, -15, -20, -15, -4,
    6, 0, 0, -13, 0, 1, -5, 0,
    -3, 0, -4, -8, 0, -2, 6, 0,
    6, 0, 6, 0, 0, 3, 6, -24,
    -13, 0, -13, 0, 3, 1, -13, -13,
    -5, -13, -6, -10, -6, -13, 0, 1,
    0, 0, 0, 0, 0, 1, 1, -3,
    -4, 0, -1, -1, -2, 0, 0, -1,
    0, 0, 0, -4, 0, -2, 0, -5,
    -4, 0, -5, -7, -7, -3, 0, -4,
    0, -4, 0, 0, 0, 0, 0, -2,
    0, 0, 2, 0, 1, -2, 1, 0,
    0, 0, 0, 2, -1, 0, 0, 0,
    -1, 2, 2, 0, 0, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 1,
    0, 3, -1, 0, -3, 0, -4, 0,
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
    -5, 0, -1, 0, 0, -9, 2, -1,
    2, -12, 2, 0, 0, 1, -11, 0,
    -12, -2, -18, -1, 0, -10, 0, 4,
    6, 0, 3, 0, 0, 0, 0, 0,
    0, 0, -5, -3, -5, 0, 0, 0,
    0, -2, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -1, -1, 0, -1, -2,
    0, 0, 0, 0, 0, 0, 0, -2,
    -2, 0, -2, -3, -1, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, -1, 0, -4, 2, 0, 0, -1,
    1, 2, 2, 0, 0, 0, 0, 0,
    0, -1, 0, 0, 0, 0, 0, 1,
    0, 0, -2, 0, -2, -1, -1, 0,
    0, 0, 0, 0, 0, 0, 1, 0,
    0, -2, 0, 0, 0, 0, -3, -3,
    -3, 0, 0, 6, -1, 1, -6, 0,
    0, 5, -10, -10, -8, -4, 2, 0,
    -1, -14, -4, 0, -4, 0, -4, 3,
    -4, -12, 0, -5, 0, 0, 1, 0,
    2, -1, 0, 2, 2, -6, -8, 0,
    -10, 0, 0, -5, -4, -6, -1, -5,
    1, -2, 1, -5, 0, 0, 0, -2,
    0, 0, 0, 1, 0, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, -1, 0, 0, -2, 0,
    -4, -5, -5, 0, 0, -6, 0, 0,
    0, 0, 0, 0, 0, -2, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 2,
    0, 0, 0, 0, 0, 0, 0, 0,
    11, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, -2, 0, 2, 0, 7,
    -2, 0, -5, -2, -8, 0, 0, -4,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 3, 0, 1, -2, 1, 0,
    -1, 0, 0, 0, -2, 0, 0, 1,
    0, -10, -6, 0, 0, 0, -3, -10,
    0, 0, -2, 2, 0, -4, -1, -10,
    0, -6, 0, 0, -3, -4, -3, -2,
    -2, 0, 0, -3, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 2, 0,
    2, 0, 0, -4, 0, 0, 0, 0,
    1, 0, 1, -4, -4, 0, -2, -2,
    -3, 0, 0, 0, 0, 0, 0, -6,
    0, -2, 0, -3, -2, 0, -5, -5,
    -6, -1, 0, -4, 2, -6, 0, 0,
    0, 0, 0, 17, 0, 0, 1, 0,
    0, -3, 0, 0, 0, -9, 0, 0,
    0, 0, 0, -20, -5, 7, 6, -2,
    -9, 0, 2, -3, 0, -10, -1, -2,
    2, -15, -2, 5, 0, 3, -7, -3,
    -8, -7, -9, 0, 0, -12, 0, 11,
    0, 0, -1, 0, 0, 0, 0, -1,
    -1, -2, -6, -7, -6, 0, 0, 0,
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
    -1, -4, -1, -1, -1, 0, -1, -1,
    0, 0, 0, 0, 0, 0, -2, -1,
    -1, 0, 0, 0, 0, 1, -1, 0,
    -1, 0, 0, 0, 0, -1, -1, -1,
    -1, -1, -1, -1, 3, 8, 0, 0,
    -5, 0, -1, 4, 0, -2, -8, -2,
    3, 1, 0, -9, -3, 2, -3, 2,
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
    -4, 0, 0, 0, -7, 0, 1, -5,
    4, 0, -1, -9, 0, 0, -5, -2,
    0, -8, -5, -6, 0, 0, -10, -2,
    -8, -9, -11, 0, -3, 0, 3, 13,
    -2, 0, -5, 0, 0, 0, -2, -3,
    -5, -4, -8, -9, -8, -5, 0, 0,
    -1, 0, 1, 0, 0, -15, 0, 6,
    4, -4, -7, 0, 1, -4, 0, -10,
    -1, -2, 4, -18, -2, 1, 0, 0,
    -14, -2, -10, -2, -15, 0, 0, -15,
    0, 10, 1, 0, -1, 0, 0, 0,
    0, 0, -1, -1, -8, -1, -8, 0,
    0, 0, 0, 0, -6, 0, -1, 0,
    -1, -6, -9, 0, 0, -1, -3, -6,
    -2, 0, -2, 0, 0, 0, 0, -9,
    -2, -7, -6, -3, -4, -5, -2, -3,
    0, -4, -1, -7, -3, 0, -2, 0,
    0, -2, -2, 0, 1, 0, -1, -7,
    -1, 0, 0, -4, 0, 0, 0, 0,
    1, 0, 1, -4, 10, 0, -2, -2,
    -3, 0, 0, 0, 0, 0, 0, -6,
    0, -2, 0, -3, -2, 0, -5, -5,
    -6, -1, 0, -4, 3, 8, 0, 0,
    0, 0, 0, 17, 0, 0, 1, 0,
    0, -3, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, -4, 0, 0, 0, 0,
    0, -1, 0, 0, 0, -2, -2, 0,
    0, -4, -2, 0, 0, -4, 0, 3,
    -1, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 4, 3,
    -1, 0, -6, -2, 0, 6, -7, -7,
    -4, -4, 8, 5, 2, -17, -2, 4,
    -2, 0, -2, 4, -2, -7, 0, -2,
    2, -2, -3, -6, -3, 0, 0, 6,
    4, 0, -6, 0, -11, 0, 0, 9,
    -2, -8, 1, -2, -7, -7, -7, -2,
    2, 0, -3, 0, -5, 0, 3, 7,
    -5, -8, -8, -5, 6, 0, 1, -16,
    -2, 2, -4, -2, -5, 0, -5, -8,
    -3, -3, -3, 0, 0, -5, -5, -2,
    0, 6, 5, -2, -11, 0, -11, 0,
    -1, -4, -7, -13, -1, -7, -4, -7,
    -4, -6, 0, 0, -2, 0, -4, -1,
    0, -2, -4, 0, 3, -7, 2, 0,
    0, -12, 0, -2, -5, -4, -2, -6,
    -5, -7, -5, 0, -6, -2, -5, -2,
    -6, -2, 0, 0, 1, 9, -4, 0,
    -6, 0, 0, 0, -2, -4, -5, -6,
    -7, -9, -7, -3, 4, 0, -3, 0,
    -10, -1, 2, 4, -7, -8, -4, -7,
    7, -2, 1, -20, -4, 4, -5, -4,
    -8, 0, -6, -9, -1, -2, -3, -2,
    -5, -6, -1, 0, 0, 6, 7, -1,
    -14, 0, -12, 0, -2, 7, -8, -15,
    -4, -7, -9, -10, -9, -7, 0, 0,
    0, 0, -1, 0, 0, 2, -1, 4,
    1, -3, 4, 0, 0, -4, 0, 0,
    0, 0, 1, 1, -1, 0, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    3, 6, 0, 0, -1, 0, 0, 0,
    0, 0, -1, -1, -1, 0, -1, 0,
    1, 3, 0, 0, 0, 0, 3, 1,
    -1, 0, 9, 0, 5, 1, 1, -3,
    0, 4, 0, 0, 0, 3, 0, 0,
    0, 0, 4, 0, 5, 1, 7, 0,
    0, 6, 0, 7, -1, 0, 0, 0,
    0, 21, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -12, 0, -2, 3, 0,
    6, -27, 0, 19, 1, -4, -4, 2,
    2, -1, 1, -10, 0, 0, 11, -12,
    -4, 6, 0, 6, -4, -2, -8, 4,
    -4, 0, 0, -15, 9, 29, 0, 0,
    0, 0, 0, 25, 0, 0, 0, 0,
    4, 0, 4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, -2, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    4, -5, 0, 0, 1, -2, 0, 2,
    25, -4, -1, 6, 5, -5, 2, 0,
    0, 2, 2, -4, -6, 11, 6, 16,
    0, -2, -2, 9, -1, 4, 0, -27,
    7, 0, -2, 0, -5, 0, 0, 24,
    0, 2, -4, -5, -3, 7, 4, 3,
    0, 0, 0, -6, 0, 0, 0, -5,
    0, 0, 0, 0, -4, -1, 0, 0,
    0, -4, 0, -1, 0, -9, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -13, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    -2, 0, 0, -3, 0, -5, 0, 0,
    0, -3, 2, -1, 0, 0, -5, -2,
    -5, 0, 0, -5, 0, -2, 0, -9,
    0, -4, 0, 0, -14, -2, -8, -4,
    -8, 0, 0, -13, 0, -5, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -3, -4, -3, -1, 0, 0, 0, 0,
    -4, 0, -5, 3, -4, 4, 0, -1,
    -5, -1, -3, -2, 0, -1, -1, -1,
    2, -5, -1, 0, 0, 0, -15, -3,
    -5, 0, -8, 0, -1, -9, -1, 0,
    0, -1, -1, 0, -1, 0, 0, 0,
    2, 0, -2, -3, -2, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 8, 0, 0, 0, 0, 0,
    0, -4, 0, -1, 0, 0, 0, -4,
    2, 0, 0, 0, -5, -2, -4, 0,
    0, -6, 0, -2, 0, -9, 0, 0,
    0, 0, -20, 0, -4, -8, -10, 0,
    0, -13, 0, -2, -3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, -3,
    -2, -1, 1, 0, 0, 3, -3, 0,
    8, 8, -2, -2, -6, 1, 8, 3,
    4, -5, 1, 8, 1, 5, 4, 5,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 13, 9, -5, -2, 2,
    -1, 2, 2, 10, 1, 0, 0, 0,
    2, 0, 2, 0, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 5, 0, 0, 0, 0, -13, -1,
    -3, -7, -8, 0, 0, -13, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 5, 0, 0, 0, 0,
    -13, -1, -3, -7, -8, 0, 0, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -3, 0, 0, 0,
    -5, 2, 0, -2, 3, 5, 2, -6,
    0, 1, -2, 2, 0, 3, 0, 0,
    0, 0, -2, 0, -1, -1, -4, 0,
    -1, -8, 0, 12, -2, 0, -5, 0,
    0, 0, -1, -4, 0, -2, -6, -4,
    -6, -3, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0, 5,
    0, 0, 0, 0, -13, -1, -3, -7,
    -8, 0, 0, -13, 0, 0, 0, 0,
    0, 0, 0, 10, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    -6, -1, -1, 6, -2, -2, -8, 1,
    2, 1, -1, -5, 0, 5, 0, 3,
    1, 3, -4, -9, -1, 0, -4, -2,
    -5, -8, -7, 0, -2, -4, -1, -3,
    -8, -1, -2, 0, -1, 0, -1, 0,
    3, 0, 3, -1, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, -2, -2, 0, 0, -5,
    0, -1, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -12,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, -2, -2, 0,
    0, 0, 0, 0, -1, 0, 0, -3,
    -2, 2, 0, -3, -3, -1, 0, -4,
    -1, -4, -2, -1, 0, -3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -13, 0, 6, 0, 0, -4, 0,
    0, 0, 0, 0, -2, 0, -2, 0,
    -2, 0, 0, 0, -1, 0, -5, 0,
    0, 8, -3, -7, -7, 1, 4, 4,
    1, -7, 1, 3, 1, 6, 1, 7,
    -1, -6, 0, 0, -4, 0, 0, -6,
    -5, 0, 0, -4, 0, -3, -4, 0,
    -3, 0, 0, -3, 0, -2, 3, 0,
    -3, -6, -3, -2, 0, 0, -1, 0,
    -4, 0, 0, 3, -5, 0, 2, -2,
    3, 2, 0, -7, 0, -1, -1, 0,
    -2, 4, -3, 0, 0, 0, -5, -1,
    -5, 0, -6, 0, 0, -9, 0, 7,
    -2, 0, -4, 0, 0, 5, 0, -2,
    0, -2, -6, 0, -6, -2, 0, 0,
    0, 0, -1, 0, 0, 2, -2, 1,
    0, 0, -1, -1, 0, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -12,
    0, 4, 0, 0, -1, 0, 0, 0,
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
const lv_font_t MontserratBold13Trae = {
#else
lv_font_t MontserratBold13Trae = {
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



#endif /*#if MONTSERRATBOLD13TRAE*/
