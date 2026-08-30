#!/usr/bin/env python3
"""Regenerate the native 17 px font used by Codex task reply details."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
FONT_SOURCE = Path("components/lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf")
FONT_OUTPUT = Path("main/assets/fonts/CodexDetail17.c")
REQUIRED_GLYPHS = Path("main/assets/fonts/CodexDetail17.required-glyphs.txt")


def current_symbols() -> str:
    header = (ROOT / FONT_OUTPUT).read_text(encoding="utf-8").split("#ifdef", 1)[0]
    return header.split("--symbols ", 1)[1].split(" --format ", 1)[0]


def main() -> None:
    required = "".join(
        character
        for character in (ROOT / REQUIRED_GLYPHS).read_text(encoding="utf-8")
        if not character.isspace()
    )
    symbols = "".join(dict.fromkeys(current_symbols() + required))
    subprocess.run(
        [
            "npx",
            "--yes",
            "lv_font_conv@1.5.3",
            "--size",
            "17",
            "--bpp",
            "2",
            "--no-prefilter",
            "--no-kerning",
            "--font",
            str(FONT_SOURCE),
            "-r",
            "0x20-0x7e",
            "--symbols",
            symbols,
            "--format",
            "lvgl",
            "--lv-include",
            "lvgl.h",
            "--lv-font-name",
            "CodexDetail17",
            "-o",
            str(FONT_OUTPUT),
        ],
        check=True,
        cwd=ROOT,
    )
    output_path = ROOT / FONT_OUTPUT
    output_path.write_text(
        output_path.read_text(encoding="utf-8").rstrip() + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
