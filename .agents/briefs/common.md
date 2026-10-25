# Common brief: how an agent works on NodeToCode 2.0 beside other agents

Every agent reads this before its task. The repo's rules are in the root `AGENTS.md` (and the local `CLAUDE.md`);
where this brief and those differ, they win and this brief is wrong: say so in your report.

## Read first
1. Root `AGENTS.md`, then your round's brief in `.agents/briefs/rounds/` (names your lane, port offset, locks).
2. Your card in full: `G:/Personal Projects/n2c/.agents/kanban/<id>-*.md` ("What was seen", "Where to look", "Done
   looks like", "Depends on" are your spec; line numbers predate later merges, re-find code by name). Review
   evidence: `python -I "G:/Personal Projects/n2c/Reviews/2026-10-22/kanban/lookup.py" <ids>`.
3. A "Depends on" card that is not done (and is not an owner decision) means stop and report. For an open owner
   decision, apply the review's recommendation provisionally and say so at the top of the PR body.

## Your name and workspace
- Your lane name (`lane-a`, `lane-b`, ...) is `--as <name>` for every `aw` command. Run `aw` from
  `G:/Personal Projects/n2c` (the board lives there).
- You work only in your lane's worktree `D:/n2c-lanes/<lane>/NodeToCode`. Start each card:
  `git fetch origin && git switch -c card/<id>-<slug> origin/dev-2.0` (tree must be clean first).
- `build.ps1` puts your hosts at `D:/n2c-lanes/<lane>/Hosts` (`<worktree>\..\Hosts`); the first `-All` build of a
  fresh lane creates them and takes several minutes. Logs: `<worktree>/BuildLogs`.
- Before any smoke, probe or live script, set your port offset from the round brief:
  `$env:N2C_PORT_OFFSET = "<n>"` (PowerShell) or `export N2C_PORT_OFFSET=<n>`.
- Never edit, build in, switch or clean `G:/Personal Projects/n2c/NodeToCode` (the integration checkout) or
  `G:/Personal Projects/n2c/Hosts`. Never touch another lane's worktree.

## Git
- Stage exact paths; never `git add -A`/`.`/`-u` or `commit -a`. Read `git diff --cached --stat` before committing.
- Commit WIP as you go; never discard work. Never rebase a pushed branch, never force-push.
- Do not push, open PRs, or move cards: the orchestrator reviews and merges.
- Before your final message: `git fetch origin`; if `origin/dev-2.0` moved, merge it into your branch, fix
  conflicts, and re-run what the conflict touched.
- Do not edit `CHANGELOG.md` (every lane would conflict). Put your changelog lines in the PR body under
  `Changelog:`; the integration pass writes them.
- If your card changes a shared number (the smoke tool counts in `Tests/smoke/expected.json`, `SettingsVersion`,
  the N2C JSON version), say so in your final message; the orchestrator claims it at merge (`aw claim`).

## Scarce resources (declared in `G:/Personal Projects/n2c/.agents/agent-works.json`)
| Lock | Protects | Needed by |
|---|---|---|
| `desktop` | the one screen and mouse | any computer-use session (windowed editor UI) |
| `mcp-port-27000` | the default MCP port | windowed editors on defaults, `Tests/Migration/migration_12x_check.py` |
| `user-secrets` | `%LOCALAPPDATA%/NodeToCode` (secrets, discovery) | the migration check, secret-store/keychain cards and live checks of them |
| `engine-user-config` | `%LOCALAPPDATA%/UnrealEngine/5.x/Saved/Config` | checks that hash or edit engine-wide editor settings |
- Lock order when you need two: `desktop`, `mcp-port-27000`, `user-secrets`, `engine-user-config`.
- Run under a lock: `aw lock run <res> --as <lane> --for "<what, minutes>" -- <command>`. Exit 3 `STILL QUEUED`
  means you did not run: do other work, retry the same command. Hold a lock only for the run.
- Declare a full build/test: `aw lease cpu --cpu 12 --mem 12GB --as <lane> --for "build -All -Smoke -Test" -- <cmd>`.
- An UnrealEditor you did not start belongs to another lane or the owner: never kill it. Kill only your own.

## Environment gotchas
- Shell heredocs collapse `\\`; write files with backslashes using the Write tool. Many files have trailing
  whitespace / mixed CRLF; the Edit tool works after a Read.
- Build logs and editor logs are UTF-16 or contain NULs: `tr -d '\000'` or decode in Python.
- UE 5.6 is the floor, 5.8 the ceiling; guard with `ENGINE_MINOR_VERSION` (`Runtime/Launch/Resources/Version.h`).
  Engine sources (read-only): `G:/UE/UE_5.6`, `G:/UE/UE_5.7`, `G:/UE/UE_5.8`.
- In `-ExecCmds` separate commands with commas. Headless Blueprint editors need `-RenderOffScreen`; closing one
  headless is fatal on 5.6/5.7 (use `N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR` in specs).
- Specs follow `Source/Private/Tests/README.md`; tool specs use the harness `Source/Private/Tests/N2CToolSpecHarness.h`.

## Verify before your final message
1. `powershell.exe -NoProfile -File build.ps1 -All -Smoke -Test` from your worktree: 5.6, 5.7, 5.8 build OK, smoke
   PASS, every automation test and pytest passing.
2. A new test for a defect must be shown to catch it (revert the fix, see it fail on one engine, restore, rebuild).
3. The card's own "Done looks like" checks, live where they can run here.
4. Docs: `python -I tools/docs_check.py --changed origin/dev-2.0` 0 critical, and the PR workflow's check
   (`.github/scripts/docs_check_pr.py`, with your PR body as the note) passes: touching a docs-parity surface needs
   one of its listed pages updated or a `Docs: none, <why>` line.
5. No UnrealEditor of yours left running; no probe patches left in your hosts' `Saved/Config`.

## Testing yourself
- Provider keys are in `G:/Personal Projects/n2c/NodeToCode/.env` (gitignored): load them only into child
  processes (`Tests/mcp_probe/env_keys.py`); never print, log, commit or type them. Use only current models from the
  catalog (cheap ones like claude-haiku-5-5, gemini-3.5-flash-lite); all four provider accounts are funded.
- UI clicks (buttons, tabs, menus, Project Settings) are yours, with the computer-use tools under the `desktop` lock.
  Launch the windowed editor yourself; on a refused click or user interrupt, bring the editor forward and retry
  (wait, re-screenshot, at least 3 tries). Screenshot every check and look at it.
- MCP end to end: `claude -p "<task>" --mcp-config <json> --allowedTools "mcp__nodetocode__*" --output-format json`.
- Make test Blueprints under a `/Game/N2CTest...` scratch folder in your hosts and delete them afterwards.

## Commit and report
- One commit on your branch in the style of `git log`: `fix(scope): subject`, body with the defect and change, a
  "Verified:" paragraph, `Review items: <ids>`, `Docs: <pages>` or `Docs: none, <why>`, and the trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- PR body to `G:/Personal Projects/n2c/Reviews/2026-10-22/kanban/pr_body_<id>.md`: card id, defect, change, the
  `Docs:` line, `Changelog:` lines, verification, ending `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- Final message: verdict first; branch, worktree, commit; files; results per engine; decisions applied; shared
  numbers changed; what was not verified and why.
