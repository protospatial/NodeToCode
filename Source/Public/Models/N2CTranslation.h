// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CTranslation.generated.h"


/**
 * @struct FN2CTranslationUsage
 * @brief Tracks the token usage for LLM requests (input vs. output tokens).
 * Used to monitor or bill usage after a request completes.
 */
USTRUCT(BlueprintType)
struct FN2CTranslationUsage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 OutputTokens;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 InputTokens;
    
};
/**
 * @struct FN2CGeneratedCode
 * @brief Container for the generated code and implementation notes for a blueprint graph
 */
USTRUCT(BlueprintType)
struct FN2CGeneratedCode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphDeclaration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphImplementation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ImplementationNotes;
};

/**
 * @struct FN2CGraphTranslation
 * @brief Represents a single graph's translation from Blueprint to C++
 */
USTRUCT(BlueprintType)
struct FN2CGraphTranslation
{
    GENERATED_BODY()

    // Graph identification
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphClass;

    /** Code generation output */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CGeneratedCode Code;
};

/**
 * @struct FN2CTranslationResponse
 * @brief Container for all translated graphs from an LLM response
 */
USTRUCT(BlueprintType)
struct FN2CTranslationResponse
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CGraphTranslation> Graphs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CTranslationUsage Usage;
};


