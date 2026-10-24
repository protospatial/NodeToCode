// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "N2CTestGraphNode.generated.h"

UCLASS(Transient, NotBlueprintable)
class UN2CTestGraphNode : public UK2Node
{
    GENERATED_BODY()

public:
    FName TestTitle;

    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
    {
        return FText::FromName(TestTitle.IsNone() ? GetFName() : TestTitle);
    }
};