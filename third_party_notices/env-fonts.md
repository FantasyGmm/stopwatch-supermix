# Environmental application fonts

`EnvSensorText16.c`, `EnvSensorTitle22.c` and `EnvSensorLauncher26.c` are subsets
of Noto Sans SC Regular, distributed with the already pinned LVGL dependency:

- LVGL commit: `85aa60d18b3d5e5588d7b247abf90198f07c8a63`
- Source: `tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf`
- Copyright 2014-2021 Adobe (http://www.adobe.com/), Reserved Font Name 'Source'
- License: SIL Open Font License 1.1; complete text in `env-fonts-OFL.txt`

The subset names do not use the reserved font name. The generated font data
remains under OFL-1.1. Regenerate it with `scripts/generate_env_fonts.sh` using
`lv_font_conv` 1.5.3. The converter is a development tool installed into the
project's sibling `work/env-font-tools` directory, not a firmware dependency.
Normal firmware builds use the checked-in C assets and do not require Node.js
or a font download.

The environment page now uses the separate 4-bpp `EnvCaption16` subset of the
same pinned Noto font. Its numeric `EnvValue91` and `EnvValue42` subsets use
Maple Mono Medium v7.9:

- [Maple Mono v7.9 source release](https://github.com/subframe7536/maple-font/releases/tag/v7.9)
- Source file: `MapleMono-Medium.ttf` from `MapleMono-TTF.zip`
- Copyright 2022 The Maple Mono Project Authors
- License: SIL Open Font License 1.1; complete text in `env-maple-OFL.txt`

These renamed subsets do not use the original reserved font names. Regenerate
the page fonts with `main/apps/app_env_iii/assets/generate_fonts.sh`, setting
`STOPWATCH_MAPLE_FONT` to that source TTF. It reuses `lv_font_conv` 1.5.3 in the
same development-tool directory above. All generated C assets are checked in;
normal builds still require neither Node.js nor downloaded font sources.
