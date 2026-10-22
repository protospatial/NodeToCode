// Example file implementation: Reference context for Node to Code, not included in compilation
// Purpose: Demonstrate splitting Blueprint events into separate C++ methods

#include "N2C_EventStyleGuide.h"

// Override engine event BeginPlay, call Super::BeginPlay()
void AN2C_EventStyleGuideActor::BeginPlay()
{
    Super::BeginPlay();

    // Example logic: Trigger event dispatcher once
    InternalCounter = 1;
    OnSomethingHappened.Broadcast(InternalCounter);
}

// Override engine event Tick, call Super::Tick(DeltaTime)
void AN2C_EventStyleGuideActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Example logic: Increment counter
    InternalCounter += 1;
}

// Custom event implementation, maintain one-to-one correspondence with Blueprint "Custom Event"
void AN2C_EventStyleGuideActor::MyCustomEvent()
{
    // Example logic: Broadcast event
    OnSomethingHappened.Broadcast(InternalCounter);
}
