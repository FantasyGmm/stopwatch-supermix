#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="${STOPWATCH_FONT_TOOLS_DIR:-$(dirname "$PROJECT_DIR")/work/env-font-tools}"
CONVERTER="$TOOLS_DIR/node_modules/lv_font_conv/lv_font_conv.js"
if [[ ! -f "$CONVERTER" ]]; then
    npm install --prefix "$TOOLS_DIR" --cache "$TOOLS_DIR/npm-cache" \
        --registry=https://registry.npmmirror.com --ignore-scripts \
        --no-audit --no-fund lv_font_conv@1.5.3
fi

cd "$PROJECT_DIR"
FONT="components/lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf"
if [[ ! -f "$FONT" ]]; then
    echo "Materialize the pinned LVGL dependency with fetch_repos.py first." >&2
    exit 2
fi
SYMBOLS="$(cat tools/env-font-characters.txt)"
node "$CONVERTER" --font "$FONT" --size 16 --bpp 2 --format lvgl \
    --no-compress --range 32-126,0xB0,0xB7 --symbols "$SYMBOLS" \
    --lv-include lvgl.h --lv-font-name EnvSensorText16 \
    -o main/assets/fonts/EnvSensorText16.c
for SPEC in 22:EnvSensorTitle22 26:EnvSensorLauncher26; do
    node "$CONVERTER" --font "$FONT" --size "${SPEC%%:*}" --bpp 2 --format lvgl \
        --no-compress --symbols "环境监测" --lv-include lvgl.h \
        --lv-font-name "${SPEC#*:}" -o "main/assets/fonts/${SPEC#*:}.c"
done

python3 - <<'PY'
from pathlib import Path

notice = (
    "/* SPDX-License-Identifier: OFL-1.1\n"
    " * Derived from the pinned LVGL NotoSansSC-Regular.ttf;\n"
    " * Copyright 2014-2021 Adobe (http://www.adobe.com/).\n"
    " * See third_party_notices/env-fonts-OFL.txt and env-fonts.md.\n"
    " * Regenerate with scripts/generate_env_fonts.sh.\n */\n"
)
for name in ("EnvSensorText16", "EnvSensorTitle22", "EnvSensorLauncher26"):
    path = Path("main/assets/fonts") / (name + ".c")
    path.write_text(notice + path.read_text(encoding="utf-8"), encoding="utf-8")
PY
