#!/usr/bin/env bash
set -euo pipefail

ASSET_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$ASSET_DIR/../../../.." && pwd)"
TOOLS_DIR="${STOPWATCH_FONT_TOOLS_DIR:-$(dirname "$PROJECT_DIR")/work/env-font-tools}"
CONVERTER="$TOOLS_DIR/node_modules/lv_font_conv/lv_font_conv.js"
MAPLE_FONT="${STOPWATCH_MAPLE_FONT:?Set STOPWATCH_MAPLE_FONT to MapleMono-Medium.ttf from Maple Mono v7.9.}"
NOTO_FONT="$PROJECT_DIR/components/lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf"

if [[ ! -f "$CONVERTER" || ! -f "$MAPLE_FONT" || ! -f "$NOTO_FONT" ]]; then
    echo "Require lv_font_conv 1.5.3, Maple Mono v7.9 Medium, and the pinned LVGL Noto font." >&2
    exit 2
fi

# Source: https://github.com/subframe7536/maple-font/releases/download/v7.9/MapleMono-TTF.zip
# Use final-size, 4-bit coverage masks rather than scaling the shared 2-bit fonts.
for SIZE in 91 42; do
    node "$CONVERTER" --font "$MAPLE_FONT" --size "$SIZE" --bpp 4 --format lvgl \
        --no-compress --symbols='-.0123456789' --lv-include lvgl.h \
        --lv-font-name "EnvValue${SIZE}" -o "$ASSET_DIR/EnvValue${SIZE}.c"
done

node "$CONVERTER" --font "$NOTO_FONT" --size 16 --bpp 4 --format lvgl \
    --no-compress --range 32-126,0xB0,0xB7 \
    --symbols "$(cat "$PROJECT_DIR/tools/env-font-characters.txt")" \
    --lv-include lvgl.h --lv-font-name EnvCaption16 -o "$ASSET_DIR/EnvCaption16.c"

python3 - "$ASSET_DIR" <<'PY'
from pathlib import Path
import re
import sys

assets = Path(sys.argv[1])
for name in ("EnvValue91", "EnvValue42", "EnvCaption16"):
    copyright = (
        "Copyright 2014-2021 Adobe (http://www.adobe.com/)."
        if name == "EnvCaption16"
        else "Copyright 2022 The Maple Mono Project Authors (https://github.com/subframe7536/maple-font)."
    )
    source = "pinned LVGL NotoSansSC-Regular.ttf" if name == "EnvCaption16" else "Maple Mono v7.9 Medium"
    license = "third_party_notices/env-fonts-OFL.txt" if name == "EnvCaption16" else "third_party_notices/env-maple-OFL.txt"
    notice = (
        "/* SPDX-License-Identifier: OFL-1.1\n"
        f" * {copyright}\n"
        f" * Derived from {source}; see {license}.\n"
        " * Regenerate with main/apps/app_env_iii/assets/generate_fonts.sh.\n"
        " */\n"
    )
    path = assets / f"{name}.c"
    data = path.read_text(encoding="utf-8")
    # Keep generated headers independent of the developer's absolute checkout paths.
    data = re.sub(r" \* Opts:.*\n", "", data)
    path.write_text(notice + data.rstrip() + "\n", encoding="utf-8")
PY
