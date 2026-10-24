#pragma once

// Example file: Reference context for Node to Code, not included in compilation
// Purpose: Demonstrate to LLM the expected style of splitting events into separate C++ methods

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// Event dispatcher example: Dynamic multicast delegate
// Demonstrates how to declare an event dispatcher
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSomethingHappened, int32, Count);

// Demonstrates event splitting rules (overriding engine events + custom events)
class AN2C_EventStyleGuideActor : public AActor
{
    GENERATED_BODY()

public:
    // Override engine events, must use override and call Super::
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // Custom events should be generated as Blueprint-callable UFUNCTIONs, do not merge multiple events into one function
    UFUNCTION(BlueprintCallable, Category="N2C|Events")
    void MyCustomEvent();

    // Demonstrates event dispatcher declaration and usage
    UPROPERTY(BlueprintAssignable, Category="N2C|Events")
    FOnSomethingHappened OnSomethingHappened;

private:
    // Example data only
    int32 InternalCounter = 0;
};
