# C++ compile check

How NodeToCode compiles generated C++ and turns the compiler's messages into data (card 0315; review
rec-agentic-landscape-6, arch-arch-core-llm-ui-13, rec-llm-local-compat-providers-11). The compile-repair loop and the
E18 compile-gated benchmark build on it.

## FN2CCppCompileDriver

`Source/Public/LLM/Verify/N2CCppCompileDriver.h`, `Source/Private/LLM/Verify/N2CCppCompileDriver.cpp`.
`Compile(FN2CCppCompileRequest)` returns `FN2CCppCompileResult`:

| Field | Meaning |
|---|---|
| `Backend` | `UbtSingleFile`, `LiveCoding`, or `None` when nothing ran |
| `bSuccess` | Compiled without an error |
| `Error` | Why nothing ran or the build failed without a compiler message: `COMPILE_DISABLED`, `INVALID_ARGUMENT`, `LIVE_CODING_UNAVAILABLE`, `TIMEOUT`, `BUILD_FAILED`, ... |
| `Diagnostics` | `FN2CCompileDiagnostic`: `File` (absolute), `Line`, `Column`, `Code` (`C2440`, `LNK2019`, a clang `-W` flag), `Severity`, `Message` |
| `Output`, `WrittenFiles`, `Seconds` | The raw output, the files written, the time taken |

`ParseDiagnostics(Output)` reads MSVC (`file(line[,col]): error C2440: ...`), clang (`file:line:col: error: ... [-Wflag]`)
and linker (`file : error LNK2019: ...`) lines, also behind a log prefix (`LogLiveCoding: Display: ...`). Notes are left
out and a message printed twice is kept once.

### Off by default

Nothing runs unless the console variable `n2c.Verify.CppCompile` is 1: set it in the console, or in
`Config/DefaultEngine.ini` under `[ConsoleVariables]` (`n2c.Verify.CppCompile=1`). Off, `Compile` returns
`COMPILE_DISABLED`. A project setting would live in `UN2CSettings`; this card adds only the console variable.

### Backend 1: UnrealBuildTool -SingleFile (the request has `Files`)

- The files go into a scratch project of their own, `N2CCompileScratch` (default folder
  `<Project>/Intermediate/NodeToCode/CompileScratch`, or the request's `ScratchRoot`), with one Editor module
  (`ModuleName`, default `N2CEvalScratch`) whose Build.cs lists the request's `Dependencies` (`NoPCHs`, no unity build).
  The module folder is emptied first, so files of an earlier request are not compiled again.
- `Build.bat N2CCompileScratchEditor Win64 Development -Project=... -SingleFile=<each .cpp> -NoMutex -NoHotReload
  -Log=<scratch>/UnrealBuildTool.log` (`Build.sh` on Mac and Linux). With no `.cpp` given, a generated one includes every
  header. One retry when another UnrealBuildTool holds a shared per-user file (an IOException).
- It never builds the running project or any of its modules, and it is not the target of the editor's Live Coding, so
  an open editor with Live Coding does not block it (`build.ps1` refuses to build a host whose editor is open; this does
  not touch the host). The scratch project cannot be the running project or lie in its `Source` folder.
- `-SingleFile` runs UnrealHeaderTool before compiling and compiles without linking: a new `UCLASS` header works
  (measured on 5.6: 7 seconds for a header and a .cpp once the scratch project exists; the first run on a new scratch
  project creates its makefile and takes about 95 seconds). Linker errors therefore never appear on this backend.
- Blocks until UnrealBuildTool ends or `TimeoutSeconds` (600) pass; any thread.

### Backend 2: Live Coding (no `Files`)

`ILiveCodingModule::Compile(WaitForCompletion)` on the running editor's own modules, for code already placed in a project
module (place-cpp-translation). The `LiveCoding` log lines of the compile are captured and parsed. Game thread only;
`LIVE_CODING_UNAVAILABLE` when Live Coding is not enabled for this session (a commandlet, `-NoLiveCoding`, the headless
test editor) or off Windows. `NodeToCode.Build.cs` depends on the `LiveCoding` module on Win64 only. Gate card 8 checks
this backend in an interactive editor; no automated test can.

## FN2CCppScratchModuleVerifier

`Source/Public/LLM/Verify/N2CCppScratchModuleVerifier.h`: the `cpp-scratch-module` adapter of `IN2CCodeVerifier`
(card 0312), for C++ only. `FN2CCppPlacement::BuildPlan` (card 0314) assembles the translation into one UCLASS header
and .cpp in the scratch module (parent from the capture's `parent_class` when it is native, else the Blueprint's native
parent, else `Actor`), the driver compiles them with the modules the placement requires, and each compiler message
becomes a diagnostic: `File` `declaration` (the header) or `implementation` (the .cpp), the line and column in the
assembled file, `COMPILE_ERROR` or `COMPILE_WARNING`, and a message with the file name, the compiler's code and text.
Off or unable to run: `bRan` false and one `VERIFIER_UNAVAILABLE` warning. `LastResult` keeps the compile.

## Tests

`NodeToCode.Translation.CppCompile` (`Source/Private/Tests/LLM/N2CCppCompileDriver.spec.cpp`): the parser on MSVC,
clang, linker and Live Coding lines; the switch; Live Coding refused headless; a path leaving the module refused; on the
UnrealBuildTool backend, a known error with its file, line and code and a valid UCLASS that compiles; the verifier on a
valid and a broken translation. Its scratch project (`Intermediate/NodeToCode/CompileScratchSpec` of the host) is kept
between runs.

## Still to do

The compile-gated benchmark script (planned under Benchmarks/compile_gate/, not yet in the repo; `python -I`, writes the files into the hosts' scratch
module and parses the build into `{success, errors[], warnings[], seconds}`), comes after card 0364's corpus.
