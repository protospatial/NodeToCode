# 1.2.x to 2.0 migration check

Proves a NodeToCode 1.2.x project's settings and saved files load in a 2.0 build, row by row against
`Context/release/migration-1.2-to-2.0.md` (card 0079). Stdlib only; nothing here ships with the plugin
(BuildPlugin leaves `/Tests` out).

```bash
python -I Tests/Migration/migration_12x_check.py --engine G:/UE/UE_5.8 \
  --host "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8" --label 5.8 [--live] [--keep]
```

`--host` is a built host project folder (build it first, for example `.\build.ps1 -UEVersion 5.8`). The check never
changes it: for each fixture it copies the host's `.uproject`, `Config/`, `Binaries/` and the plugin's `.uplugin`,
`Binaries/`, `Content/`, `Resources/` and `ThirdParty/` (copies, not junctions) to a temp folder, puts the fixture
in, and starts a headless editor (`UnrealEditor-Cmd -ExecutePythonScript=migration_dump.py`) twice. The 1.2.3
fixture goes first, then 1.2.x-main as a second project sharing the per-user secrets file (K2); a last start of
that project with only `ANTHROPIC_API_KEY=<dummy>` in the editor's environment checks K3. It prints one line per
matrix row and exits non-zero on any FAIL. Report and editor logs: `logs/` (gitignored).

- **PASS / FAIL**: the row behaves as the matrix says, or not.
- **KNOWN**: the row differs because of a defect an open card owns (S24 and S25, card 0186: the settings
  constructor reads the reference files before the config is loaded). It turns into PASS once the defect is fixed
  and into FAIL if the behaviour changes some other way. Update `KNOWN_NOTE` when the card lands.
- **SKIP**: the row has nothing to check in that fixture, or needs `--live`.
- **Idempotence**: the second start must leave both ini files, the loaded values and the graph state unchanged and
  log no NodeToCode warning or engine import error. Running the whole check again gives the same result.
- `--live` adds S26 and F2: on the migrated project it makes an in-memory probe Blueprint, picks Claude Haiku 5.5,
  tags the graph and runs a batch translation with the key from the repo's `.env` in the editor's environment only
  (`Tests/mcp_probe/env_keys.py`), then checks the output landed in the migrated custom output directory, its N2C
  JSON says `1.0.0`, and no key value is in the output or the log. (`translate-focused-blueprint` saves no
  folder, so it cannot show S26.)
- `--keep` keeps the scratch projects for inspection.

Machine state it touches, restored in a `finally` block: the per-user secrets folder
(`%LOCALAPPDATA%/NodeToCode`) is moved aside for the run and put back, so the dummy keys never mix with real ones;
the engine's per-user `EditorSettings.ini` (`%LOCALAPPDATA%/UnrealEngine/<ver>/Saved/Config/WindowsEditor`) is set
to `bThrottleCPUWhenNotForeground=False` as 1.2.x left it (F8) and written back byte for byte. After a hard kill,
check those two places.

## Fixtures

| Folder | From | Holds |
|---|---|---|
| `1.2.3/` | v1.2.3 tag (c7aa4c0) | `Config/DefaultNodeToCode.ini`, `Config/DefaultEngine.HTTP.ini` (appended to the copy's `DefaultEngine.ini`), `Saved/NodeToCode/User/secrets.json`, a 1.2.3 translation folder, `N2CReference/DoorHelpers.h` |
| `1.2.x-main/` | origin/main (cb1483a, the 1.2.x line) | The same without a translation folder, plus the LM Studio keys |
| `prerelease/` | dev-2.0 builds from 2025-06-21 (not 1.2.x) | `Saved/NodeToCode/Tags/BlueprintTags.json`, added to the 1.2.3 run for F4 |

`{PROJECT_DIR}` in the ini files is replaced with the scratch project's folder. The secrets files hold obvious
dummies (`test-<provider>-key-0000`, `test-<provider>-key-main`); never put a real key in a fixture.

What 1.2.x wrote itself: `make_fixture.py`, run inside a v1.2.3 or origin/main editor built for UE 5.6 on a scratch
host (`git archive v1.2.3` into its `Plugins/NodeToCode`, PythonScriptPlugin enabled; both build on 5.6 as they are),
sets every setting with `set_editor_property(..., ALWAYS)`, which calls 1.2.x's `PostEditChangeProperty` and its
per-key ini save, and the four API key fields, which 1.2.x saves through `UN2CUserSecrets`. The `[HTTP]` keys are
what 1.2.x's module startup wrote. Then the absolute scratch paths were replaced with `{PROJECT_DIR}`.

Added by hand, following 1.2.x's code:

- `EstimatedReferenceTokens=999` and the two `+ReferenceSourceFilePaths` lines (one to a missing file): 1.2.x skips
  its own save when the array changes, so these keys reach the ini only through a whole-section save.
- The `1.2.3/Saved/NodeToCode/Translations/BP_Door_<stamp>/` folder, laid out as 1.2.3's
  `UN2CLLMModule::SaveTranslationToDisk` writes it: 1.2.3 asks for retired model IDs, so it could not run one.
- In `1.2.x-main/Config/DefaultEngine.HTTP.ini`, `HttpConnectionTimeout=120` and `HttpTimeout=180` stand for values
  a user set (F7 must keep them); 1.2.x itself writes 300 and 3600.

To add a fixture, make a folder in the same shape, add its expected values to `FIXTURES` in
`migration_12x_check.py`, and add it to `--fixtures`.
