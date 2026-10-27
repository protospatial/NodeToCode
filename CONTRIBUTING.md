# Contributing to NodeToCode

Thanks for helping. This page covers where to send changes, how to build and test them, and the sign-off every
commit needs. For questions and support, use the [Discord](https://discord.gg/4t3Syvk4AG); for bugs and feature
ideas, open an issue with one of the templates. Security problems go through [SECURITY.md](SECURITY.md), never a
public issue.

## Branches

- **`dev-2.0` is the target branch.** NodeToCode 2.0 (UE 5.6 to 5.8, the MCP server and agent tools) is developed
  there. Branch from an up-to-date `dev-2.0` and open your pull request against it.
- **`main` stays at 1.2.x** and takes no new work, fixes included. Pull requests against `main` are not
  merged. If one carries something 2.0 needs, the maintainer ports it to `dev-2.0` by hand, keeps you as a
  co-author with a `Co-authored-by:` trailer, and closes the original with a link to the port. You can save that
  step by rebasing your change onto `dev-2.0` yourself.
- Keep one topic per pull request. Open an issue first for anything large (a new provider, a new MCP tool family,
  a change to the translation format), so the design can be agreed before you write it.

## Building and testing

NodeToCode 2.0 supports Unreal Engine 5.6 (the floor), 5.7 and 5.8 (the ceiling), editor only, Win64. A change to
C++ must compile on all three.

1. Build with `build.ps1` at the repo root (Windows PowerShell 5.1 or PowerShell 7). It finds each installed engine
   (registry, Epic launcher, registered source builds, or the `N2C_UE_5_6` / `N2C_UE_5_7` / `N2C_UE_5_8` environment
   variables and `-Engine '5.8=<path>'`), creates a small C++ host project per engine under `..\Hosts` (or
   `-HostsRoot <dir>`) whose `Plugins/NodeToCode` folders link back to your checkout, and builds its editor target.
   `.\build.ps1 -WhatIf` shows what it found; `.\build.ps1 -All -Smoke` builds 5.6, 5.7 and 5.8 and smoke-tests the plugin
   headless. Machine-specific paths go in a gitignored `build.local.ps1` (see `Context/development-workflow/AGENTS-development-workflow.md`). Switch branches only
   when no host editor is open.
2. Guard version-specific code with `ENGINE_MINOR_VERSION` (include `Runtime/Launch/Resources/Version.h`).
3. Run the plugin's automation tests on each engine: `.\build.ps1 -All -Test` builds, runs
   `Automation RunTests NodeToCode` in a headless editor and fails if any test fails. You can also run them in the
   editor's Session Frontend (Automation tab, filter `NodeToCode`). Every test should pass. Tests live in
   `Source/Private/Tests/` (its `README.md` covers naming, layout and helpers); add one for a bug you fix where
   you can.
4. A change to the Python package or the bundled scripts under `Content/Python` gets a test in `Tests/python`.
   Those run on plain CPython with a stub `unreal` module, no editor needed, in about a second. With
   [uv](https://docs.astral.sh/uv/) installed, from the repo root:

   ```
   uv run --no-project --python 3.11 --with-requirements Tests/python/requirements.txt -- python -m pytest Tests/python
   ```

   `.\build.ps1 -Test` also runs them on each engine's bundled Python. Python 3.11 is what UE 5.6 to 5.8 embed, so
   do not use newer syntax in `Content/Python`. Known defects are `xfail(strict=True)` tests named after their
   finding; fixing one means removing its marker. Tests that need the editor go in `Tests/python/editor`;
   `Tests/python/README.md` gives the command that runs them.
5. If you cannot build on every engine, say which ones you built and tested in the pull request. The maintainer
   runs the rest before merging.

## Code and headers

- Follow the style of the code around your change (Unreal naming, `N2C` prefixes, tabs or spaces as the file uses).
- New source files start with the project header from decision record
  [`0002`](.agents/decisions/0002-licence-and-contribution-model.md) (`#` instead of `//` in Python and shell):

  ```
  // Copyright (c) 2025 Nick McClure (Protospatial)
  // SPDX-License-Identifier: Apache-2.0
  ```

  Use the current year in new files. Code you bring in from elsewhere keeps its own licence header, must have a
  licence compatible with Apache-2.0, and is listed in `NOTICE`.
- Never commit API keys, `.env` files or a `secrets.json`, including in tests, logs or screenshots.

## Commits

Commit messages follow the style of `git log` on `dev-2.0`:

```
fix(mcp): refuse conversion-node links into an already linked input

What was wrong, and what the change does.

Verified: what you built and ran, on which engine versions, and what you saw.

Docs: wiki/Some-Page.md
Signed-off-by: Your Name <you@example.com>
```

- Subject: `type(scope): summary`, with `type` one of `feat`, `fix`, `docs`, `test`, `chore` or `refactor`.
- `Docs:` names the `wiki/` pages the change updates, or says `Docs: none, <why>`. A change to anything a user or
  an MCP client sees updates its wiki page in the same pull request; the rules for those pages are in
  [`.agents/contracts/docs.md`](.agents/contracts/docs.md), and `python tools/docs_check.py --changed dev-2.0`
  checks them. The **Docs check** job (`.github/workflows/docs-check.yml`) runs that check on every pull request
  into `dev-2.0`: it fails when the change touches the code of an area mapped in `.agents/docs-parity.json`
  without touching its pages, unless the description or a commit message has a `Docs: none, <why>` line with a
  real reason (the template's unedited `<why>` does not count). Other warnings show in the job summary.

## Sign-off (DCO)

NodeToCode is licensed under the [Apache License 2.0](LICENSE), and contributions are accepted under the same
licence. Every commit in a pull request must be signed off, certifying the
[Developer Certificate of Origin 1.1](https://developercertificate.org/): that you wrote the change, or otherwise
have the right to submit it under the project's licence.

Sign off with `git commit -s`, which adds a line with your name and the email address of the commit author:

```
Signed-off-by: Your Name <you@example.com>
```

Use your real name or the name you are known by publicly. To sign off commits you already made, run
`git rebase --signoff dev-2.0` and force-push your branch. There is no CLA.

## Pull requests

Fill in the pull request template: what changed and why, the engine versions you built and tested, the `Docs:`
line, and the sign-off box. The maintainer may squash your commits when merging; your authorship and sign-off are
kept.
