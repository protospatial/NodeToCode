# MCP transport probes

Live checks of NodeToCode's MCP server against a headless editor on a host project. They confirm or correct the
review's static findings, then stay as regression tests. Nothing here ships with the plugin.

```bash
python -I Tests/mcp_probe/mcp_probe.py --engine G:/UE/UE_5.8 \
  --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8
```

The run starts `UnrealEditor-Cmd` (see `editor_session.py` for the launch form), uses a free port pair from 27150 so
an editor on 27000 cannot answer, probes, stops the editor (no UnrealEditor process is left), and writes
`logs/report_<label>.json`. It exits 0 only when every status matches `expected.json` for the label. `--skip-p4`
skips the probe that launches a second editor.

| Probe | Checks | Finding ids |
|---|---|---|
| P1 | A cross-origin `text/plain` POST (what any web page can send) lists tools and runs `run-python`, which writes `Saved/n2c_probe.txt` | mcp-transport-1, dx-docs-release-1, p0-1 |
| P2 | A POST whose `Host` is `attacker.example` (DNS rebinding) is answered | mcp-transport-1 |
| P3 | A two-element JSON-RPC batch is answered element by element, or rejected with -32600 | mcp-transport-9 |
| P4 | With the MCP port already held by another process, the editor logs that the server started | mcp-transport-10 |
| P5 | The notification server (port+1) accepts a connection on a non-loopback local address | mcp-transport-14 |
| P6 | The notification server (port+1) answers 200 to `/mcp/notifications` with 10,000 headers, a 10 MB chunked body or a 4 KB request target, or stops answering afterwards. Evidence also records the editor's working set before and after, and `task_stream`: whether a `translate-focused-blueprint` task stream delivers its `progress` and `response` events (not gating; headless, the task fails after its first progress report) | auth-settings-module-17, dx-docs-release-4, p0-9 |

Statuses are `reproduced` (the finding's defect is present), `fixed`, or `skipped` (with a reason). P4 holds the
port with a plain socket instead of a second editor on a copy of the project; the effect on the bind is the same.

**Rule:** a card that fixes one of these findings flips its probe's entry in `expected.json` to `fixed` in the same
commit as the fix, and runs the probe on 5.6, 5.7 and 5.8.

The session temporarily writes the port and Allow MCP Python Execution into the host's `Saved/Config/<Platform>Editor`
files and restores them when it stops; the host's shared `Config/` folder is never changed.

## Live provider checks

The editor uses `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY` or `DEEPSEEK_API_KEY` (or `DEEPSEEK_API`) from
its environment when no key is saved, so a live check never types or saves a key:

- `env_keys.py`: `load_provider_keys()` parses the repo's gitignored `.env` into a dict for a child process
  (`DEEPSEEK_API` becomes `DEEPSEEK_API_KEY`); `find_leaks(keys, paths)` lists files that contain a key value. Run
  alone, it prints only the variable names it found (`--check <paths>` adds a leak scan). Never print the dict.
- `EditorSession(..., extra_env=keys)` adds those variables to the editor's environment only.
- `live_env_translation.py --engine ... --project ... [--provider gemini --model gemini-3.5-flash-lite]` runs one
  real translation that way: an in-memory Blueprint under `/Game/N2CTest`, `translate-focused-blueprint` over MCP,
  then checks the response parsed, Project Settings shows the key's source by variable name only, and no key value is
  in the editor log or the report (`logs/live_env_translation.json`). It refuses to run if a key for that provider is
  saved in the per-user secrets file, since the saved key would be used instead.
- `live_translate_cancel.py --engine ... --project ... [--provider anthropic --model claude-haiku-5-5]` cancels a
  live translation: it starts `translate-focused-blueprint` on an in-memory Blueprint, sends `nodetocode/cancelTask`
  `--cancel-after` seconds in (default 1.5), watches the task stream for `--settle` seconds (default 30), and checks
  the cancel answered `cancellation_initiated`, the one response is the cancelled error, no event arrives after it,
  the log shows the LLM request cancelled and the task completed once, a second cancel answers
  `task_already_completed`, the editor stays up, and a control translation run to the end returns code. It also
  scans the editor log and report for key values. Report: `logs/live_translate_cancel.json`.
- `live_provider_matrix.py --engine ... --project ... --label 5.8 [--cases <id> ...] [--providers ...] [--timeout 300]`
  runs real translations through several providers and models in one headless editor (`--list` prints the case
  ids). It sets Min Severity to Debug in the host's `Saved/Config` layer for the session so each request payload is
  logged, wires BeginPlay to a Print String in the in-memory Blueprint so there is logic to translate, then per case
  applies the case's settings (DeepSeek model entry, Max Output Tokens), translates, restores the settings and checks
  that case's logged payload: no temperature/top_p/top_k where the provider rules forbid them, the expected output
  budget field and value, OpenAI `response_format` `json_schema` with `strict: true`, Gemini `responseSchema` with no
  `additionalProperties`, DeepSeek `thinking` and `reasoning_effort`. A case passes when the translation returns
  code and every rule holds. It also scans the editor log, `Saved/Logs`, the plugin log and new translation folders
  for key values and `Saved/Logs` for `key=`. Report: `logs/live_provider_matrix_<label>.json`.
  - The case list holds current models only (the live entries of `N2CLLMModelRegistry.cpp`): no retired, deprecated
    or "shuts down" IDs are tested. Update it when the registry's lineup changes.
  - Each translation is bounded by `--timeout` (wall clock; the task stream's half-second progress events keep a
    socket timeout from ever firing). A case that gets no response in time fails as timed out and the remaining cases
    are skipped, since the editor still holds that request. Pass `--cases` in the order to run them and put slow
    models last.
  - `EditorSession` puts the editor in a Windows job object that kills it when the Python process exits, so a killed
    or crashed run cannot leave an editor running; only `stop()` restores the `Saved/Config` patches.
  - The editor runs with `-RenderOffScreen`: on UE 5.6 under `-nullrhi` the opened Blueprint editor's window
    otherwise reaches `FGenericWindow::GetRestoredDimensions`, which is Fatal.
  - Provider HTTP errors (429, 503, 402) surface in the response as "Failed to parse LLM response" with the raw body;
    the case's `log_errors` hold the HTTP status.
