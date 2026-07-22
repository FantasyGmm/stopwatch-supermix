# XiaoZhi Build Input Provenance

The unified StopWatch firmware uses two unmodified upstream files without
including the standalone XiaoZhi application source tree.

## Asset Builder

- Project: `78/xiaozhi-esp32`
- Repository: `https://github.com/78/xiaozhi-esp32.git`
- License: MIT
- Release: `v2.2.6`
- Commit: `49ac8a6da399f27a9546d4f73640b7f86c24bac6`
- Upstream path: `scripts/build_default_assets.py`
- Local path: `tools/xiaozhi/build_default_assets.py`
- SHA256: `b0fd24a9f2be39e94ecfaf25cac8a5f53f77a5dcc0d9ea58167c193357905338`

## CJK Font Source

- Component: `78/xiaozhi-fonts`
- Repository: `https://github.com/78/xiaozhi-fonts`
- License: MIT
- ESP Component Registry version: `1.6.0`
- Registry component hash: `19ee829a7439702e18084a313b7b86017d3b8d17a094684575edc76bb6d11023`
- Upstream component path: `src/font_puhui_14_1.c`
- Local path: `main/assets/fonts/font_puhui_14_1.c`
- SHA256: `0684c8dbe08fe5ab374037b222fcc1376c28290146828baa67cf353ac465eac1`

Both local files are byte-identical to the sources identified above. Any update
must replace only the reviewed input, update this provenance record, and pass
the cryptographic source-contract tests before firmware validation.
