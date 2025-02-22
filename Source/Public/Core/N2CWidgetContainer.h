// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "N2CWidgetContainer.generated.h"

/**
 * Persistent container for Node to Code editor widgets
 * Lives for the duration of the editor module
 */
UCLASS()
class UN2CWidgetContainer : public UObject
{
    GENERATED_BODY()

public:
    static UN2CWidgetContainer* Get();

    /** Reset the singleton instance */
    static void Reset();
    
    /** Override GetWorld to return the editor world context */
    virtual UWorld* GetWorld() const override;

    /** Called before destruction */
    virtual void BeginDestroy() override;
    
private:
    static UN2CWidgetContainer* Instance;
};
