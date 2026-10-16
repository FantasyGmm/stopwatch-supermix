# Read today's usage by model

Usage keeps the 7D remaining quota and reset countdown above today's total tokens
and API-equivalent USD estimate. The model section shows one model at a time;
tap its arrows to inspect another model without leaving Usage. It shows fresh
input, output, cache-read and cache-write tokens, model total/cost, and estimated
TPS / median TTFT with the number of valid samples.

Prices come from the existing models.dev catalog and are applied to each
request's context tier and token buckets on the Mac. A model without a public
price remains unpriced. The displayed amount values the recorded usage at API
rates; it is not the subscription's monthly price or an account bill.

## CC Switch session timing is an estimate

The Agent reads today's `app_type='codex', data_source='codex_session'` rows from
CC Switch's schema-20 `proxy_request_logs`. It does not combine them with proxy
traffic or daily rollups. Local-day boundaries and the existing two-second
read deadline, request bound, 60-second refresh and 120-second cache TTL apply.
An idle day remains valid: freshness measures successful collection, not the
time of the last request.

The calculation follows CC Switch's session-statistics method:

- **Estimated TPS:** sum output tokens over samples with at least 200 output
  tokens and `latency_ms >= 1000`, divided by their total latency in seconds.
  Do not average per-request TPS or subtract estimated TTFT from this denominator.
- **Estimated TTFT:** median of samples with `0 < first_token_ms <= latency_ms`;
  for an even count, average the middle two values. The transmitted unit is ms.
- Each value carries its own valid-sample count alongside the model's request
  count. No samples produces `null`, displayed as `--`.

CC Switch derives session timing from event boundaries and output-item start
times, including client overhead. Its separate proxy timing uses a different
denominator. Session imports also use fixed success-status fields; they cannot
establish API success/error rates. Sources:
[usage_stats.rs](https://github.com/farion1231/cc-switch/blob/main/src-tauri/src/services/usage_stats.rs),
[session_usage_codex.rs](https://github.com/farion1231/cc-switch/blob/main/src-tauri/src/services/session_usage_codex.rs).

Token normalization preserves the stored `input_token_semantics`: semantics 0
subtracts cache reads from input, 1 subtracts cache reads and writes, and 2 uses
input as fresh input. Total = fresh input + cache reads + cache writes + output.
Reasoning is already included in output. Invalid token records make the token
result incomplete; missing prices only affect cost coverage. See CC Switch's
[normalization helpers](https://github.com/farion1231/cc-switch/blob/main/src-tauri/src/services/sql_helpers.rs).

## Optional wire extension: `usage.d`

The existing `telemetry_v: 1` fields remain unchanged, including 5H fields for
older clients. Only the new Usage view hides 5H. There is no new BLE service,
quota request, hardware owner or transport. The extension contains display
values only; the device performs no collection or price calculation.

`d` contains:

| Field | Meaning |
| --- | --- |
| `v` | Detail version, `1` |
| `src` | `ccs_session` or `rollout` |
| `p` | Token statistics are incomplete, independent of missing prices |
| `m` | Up to six model rows, ordered by descending total tokens, model-name tie-break |
| `more` | Number of model rows not sent |

Each row contains:

| Field | Meaning |
| --- | --- |
| `m` | Nonempty printable-ASCII model identifier, at most 48 bytes |
| `n` | Request/sample-event count for this model |
| `t` | Total tokens, equal to `i + o + r + w` |
| `i`, `o`, `r`, `w` | Fresh input, output, cache-read, cache-write tokens |
| `c`, `cs` | Nullable estimated USD cost; coverage `ok`, `partial` or `unavailable` |
| `p` | This model's token count is incomplete |
| `ts`, `ft` | Nullable estimated TPS and median TTFT in ms |
| `tn`, `fn` | TPS and TTFT valid-sample counts, each no greater than `n` |

Integers are nonnegative and no greater than 9,007,199,254,740,991. Cost values
are finite and nonnegative; non-null performance values are finite and positive.
Both are no greater than 1,000,000,000. Each performance value is paired with
its valid-sample count: an unavailable value has zero samples, and a non-null
value has at least one sample.
The rollout fallback has no reliable timing samples and sends null performance
values with zero sample counts.

The sender checks the complete 2048-byte status budget in the negotiated JSON
or MessagePack encoding and removes detail rows
from the tail when needed, increasing `more`. Total usage remains the total of
the collection, not just the visible models. Malformed optional details are not
displayed; valid basic quota/host data remain usable. An absent or unknown
detail version leaves the new watch's basic weekly/total view available. Older
firmware ignores the optional field. Date changes and usage expiry clear model
details along with the daily totals.
