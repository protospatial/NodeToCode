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
     * Every extension a saved implementation file of this language may have, the one GetFileExtensionForLanguage
     * writes first. Older 2.0 builds' loader looked for Pseudocode as .txt, so .txt follows .md for Pseudocode.
     */
    static TArray<FString> GetTranslationFileExtensions(EN2CCodeLanguage Language);

    /**
     * The language id translation records store (the enum's short name: Cpp, Python, JavaScript, CSharp, Swift,
     * Pseudocode). Every writer of a translation record uses it, and ParseLanguageId reads it back.
     */
    static FString GetLanguageId(EN2CCodeLanguage Language);

    /**
     * Read a stored language id, ignoring case: the short name GetLanguageId writes, the enum-qualified form older
     * batch translations stored (EN2CCodeLanguage::Python), the display name (C++, C#) and CPP, which the main
     * window stored for every language. False for an empty or unknown id (OutLanguage is then Cpp).
     */
    static bool ParseLanguageId(const FString& Id, EN2CCodeLanguage& OutLanguage);

    /**
     * The output path a translation record stores for a graph folder: relative to the project folder when the folder
     * is inside it, else the absolute path (a custom output directory can be anywhere).
     */
    static FString MakeTranslationRecordPath(const FString& GraphDirectory);

    /** The absolute graph folder for a stored output path: relative paths are under the project folder. */
    static FString ResolveTranslationRecordPath(const FString& RecordPath);

    /**
     * True for paths MCP clients must never read or list because they hold credentials:
     * <ProjectSaved>/NodeToCode/User (where 1.x kept secrets), <UserSettingsDir>/NodeToCode (the per-user secret
     * store, FN2CSecretStore), any secrets.json* file, <Project>/.git, and .env files.
     * @param AbsolutePath A normalized absolute path
     */
    static bool IsDeniedForAgentAccess(const FString& AbsolutePath);

    /**
     * Map a name to a safe file or folder stem: every character outside [A-Za-z0-9_] becomes '_', a leading
     * digit gets a '_' prefix, the result is capped at 64 characters. Empty input gives an empty stem.
     */
    static FString MakeSafeFileStem(const FString& Name);

    /**
     * The file stem translations use for a graph or Blueprint name. A name with no path separators, "..",
     * drive colon or control characters is kept as-is (so translations saved by older versions still load);
     * anything else goes through MakeSafeFileStem. Names can come from an LLM response, so never use them raw.
     */
    static FString GetTranslationFileStem(const FString& Name);

    /** Resolve symlinks and junctions. For a path that does not exist yet, resolves its deepest existing ancestor. */
    static FString ResolveRealPath(const FString& Path);

    /** True when Path, after resolving links, is Root itself or inside it. */
    static bool IsUnderDirectoryResolved(const FString& Path, const FString& Root);
};