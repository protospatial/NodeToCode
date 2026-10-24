# 0001. Platforms for 2.0-beta: Win64 only until Mac or Linux is verified

## Status

proposed (2026-10-23), provisional pending the owner's confirmation on card 0073

**Provisional:** the owner has not decided card 0073 (review decision dec-11). Card 0074 applies the review's
recommendation; the owner may still choose to verify Mac before 2.0-beta.

## Sources

- Kanban card 0073 "Decide Mac and Linux support for 2.0-beta" (owner decision, open) and card 0074, which applies it.
- NodeToCode 2.0 review, 2026-10-22: dec-11 (options and recommendation), gap-10 (Mac never compiled), pr-23
  (Linux port). Evidence: `python -I Reviews/2026-10-22/kanban/lookup.py dec-11 gap-10 pr-23` in the workspace.
- PR #23 "Add Linux editor support" (Dylan Martin), tested on CachyOS with UE 5.8.3 against main only.

## Context

1.2.x and dev-2.0 allowlisted Win64 and Mac in `NodeToCode.uplugin`. The only build machine is a Windows 11 PC;
the build loop (`build.ps1`) and CI compile Win64 only, and Mac has never been compiled: the Mac-only branch of
`UN2CSettings::CopyToClipboard` was missing a semicolon and copied twice. Fab compiles every allowlisted platform on
each supported engine and rejects a listing whose build fails, so an unverified platform risks the whole release.

Options (dec-11):

1. Verify Mac on a self-hosted macOS runner and add Linux through CI.
2. Remove Mac from PlatformAllowList until it is verified; add Linux later.
3. Keep Mac allowlisted, unverified.

No Mac runner exists today, so option 1 cannot happen before 2.0-beta.

## Decision

Option 2. The 2.0-beta `.uplugin` allowlists **Win64 only**.

The source stays portable, so adding a platform is a `.uplugin` change only: no `PLATFORM_MAC`, Mac or Windows
`PlatformApplicationMisc` includes, or `FMacPlatformProcess` calls in `Source/` (card 0074 replaced them with
`HAL/PlatformApplicationMisc.h` and `FPlatformProcess`), and include paths match the engine's file-name case.

- **Mac is re-added** when `RunUAT BuildPlugin` succeeds for Mac on each supported engine (5.6, 5.7, 5.8) on a Mac or
  a self-hosted macOS runner, and the editor loads the plugin there (window opens, one translation, the MCP tools/list request).
- **Linux is added** when the same BuildPlugin passes for Linux on each supported engine (a Linux runner, or
  cross-compiling from Windows with the engine's Linux toolchain) and a Linux smoke test passes (the PR #23 author
  offered one: editor load, a translation, the MCP tools/list request, the stdio bridge launching). Linux is then listed as
  community-tested.

## Consequences

- Fab builds and lists Win64 only; Mac users of 1.2.x stay on 1.2.x until Mac is re-added (noted in the migration
  matrix, `Context/release/migration-1.2-to-2.0.md`, and on the wiki's Plugin Installation page).
- Re-adding Mac or adding Linux needs a `PlatformAllowList` entry, the docs line, and the verified build above;
  no source change should be needed. A new platform-specific `#if` in `Source/` must keep that true.
- If the owner chooses option 1 or 3 on card 0073, a new record supersedes this one.
