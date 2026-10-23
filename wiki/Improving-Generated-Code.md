### Troubleshooting → Improving Generated Code
***

Although Node to Code tries to provide ample output steering via detailed system instructions, sometimes a less capable model may output code that doesn't meet expectations or needs some cleanup. There are several factors to consider and steps you can take to improve results.

## Common Quality Issues

### 1. Inconsistent Coding Style
```cpp
// Poor quality example:
UFUNCTION(BlueprintCallable)
float DoComplexMath(float input_value,FString operation_type,bool should_clamp){
    float result=input_value;if(operation_type=="square"){result*=result;}
    return should_clamp?FMath::Clamp(result,0.f,1.f):result;
}

// Better quality example:
UFUNCTION(BlueprintCallable, Category = "Math|Operations")
float DoComplexMath(const float InputValue, const FString& OperationType, const bool bShouldClamp)
{
    float Result = InputValue;
    
    if (OperationType == TEXT("Square"))
    {
        Result *= Result;
    }
    
    return bShouldClamp ? FMath::Clamp(Result, 0.0f, 1.0f) : Result;
}
```

### 2. Incorrect UE Type Usage
```cpp
// Poor quality example:
FString* StringPtr = new FString("Unsafe");  // Manual memory management
TArray<int> Numbers;  // Missing size hints
FVector Location(0);  // Incomplete initialization

// Better quality example:
FString SafeString = TEXT("Safe");  // Automatic memory management
TArray<int32> Numbers;  // Explicit integer type
FVector Location(0.0f, 0.0f, 0.0f);  // Full initialization
```

### 3. Missing or Incorrect Metadata
```cpp
// Poor quality example:
void ProcessData();  // Missing UFUNCTION
FString PlayerName;  // Missing UPROPERTY

// Better quality example:
UFUNCTION(BlueprintCallable, Category = "Data Processing")
void ProcessData();

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Info")
FString PlayerName;
```

### 4. Irrelevant or "Hallucinated" Code

This problem is typically more common in smaller local models but can also (less commonly) happen with larger, more capable, cloud-based LLMs.

- The context window was completely filled, leaving out critical sections of the serialized blueprint or reference code used for translation. This will leave the LLM to fill in blanks without relevant context.
- There are either too many reference source files added, or they are too large, filling up a **too much** of the context window and making it more difficult for the LLM to accurately retrieve relevant context.
- The model being used is one that is simply not capable enough for this task.

> [!NOTE]
> When a translation is finished, you can see the input and output token usage through the
> [UE Output Log](https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine). Input usage is the most relevant statistic to > keep an eye on.

> [!NOTE]
> It's generally recommended to stay under or within 65% - 85% utilization of an LLM's
> advertised context window to retain the majority of model's capabilities and reduce hallucinations.

> [!NOTE]
> If you're using [Ollama](LLM-Provider-Documentation-&-Resources#ollama-local) for your LLM provider, ensure that you adjust the Context Window
> paramater accordingly in the [Node to Code Plugin settings](Provider-Configuration). It's recommended to keep this value
> at or above 16,000 for up to medium sized blueprint graph translations. As you translate
> larger blueprint graphs, increase [Translation Depth](Translation-Depth), and/or add reference source code files, 
> the context window will need to increase dramatically.

## Understanding the Translation Structure

### Blueprint to C++ Mapping

Node to Code follows specific patterns when translating Blueprint elements:

```cpp
// Blueprint Event Graph typically becomes:
UCLASS()
class MYGAME_API AMyActor : public AActor
{
    GENERATED_BODY()

    // Blueprint variables become properties
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MyCategory")
    float MyBlueprintVariable;

    // Blueprint events become functions
    UFUNCTION(BlueprintImplementableEvent, Category = "MyCategory")
    void MyBlueprintEvent();

    // Blueprint custom events become UFUNCTIONs
    UFUNCTION(BlueprintCallable, Category = "MyCategory")
    void MyCustomEvent();
};
```

### Common Translation Patterns

1. **Event Handling**

```cpp
// Blueprint BeginPlay event becomes:
virtual void BeginPlay() override
{
    Super::BeginPlay();
    // Translated logic here
}

// Blueprint Tick event becomes:
virtual void Tick(float DeltaTime) override
{
    Super::Tick(DeltaTime);
    // Translated logic here
}
```

2. **Variable Access**

```cpp
// Blueprint "Get" node becomes direct property access
float Value = MyVariable;

// Blueprint "Set" node becomes assignment
MyVariable = NewValue;

// Blueprint "Get" with validity check becomes:
if (UObject* Object = MyObjectVariable)
{
    // Safe usage here
}
```

3. **Flow Control**

```cpp
// Blueprint branch node becomes if-else
if (Condition)
{
    // True path
}
else
{
    // False path
}

// Blueprint sequence node becomes sequential statements
FirstFunction();
SecondFunction();
ThirdFunction();

// Blueprint for-each loop becomes range-based for
for (AActor* Actor : TActorRange<AActor>(GetWorld()))
{
    // Loop body
}
```

## Improving Translation Quality

### 1. Switch to a More Capable Model

Check out [Choosing an LLM Provider](Choosing-an-LLM-Provider) for a more detailed breakdown on model strengths.

### 2. Provide Better Context

#### [Reference File Selection](Reference-Source-Files)
```cpp
// Example header to include as reference:
UCLASS()
class MYGAME_API AMyBaseActor : public AActor
{
    GENERATED_BODY()
public:
    // Document your patterns
    UFUNCTION(BlueprintCallable, Category = "MyGame|Utilities")
    void StandardUtilityFunction();
    
    // Show proper property usage
    UPROPERTY(EditDefaultsOnly, Category = "MyGame|Configuration")
    float ConfigValue;
};
```

Key reference file characteristics:
- Clean, well-documented code
- Proper UE patterns and macros
- Relevant to immediate translation context
- Demonstrates project conventions

#### Add Node Comments

- Add comments directly to nodes (eg "IMPORTANT: <notes>") to help steer the output in relation to that node 

### 3. Optimize Blueprint Structure

#### Before Translation:
1. **Clean Up Nodes**
   - Remove unused variables and nodes
   - Delete deprecated nodes
   - Organize node layout
   - Group related functionality

2. **Add Documentation**
   - Document node purpose
   - Explain reasoning for any complex logic
   - Note performance considerations
   - Specify expected behavior

> [!NOTE]
> Node comments can improve translation quality by providing additional
> context to the LLM.

3. **Simplify Complex Operations**
   - Break down large functions
   - Extract reusable logic
   - Use function libraries
   - Implement clear interfaces

## Common Issues and Fixes

### 1. Header File Dependencies

**Problem**: Missing includes or forward declarations.

```cpp
// Generated code with missing dependencies
class UMyComponent;  // Undefined class error
```

**Fix**: Add necessary includes or forward declarations.

```cpp
// In header (.h)
#include "GameFramework/Actor.h"
class UMyComponent;  // Forward declaration for pointers/references

// In source (.cpp)
#include "MyComponent.h"  // Full include for implementation
```

### 2. Macro Corrections

**Problem**: Incorrect or missing UE macros.

```cpp
// Generated code with incorrect macros
UCLASS()
class MyActor : public AActor  // Missing API macro
```

**Fix**: Add appropriate module API and other required macros.

```cpp
UCLASS()
class MYGAME_API AMyActor : public AActor  // Added API macro
{
    GENERATED_BODY()  // Don't forget this!
```

### 3. Blueprint Function Conversion

**Problem**: Incorrect function specifiers or implementation.

```cpp
// Generated code with incorrect function specification
void BlueprintFunction()  // Missing UFUNCTION
```

**Fix**: Add appropriate UFUNCTION macro and implementation.

```cpp
UFUNCTION(BlueprintCallable, Category = "MyFunctions")
void BlueprintFunction()
{
    // Implementation
}
```

### 4. Latent Function Handling

**Problem**: Direct translation of latent Blueprint nodes.

```cpp
// Generated code attempting direct latent operation
void MyLatentOperation()
{
    Delay(2.0f);  // Won't work as expected
}
```

**Fix**: Use proper latent function mechanisms.

```cpp
UFUNCTION(BlueprintCallable, meta = (Latent, LatentInfo = "LatentInfo"))
void MyLatentOperation(FLatentActionInfo LatentInfo)
{
    if (UWorld* World = GetWorld())
    {
        FLatentActionManager& LatentManager = World->GetLatentActionManager();
        FMyLatentAction* Action = new FMyLatentAction(LatentInfo);
        LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, Action);
    }
}
```

### 5. Memory Management

**Problem**: Unsafe pointer handling or memory leaks.

```cpp
// Generated code with potential issues
UObject* NewObject = CreateObject();  // No null check
NewObject->DoSomething();  // Potential crash
```

**Fix**: Add proper checks and use smart pointers where appropriate.

```cpp
if (UObject* NewObject = CreateObject())
{
    // Safe to use
    NewObject->DoSomething();
}
```

## Debugging Strategies

### 1. Compare with Blueprint

> [!NOTE]
> Keep the original Blueprint accessible while debugging the translated code.

Steps for comparison:
1. Set breakpoints in C++ code
2. Enable Blueprint debug visualization
3. Compare execution flow
4. Verify variable values at key points

### 2. Common Debugging Points

```cpp
// Add debug logging at key points
void MyFunction()
{
    UE_LOG(LogTemp, Log, TEXT("Starting MyFunction"));

    // Check variable values
    UE_LOG(LogTemp, Log, TEXT("MyVar = %f"), MyVar);

    // Verify object validity
    if (IsValid(TargetActor))
    {
        UE_LOG(LogTemp, Log, TEXT("TargetActor is valid"));
    }
}
```

### 3. Integration Testing

Steps to verify integration:
1. Test in isolation first
2. Verify Blueprint interface exposure
3. Check performance compared to Blueprint
4. Validate network replication if applicable

## Best Practices

### 1. Documentation

Always document significant changes:

```cpp
/**
 * Modified from Blueprint translation to handle edge cases
 * @param Input - Description of input parameter
 * @return Description of return value
 */
UFUNCTION(BlueprintCallable, Category = "MyFunctions")
float ModifiedFunction(float Input);
```

### 2. Incremental Testing

Testing checklist:
- [X]  Compile successfully
- [X]  Runtime behavior matches Blueprint
- [X]  Edge cases handled
- [X]  Performance impact acceptable
- [X]  No new errors or warnings introduced

> [!NOTE]
> Test each fix individually before moving on to the next issue.

### 3. Common Optimization Points

Look for these opportunities:

```cpp
// Before optimization
for (int32 i = 0; i < MyArray.Num(); ++i)
{
    // Array accessed multiple times
    ProcessItem(MyArray[i]);
}

// After optimization
const int32 Count = MyArray.Num();
for (int32 i = 0; i < Count; ++i)
{
    // Array size cached
    ProcessItem(MyArray[i]);
}
```

### 4. Safety Improvements

Add defensive programming:

```cpp
// Add parameter validation
void ProcessData(const TArray<FString>& Data)
{
    if (Data.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ProcessData called with empty array"));
        return;
    }

    // Process valid data
}
```

Remember that fixing generated code is an iterative process. Take time to understand the translation, verify behavior, and implement improvements systematically. The goal is to maintain the Blueprint's functionality while leveraging C++'s performance and safety features. For a thorough approach to validating your changes, see [Review and Validation](Review-and-Validation).
