# Writing the user docs

The contract for every author of `wiki/`, human or agent. docs-parity's `tools/docs_check.py` checks what a
script can; review checks the rest. Keep this page current when the docs' shape changes.

## Where the docs are

- **Target:** a GitHub wiki, plain markdown. Pages in `wiki/`, order in `wiki/_Sidebar.md`.
- **Source of truth:** `wiki/` on dev-2.0 describes NodeToCode 2.0. It was copied from the wiki repository at
  1dc2904 on 2026-10-22; that repository keeps the older history.
- **Published at:** https://github.com/protospatial/NodeToCode/wiki, by `.github/workflows/publish-wiki.yml`.
  Until 2.0-beta the live wiki stays as the 1.2.x docs and the workflow runs only by hand (Run workflow, on
  dev-2.0). When dev-2.0 becomes the default branch, switch it to run on every push to that branch. Nothing is
  ever published from `main`.
- **Derived copy:** the published wiki. Nobody edits it in the browser; a browser edit is overwritten by the
  next publish. Fixes for 1.2.x readers before the beta are the one exception, made in the wiki repository by
  the owner.
- **Check:** `python3 tools/docs_check.py` (tree), `--changed dev-2.0..HEAD` (a branch), `--stale` (shots).

## The roots

| Root | Folder | Reads it | Allowed | Never |
|---|---|---|---|---|
| Guide | `wiki/` (top level) | Unreal developers and designers using the plugin in the editor | the editor's own words: menus, buttons, Project Settings entries, what happens and why; C++ in examples of generated or reference code | the plugin's own source paths, internal class names, build or contribution steps |
| Developers | `wiki/developers/` | people driving NodeToCode from an agent or a script: MCP clients, Python, Agent Skills | MCP tool names and arguments, JSON payloads, client configuration, Python calls, commands they run | how to build, package or contribute to the plugin; anything only the maintainer sees |

Contributor and agent docs (build loop, architecture, tool internals) stay in `AGENTS.md` and `Context/`,
never in the wiki. No root cites a plugin source path. GitHub flattens wiki folders on display, so every page
name is unique across both roots.

## A page

- One topic per file, 120 lines at most, file name `Title-With-Dashes.md` (the page's URL).
- No frontmatter (GitHub wikis do not read it). The first line may be the existing breadcrumb heading
  (`### Section → Page`); the page then opens with a two-to-four sentence summary that answers its question.
- Added to `wiki/_Sidebar.md` in reading order, in the right section; removed from it when deleted, with
  every link to it fixed.
- Links between pages: `[text](Page-Name)` or `[[Page Name]]`, never a full URL to the wiki.
- Tables for facts, prose for reasoning, bullets for real lists; the editor's own words in **bold**.
- Plain markdown and GitHub's HTML subset only (`<details>`, `<img>`, `<picture>`).
- Voice: plain and direct, second person; no em-dashes, no emoji in body text, no hype words.
- Every setting, menu path, default and example was read from the built plugin (Project Settings →
  Plugins → Node to Code, or the editor itself) on dev-2.0 before it was written.

## A screenshot

- Made only by the capture harness, once it exists (owed: a board card in epic E16). Until then, keep the
  existing images and do not hand-capture new ones; a page that needs a new picture says so in its PR.
- Single images (the Unreal editor has one theme), `wiki/images/<section>/<name>.png`, captured at 2x and
  shown at 1x with `<img src="images/<section>/<name>.png" alt="..." width="<half the pixel width>">`.
- Alt text says what the picture shows. Cropped to what the text describes, never scaled.
- Sample content only: a fixture project and Blueprints, fictional names, no real API keys or account
  details on screen (mask the key fields in Project Settings).
- The existing images are absolute links to `assets/` on `main` and show the 1.x UI. Replace each through the
  harness when its page is rewritten for 2.0.

## The same change

A change that alters what a reader sees or depends on updates its pages (and, once the harness exists,
retakes its shots) in that commit or pull request, and says so in one line: `Docs: <pages touched>` or
`Docs: none, <why>`. The surface map in `.agents/docs-parity.json` names the pages for each area of the code;
when the map is wrong, fix it in the same change. The MCP server, the Python bridge, the tag manager and batch
translation get surfaces when their first pages are written.
