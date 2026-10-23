// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CPathUtils.h"
#include "Core/N2CSettings.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Misc/Paths.h"

FString FN2CPathUtils::GetTranslationsBasePath()
{
    // Check if custom output directory is set in settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings && !Settings->CustomTranslationOutputDirectory.Path.IsEmpty())
    {
        // Use custom path if specified
        return Settings->CustomTranslationOutputDirectory.Path;
    }
    
    // Use default path
    return FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations");
}

bool FN2CPathUtils::ValidatePathWithinBounds(const FString& BasePath, const FString& PathToValidate, FString& OutNormalizedPath)
{
    // Convert base path to absolute
    FString NormalizedBasePath = FPaths::ConvertRelativePathToFull(BasePath);
    
    // Start with the path to validate
    OutNormalizedPath = PathToValidate;
    
    // Collapse relative directories (resolve .. and .) BEFORE validation
    FPaths::CollapseRelativeDirectories(OutNormalizedPath);
    
    // Convert to absolute path for final validation
    OutNormalizedPath = FPaths::ConvertRelativePathToFull(OutNormalizedPath);
    
    // Security check: Ensure the resolved path is still within the base path
    return OutNormalizedPath.StartsWith(NormalizedBasePath);
}

FString FN2CPathUtils::GetFileExtensionForLanguage(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return TEXT(".cpp");
        case EN2CCodeLanguage::Python:
            return TEXT(".py");
        case EN2CCodeLanguage::JavaScript:
            return TEXT(".js");
        case EN2CCodeLanguage::CSharp:
            return TEXT(".cs");
        case EN2CCodeLanguage::Swift:
            return TEXT(".swift");
        case EN2CCodeLanguage::Pseudocode:
            return TEXT(".md");
        default:
            return TEXT(".txt");
    }
}


bool FN2CPathUtils::IsDeniedForAgentAccess(const FString& AbsolutePath)
{
    FString Path = FPaths::ConvertRelativePathToFull(AbsolutePath);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);

    auto IsAtOrUnder = [&Path](FString Dir) -> bool
    {
        Dir = FPaths::ConvertRelativePathToFull(Dir);
        FPaths::NormalizeDirectoryName(Dir);
        return Path.Equals(Dir, ESearchCase::IgnoreCase) || FPaths::IsUnderDirectory(Path, Dir);
    };

    if (IsAtOrUnder(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("User"))) ||
        IsAtOrUnder(FPaths::Combine(FPaths::ProjectDir(), TEXT(".git"))))
    {
        return true;
    }

    const FString FileName = FPaths::GetCleanFilename(Path);
    return FileName.Equals(TEXT("secrets.json"), ESearchCase::IgnoreCase)
        || FileName.Equals(TEXT(".env"), ESearchCase::IgnoreCase)
        || FileName.EndsWith(TEXT(".env"), ESearchCase::IgnoreCase);
}
