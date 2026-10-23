// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Forward declaration with proper underlying type
enum class EN2CCodeLanguage : uint8;

/**
 * Utility class for common path operations in NodeToCode
 */
class NODETOCODE_API FN2CPathUtils
{
public:
    /**
     * Get the base path for translations output
     * Checks settings for custom path, falls back to default
     * @return The absolute path to the translations directory
     */
    static FString GetTranslationsBasePath();
    
    /**
     * Validate that a path stays within a base directory (prevents traversal attacks)
     * @param BasePath The base directory that should contain the path
     * @param PathToValidate The path to validate (will be normalized)
     * @param OutNormalizedPath The normalized absolute path if validation succeeds
     * @return True if the path is valid and within bounds
     */
    static bool ValidatePathWithinBounds(const FString& BasePath, const FString& PathToValidate, FString& OutNormalizedPath);
    
    /**
     * Get the appropriate file extension for a code language
     * @param Language The target code language
     * @return The file extension including the dot (e.g., ".cpp")
     */
    static FString GetFileExtensionForLanguage(EN2CCodeLanguage Language);

    /**
     * True for paths MCP clients must never read or list because they hold credentials:
     * <ProjectSaved>/NodeToCode/User (the secrets store), any secrets.json, <Project>/.git, and .env files.
     * @param AbsolutePath A normalized absolute path
     */
    static bool IsDeniedForAgentAccess(const FString& AbsolutePath);
};