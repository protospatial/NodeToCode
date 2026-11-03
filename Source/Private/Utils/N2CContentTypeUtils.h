// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Utility class for content type detection and file type operations
 */
class FN2CContentTypeUtils
{
public:
    /**
     * Gets the MIME content type for a given file extension
     * @param Extension The file extension (without dot)
     * @return MIME type string (e.g., "text/plain", "application/json")
     */
    static FString GetContentTypeFromExtension(const FString& Extension);
    
    /**
     * Gets a human-readable description of the file type
     * @param Extension The file extension (without dot)
     * @return Description string (e.g., "C++ Source File", "JSON Data")
     */
    static FString GetFileTypeDescription(const FString& Extension);
};