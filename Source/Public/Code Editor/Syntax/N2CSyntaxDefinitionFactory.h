// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"

class FN2CSyntaxDefinition;

class FN2CSyntaxDefinitionFactory
{
public:
    /** Get the singleton instance */
    static FN2CSyntaxDefinitionFactory& Get();

    /** Create a syntax definition for the specified language */
    TSharedPtr<FN2CSyntaxDefinition> CreateDefinition(EN2CCodeLanguage Language);

private:
    FN2CSyntaxDefinitionFactory() = default;
};
