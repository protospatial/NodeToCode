# Performance budget and chunking design

Card 0362 (epic 0017; review items later-7, gap-6, llm-layer-19). This page proposes a performance budget for capture
and a design for translating graphs that do not fit a model's context window. The budget is ready for the owner's
decision (card 0363). Chunking is not built: the follow-up cards at the end carry it. Input:
[performance-baseline.md](performance-baseline.md) (card 0360, run of 2026-11-03).

## The budget

The limits live in `Benchmarks/perf/budget.json`. `python -I Benchmarks/perf/check_budget.py [run folder]` compares a
`run_perf.py` result with them and exits 1 on any breach (0 when every measured limit holds, 2 when nothing could be
read). A metric the run did not record is reported as "not measured" and fails nothing.

The baseline was taken while two builds ran (see the baseline page), so the time limits keep about 15-30% headroom
over the loaded 5.8 numbers. They should be tightened after a quiet-window rerun. Sizes and token counts do not depend
on load, so those limits sit about 8-10% above the measured values.

| Limit (`budget.json` id) | Proposed | Measured 2026-11-03 (5.8 / 5.6) | Why this number |
|---|---|---|---|
| Whole read of 1k nodes, median (`read_ms_1k`) | 2,000 ms | 1,705 / 616 | Capture + validate + serialize + ID expansion, the get-blueprint-n2cjson path. 5.8 was 822 ms with one build running |
| Whole read of 5k nodes, median (`read_ms_5k`) | 8,000 ms | 4,467 / 3,162 | Same path. A 5k-node Blueprint is rare; the read runs only when asked |
| Longest `N2C_Capture_BuildStruct` (`build_struct_max_ms`) | 1,500 ms | 1,038 / 419 | Game-thread hitch of the struct build on 5k nodes |
| Longest `N2C_Serialize_ToJson` (`serialize_max_ms`) | 4,000 ms | 2,785 / 1,044 | Serialization is 3x the struct build: the first place to optimise (follow-up) |
| Longest `N2C_Validate` (`validate_max_ms`) | 500 ms | 303 / 114 | |
| Longest `N2C_TokenEstimate` tick (`token_estimate_tick_ms`) | 4 ms | not measured | One estimator tick runs on the game thread while the window is open; 4 ms keeps a 60 fps editor under a quarter frame. A script read never runs the estimator, so run_perf.py needs an estimator pass (follow-up) |
| Claude tokens per node, minified translation document (`tokens_per_node_anthropic`) | 250 | 231.8 (100-node fixture, the worst) | About 227-232 on every fixture: a format change that adds keys shows here first |
| chars/4 tokens per node (`tokens_per_node_chars4`) | 135 | 123.6 | What the plugin's own estimate sees |
| Bytes per node (`bytes_per_node`) | 550 | 494.5 | |
| tools/list, default mode, Claude tokens (`tools_list_tokens_anthropic`) | 50,000 | 46,203 | What a client pays before its first call: 66 tools |
| tools/list bytes (`tools_list_bytes`) | 135,000 | 121,667 | |
| Tag search, 43 tagged graphs (`tag_search_ms`) | 5 ms | 0.77 / 0.33 | The tag manager's search box |

## 1. What has to fit

A translation request is the system prompt, the reference source files, the N2C JSON and room for the answer:

```
needed = system_prompt + reference_files + input_json + expected_output + safety
fits when needed <= context_window
```

| Term | Today | Proposed |
|---|---|---|
| system_prompt | `ComputeRequestOverheadTokens()`: the language's CodeGen prompt with `{{N2C_JSON_SPEC}}` and `{{N2C_RESPONSE_FORMAT}}` expanded (CodeGen_CPP.md 9.8 KB + N2CJsonSpec.md 22.3 KB + N2CResponseFormat.md 1.8 KB, about 34 KB) at chars/4. A near-empty C++ translation measured 13,082 Anthropic input tokens (click check of 2026-11-03), so chars/4 (about 8.5k) undercounts it | Count it once per language with the provider's own counter where one exists, and cache it; else chars/4 x the measured ratio |
| reference_files | `GetReferenceFilesTokenEstimate()`, included | Same |
| input_json | chars/4 of the minified translation document | chars/4 x 1.87 for Claude (the baseline's measured ratio), a per-dialect ratio otherwise; replaced by a real count when the provider offers one (Anthropic `count_tokens`, Gemini `countTokens`) |
| expected_output | Not reserved: the usable window is `ContextWindowFactor` 0.45 of the full window. `ExpectedOutputRatio` 1.0 feeds only the cost | `min(max_output, ExpectedOutputRatio x input_json)`, capped at the model's `max_output` from the catalog |
| context_window | Cloud: the catalog row's `context_window`. OpenAI-Compatible: the live list's context length (0216), else 128,000. LM Studio: 128,000. Ollama: `NumCtx` when set, else 128,000, while Ollama itself then uses its own small default and cuts the prompt silently (`WarnIfPromptExceedsContext` only warns when NumCtx is set) | Local providers: ask the server (Ollama `/api/show` `model_info.*.context_length` and the `num_ctx` actually sent; LM Studio `/api/v0/models` `max_context_length`; OpenAI-compatible live lists). Unknown means 8,192, never 128,000 |
| safety | (0.55 of the window, through the 0.45 factor) | 5% of the window |

When `needed` exceeds the window, the request is split (sections 2 and 3). It is never sent truncated.

## 2. Split by graph first

The unit of work becomes a **chunk**: a set of graphs of one Blueprint, sent as one N2C JSON document. Planning
(`FN2CChunkPlanner`, new) starts from the capture (`FN2CCapture`, every graph at the requested depth):

1. If the whole capture fits, one chunk: today's behaviour, unchanged.
2. Otherwise one chunk per function, macro and collapsed graph (a collapsed graph goes with the graph that holds it,
   as capture already keeps it), packed greedily in capture order while the chunk fits.
3. Every chunk carries a shared **class-context header**: the Blueprint's metadata, its variables (with types and
   defaults), the user structs and enums the capture found, and the signature of every graph in the Blueprint (name,
   inputs, outputs, pure/const, access). It is a small N2C JSON document with `graphs` holding signature-only
   entries, so the model knows what every other chunk declares without seeing their bodies. Measured cost on the
   fixtures: one signature is a few dozen tokens, against about 230 per node of a body.
4. A graph that alone does not fit goes to section 3.

## 3. Then split oversized EventGraphs

An EventGraph (or a huge function) that does not fit alone is split by **exec subgraph**:

1. One subgraph per event entry (`Event`, `CustomEvent`, `InputAction`, `ComponentBoundEvent` nodes) with every node
   reachable from it by exec flow, plus the pure nodes those read (the liveness pass already knows which nodes a
   live node reads).
2. A subgraph still too large is split at the outputs of its first `Sequence` (then `ExecutionSequence`, then
   `MultiGate`): each `then_N` branch becomes a part, and the part before the Sequence calls the parts in order.
3. Pure nodes read by several parts are copied into each part. Their ids stay the same (section 4), so the model and
   the stitcher see the same value.
4. A single linear exec chain that still does not fit is refused with a typed error (`GRAPH_TOO_LARGE_FOR_MODEL`,
   naming the graph, its estimated tokens and the model's window), never truncated.

## 4. References across chunks

N2C JSON v2 ids make cross-chunk references stable (decision 0003):

- A node keeps its capture id (`ids.short`, e.g. `N12`) in every chunk it appears in. A pin keeps its `P` id.
- A chunk lists, in a `chunk` metadata block (an optional, additive field), its `part` (for example `EventGraph#BeginPlay`
  or `EventGraph#BeginPlay.then_1`), the ids of the boundary nodes it calls into (`calls_parts`) and the parts that call
  it.
- A function call to a graph in another chunk is an ordinary `CallFunction` node with `member_name`. The header gives
  its signature, so no body is needed.
- Variables are read through the header's variable list in every chunk.

## 5. Stitching the answers

Each chunk's answer is an `FN2CTranslationResponse` (`Source/Public/Models/N2CTranslation.h`) whose graphs each carry
`FN2CGeneratedCode` {GraphDeclaration, GraphImplementation, ImplementationNotes}. The stitcher (`FN2CChunkStitcher`,
new) merges them into one response for the Blueprint:

- **Declarations:** the union of every chunk's `GraphDeclaration` lines, de-duplicated by member name. A member that
  two chunks declare differently is a conflict: it is reported, and the header's signature wins.
- **Implementations:** concatenated in capture order of their graphs. An EventGraph's parts are emitted as one
  function per event, whose body calls a private helper per Sequence part (named `<Event>_Part<N>`, declared by the
  stitcher), so the C++ compiles as one class.
- **Notes:** each chunk's notes under a heading naming its part.
- The archive gets one run with one entry per stitched graph (`FN2CTranslationArchive`), plus each chunk's raw
  response in `chunks/<n>.json` for debugging. The viewer's graph list (0257) shows the stitched graphs.

## 6. Batch translation and the compile loop

- `UN2CBatchTranslationOrchestrator` keeps one item per tagged graph, but an item may now plan several chunks. The
  chunks of an item are sent one after another, and the item ends Succeeded only when every chunk did. Progress is
  reported per chunk ("Translating BP_Door.EventGraph, part 2 of 3"). Cancel stops at the chunk in flight, exactly as
  it stops an item today (0255).
- Verify and Refine (0313) runs on the **stitched** result, not per chunk, because its verifiers (capture coverage,
  Python syntax, the C++ scratch module of 0315) judge a whole class. A refine turn sends the findings with only the
  chunks whose graphs the findings name, plus the header, and re-stitches.
- The translate-and-compile tool (0316) consumes the stitched files like any other translation.
- Token usage and cost add up over chunks and refine turns. The manifest records each chunk's usage.

## 7. What the overlay and the window show

- The overlay's context ring (`SN2CGraphOverlay`, `GetContextUsagePercent`) and the window's
  `SN2CContextWindowVisualizer` use `needed` from section 1, not the bare graph estimate.
- Over 100%: the ring shows the chunk count instead of a percentage (for example "3 parts"). Its tooltip says the graph
  will be translated in N parts, at about N x the system prompt in extra input tokens.
- The batch estimate line adds the extra requests: "Estimated batch cost: $x (3 graphs in 7 requests)".
- A graph refused by section 3.4 shows the ring in the error colour, with the reason, and Translate stays enabled only
  for models whose window fits.

## 8. Failure modes

| Failure | Handling |
|---|---|
| A chunk fails (provider error, truncated answer) | The item fails with that chunk's typed error. The finished chunks stay in `chunks/` so a retry re-sends only the failed chunk (Retry failed, 0259) |
| A chunk's answer omits a graph it was given | Capture coverage (0312) reports it on the stitched result. Refine re-sends that chunk |
| Two chunks declare a member differently | Reported as a stitch conflict in the notes. The header's signature wins |
| The estimate is wrong and the provider rejects the prompt as too long | The provider's context error (HTTP 400 / `context_length_exceeded` / Anthropic `prompt is too long`) re-plans with the window halved, once, then fails with `GRAPH_TOO_LARGE_FOR_MODEL` |
| A local server silently truncates (Ollama past `num_ctx`) | Prevented by sending `num_ctx` = the planned need (capped at the model's maximum) on every Ollama request. Detected by an answer that names graphs it was never sent |
| Cancel mid-item | Stops at the chunk in flight. The item ends Cancelled with its finished chunks archived but not stitched |
| The class-context header alone does not fit | `GRAPH_TOO_LARGE_FOR_MODEL` for the Blueprint (signatures only would still need a bigger model) |

## Follow-up cards

Filed with this design (agent-works kanban; each links this page):

- **0546**: real token counts and output reservation in the estimate (section 1); local context windows read from
  the server; Ollama `num_ctx` set from the plan.
- **0547**: the chunk planner, class-context header and exec-subgraph split (sections 2-4).
- **0548**: the stitcher and the archive's chunk records (section 5), then the orchestrator, refine and cost
  integration (section 6).
- **0549**: the overlay ring, visualizer and batch estimate for multi-part graphs (section 7).
- **0550**: run_perf.py records an `N2C_TokenEstimate` pass, and a quiet-window rerun tightens `budget.json`. Also
  look at serialization, which takes 3x the struct build.
