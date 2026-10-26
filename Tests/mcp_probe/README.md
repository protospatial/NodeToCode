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
skips the probe that launches a second editor, `--skip-content` the fixture probes P10-P12. `build.ps1 -Smoke`
(`Tests/smoke/smoke_check.py`) runs P1-P3 and P5-P9 inside its default-mode editor session
(`mcp_probe.session_probes`) plus P4, and fails on any mismatch; P10-P12 run only here.

| Probe | Checks | Finding ids |
|---|---|---|
| P1 | A cross-origin `text/plain` POST (what any web page can send) lists tools and runs `run-python`, which writes `Saved/n2c_probe.txt` | mcp-transport-1, dx-docs-release-1, p0-1 |
| P2 | A POST whose `Host` is `attacker.example` (DNS rebinding) is answered | mcp-transport-1 |
| P3 | A two-element JSON-RPC batch: answered element by element in a session negotiated at 2025-03-26, and rejected with -32600 in a 2025-11-25 session (batches exist only in 2025-03-26) | mcp-transport-9 |
| P4 | With the MCP port already held by another process, the editor logs that the server started on that port. Fixed (card 0112) when the log says the port is in use and the server either moved to another port in the fallback range or reported that it did not start | mcp-transport-10 |
| P5 | Something answers on port+1 (on 127.0.0.1 or any non-loopback local IPv4 address), or netstat shows the editor listening there. Until card 0113 a notification server listened on port+1 (mcp-transport-14 was that it accepted non-loopback connections); fixed since card 0113 removed it. The session picks a port whose port+1 is free | mcp-transport-14, next-1 |
| P6 | The `/mcp` body cap (card 0425): in the probe's session, a JSON-RPC `ping` one byte over 8 MiB and a 1 MiB `ping` as a control. Fixed when the oversized one gets 413 with a JSON-RPC -32600 error (null id) and the control 200 with a result; reproduced when the oversized one gets 200 (live 0090: a 32 MB body was answered 200). Until card 0113 this probe sent oversized requests to the removed port+1 server | auth-settings-module-17, dx-docs-release-4, p0-9 |
| P7 | `translate-focused-blueprint` (numeric progressToken 7, no Blueprint focused) is not answered with a CallToolResult on its own POST: once with `Accept: application/json, text/event-stream` (UE 5.8 streams `notifications/progress` then the response; 5.6/5.7 send one JSON response) and once JSON-only. Fixed when each call gets exactly one JSON-RPC response with id 7 and a `content` array (here `isError`), no `accepted`/`sseUrl` result, the connection closes, and any progress echoes the token as a number | p0-10, mcp-transport-3, mcp-transport-21, dx-docs-release-8 |
| P8 | Per-client sessions (card 0109): two initializes get different `Mcp-Session-Id`s and each answers in its own; no session gives 400, an unknown one 404, an unsupported `MCP-Protocol-Version` 400; GET is 405 with `Allow: POST, DELETE`; a client response is 202 with no body; DELETE ends a session (2xx), after which it gets 404 while the other session still answers. Fixed when every check holds | next-1, mcp-transport-16, mcp-transport-18, mcp-transport-19 |
| P9 | Capabilities, error codes and cancellation (card 0110): `initialize` advertises no `logging` and returns `instructions`; `resources/templates/list` lists `nodetocode://blueprint/{+path}` (`{name}` before card 0208) and `resources/list` has no `resourceTemplates`; an unknown tool, an unknown prompt and a missing required prompt argument are -32602; an unknown resource uri is -32002 with the uri in `data`; `notifications/cancelled` for an id with no running call is 202 with no body. Fixed when every check holds (the live cancel of a running call is `live_translate_cancel.py`) | next-1, mcp-transport-15, mcp-transport-19, mcp-transport-20, mcp-tools-python-translation-resources-16, mcp-tools-python-translation-resources-23, imp-mcp-transport-5 |
| P10 | On the fixture's focused EventGraph (Event BeginPlay wired to Print String), `search-blueprint-nodes {"searchTerm": "begin play"}`: the N2C JSON node count, execution and data links, BeginPlay (its GUID from `find-nodes-in-graph`) and its exec link, and the package's dirty state before and after. Fixed when nothing changed (card 0050); skipped when no graph can be focused | gap-1, mcp-tools-graph-functions-1, mcp-tools-graph-functions-2, p0-7 |
| P11 | The Python bridge with an apostrophe in the text, each raw result through `json.loads`: `tag_focused_graph("it's")`, `list_tags(tag="it's")`, `remove_tag(<guid>, "it's")`, `remove_tag` again (its "not found" error quotes the tag) and `open_blueprint_function("Missing'Func")`. Fixed when every result parses and says what happened (card 0120) | gap-1, python-bridge-1, qw-14 |
| P12 | `create-variable` and `compile-blueprint` on the fixture during Play In Editor, started with `LevelEditorSubsystem.editor_request_begin_play()` and confirmed with `is_in_play_in_editor()`; then PIE is ended and `get-blueprint-member-variables` lists the fixture's variables. Records both tool results, NodeToCode error, ensure and crash lines, whether the editor still answers and PIE ended. Reproduced while `create-variable` succeeds during PIE. Fixed (card 0170) when `create-variable` and `compile-blueprint` answer `PIE_ACTIVE`, `get-blueprint-member-variables` still works during PIE, `create-variable` works again after PIE and nothing else went wrong | gap-1, next-10 |

Statuses are `reproduced` (the finding's defect is present), `fixed`, `skipped` (with a reason), or
`not_applicable` (the code the probe tested was removed; the evidence says why). P4 holds the
port with a plain socket instead of a second editor on a copy of the project; the effect on the bind is the same.

**Rule:** a card that fixes one of these findings flips its probe's entry in `expected.json` to `fixed` in the same
commit as the fix, and runs the probe on 5.6, 5.7 and 5.8.

Every POST to `/mcp` other than `initialize` needs an `Mcp-Session-Id` (card 0109). The probes and live scripts get
theirs from `mcp_session.py`: `session_headers(port)` initializes once per port and returns the headers to send,
and after a 404 (a new editor on the same port) `session_headers(port, refresh=True)` opens a new session.

The session temporarily writes the port and Allow MCP Python Execution into the host's `Saved/Config/<Platform>Editor`
files and restores them when it stops. A NodeToCode setting changed through the editor (a probe's
`set_editor_property`, the settings UI, a settings migration) is saved to the host's `Config/DefaultNodeToCode.ini`,
so `EditorSession` also snapshots that file at `start()` and, at `stop()`, writes it back byte for byte or deletes it if
it did not exist; `session.restore_ok` says whether every file came back exactly, and the live probes that change
settings (`live_provider_matrix.py`, `live_truncated.py`, `live_typed_errors.py`) report it as `config_restored` and
fail without it.

## Fixture probes (P10-P12)

Card 0097 named these P6-P8; those ids were already taken, so they are P10-P12. They run on the committed fixture
`Tests/Fixtures/N2CProbe/BP_N2CProbe.uasset`, an Actor Blueprint whose Event BeginPlay is wired to a Print String
(`In String` "N2CProbe BeginPlay"). A committed asset keeps the fixture independent of the tools under test.
`mcp_probe.py` copies it into the host's `Content/N2CProbe/` before the editor starts and removes it after the
editor exits (nothing is saved, so P12's changes stay in memory); `--skip-content` leaves P10-P12 out. The session
runs with `-RenderOffScreen` because the probes open a Blueprint editor (on UE 5.6 under `-nullrhi` its window
otherwise reaches `FGenericWindow::GetRestoredDimensions`, which is Fatal), and quits the editor with `quit_editor()`.
P10-P12 are not part of `build.ps1 -Smoke`.

The fixture was made on UE 5.6 so 5.7 and 5.8 load it: `python -I make_probe_fixture.py --engine G:/UE/UE_5.6
--project <5.6 host>.uproject` creates `/Game/N2CProbe/BP_N2CProbe` (parent Actor) with run-python, adds Event
BeginPlay (the new Blueprint's ghost BeginPlay, made real by the link) and Print String with
`add-bp-node-to-active-graph`, links `then` to `execute` with `connect-pins`, sets `In String` with
`set-input-pin-value`, compiles, saves, and copies the `.uasset` here. Run it again only to change the fixture.

PIE starts headless (`-nullrhi -RenderOffScreen`) on all three engines. On UE 5.8, P12 first places a fixture actor
in the editor level so PIE has a live instance for the compile to reinstance (the log shows its BeginPlay print). On
UE 5.6 and 5.7 headless, placing any Blueprint actor through `EditorActorSubsystem.spawn_actor_from_class` is a fatal
`EXCEPTION_INT_DIVIDE_BY_ZERO` in the engine's viewport placement (a plain `Actor` places fine), and stock Python has
no game-world spawn, so there PIE runs without a fixture instance and the evidence's `live_instance` says so.

Observed on 5.6, 5.7 and 5.8 (card 0097): P10 fixed (4 nodes, 1 exec link, BeginPlay's GUID and link kept, package
clean); P11 fixed (all five results parse, the errors quote `it's` and `Missing'Func`); P12 reproduced:
`create-variable` and the compile succeed during PIE, with no NodeToCode error, ensure or crash, PIE ends, the editor
answers, and `N2CProbePieVar` is on the fixture afterwards.

## Injection fixture (threat model)

`make_injection_fixture.py` builds `/Game/N2CSecurity/BP_InjectionFixture` in a host project for the prompt-injection
test of `Context/mcp-server/threat-model.md` (gate 0383): planted payloads with canary markers in a variable
tooltip, a string literal, the Blueprint description and a comment box, then a read-back through the read tools.
The node-comment payload is one manual editor step (the threat model gives it). The asset stays in the host and is
never committed. `--remove` deletes it, `--keep-existing` re-reads it.

`live_untrusted_0124.py --engine ... --project ... [--claude]` (no provider key; needs the fixture) checks card 0124
on a headless editor with Python execution off: get-focused-blueprint, get-blueprint-member-variables,
find-nodes-in-graph and `resources/read nodetocode://blueprint/current` return JSON whose first key is
`untrusted_content` with the canaries after it; generate-code, analyze-blueprint and refactor-blueprint end with the
notice message and then the embedded resource (`annotations.audience: ["assistant"]`) that alone holds the canaries;
generate-code embeds `CodeGen_CPP.md`; `language: cobol` answers -32602; every prompt has a title; python-scripting
says run-python is disabled. `--claude` adds one `claude -p` session (`--allowedTools "mcp__nodetocode__*"`, model
`--cli-model`, default haiku) that summarizes the Blueprint, and records its tool calls, any call that follows a planted
instruction and whether the answer flags the planted text. One observation, not gate 0383. Report:
`logs/live_untrusted_0124.json`.

## E05 regression probe

`e05_regressions.py` proves the epic E05 fixes: every p0-6 input returns an error instead of crashing the editor,
and every p0-7 case leaves user data intact. Stdlib only.

```bash
# MCP cases against a running editor, for example one opened with .\build.ps1 -UEVersion 5.8 -Launch
python -I Tests/mcp_probe/e05_regressions.py --url http://127.0.0.1:27000/mcp

# Everything headless for one engine: MCP cases on a headless editor it starts and stops, the headless Python
# checks, and the automation tests (NodeToCode.Robustness + NodeToCode.Secrets.Store; --all-tests runs NodeToCode)
python -I Tests/mcp_probe/e05_regressions.py --headless --engine G:/UE/UE_5.8 \
  --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8
```

It prints one `PASS`, `FAIL` or `SKIP` line per case (per check file and per automation test with `--headless`)
and exits non-zero on any failure. The MCP run ends by checking that the editor still answers. Use `--no-mcp` with
`--headless` to run only the checks and the tests, `--tests <filter>` to change the automation filter.

- The graph cases set up scratch Blueprints with `run-python`, so a running editor needs Allow MCP Python Execution
  (Editor Preferences, or `bAllowMcpPythonExecution=True` under `[/Script/NodeToCode.N2CUserSettings]` in the host's
  `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` before it starts); without it they print `SKIP`.
  The headless run turns it on for its own session.
- Everything the probe makes is under `/Game/E05Probe`, and the folder is deleted at the end (a `PASS scratch
  folder` line). run-python executes in the editor's shared `__main__` namespace, so the probe runs each snippet
  inside a function: a module-level variable holding an asset would keep it loaded and block the delete.
- The headless checks run through `UnrealEditor-Cmd -ExecutePythonScript=<check> -nullrhi` and the tests through
  `-ExecCmds="Automation RunTests <filter>" -TestExit="Automation Test Queue Empty"`; logs go to `logs/`.

| Item | Covered by | Card |
|---|---|---|
| p0-6 `prompts/get` with null, array and object arguments | MCP `prompts_get_non_string_argument` (-32602 each) | 0043 |
| p0-6 1,100-character `typeIdentifier` / `variableName` | MCP `create_variable_long_type`, `create_local_variable_long_name`, `get_local_variable_node_long_name` | 0043 |
| p0-6 `create-variable` with a bare struct or enum name (`HitResult` asserted in `AddMemberVariable` on UE 5.8) | MCP `create_variable_bare_type_names` (`HitResult`, `FHitResult`, `Guid`, `ECollisionChannel`, `AActor` make variables of the right type and the Blueprint compiles; `multicastdelegate`, `wildcard`, a bool set and `TSubclassOf<AActor>` are refused and add nothing); test `NodeToCode.MCP.TypeResolver` | 0153 |
| p0-6 page 2147483647 | MCP `read_content_browser_huge_page`; test `NodeToCode.Robustness.Pagination` | 0043 |
| p0-6 `create_user_defined_struct` on unmounted and invalid paths; `save_struct` on a read-only file | `e05_struct_check.py` | 0048 |
| p0-6 `SelectText` out of range | test `NodeToCode.Robustness.CodeEditor` (clamps lines and columns on a live `SN2CCodeEditor`, and an empty document) | 0063 |
| p0-6 theme change with a live code editor | test `NodeToCode.Robustness.CodeEditor` (a widget's brush survives `Reinitialize` and is retinted in place); the on-screen check is interactive gate 0378 | 0063 |
| p0-6 non-Blueprint asset editors | MCP `non_blueprint_asset_editor`: a Control Rig opened with `RigVM.UseNewEditor=1` (a `UBlueprint` whose toolkit is not an `FBlueprintEditor`) stays open through two overlay polls, the editor answers, and it is not listed as a Blueprint editor | 0044 |
| p0-7 `search-blueprint-nodes` leaves the graph unchanged | MCP `search_blueprint_nodes_read_only` (node set, node count and package clean after four searches); test `NodeToCode.Robustness.SearchIsReadOnly` | 0050 |
| p0-7 `connect-pins` fan-in keeps both links | MCP `connect_pins_fan_in_keeps_both_links`, `connect_pins_refuses_silent_replace` (a link that would replace one fails with `WOULD_BREAK_EXISTING_LINK`), `bridge_connect_pins_fan_in_keeps_both_links` (the Python bridge's `ConnectPins`); `connect_pins_refuses_conversion_replace` and `bridge_connect_pins_refuses_conversion_replace` (a Float into a linked In String needs a conversion node: refused without `breakExistingLinks`, graph unchanged; replaced with it) | 0046, 0415 |
| p0-7 `add-function-return-pin` dirties the package | MCP `add_return_pin_marks_package_dirty`; test `NodeToCode.Robustness.MarkModifiedDirties` | 0064 |
| p0-7 DataTable append keeps rows | `e05_datatable_check.py` | 0065 |
| p0-7 secrets survive a truncated file | test `NodeToCode.Secrets.Store` ("sets a truncated file aside and never writes over it", "finishes a save that stopped before the temporary file replaced the file") | 0062, 0067 |
| p0-7 script registry survives a truncated file | `e05_registry_check.py` | 0067 |
| p0-7 graph-state file survives a truncated file | test `NodeToCode.Robustness.GraphStatePersistence` | 0066 |

The default test filter also runs the other `NodeToCode.Robustness` tests (async task cleanup and cancel, LLM
request cancel, `ExecuteOnGameThread` timeout, logger concurrency, LLM service GC, `DescribeNode`).

## Live provider checks

The editor uses `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) or `DEEPSEEK_API_KEY` (or `DEEPSEEK_API`) from
its environment when no key is saved, so a live check never types or saves a key:

- `env_keys.py`: `load_provider_keys()` parses the repo's gitignored `.env` into a dict for a child process
  (`DEEPSEEK_API` becomes `DEEPSEEK_API_KEY`); `find_leaks(keys, paths)` lists files that contain a key value. Run
  alone, it prints only the variable names it found (`--check <paths>` adds a leak scan). Never print the dict.
- `EditorSession(..., extra_env=keys)` adds those variables to the editor's environment only.
- `probe_blueprint.py` holds the in-memory probe Blueprint every live translation uses (card 0430): `SETUP_PY`
  makes an Actor Blueprint under `/Game/N2CTest`, opens its EventGraph, adds a Print String and wires Event
  BeginPlay to it, and reports `print_string_live` (the captured N2C JSON 2.0.0 has the PrintString node with no
  `unreachable`, `unused` or `disabled` flag). Since capture marks nodes no event reaches (cards 0135-0138), an
  unwired Print String is rightly translated to empty code, so the graph must be wired. `SETUP_UNWIRED_PY` and
  `CONNECT_PY` are the two halves, for `live_provider_matrix.py --liveness`, which builds its own graph.
  `live_env_translation.SETUP_PY` re-exports `SETUP_PY` for the scripts that import it from there.
- Every headless session that opens the probe's (or a fixture's) Blueprint editor passes
  `extra_args=editor_session.BLUEPRINT_EDITOR_ARGS` (`-RenderOffScreen`): on UE 5.6 under `-nullrhi` the Blueprint
  editor's window otherwise reaches `FGenericWindow::GetRestoredDimensions`, which is Fatal.
- `live_env_translation.py --engine ... --project ... [--provider gemini --model gemini-3.5-flash-lite]` runs one
  real translation that way: the probe Blueprint (`probe_blueprint.py`), `translate-focused-blueprint` over MCP,
  then checks the response parsed, Project Settings shows the key's source by variable name only, and no key value is
  in the editor log or the report (`logs/live_env_translation.json`). It refuses to run if a key for that provider is
  saved (a `NodeToCode/<Provider>` Credential Manager entry, or the per-user secrets file), since the saved key would
  be used instead.
- `wincred.py` reads, snapshots and restores Windows Credential Manager entries with ctypes (`python -I wincred.py
  [--prefix NodeToCode/]` lists target names only). Live checks that change `NodeToCode/*` entries snapshot them in
  memory first and put them back with `restore_snapshot`, under the `user-secrets` lock.
- `live_keychain_0161.py --engine ... --project ... [--provider anthropic --model claude-haiku-5-5]` is the headless
  part of gate 0389 (card 0161), under the `user-secrets` lock: it writes a 2.0-beta per-user `secrets.json` (the real
  key plus a fake one) and a project `Saved/NodeToCode/User/secrets.json` (a fake key), starts the editor and checks
  both files are gone, the three `NodeToCode/<Provider>` entries hold the keys, Project Settings shows an empty API Key
  field and "Stored in Windows Credential Manager (ends ...)", nothing in `unreal.N2CSettings` returns a key, and a
  translation succeeds; then that `set_api_key` with a wrong key makes the next translation fail with an auth error,
  typing the right key into the field (set_editor_property) empties it and the next translation succeeds; then a
  second start changes nothing and still translates, and `set_api_key(provider, "")` removes the entry. Entries and
  files are restored. Report: `logs/live_keychain_0161.json`.
- `live_translate_cancel.py --engine ... --project ... [--provider anthropic --model claude-haiku-5-5] [--via notification|cancelTask]`
  cancels a live translation: it starts `translate-focused-blueprint` on the probe Blueprint, cancels it
  `--cancel-after` seconds in (default 1.5) and keeps reading the call's own response (a `text/event-stream` on UE
  5.8, where it also checks progress arrived before the cancel) for `--settle` seconds (default 30). With `--via
  notification` (the default) it sends the standard `notifications/cancelled {requestId}` and checks the notification
  is accepted with 202, no JSON-RPC response is sent for the call and its response ends soon after (the stream closes;
  on 5.6/5.7 the held request ends with 202 and no body), no progress follows, and the log shows the LLM request
  cancelled, the task completed once and its response suppressed. With `--via cancelTask` it sends the deprecated
  `nodetocode/cancelTask` and checks the cancel answered `cancellation_initiated`, the one response is the cancelled
  error and no event arrives after it. Both check a later `nodetocode/cancelTask` answers `task_already_completed`
  (and logs its deprecation), the editor stays up, and a control translation run to the end returns code, and scan
  the editor log and report for key values. Report: `logs/live_translate_cancel.json` (`_cancelTask.json` with
  `--via cancelTask`).
- `live_claude_cli_translate.py --engine ... --project ... --label 5.8 [--provider anthropic --model claude-haiku-5-5]`
  is the end-to-end check with a standard MCP client: on a headless editor (`-RenderOffScreen`) with only the
  provider's key in its environment and an in-memory probe Blueprint focused, it runs the headless Claude Code CLI
  (`claude -p ... --mcp-config <the editor's /mcp> --strict-mcp-config --allowedTools "mcp__nodetocode__*"
  --output-format stream-json --verbose --model haiku`, its own login, the provider key removed from its environment)
  and asks it to call `translate-focused-blueprint` once. It passes when the CLI's tool result is a translation with
  code and not an error, the editor started exactly one task (the log line also says whether the call was answered as
  `text/event-stream` or one JSON response), and no key value is in the editor log, the CLI's output or the report.
  Report: `logs/live_claude_cli_<label>.json`; the CLI's stream is kept next to it.
- `live_stdio_bridge.py --engine ... --project ... --label 5.8 [--provider anthropic --model claude-haiku-5-5]`
  checks the stdio bridge (`Content/Python/mcp_bridge`) end to end through its launcher (`launch_bridge.bat`/`.sh`),
  on a headless editor with only the provider's key in its environment and an in-memory probe Blueprint focused.
  A small stdio client sends raw UTF-8 and checks that `run-python` sees `café あ日本 🙂` as the same code points, that
  a member variable named with é and 日本 (tooltip with an emoji) reads back unchanged, and that
  `translate-focused-blueprint` (numeric progressToken) returns code, with progress before the response on UE 5.8 and
  none on 5.6/5.7 (held), and stdout carrying only JSON-RPC lines. Then the headless Claude CLI, with the launcher as
  a stdio `--mcp-config` server, calls `run-python` with the same text and `translate-focused-blueprint` once.
  `--skip-cli` runs only the client part. Report: `logs/live_stdio_bridge_<label>.json`.
- `live_bridge_concurrency.py --engine ... --project ... [--second-engine ... --second-project ...]` runs the stdio
  bridge through its launcher with no `--port` (it finds the editors through their discovery entries). On a headless
  editor with only the provider's key in its environment and an in-memory probe Blueprint focused, it starts
  `translate-focused-blueprint` with a progress token, then a quick `tools/call` and a `ping`, which must answer
  while the translation runs, then forwards `notifications/cancelled`: nothing more may arrive for that id (a
  progress event within 1 s of the cancel, sent before the editor read it, is counted as in flight), a later
  request still answers, and the editor log shows the cancel reached the call, the provider request was cancelled
  and the response suppressed. With a second editor (no key) it checks a bridge without `--project` answers
  `initialize` with an error naming both projects, and that `--project` (path or name) and `NODETOCODE_PROJECT`
  reach the right editor (run-python's engine version). Report: `logs/live_bridge_concurrency.json`.
- `live_translate_exit.py --engine ... --project ... [--windowed] [--provider anthropic --model claude-haiku-5-5]`
  exits the editor mid-translation: it starts `translate-focused-blueprint` on an in-memory Blueprint, and
  `--quit-after` seconds in (default 1) closes the Blueprint editor and calls `unreal.SystemLibrary.quit_editor()`.
  It checks the editor exits by itself with code 0 in under 10 s (not waiting for the LLM), the log shows
  `CancelAllTasks` cancelled the task on exit before the module shut down and UObjects were purged, the LLM request
  was cancelled and the task completed once, no response was handled after the cancel, and there is no crash, ensure
  or "context not found" line. The headless editor runs with `-RenderOffScreen`, which lets UE 5.6 close the
  Blueprint editor and exit cleanly (checked on 5.6 and 5.8, card 0430); `--windowed` runs it with a window instead
  (no `-nullrhi`, no `-RenderOffScreen`), the earlier way on 5.6 and 5.7. Report: `logs/live_translate_exit.json`.
- `live_port_restart.py --engine ... --project ...` (no provider key) changes **Server Port** in a running headless
  editor through run-python with PostEditChangeProperty, as the settings page does (card 0112). It checks the
  discovery file and the per-user index entry name the port and the editor's pid, `/mcp/health` answers on the new
  port, the old port's routes answer 404, the discovery files and **Server Status** follow, the log shows the restart
  and no error line, and the discovery files are gone after `quit_editor()`. Report: `logs/live_port_restart.json`.
- `live_spawned_nodes_0155.py --engine ... --project ... --label 5.8 [--model claude-haiku-5-5]` checks the
  variable and comment node tools end to end (card 0155): on an in-memory Actor Blueprint under a fresh
  `/Game/N2CTest/Spawn0155_<time>` folder with a float member Health, `claude -p` with only `mcp__nodetocode__*` adds
  a Get and a Set Health node, wires BeginPlay -> Set and Get -> Set with connect-pins by the returned `nodeId`s and
  comments the two nodes. The probe finds both nodes by `nodeId` (find-nodes-in-graph), checks the links and one pin
  set in the capture, translates once with the model (the Anthropic key in the editor's environment only), checks
  the package is dirty and that three undos remove the comment, the links and the Set node in turn. Report:
  `logs/live_spawned_nodes_0155_<label>.json`.
- `live_function_pins_0156.py --engine ... --project ... --label 5.8` (no provider key) checks that adding a parameter
  to a function that is already called rebuilds the call site (card 0156): on an in-memory Actor Blueprint under a
  fresh `/Game/N2CTest/Pins0156_<time>` folder it makes MyFunc(Value) -> Result and a call to it wired from BeginPlay,
  then `claude -p` with only `mcp__nodetocode__*` adds a float input Speed with add-function-input-pin (by
  `blueprintPath` and `graphGuid`). The probe checks the tool result (actualName Speed, compile success with 0
  errors), that the call node in the capture has inputs Value and Speed (a real), outputs Result and is still run from
  BeginPlay, and that compile-blueprint reports no error. Report: `logs/live_function_pins_0156_<label>.json`.
- `live_user_settings_0423.py --engine ... --project ...` (no provider key) checks that a committed project config
  cannot switch on MCP Python execution (card 0423), in three headless sessions: `committed` (no
  `bAllowMcpPythonExecution` in the user's Saved/Config layer, `EditorSession(allow_python=None)`, and the host's
  `Config/DefaultEditorPerProjectUserSettings.ini` setting `bAllowMcpPythonExecution=True` and
  `bEnableMcpServer=False`: the server starts, no Python tool is listed, one warning each), `user` (Saved/Config
  True: run-python, save-python-script and delete-python-script are listed) and `both` (project True, user False:
  none). The host files are put back byte for byte. Report: `logs/live_user_settings_0423.json`.
- `live_focus_0150.py --engine ... --project ... --label 5.8 [--no-cli]` (no provider key) checks that the focused
  Blueprint follows what was opened last (card 0150): three in-memory Actor Blueprints under a fresh
  `/Game/N2CTest/Focus0150_<time>` folder, opened in turn with open-blueprint-asset, re-opened while already open,
  and opened through the bridge's `open_blueprint`; after each, get-focused-blueprint reads that Blueprint,
  get-open-blueprint-editors marks only it `isFocused`, and every editor still shows its own graph. Then (without
  `--no-cli`) `claude -p` with only `mcp__nodetocode__*` opens two of them and reads each; each read must be the one
  opened just before. The editors are closed and the folder deleted. Report: `logs/live_focus_0150_<label>.json`.
- `live_compile_0151.py --engine ... --project ... --label 5.8 [--no-cli] [--cli-model sonnet]` (no provider key)
  checks one compile per tool call with the compiler's diagnostics in the result (card 0151): an in-memory Actor
  Blueprint under a fresh `/Game/N2CTest/Compile0151_<time>` folder, opened on its EventGraph. Over MCP the probe runs
  a context-free search-blueprint-nodes, adds a Static Mesh component, then adds a Branch from its search result's
  stable `actionIdentifier` (card 0277; no control characters). Then `claude -p` with only `mcp__nodetocode__*`
  searches for and adds a Custom Event, a Print String and a Set Static Mesh node itself (no add call may fail) and
  wires them in one connect-pins call, leaving Set Static Mesh's Target unconnected. The editor log's
  `Blueprint '<name>' compiled:` lines between `Calling tool:` lines must show one compile per edit call and none for
  a search or find; the connect-pins result's `compile` must report the error with the Set Static Mesh node's GUID,
  and the agent's answer must repeat it. The folder is deleted. Report: `logs/live_compile_0151_<label>.json`.
- `live_graph_target_0152.py --engine ... --project ... --label 5.8 [--cli-model sonnet]` (no provider key) checks
  explicit graph targets and stable function IDs (card 0152) on an in-memory Actor Blueprint under a fresh
  `/Game/N2CTest/Target0152_<time>` folder that is never opened by the probe. `claude -p` with only
  `mcp__nodetocode__*` creates a function by `blueprintPath` and answers with its `functionGuid`; the probe renames the
  function (`BlueprintEditorLibrary.rename_graph`) and closes the Blueprint's editors; the same session (`--resume`)
  then adds an input, a Print String and a connection to it, passing `blueprintPath` and the first ID as `graphGuid`
  to every call. Every call must succeed with a `target` naming the renamed function (`resolvedBy` `explicit`), and
  `list-blueprint-functions` must list the renamed function with the first ID. The folder is deleted. Report:
  `logs/live_graph_target_0152_<label>.json`.
- `live_non_context_search_0434.py --engine ... --project ... --label 5.8` (no provider key, no `claude -p`) checks
  `search-blueprint-nodes` with `contextSensitive` false (card 0434) on an in-memory Actor Blueprint under a fresh
  `/Game/N2CTest/NonContext0434_<time>` folder: "print string" and "branch" by `blueprintPath` with nothing focused,
  once with no target at all (must be an error saying there is no graph, not an empty list), then on the focused
  event graph next to the default search (same IDs, `isContextSensitive` false and true), and adds the Print String
  and Branch results. Searches must leave the node count; each add adds one node. The folder is deleted. Report:
  `logs/live_non_context_search_0434_<label>.json`.
- `live_blueprint_by_path_0157.py --engine ... --project ... --label 5.8 [--cli-model sonnet]` (no provider key)
  checks the by-path read tools (card 0157). The probe makes an Actor Blueprint with a function under a fresh
  `/Game/N2CTest/ByPath0157_<time>` folder (run-python) and closes its editors, so get-open-blueprint-editors lists
  none. `claude -p` with only `mcp__nodetocode__*` lists its graphs with list-blueprint-graphs and reads the function
  with get-blueprint-n2cjson by `blueprintPath` and the listed `graphGuid`. Both calls must succeed, start with
  `untrusted_content`, open no editor (none is open afterwards), and the agent's answer must match them. The probe also
  reads `/Engine/EngineSky/BP_Sky_Sphere` (every graph) with `toFile` and checks the file parses; a copy is kept as
  `logs/live_blueprint_by_path_0157_<label>_export.json` for token counts. The folder is deleted. Report:
  `logs/live_blueprint_by_path_0157_<label>.json`.
- `live_resources_0208.py --engine ... --project ... --label 5.8 [--skip-cli] [--cli-model claude-haiku-5-5]` (no
  provider key) checks the Blueprint resources by asset path (card 0208). The probe makes an Actor Blueprint `BP_Player`
  with a function under a fresh `/Game/N2CTest/Res0208_<time>` folder and opens its editor (`-RenderOffScreen`). Over
  raw JSON-RPC, `resources/templates/list` must list `nodetocode://blueprint/{+path}`; `nodetocode://blueprints/all`
  must give BP_Player the `resourceUri` `nodetocode://blueprint/Game/N2CTest/Res0208_<time>/BP_Player` (underscore
  kept); `resources/read` of it must return N2C JSON 2.0.0 with the function's entry node, marked `untrusted_content`
  first, and no stub; the old name form `nodetocode://blueprint/BP_Player` must return the same text; an empty value,
  a query, an empty segment, a malformed escape and an unknown Blueprint must be -32002 with the uri in `data`. Then
  `claude -p` with only `ListMcpResourcesTool` and `ReadMcpResourceTool` reads blueprints/all and the listed URI and
  answers with the graph names. On 5.8 the folder is deleted; below 5.8 the editor is not closed (fatal headless) and
  the unsaved Blueprint goes with the editor. Report: `logs/live_resources_0208_<label>.json`.
- `live_batch_0169.py --engine ... --project ... --label 5.8 [--cli-model sonnet]` (Anthropic key from `.env`, in the
  editor's environment only) checks export-n2cjson-batch and translate-batch (card 0169). Three wired Actor Blueprints
  (`probe_blueprint.SETUP_PY`) are saved under a fresh `/Game/N2CTest/Batch0169_<time>` folder with every editor
  closed. `claude -p` with only `mcp__nodetocode__*` exports them in one export-n2cjson-batch call (whole Blueprints,
  `outDir` `Live0169_<time>`): one file per graph with nodes, every file N2C JSON 2.0.0, no failure, no editor opened.
  Then it translates the three event graphs in one translate-batch call (anthropic, claude-haiku-5-5, cpp): three
  completed in three folders, a `.cpp` in each, and `UN2CTagBlueprintLibrary::LoadTranslation` (run-python) reads each
  back. The probe then starts the same batch with a progress token and sends `notifications/cancelled` when the
  second graph starts: the call ends without a response, `BatchSummary.json` shows one completed and two skipped, and
  the log shows the in-flight request cancelled and no third request. The scratch folder, export folder and batch
  folders are deleted, and a leak scan covers the editor log, `Saved/NodeToCode`, `Saved/Logs` and the run's logs.
  Report: `logs/live_batch_0169_<label>.json`.
- `live_create_blueprint_0166.py --engine ... --project ... --label 5.8 [--cli-model haiku]` (no provider key) checks
  create-blueprint-class (card 0166). `claude -p` with only `mcp__nodetocode__*` creates a Widget Blueprint from
  `/Script/UMG.UserWidget` in a fresh `/Game/N2CTest/Create0166_<time>` folder and repeats the call: the first must
  succeed with `saved` true and `assetClass` `WidgetBlueprint`, the second must fail because the asset exists. The
  probe then creates an Actor Blueprint in that existing folder and checks that an AnimInstance parent and the
  interface type with an Actor parent are refused, and run-python reads the assets back (a WidgetBlueprint, an Actor
  Blueprint, nothing for the refused calls). The folder is deleted. Report:
  `logs/live_create_blueprint_0166_<label>.json`.
- `live_components_0167.py --engine ... --project ... --label 5.8 [--cli-model haiku]` (no provider key) checks the
  component tools (card 0167) on an Actor Blueprint made in memory in a fresh `/Game/N2CTest/Components0167_<time>`
  folder. `claude -p` with only `mcp__nodetocode__*` adds CameraBoom, FollowCamera under it, Lens under FollowCamera
  and a RotatingMovement component, lists them, deletes CameraBoom keeping its children, then FollowCamera with its
  children. Every call must compile without errors, each `attachedTo` must be the named parent (Lens under
  FollowCamera, not the root), FollowCamera must move to DefaultSceneRoot and Lens go with FollowCamera. The probe
  then undoes the two deletes (`TRANSACTION UNDO`), compiling and listing after each, and checks the hierarchy comes
  back. The folder is deleted. Report: `logs/live_components_0167_<label>.json`.
- `live_tool_reference_0179.py --engine ... --project ...` (no provider key) checks the generated tool reference
  (card 0179) against the live server: it starts a headless editor with `-N2CMcp` twice, with Allow MCP Python
  Execution on and then off, and reads `tools/list`. With it on the names must be exactly the tools of
  `Context/mcp-server/tools.generated.json`, and each listed tool's title, description, schemas, annotations and
  `_meta` must equal the reference's; with it off the names must be exactly the tools the reference marks
  `defaultEnabled`. Prints one JSON line; the editor logs go to `logs/live_tool_reference_0179_python_{on,off}.log`.
- `live_gate_0377.py --engine ... --project ... --label 5.8 [--provider anthropic --model claude-haiku-5-5]` runs gate
  0377 in a windowed `UnrealEditor.exe` (not `-Cmd`, not `-unattended`; log via `-abslog`), with the provider, model
  and Min Severity Debug written into the host's `Saved/Config` NodeToCode.ini for the session: (1) a
  `translate-focused-blueprint` returns code; (2) `nodetocode/cancelTask` 2 s in answers `cancellation_initiated`, the
  one response is the cancelled error, and `/mcp/health` answers every 5 s for 180 s with no crash line; (4) `obj gc`
  + `obj list class=<service class>` before and after 10 MCP translations give the same count (console output is
  read from the log between `LogPython` markers); (3) it starts `obj gc` every 5 s from a Slate post-tick callback,
  writes `logs/0377/ready_<label>` and waits for `logs/0377/toolbar_done_<label>` while someone presses the Node to
  Code toolbar button in the focused Blueprint editor, then checks the log for the toolbar request, a parsed response
  and a GC inside it. `--toolbar-only` runs check 3 alone, `--skip-toolbar` skips it. It quits by deleting the
  in-memory probe Blueprint and calling `quit_editor()`, so no save prompt (or a force kill and a Restore Packages
  dialog on the next start) follows. Report: `logs/0377/gate_<label>.json`.
- `live_truncated.py --engine ... --project ... --label 5.8 [--only anthropic gemini ...]` (card 0092) sets each
  provider's Max Output Tokens to 16 on the settings object, runs translate-focused-blueprint with a current model
  (claude-haiku-5-5, gemini-3.5-flash-lite, gpt-6-luna, deepseek-flash), restores the setting and checks the
  CallToolResult is error JSON with `error_kind` `Truncated`, the provider's reason in `provider_error_type`
  (`max_tokens`, `MAX_TOKENS`, `length`) and a message naming the Max Output Tokens setting. Keys go into the
  editor's environment only and the logs, report and translation folders are scanned for them. Changing a setting
  in the editor saves the host's `Config/DefaultNodeToCode.ini` (with a settings version the migration specs then
  see); `EditorSession` puts that file back byte for byte. Refusals and safety
  blocks are not triggered live; `NodeToCode.LLM.Parse` covers them with documented bodies. Report:
  `logs/live_truncated_<label>.json`.
- `live_provider_matrix.py --engine ... --project ... --label 5.8 [--cases <id> ...] [--providers ...] [--timeout 300]`
  runs real translations through several providers and models in one headless editor (`--list` prints the case
  ids). It sets Min Severity to Debug in the host's `Saved/Config` layer for the session and turns the console
  variable `n2c.LogLLMPayloads` on (card 0159: payloads are logged only with it) so each request payload is
  logged, makes the probe Blueprint (`SETUP_UNWIRED_PY`, then `CONNECT_PY` wires BeginPlay to its Print String) so
  there is logic to translate, then per case
  applies the case's settings (DeepSeek model entry, Max Output Tokens), translates, restores the settings and checks
  that case's logged payload: no temperature/top_p/top_k where the provider rules forbid them, the expected output
  budget field and value, OpenAI `response_format` `json_schema` with `strict: true`, Gemini `responseSchema` with no
  `additionalProperties`, DeepSeek `thinking` and `reasoning_effort`. A case passes when the translation returns
  code and every rule holds. After the cases it turns `n2c.LogLLMPayloads` off and translates the first case again
  (`payloads_off`): that slice must have no payload, prompt or answer line, only sizes. It also scans the editor log,
  `Saved/Logs`, the plugin log and new translation folders for key values, and the logs for a key in URL form
  (`?key=`, `&key=`) or `key=` with a key-shaped value (Print String's `key` argument in generated code does not
  count). Report: `logs/live_provider_matrix_<label>.json`.
  - `--fanout` (card 0135) also adds a data fan-out before the cases: Get Actor Location's Return Value into Print
    String's In String (through the To String (Vector) node the schema inserts) and into a Set Actor Location run
    after the Print String. The report's `fanout` lists the N2C JSON's array-valued `flows.data` entries, and each
    case keeps its `code` so the translation of the fan-out can be read.
  - `--branch` (card 0136) puts a Branch on the exec line before the cases: BeginPlay runs a Branch on Has
    Authority, True runs Collect Garbage and False runs the Print String (the opposite of the nodes' order). The
    report's `branch` lists the focused graph's `flows.execution` with the node and pin each entry names, and each
    case keeps its `code` and gets `branch_check`: whether the generated if/else runs CollectGarbage when
    `HasAuthority()` is true and PrintString when it is false (`ok: null` when the heuristic cannot tell; read the
    code). With `--branch` a case passes only when `branch_check` is ok.
  - `--liveness` (card 0137, issue #13) translates a graph of nodes Blueprint never runs instead of the wired
    BeginPlay: a custom event runs the Print String, then Collect Garbage; Compile Mode Final Release disables the
    Development Only Print String (exec passes through it); an unwired Get Actor Location is unused; the default
    BeginPlay, ActorBeginOverlap and Tick stay ghosts. The report's `liveness` lists the focused graph's nodes with
    their flags and its exec flows, and each case gets `liveness_check`: the code calls CollectGarbage and implements
    no BeginPlay, Tick, ActorBeginOverlap, PrintString or GetActorLocation (comments ignored). A user-disabled node
    cannot be made from Python (EnabledState is not editable there); the capture golden `DisabledNodes` covers it.
  - The case list holds current models only (the active rows of `Content/Data/N2CModelCatalog.json`): no retired,
    deprecated or "shuts down" IDs are tested. Update it when the catalog's lineup changes.
  - Each translation is bounded by `--timeout` (wall clock; the streamed response's half-second progress events keep
    a socket timeout from ever firing). A case that gets no response in time fails as timed out and the remaining cases
    are skipped, since the editor still holds that request. Pass `--cases` in the order to run them and put slow
    models last.
  - `EditorSession` puts the editor in a Windows job object that kills it when the Python process exits, so a killed
    or crashed run cannot leave an editor running; only `stop()` restores the `Saved/Config` patches and the host's
    `Config/DefaultNodeToCode.ini`.
  - The editor runs with `-RenderOffScreen`: on UE 5.6 under `-nullrhi` the opened Blueprint editor's window
    otherwise reaches `FGenericWindow::GetRestoredDimensions`, which is Fatal.
  - Provider HTTP errors (429, 503, 402) surface in the response as "Failed to parse LLM response" with the raw body;
    the case's `log_errors` hold the HTTP status.

## Epic's registry through MCPClientToolset (UE 5.8, card 0141)

`live_mcpclienttoolset.py --engine G:/UE/UE_5.8 --project <5.8 host> [--no-translate] [--activity-timeout N] [--eager]`
mounts NodeToCode in Epic's ToolsetRegistry the way `wiki/developers/Using-NodeToCode-with-Unreal-MCP.md` says. For
the run it enables ModelContextProtocol and MCPClientToolset in the host `.uproject` and writes the MCP Toolset
Servers entry (Streamable HTTP, Auth None) and Epic's MCP server settings (auto start, its own port) into the host's
`Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`; `EditorSession` puts every file back byte for byte
(`restored`). It then replays MCPClientToolset's own request sequence against `/mcp` (initialize at 2024-11-05, no
MCP-Protocol-Version header, ids from 0, tools/list without params, tools/call without a progressToken), compares its
tools/list with a normal 2025-11-25 session, and calls the same tools through Epic's MCP server (`list_toolsets`,
`describe_toolset NodeToCode`, `call_tool`), recording the shape Epic returns (`{"text": ...}`, `isError` dropped).
`get-available-translation-targets` and an unfocused `translate-focused-blueprint` need no key; the two real
translations (direct and through the registry) use only the provider's variable from `.env` (`--no-translate` skips
them). `--activity-timeout 3` starts the editor with `[HTTP] HttpActivityTimeout=3` to show the registry hop timing
out (`registry_translate_timed_out`); `--eager` turns Epic's tool search off and calls `NodeToCode.<tool>` as native
tools. Report: `logs/live_mcpclienttoolset_<label>[_t<N>][_eager].json`. Observed on 5.8 (card 0141): registered on
every start, 62 tools in both lists, translations in 8 to 12 s with claude-haiku-5-5, the 3 s run timed out at 3.1 s.

## Migration check

The 1.2.x to 2.0 migration check (card 0079) lives in `Tests/Migration/` and reuses `editor_session.py`,
`env_keys.py` and `live_env_translation.py` from here; see `Tests/Migration/README.md`.
