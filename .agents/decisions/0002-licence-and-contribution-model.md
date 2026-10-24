# 0002. Licence and contribution model: Apache-2.0 only, NOTICE and DCO

## Status

proposed (2026-10-23), provisional pending the owner's confirmation on card 0053

**Provisional:** the owner has not decided card 0053 (review decision dec-9). Card 0076 applies this record so the
community files can exist; card 0077 (source headers) follows it. If the owner chooses a CLA, a new record
supersedes this one; nothing here has to be undone, because a CLA is added on top of Apache-2.0 contributions.

## Sources

- Kanban card 0053 "Decide the licence and contribution model" (owner decision, open), card 0076 (NOTICE,
  CONTRIBUTING, SECURITY, templates) and card 0077 (licence headers).
- NodeToCode 2.0 review, 2026-10-22: dec-9 (options and recommendation), gap-4 (no contribution model),
  rec-agentic-landscape-8 (a possible paid Pro tier on Fab). Evidence:
  `python -I Reviews/2026-10-22/kanban/lookup.py dec-9 gap-4 rec-agentic-landscape-8` in the workspace.
- `LICENSE` (Apache License 2.0, the whole repository), `ThirdParty/cpp-httplib/LICENSE` (MIT).

## Context

`LICENSE` is Apache-2.0, but every source file starts with
`// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.`, which reads as the opposite. There was no
NOTICE, CONTRIBUTING, DCO or CLA, while community pull requests are open and code from some of them has already been
taken into dev-2.0 by hand.

Options (dec-9):

1. Apache-2.0 only: SPDX headers, a NOTICE file, and a Developer Certificate of Origin (DCO) sign-off on commits.
2. Dual licence with a Contributor Licence Agreement (CLA), to keep a closed paid Pro tier possible.
3. Leave as-is. Not viable: the headers contradict the licence and contributions have no stated terms.

The review's recommendation: decide before merging any community PR; a CLA if a paid Pro tier is plausible,
otherwise option 1. No paid tier is planned today, and option 1 matches the licence the repository already has.

## Decision

Option 1, provisionally.

- **Licence:** Apache License 2.0 for the whole plugin. Contributions come in under the same licence (Apache-2.0
  section 5, "inbound = outbound"); there is no CLA.
- **NOTICE:** a `NOTICE` file at the repository root names the project's copyright holder and the third-party code
  shipped with the plugin (today only cpp-httplib, MIT). `Config/FilterPlugin.ini` packages it with the plugin.
- **DCO:** every commit in a pull request carries `Signed-off-by: Name <email>` (`git commit -s`), certifying the
  Developer Certificate of Origin 1.1 (https://developercertificate.org/). `CONTRIBUTING.md` explains it and the
  pull request template asks for it. No bot enforces it yet; the maintainer checks it at review.
- **Source header** (applied by card 0077 to every first-party source file, replacing the "All Rights Reserved"
  line; Python and shell files use `#` instead of `//`):

  ```
  // Copyright (c) 2025 Nick McClure (Protospatial)
  // SPDX-License-Identifier: Apache-2.0
  ```

  The year is the year the file was first published; existing files keep 2025. Vendored code under `ThirdParty/`
  keeps its own header.
- **Community code already in dev-2.0** (outside contributors, found with
  `git log dev-2.0 --format='%an %(trailers:key=Co-authored-by)'`,
  `gh pr list --repo protospatial/NodeToCode --state all --json number,author,state,baseRefName,title` and a
  comparison of PR diffs with dev-2.0):
  - Dylan Martin (GitHub Subj3kt1): PR #23 (Linux-portable platform code, in 5de97c4) and PR #24 (Anthropic model
    update, redone by hand in ebf6414); both commits carry his Co-authored-by trailer.
  - cryptosebek: PR #20 (LM Studio max-token setting, taken in 87ac8f4 with a Co-authored-by trailer).
  - YakovAU: PR #3 (Copy Blueprint JSON menu command), merged into the old `dev` branch on GitHub in 2025 and carried
    into dev-2.0 as c5177ab under the maintainer's name.
  None of these was signed off; their contributions were offered as pull requests to an Apache-2.0 repository and are
  credited in `NOTICE`. From now on, code taken from a pull request keeps its author's credit as a Co-authored-by
  trailer, and its sign-off when there is one.

## Consequences

- `NOTICE`, `CONTRIBUTING.md` (DCO, target branch, header) and the pull request template exist (card 0076).
- Card 0077 rewrites the source headers to the two lines above; until then new files use them already.
- A paid closed-source tier built on community code would need every contributor's permission or a rewrite of their
  code. If that tier becomes likely, the owner moves to option 2 with a new record and a CLA bot; earlier
  contributions stay Apache-2.0.
- A DCO check (a GitHub App or a CI step) can be added later without changing this record.
