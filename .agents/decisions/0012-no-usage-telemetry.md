# 0012. Usage telemetry: none; public stats and a poll answer the usage question

## Status

decided (2026-11-02), by the owner on card 0094

## Sources

- Kanban card 0094 "Decide whether NodeToCode collects usage telemetry" (owner decision, decided 2026-11-02). Cards
  that build on it: 0096 (the public answer to issue #11) and the privacy and data-flow statements of epic 0004
  (0189).
- NodeToCode 2.0 review, 2026-10-22: dec-12, issue-11, gap-7. Evidence: `python -I
  Reviews/2026-10-22/kanban/lookup.py dec-12 issue-11 gap-7` in the workspace.
- Issue #11: https://github.com/protospatial/NodeToCode/issues/11 (do users rely on the MCP server or on skills?).

## Context

Issue #11 asks which surfaces people use. Options (dec-12): opt-in anonymous telemetry, or no telemetry and use
GitHub and Fab stats plus a public survey. Studios are sensitive to data leaving the machine, and the roadmap does not
depend on exact numbers.

## Decision

1. **NodeToCode collects no usage telemetry,** opt-in or otherwise. The only data that leaves the machine is what the
   user sends to the LLM provider they configured.
2. **What replaces usage numbers:** GitHub stars, clones, issues and release downloads, Fab listing stats, and a public
   poll (a GitHub Discussions poll or a pinned comment on issue #11). Turning on GitHub Discussions is a repository
   setting the owner changes; until then the poll lives on issue #11.

## Consequences

- No consent UI, data policy or telemetry endpoint is built.
- The privacy statement (card 0189) says plainly that NodeToCode sends nothing to Protospatial.
- Adding telemetry later needs a new record that supersedes this one, with consent UI and a data policy.
