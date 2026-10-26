# Translating N2C JSON to Unreal C++

Distilled from NodeToCode's own C++ translation prompt (`Content/Prompting/CodeGen_CPP.md` in the plugin), with the
engine conventions an agent writing into a real project needs on top.

## Graphs

| `graph_type` | Becomes |
|---|---|
| `Function` | A member function. `FunctionEntry` output pins are parameters; `FunctionResult` input pins are the return value (one) or out parameters (`Type& Name`, several) |
| `EventGraph` | One function or override per event that its exec wires reach; graphs it calls are called as functions |
| `Macro` | A private helper function; its inputs and outputs become parameters and return values |
| `Composite` (collapsed graph) | Inlined where it is used, or a private helper |
| `Construction` | `OnConstruction(const FTransform& Transform)`; note that construction scripts often rely on editor-only context |
| `structs` entry | `USTRUCT(BlueprintType)` with `UPROPERTY(EditAnywhere, BlueprintReadWrite)` members and their defaults |
| `enums` entry | `UENUM(BlueprintType)` `enum class E... : uint8` with `UMETA(DisplayName = ...)` where names differ |

Translate every graph you were given, not only the first.

## Events

- Engine events are overrides with the engine's C++ name: `ReceiveBeginPlay` is `virtual void BeginPlay() override`
  (call `Super::BeginPlay()` first), `ReceiveTick` is `Tick(float DeltaSeconds)` (set
  `PrimaryActorTick.bCanEverTick = true` in the constructor), `ReceiveEndPlay` is `EndPlay(const EEndPlayReason::Type)`,
  `ReceiveActorBeginOverlap` is `NotifyActorBeginOverlap(AActor*)`, `ReceiveAnyDamage` is handled by overriding
  `TakeDamage` or binding `OnTakeAnyDamage`. Check the parent class's header for the exact signature.
- A custom event is a `UFUNCTION(BlueprintCallable)` member (add `Server`/`Client`/`NetMulticast, Reliable` when the
  node says it replicates).
- Input action events become bindings in `SetupPlayerInputComponent` (Enhanced Input when the project uses it).

## Nodes

- `CallFunction`: call `member_parent::member_name` natively. Static library calls stay static
  (`UKismetSystemLibrary::PrintString(this, ...)`, `UGameplayStatics::GetPlayerPawn(this, 0)`); the `self` pin is the
  object the member function is called on (`this` when unwired).
- K2 wrappers are not callable from C++. Use the native function: `K2_SetActorLocation` is `SetActorLocation`,
  `K2_GetActorLocation` is `GetActorLocation`, `K2_DestroyActor` is `Destroy`, `K2_AttachToComponent` is
  `AttachToComponent`. Prefer operators and `FMath` over `UKismetMathLibrary` wrappers (`Less_DoubleDouble` is `<`,
  `Add_VectorVector` is `+`, `FClamp` is `FMath::Clamp`).
- `VariableGet` / `VariableSet`: read or assign the member (or local). A member of this Blueprint becomes a
  `UPROPERTY`; pick specifiers from how the Blueprint uses it (`EditAnywhere, BlueprintReadWrite, Category = ...`).
- `DynamicCast`: `Cast<AType>(Object)` with an `if` for the success and failure exec pins.
- `MakeStruct` / `BreakStruct`: construct the struct or read its members.
- Literal `default_value`s on unwired inputs become literal arguments. Use `sub_type` and `member_parent` for C++
  types (`TObjectPtr<AActor>`, `TSubclassOf<...>`, `TArray<...>` for `is_array` pins, `TSet`, `TMap`).
- `latent` nodes finish later: `Delay` becomes a timer (`GetWorldTimerManager().SetTimer(Handle, this,
  &AMyActor::Continue, Seconds, false)`) with the code after it moved into the callback; other async actions need
  their delegate or a latent action. Explain the timing in a comment and in the notes.
- `Timeline`: a `UTimelineComponent` or `FTimeline` with curve assets the user keeps in a Blueprint child; describe
  what is needed.
- Any other node: express what it does with native APIs and say what you chose.

## Flow

The C++ must follow the exec wires exactly; do not simplify, merge or reorder steps.

| Node | C++ |
|---|---|
| Branch | `if (Condition) { then chain } else { else chain }` |
| Sequence | `then_0` chain, then `then_1` chain, in pin order |
| DoOnce | a `bool` member that guards the chain; Reset clears it |
| ForLoop | `for (int32 i = First; i <= Last; ++i)` (Last is inclusive) |
| ForEachLoop | range-based `for`, or an index loop when Array Index is used; `Completed` runs after the loop |
| WhileLoop | `while (Condition)` |
| Gate | a `bool` member: Open and Close set it, Enter runs the chain only while it is true |
| Switch on int, enum, string, name | `switch` for ints and enums, an `if`/`else if` chain for strings and names; Default is `default`/`else` |
| FlipFlop | a `bool` member toggled each run, choosing A or B |

Pure nodes become expressions evaluated where a running node reads them. A pure node read by several running nodes
is evaluated at each read, as in Blueprint; keep it in a local only when it has no side effects and nothing changes in
between.

## What to leave out

Nodes marked `unused`, `unreachable` or `disabled` never run: do not translate them or any value they compute, and
list each (its `member_name` or `title`) in the notes. A data input fed only by a disabled node uses its
`default_value`. Never add the editor's placeholder events.

## Names and output

- Use `member_name` as declared. Use the class name the user's own files give; otherwise the agreed name from
  `metadata.blueprint_class` with the Unreal prefix.
- Expose functions with `UFUNCTION(BlueprintCallable, Category = ...)` where a Blueprint child or designer would call
  them; `BlueprintPure` for side-effect-free getters; `BlueprintImplementableEvent` or `BlueprintNativeEvent` for
  hooks that stay in Blueprint.
- Give each declaration a short doxygen comment.
- Notes: includes and module dependencies the code needs, members it assumes, nodes left out, and behaviour that
  differs.
