// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

/**
 * @file N2CLogging.h
 * @brief Logging and error-related data type definitions for the Node to Code plugin
 */

#pragma once

#include "CoreMinimal.h"
#include "N2CLogging.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNodeToCode, Log, All);

/** Severity levels for logging */
UENUM(BlueprintType)
enum class EN2CLogSeverity : uint8
{
    Debug    UMETA(DisplayName = "Debug"),
    Info     UMETA(DisplayName = "Info"),
    Warning  UMETA(DisplayName = "Warning"),
    Error    UMETA(DisplayName = "Error"),
    Fatal    UMETA(DisplayName = "Fatal")
};

/** Categories of parsing errors */
UENUM(BlueprintType)
enum class EN2CErrorCategory : uint8
{
    Syntax      UMETA(DisplayName = "Syntax"),
    Reference   UMETA(DisplayName = "Reference"),
    Type        UMETA(DisplayName = "Type"),
    Connection  UMETA(DisplayName = "Connection"),
    Validation  UMETA(DisplayName = "Validation"),
    Internal    UMETA(DisplayName = "Internal")
};

/** Structured error information */
USTRUCT(BlueprintType)
struct FN2CError
{
    GENERATED_BODY()

    /** Error message */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Message;

    /** Error severity */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CLogSeverity Severity;

    /** Error category */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CErrorCategory Category;

    /** Optional node name for context */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NodeName;

    /** Optional additional context */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Context;

    /** Timestamp when error occurred */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FDateTime Timestamp;

    FN2CError()
        : Severity(EN2CLogSeverity::Error)
        , Category(EN2CErrorCategory::Internal)
        , Timestamp(FDateTime::Now())
    {
    }
};
