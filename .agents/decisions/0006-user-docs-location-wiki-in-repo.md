# 0006. User docs location: the GitHub wiki stays the target, its source lives in wiki/ in the repo

## Status

decided (2026-10-22), by the owner on card 0123

## Sources

- Kanban card 0123 "Decide whether the 2.0 user docs stay in the GitHub wiki or move into the repo" (owner
  decision, decided 2026-10-22). Cards that build on it: 0335 (publish the 2.0 pages), 0334 (rewrite the user UI and
  provider pages for 2.0), 0411 (run the docs parity check on every pull request), 0180 (docs lint against the tool
  reference), and the E14 cards that write the 2.0 user and developer pages.
- NodeToCode 2.0 review, 2026-10-22: imp-dx-docs-release-8 (the wiki ends at v1.2.3, options a-c),
  rec-agentic-landscape-9. Evidence: `python -I Reviews/2026-10-22/kanban/lookup.py imp-dx-docs-release-8
  rec-agentic-landscape-9` in the workspace.
- The docs-parity adoption (commit 014c61f, local branch docs/wiki-in-repo), which moved the wiki source into `wiki/`.
  In the repo: `.agents/docs-parity.json`, `.agents/contracts/docs.md`, `tools/docs_check.py`,
  `.github/workflows/docs-check.yml`, `.github/scripts/docs_check_pr.py`, `.github/workflows/publish-wiki.yml`.

## Context

The user docs were the GitHub wiki, a separate repository (`protospatial/NodeToCode.wiki`). It ends at v1.2.3: no
MCP, no Python script-only mode, no tags, batch translation, Slate window, graph overlay or 5.6-5.8 range. Because
it is a separate repository, a code PR could not update the page it made wrong, CI could not check the pages in the
same PR, and one set of pages had to serve both the 1.2.x Fab users and 2.0. `NodeToCode.uplugin` `DocsURL` and the
Fab listing both point at the wiki.

Options (imp-dx-docs-release-8):

1. **(a) Keep the wiki** and label each page 1.2.x or 2.0; lint against a local wiki checkout.
2. **(b) Move the 2.0 user docs into a `docs/` folder** in the repo, keep the wiki as the 1.2.x archive plus a
   pointer page.
3. **(c) Build a docs site** from that folder.

## Decision

A variant of (a) and (b): the wiki stays the published target, and its source lives in the code repository.

1. **Where the 2.0 user pages live.** In `wiki/` in the code repository; `wiki/` on `dev-2.0` is the 2.0
   documentation, edited in the same PR as the code it describes. Pages for plugin users sit at the wiki root (the
   Guide, listed in `wiki/_Sidebar.md`). Pages for developers and agents (MCP, Python, Agent Skills, the Claude Code
   plugin, Unreal MCP) sit in `wiki/developers/`, the `developers` root of `.agents/docs-parity.json`. Screenshots
   are tracked with the pages, in `wiki/images/` (`images.dir` in `.agents/docs-parity.json`; the folder appears
   with the first 2.0 screenshot). Agent-facing reference (`AGENTS.md`, `Context/**`) is not user
   docs and stays where it is.
2. **How the 1.2.x pages stay reachable.** The live GitHub wiki keeps the 1.2.x pages until card 0335 publishes the
   2.0 set with `.github/workflows/publish-wiki.yml`: run by hand, only from `dev-2.0`, never from `main`. The
   published wiki is a derived copy of `wiki/`; nobody edits it in the browser. Card 0335 decides how the 1.2.x
   pages are kept once the 2.0 set replaces them (for example a 1.2.x archive page or the `v1.2.x` tags of the
   wiki repository); until then they are simply the live wiki.
3. **What `DocsURL` points at for 2.0.** It stays `https://github.com/protospatial/NodeToCode/wiki`, as does the Fab
   listing. The URL does not change; what it serves changes when card 0335 publishes.
4. **Whether the docs lint checks those pages in the same PR.** Yes. Because the pages are in the same repository,
   `tools/docs_check.py` runs on every pull request into `dev-2.0` (`.github/workflows/docs-check.yml`, card 0411):
   a change to a surface's code must change that surface's pages under `wiki/` or carry a `Docs: none, <why>` line,
   and links, images and the sidebar are checked in the same PR. Card 0180's lint (tool names, arguments and links
   in the agent docs against the generated tool reference) runs over the same checkout, and its link check covers
   `wiki/` with no separate wiki clone.

## Consequences

- A code PR that changes a documented surface updates its `wiki/` page in the same PR, or says why not; reviewers
  see both in one diff.
- Nothing in `wiki/` is live until card 0335's first publish; the published wiki lags `dev-2.0` until then, by
  design, so 1.2.x users keep matching docs.
- After the first 2.0 publish, `publish-wiki.yml` may gain a `push` trigger on `dev-2.0` for `wiki/**`; it never
  publishes from `main`.
- No separate docs site (option c). If one is wanted later, it builds from `wiki/`, and a new record supersedes this
  one.
- The wiki repository is written only by the publish workflow; an edit made in the browser is overwritten at the
  next publish.
