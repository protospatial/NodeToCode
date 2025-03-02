// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CCodeLanguage.generated.h"

UENUM()
enum class EN2CCodeLanguage : uint8
{
    Cpp         UMETA(DisplayName = "C++"),
    Python      UMETA(DisplayName = "Python"),
    JavaScript  UMETA(DisplayName = "JavaScript"),
    CSharp      UMETA(DisplayName = "C#"),
    Swift       UMETA(DisplayName = "Swift"),
    Pseudocode  UMETA(DisplayName = "Pseudocode")
};
