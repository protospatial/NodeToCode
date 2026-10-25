// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CPathUtils.h"
#include "Core/N2CSettings.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <limits.h>
#include <stdlib.h>
#endif

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
    
    // Lexical check first, then the same check after resolving junctions and symlinks, so a link inside the
    // base cannot point the caller at another tree on disk.
    if (!OutNormalizedPath.StartsWith(NormalizedBasePath))
    {
        return false;
    }
    if (!IsUnderDirectoryResolved(OutNormalizedPath, NormalizedBasePath))
    {
        return false;
    }
    OutNormalizedPath = ResolveRealPath(OutNormalizedPath);
    return true;
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

TArray<FString> FN2CPathUtils::GetTranslationFileExtensions(EN2CCodeLanguage Language)
{
    TArray<FString> Extensions;
    Extensions.Add(GetFileExtensionForLanguage(Language));
    if (Language == EN2CCodeLanguage::Pseudocode)
    {
        Extensions.AddUnique(TEXT(".txt"));
    }
    return Extensions;
}

FString FN2CPathUtils::GetLanguageId(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return TEXT("Cpp");
        case EN2CCodeLanguage::Python:
            return TEXT("Python");
        case EN2CCodeLanguage::JavaScript:
            return TEXT("JavaScript");
        case EN2CCodeLanguage::CSharp:
            return TEXT("CSharp");
        case EN2CCodeLanguage::Swift:
            return TEXT("Swift");
        case EN2CCodeLanguage::Pseudocode:
            return TEXT("Pseudocode");
        default:
            return TEXT("Cpp");
    }
}

bool FN2CPathUtils::ParseLanguageId(const FString& Id, EN2CCodeLanguage& OutLanguage)
{
    OutLanguage = EN2CCodeLanguage::Cpp;
    FString Name = Id.TrimStartAndEnd();
    int32 Separator = INDEX_NONE;
    if (Name.FindLastChar(TEXT(':'), Separator))
    {
        Name.RightChopInline(Separator + 1); // EN2CCodeLanguage::Python -> Python
    }
    if (Name.IsEmpty())
    {
        return false;
    }

    static const EN2CCodeLanguage All[] = { EN2CCodeLanguage::Cpp, EN2CCodeLanguage::Python, EN2CCodeLanguage::JavaScript,
        EN2CCodeLanguage::CSharp, EN2CCodeLanguage::Swift, EN2CCodeLanguage::Pseudocode };
    for (EN2CCodeLanguage Language : All)
    {
        if (Name.Equals(GetLanguageId(Language), ESearchCase::IgnoreCase))
        {
            OutLanguage = Language;
            return true;
        }
    }
    if (Name.Equals(TEXT("C++"), ESearchCase::IgnoreCase))
    {
        OutLanguage = EN2CCodeLanguage::Cpp;
        return true;
    }
    if (Name.Equals(TEXT("C#"), ESearchCase::IgnoreCase))
    {
        OutLanguage = EN2CCodeLanguage::CSharp;
        return true;
    }
    return false;
}

FString FN2CPathUtils::MakeTranslationRecordPath(const FString& GraphDirectory)
{
    FString Full = FPaths::ConvertRelativePathToFull(GraphDirectory);
    FPaths::NormalizeDirectoryName(Full);
    FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(ProjectDir);
    if (FPaths::IsUnderDirectory(Full, ProjectDir) && !Full.Equals(ProjectDir, ESearchCase::IgnoreCase))
    {
        FString Relative = Full;
        if (FPaths::MakePathRelativeTo(Relative, *(ProjectDir + TEXT("/"))))
        {
            return Relative;
        }
    }
    return Full;
}

FString FN2CPathUtils::ResolveTranslationRecordPath(const FString& RecordPath)
{
    if (RecordPath.IsEmpty() || FPaths::IsRelative(RecordPath))
    {
        return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), RecordPath));
    }
    return RecordPath;
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
        IsAtOrUnder(FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("NodeToCode"))) ||
        IsAtOrUnder(FPaths::Combine(FPaths::ProjectDir(), TEXT(".git"))))
    {
        return true;
    }

    const FString FileName = FPaths::GetCleanFilename(Path);
    // secrets.json and its .tmp, .bak and .corrupt-<timestamp> siblings
    return FileName.StartsWith(TEXT("secrets.json"), ESearchCase::IgnoreCase)
        || FileName.Equals(TEXT(".env"), ESearchCase::IgnoreCase)
        || FileName.EndsWith(TEXT(".env"), ESearchCase::IgnoreCase);
}

FString FN2CPathUtils::MakeSafeFileStem(const FString& Name)
{
    FString Stem;
    Stem.Reserve(Name.Len() + 1);
    for (TCHAR C : Name)
    {
        const bool bSafe = (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z'))
            || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_');
        Stem.AppendChar(bSafe ? C : TEXT('_'));
    }
    if (Stem.Len() > 0 && Stem[0] >= TEXT('0') && Stem[0] <= TEXT('9'))
    {
        Stem.InsertAt(0, TEXT('_'));
    }
    return Stem.Left(64);
}

FString FN2CPathUtils::GetTranslationFileStem(const FString& Name)
{
    const FString Trimmed = Name.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return FString();
    }
    bool bPlain = Trimmed.Len() <= 128 && !Trimmed.Contains(TEXT("..")) && Trimmed != TEXT(".");
    for (TCHAR C : Trimmed)
    {
        if (C < 32 || C == TEXT('/') || C == TEXT('\\') || C == TEXT(':') || C == TEXT('*') || C == TEXT('?')
            || C == TEXT('"') || C == TEXT('<') || C == TEXT('>') || C == TEXT('|'))
        {
            bPlain = false;
            break;
        }
    }
    return bPlain ? Trimmed : MakeSafeFileStem(Trimmed);
}

namespace
{
    /** Resolve an existing path through links; returns false when the OS cannot open it. */
    bool ResolveExisting(const FString& Path, FString& OutResolved)
    {
#if PLATFORM_WINDOWS
        HANDLE Handle = ::CreateFileW(*Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (Handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        TCHAR Buffer[4096];
        const DWORD Len = ::GetFinalPathNameByHandleW(Handle, Buffer, UE_ARRAY_COUNT(Buffer), FILE_NAME_NORMALIZED);
        ::CloseHandle(Handle);
        if (Len == 0 || Len >= UE_ARRAY_COUNT(Buffer))
        {
            return false;
        }
        FString Result(Buffer);
        if (Result.StartsWith(TEXT("\\\\?\\UNC\\")))
        {
            Result = TEXT("\\\\") + Result.Mid(8);
        }
        else if (Result.StartsWith(TEXT("\\\\?\\")))
        {
            Result = Result.Mid(4);
        }
        FPaths::NormalizeFilename(Result);
        OutResolved = Result;
        return true;
#else
        char Buffer[PATH_MAX];
        if (!realpath(TCHAR_TO_UTF8(*Path), Buffer))
        {
            return false;
        }
        OutResolved = UTF8_TO_TCHAR(Buffer);
        return true;
#endif
    }
}

FString FN2CPathUtils::ResolveRealPath(const FString& Path)
{
    FString Full = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Full);
    FPaths::CollapseRelativeDirectories(Full);
    while (Full.EndsWith(TEXT("/")) && Full.Len() > 3)
    {
        Full.LeftChopInline(1);
    }

    // Walk up to the deepest ancestor that exists, resolve it, and re-append the rest.
    FString Existing = Full;
    FString Remainder;
    while (!Existing.IsEmpty())
    {
        FString Resolved;
        if (ResolveExisting(Existing, Resolved))
        {
            return Remainder.IsEmpty() ? Resolved : Resolved / Remainder;
        }
        int32 Slash = INDEX_NONE;
        if (!Existing.FindLastChar(TEXT('/'), Slash) || Slash <= 0)
        {
            break;
        }
        const FString Leaf = Existing.Mid(Slash + 1);
        Remainder = Remainder.IsEmpty() ? Leaf : Leaf / Remainder;
        Existing = Existing.Left(Slash);
    }
    return Full;
}

bool FN2CPathUtils::IsUnderDirectoryResolved(const FString& Path, const FString& Root)
{
    const FString ResolvedPath = ResolveRealPath(Path);
    const FString ResolvedRoot = ResolveRealPath(Root);
    return ResolvedPath.Equals(ResolvedRoot, ESearchCase::IgnoreCase) || FPaths::IsUnderDirectory(ResolvedPath, ResolvedRoot);
}
