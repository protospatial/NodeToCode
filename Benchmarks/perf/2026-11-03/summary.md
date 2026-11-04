Commit 6618c03ec972, 2026-11-03; CPU Intel64 Family 6 Model 151 Stepping 2, GenuineIntel (24 threads), 31.8 GB RAM.

| Fixture | Engine | Nodes | Read ms (median of 5) | Agent JSON bytes min / pretty | Translation JSON bytes min / pretty | Translation tokens chars_div4 | Translation tokens o200k_base | Translation tokens qwen | Translation tokens anthropic | Bytes per node (translation, min) | Tokens per node (chars/4 / Anthropic) | Working set delta MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| BP_N2CPerf_100 | 5.8 | 100 | 158.5 | 112,863 / 205,637 | 49,450 / 99,198 | 12,363 | n/m | n/m | 23,185 | 494.5 | 123.6 / 231.8 | -24.4 |
| BP_N2CPerf_1k | 5.8 | 1,000 | 1,705.2 | 1,148,351 / 2,094,251 | 490,833 / 992,211 | 122,709 | n/m | n/m | 228,478 | 490.8 | 122.7 / 228.5 | 8.2 |
| BP_N2CPerf_5k | 5.8 | 5,000 | 4,467.4 | 5,652,146 / 10,246,635 | 2,430,054 / 4,873,973 | 607,514 | n/m | n/m | 1,135,095 | 486.0 | 121.5 / 227.0 | 33.7 |
| BP_N2CPerf_100 | 5.6 | 100 | 57.3 | 112,863 / 205,637 | 49,450 / 99,198 | 12,363 | n/m | n/m | 23,185 | 494.5 | 123.6 / 231.8 | 0.7 |
| BP_N2CPerf_1k | 5.6 | 1,000 | 615.6 | 1,148,351 / 2,094,251 | 490,833 / 992,211 | 122,709 | n/m | n/m | 228,478 | 490.8 | 122.7 / 228.5 | 9.2 |
| BP_N2CPerf_5k | 5.6 | 5,000 | 3,161.7 | 5,652,146 / 10,246,635 | 2,430,054 / 4,873,973 | 607,514 | n/m | n/m | 1,135,095 | 486.0 | 121.5 / 227.0 | 25.3 |

Trace scopes (UnrealInsights timer statistics, inclusive, over every call in the capture session: the warm-up and timed reads of all three fixtures, then the depth reads):

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

Max Translation Depth on BP_N2CPerf_5k F_Chain_0:

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

Other:

| Engine | tools/list bytes | tools/list tokens chars/4 / Anthropic | Tagged graphs | SearchTags ms | GetAllTags ms |
|---|---|---|---|---|---|
| 5.8 | 121,667 | 30,417 / 46,203 | 43 | 0.765 | 0.748 |
| 5.6 | 121,667 | 30,417 / 46,203 | 43 | 0.330 | 0.293 |
