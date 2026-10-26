// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "Models/N2CLogging.h"
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
        N2C_LOG_FUNCTION_ENTRY();
        return FText::FromName(TestTitle.IsNone() ? GetFName() : TestTitle);
    }
};

UCLASS(Transient, NotBlueprintable)
class UN2CTestNativeGraphNode : public UEdGraphNode
{
    GENERATED_BODY()

public:
    FName TestTitle;

    UPROPERTY()
    int32 TestValue = 0;

    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override
    {
        N2C_LOG_FUNCTION_ENTRY();
        return FText::FromName(TestTitle.IsNone() ? GetFName() : TestTitle);
    }
};
