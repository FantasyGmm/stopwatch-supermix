# CC Switch usage-statistics compatibility

The Rust Agent reads CC Switch's local usage database and independently applies
its documented cache/input-token semantics. No CC Switch executable, provider
configuration, credentials, or request bodies are bundled or copied.

References:
- [CC Switch usage guide](https://github.com/farion1231/cc-switch/blob/main/docs/user-manual/en/4-proxy/4.4-usage.md)
- [Cache-normalized aggregation semantics](https://github.com/farion1231/cc-switch/blob/main/src-tauri/src/services/sql_helpers.rs)
- [Session-estimated TPS and median TTFT](https://github.com/farion1231/cc-switch/blob/main/src-tauri/src/services/usage_stats.rs)
- [Codex session timing import](https://github.com/farion1231/cc-switch/blob/main/src-tauri/src/services/session_usage_codex.rs)
- [4.0.13 schema](https://github.com/farion1231/cc-switch/blob/v4.0.13/src-tauri/src/database/schema.rs)
- [4.0.13 release](https://github.com/farion1231/cc-switch/releases/tag/v4.0.13)

CC Switch is MIT licensed. The exact tagged parser was unavailable during
inspection; the adapter checks schema 20 and explicit per-row semantics instead
of depending on its private importer implementation. Its imported cache buckets
are used as reported, so any upstream omission also affects this estimate.
Performance aggregation follows the upstream `main` session-statistics formulas
inspected on 2026-10-16. Imported session timing is shown as estimated, with
valid-sample counts; it is not combined with the separate proxy timing metric.

## Upstream license

MIT License

Copyright (c) 2025 Jason Young

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
