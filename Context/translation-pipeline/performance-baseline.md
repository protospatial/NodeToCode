# Performance and token baseline

How long capture takes, how large N2C JSON is and how many tokens it costs, on card 0359's perf fixtures
(`BP_N2CPerf_100`, `_1k`, `_5k` in `Tests/NodeToCodeTestContent`). Card 0360. These numbers are the baseline for the
budget and chunking design, the tag-list work and the format benchmark (card 0367), and they test the claim that N2C
JSON is token-efficient.

## Rerun

```
python -I Benchmarks/perf/run_perf.py --engines 5.8 5.6
```

Run it from the checkout with the build.ps1 hosts built, `N2C_PORT_OFFSET` set, and no other build or editor on the
machine. Check with `aw sys --quiet-for timing` and hold `aw lease cpu --cpu 24` for the run: timings are wall clock.

What it does:
- It starts a headless editor per engine with `-trace=cpu,memory` and runs `Benchmarks/perf/capture_in_editor.py`.
- It exports the trace's timer statistics with UnrealInsights.
- It reads `tools/list` from a second headless editor.
- It counts tokens with `Benchmarks/common/tokens.py`.

It writes `Benchmarks/perf/<date>/<engine>/` (`editor_results.json`, `timers_n2c.csv` with the N2C_* timer rows,
`tokens.json`) and `<date>/summary.json` and `summary.md`, whose tables are copied below. The traces (about 1.5 GB per
engine) and the captured documents stay under `BuildLogs/perf/`.

What is measured:
- **Read:** `nodetocode.get_blueprint_n2cjson` of the whole Blueprint, the path the MCP get-blueprint-n2cjson tool and
  the Python bridge share. One warm-up, then the median of 5 reads.
- **Agent document:** with `ids` {short, guid} and the `guids` table.
- **Translation document:** the same with `ids.short` as `id` and no `guids`, which is what a translation prompt
  carries.
- **Tokens:**
  - `chars_div4`: the plugin's own estimate (N2CTokenEstimationService).
  - `anthropic`: Anthropic `count_tokens` with claude-haiku-5-5, for one user message holding the document.
  - `o200k_base` (tiktoken) and `qwen` (Hugging Face tokenizers): counted only when their tokenizer files are already on
    the machine; `tokens.py` never downloads one. They were not there for this run, so those columns read n/m.
- **Working set delta:** the editor process's working set before and after the 5 timed reads. It is noisy, and can be
  negative when the garbage collector runs.
- **Trace scopes:** UnrealInsights `TimingInsights.ExportTimerStatistics`, inclusive times. Each row covers every call
  in the session (the warm-up and timed reads of all three fixtures, then the depth reads), so its median mixes
  fixture sizes; its max is the 5k fixture.

## Run of 2026-11-03

- **Code:** commit 6618c03ec972 (dev-2.0 after #344).
- **Engines:** UE 5.8.4-58902052 and UE 5.6.1-44394996.
- **Machine:** Intel64 Family 6 Model 151 (24 threads), 31.8 GB RAM, Windows 11.

**Timings were taken under load.** The machine was quiet at `aw sys` (`record: 2026-11-03T22:58:42Z; cpu 5.7 %;
leases 0; locks 0`), but by the time the run started, lane-b's and lane-ui's builds held both CPU leases. Read
times and scope times are therefore high and noisy: a trial run on 5.8 with one other build running read the 1k
fixture in 822 ms, against 1,705 ms here. Sizes and token counts do not depend on load. Rerun in a quiet window before
using the timings for a budget.

### Fixtures

| Fixture | Engine | Nodes | Read ms (median of 5) | Agent JSON bytes min / pretty | Translation JSON bytes min / pretty | Tokens chars/4 | Tokens Anthropic | Bytes per node | Tokens per node chars/4 / Anthropic | Working set delta MB |
|---|---|---|---|---|---|---|---|---|---|---|
| BP_N2CPerf_100 | 5.8 | 100 | 158.5 | 112,863 / 205,637 | 49,450 / 99,198 | 12,363 | 23,185 | 494.5 | 123.6 / 231.8 | -24.4 |
| BP_N2CPerf_1k | 5.8 | 1,000 | 1,705.2 | 1,148,351 / 2,094,251 | 490,833 / 992,211 | 122,709 | 228,478 | 490.8 | 122.7 / 228.5 | 8.2 |
| BP_N2CPerf_5k | 5.8 | 5,000 | 4,467.4 | 5,652,146 / 10,246,635 | 2,430,054 / 4,873,973 | 607,514 | 1,135,095 | 486.0 | 121.5 / 227.0 | 33.7 |
| BP_N2CPerf_100 | 5.6 | 100 | 57.3 | 112,863 / 205,637 | 49,450 / 99,198 | 12,363 | 23,185 | 494.5 | 123.6 / 231.8 | 0.7 |
| BP_N2CPerf_1k | 5.6 | 1,000 | 615.6 | 1,148,351 / 2,094,251 | 490,833 / 992,211 | 122,709 | 228,478 | 490.8 | 122.7 / 228.5 | 9.2 |
| BP_N2CPerf_5k | 5.6 | 5,000 | 3,161.7 | 5,652,146 / 10,246,635 | 2,430,054 / 4,873,973 | 607,514 | 1,135,095 | 486.0 | 121.5 / 227.0 | 25.3 |

Token counts are of the minified translation document. o200k_base and Qwen: not measured (no tokenizer file on the
machine).

### Trace scopes (inclusive)

| Scope | Engine | Calls | Total s | Median ms | Max ms |
|---|---|---|---|---|---|
| N2C_Capture_BuildStruct | 5.8 | 54 | 5.606 | 12.256 | 1,038.0 |
| N2C_Capture_CollectNodes | 5.8 | 36 | 0.002 | 0.045 | 0.1 |
| N2C_Serialize_ToJson | 5.8 | 54 | 16.738 | 33.461 | 2,785.4 |
| N2C_Validate | 5.8 | 108 | 3.523 | 3.657 | 303.1 |
| N2C_Capture_BuildStruct | 5.6 | 54 | 2.859 | 10.460 | 419.3 |
| N2C_Capture_CollectNodes | 5.6 | 36 | 0.002 | 0.038 | 0.2 |
| N2C_Serialize_ToJson | 5.6 | 54 | 7.608 | 20.122 | 1,044.2 |
| N2C_Validate | 5.6 | 108 | 1.722 | 3.032 | 114.5 |

N2C_TokenEstimate and N2C_Mcp_* do not appear: a script read runs neither the estimator nor an MCP tool call.

### Max Translation Depth on BP_N2CPerf_5k, function F_Chain_0

| Engine | Depth | Graphs | Nodes | Read ms | Translation bytes | Tokens chars/4 | Tokens Anthropic |
|---|---|---|---|---|---|---|---|
| 5.8 | 0 | 1 | 20 | 18.3 | 10,308 | 2,577 | 4,810 |
| 5.8 | 1 | 2 | 43 | 44.1 | 22,279 | 5,570 | 10,369 |
| 5.8 | 2 | 3 | 64 | 62.7 | 33,759 | 8,440 | 15,657 |
| 5.8 | 3 | 4 | 86 | 85.5 | 46,495 | 11,624 | 21,491 |
| 5.8 | 4 | 5 | 108 | 105.0 | 58,235 | 14,559 | 26,805 |
| 5.8 | 5 | 6 | 129 | 132.8 | 69,170 | 17,293 | 31,734 |
| 5.6 | 0 | 1 | 20 | 17.6 | 10,308 | 2,577 | 4,810 |
| 5.6 | 1 | 2 | 43 | 39.4 | 22,279 | 5,570 | 10,369 |
| 5.6 | 2 | 3 | 64 | 46.4 | 33,759 | 8,440 | 15,657 |
| 5.6 | 3 | 4 | 86 | 88.2 | 46,495 | 11,624 | 21,491 |
| 5.6 | 4 | 5 | 108 | 83.6 | 58,235 | 14,559 | 26,805 |
| 5.6 | 5 | 6 | 129 | 101.1 | 69,170 | 17,293 | 31,734 |

### tools/list and the tag search

| Engine | tools/list bytes | tools/list tokens chars/4 / Anthropic | Tagged graphs | SearchTags ms | GetAllTags ms |
|---|---|---|---|---|---|
| 5.8 | 121,667 | 30,417 / 46,203 | 43 | 0.765 | 0.748 |
| 5.6 | 121,667 | 30,417 / 46,203 | 43 | 0.330 | 0.293 |

These are the default tool mode, with 66 tools at this commit. The tag search tags every function graph of the three
fixtures (43 graphs), times `UN2CTagBlueprintLibrary::SearchTags` and `GetAllTags` (median of 5), then removes the tags.
It is the headless proxy for the tag manager's search: the widget itself is not driven.

## What the numbers say

- **The chars/4 estimate undercounts Claude tokens by about 47%.** Anthropic counts 1.87x chars/4 on every fixture
  (about 227 against 121.5 tokens per node) and 1.52x on tools/list. N2C JSON is short keys, IDs and punctuation, which
  tokenize densely. Budgets, the context-window visualizer and the toFile threshold all read chars/4 today.
- **Size is linear in nodes:** about 490 bytes per node in the translation document. That is roughly 230 Claude tokens
  per node, so a 1k-node Blueprint is about 228k tokens and a 5k-node one about 1.1M. The agent document is 2.3x the
  translation document (GUIDs and the guids table), and pretty printing doubles either.
- **Depth costs one graph at a time:** each depth step on the chain adds about 21 nodes and 5k Claude tokens, linearly.
- **Serialization dominates capture time.** N2C_Serialize_ToJson takes 3x N2C_Capture_BuildStruct (5.8: 16.7 s against
  5.6 s in total; max 2.8 s against 1.0 s on the 5k fixture). The read path also parses the serialized JSON again to
  expand the IDs (N2CMcpBlueprintExport::ToResultJson), which no scope measures.
- **tools/list is 121.7 KB, about 46k Claude tokens**, in the default mode with 66 tools.
- **The tag search costs under 1 ms** with 43 tagged graphs, so the tag store is not the bottleneck.
