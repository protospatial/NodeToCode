# 0011. Engine versions: UE 5.6 to 5.8, the three latest, with agent features gated to 5.8

## Status

decided (2026-11-02), by the owner on card 0052

## Sources

- Kanban card 0052 "Decide which engine versions 2.0 supports" (owner decision, decided 2026-11-02). Cards that build
  on it: 0199 (move the window when UE 5.9 ships), the packaging and README cards of epic 0006, the CI matrix of
  epic 0007 (0105), and the 5.8-only cards of epic 0015.
- NodeToCode 2.0 review, 2026-10-22: dec-4. Evidence: `python -I Reviews/2026-10-22/kanban/lookup.py dec-4` in the
  workspace.

## Context

`build.ps1` already targets UE 5.6, 5.7 and 5.8 and `NodeToCode.uplugin` names no engine version. UE 5.8 adds Epic's
experimental Unreal MCP and ToolsetRegistry, which epic 0015 builds on. Options (dec-4): 5.6 to 5.8 with agent
integration gated to 5.8; 5.7 to 5.8 only; 5.8 and later only.

## Decision

1. **2.0 supports UE 5.6, 5.7 and 5.8,** matching Fab's rule of the three latest engines. Every feature that does not
   need 5.8 works on all three.
2. **Gated to 5.8:** the ToolsetRegistry toolset, the Agent Skills registered through it, Live Coding integration, and
   anything else that needs an engine API 5.6 or 5.7 lacks. They are guarded with `ENGINE_MINOR_VERSION` and absent,
   not broken, on older engines.
3. **Moving the window:** when a new engine ships, it is added and the oldest is dropped in the same release, so the
   window stays three engines (UE 5.9 adds 5.9 and drops 5.6; card 0199). Code guarded only for the dropped engine is
   removed then.

## Consequences

- `build.ps1`, the packaging matrix, CI and the README list exactly 5.6, 5.7 and 5.8 until card 0199 moves them.
- Specs that need a 5.8 API run on 5.8 only and skip with a reason elsewhere.
- Supporting fewer engines later, or more than three, needs a new record that supersedes this one.
