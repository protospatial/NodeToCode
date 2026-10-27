// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CPathUtils.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
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
    // The custom output directory from the settings: a relative one is under the project folder (card 0252)
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const FString Custom = Settings ? Settings->CustomTranslationOutputDirectory.Path.TrimStartAndEnd() : FString();
    if (!Custom.IsEmpty())
    {
        FString Path = FPaths::IsRelative(Custom)
            ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Custom)
            : FPaths::ConvertRelativePathToFull(Custom);
        FPaths::NormalizeDirectoryName(Path);
        return Path;
    }
    return GetDefaultTranslationsBasePath();
}

FString FN2CPathUtils::GetDefaultTranslationsBasePath()
{
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations"));
    FPaths::NormalizeDirectoryName(Path);
    return Path;
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
    
    // A .. that could not be collapsed (it climbs above the drive root) is never inside the base.
    if (OutNormalizedPath.Contains(TEXT("/../")) || OutNormalizedPath.EndsWith(TEXT("/..")) || OutNormalizedPath.StartsWith(TEXT("../")))
    {
        return false;
    }

    // Lexical check first, then the same check after resolving junctions and symlinks, so a link inside the
    // base cannot point the caller at another tree on disk. IsUnderDirectory compares on a separator boundary and
    // ignores a trailing slash on either side, so "<Base>/." (what "." collapses to) is the base itself and
    // "<Base>Evil" is not inside it.
    if (!FPaths::IsUnderDirectory(OutNormalizedPath, NormalizedBasePath))
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
    return N2C::GetLanguage(Language).ImplementationExtension;
}

TArray<FString> FN2CPathUtils::GetTranslationFileExtensions(EN2CCodeLanguage Language)
{
    return N2C::GetLanguage(Language).GetImplementationExtensions();
}

FString FN2CPathUtils::GetLanguageId(EN2CCodeLanguage Language)
{
    return N2C::GetLanguage(Language).Id;
}

bool FN2CPathUtils::ParseLanguageId(const FString& Id, EN2CCodeLanguage& OutLanguage)
{
    const FN2CTargetLanguage* Row = N2C::FindLanguage(Id);
    OutLanguage = Row ? Row->Enum : EN2CCodeLanguage::Cpp;
    return Row != nullptr;
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

namespace
{
    /** A drive letter (C:/, C:) or a UNC or device path (//server, \?\): absolute on every platform. */
    bool HasDriveOrUncPrefix(const FString& Path)
    {
        return (Path.Len() >= 2 && Path[1] == TEXT(':')) || Path.StartsWith(TEXT("//"));
    }

    FString FullDirectory(const FString& Dir)
    {
        FString Full = FPaths::ConvertRelativePathToFull(Dir);
        FPaths::NormalizeDirectoryName(Full);
        FPaths::CollapseRelativeDirectories(Full);
        return Full;
    }

    bool IsAtOrUnder(const FString& Path, const FString& Dir)
    {
        return FPaths::IsUnderDirectory(Path, Dir);
    }
}

TArray<FString> FN2CPathUtils::GetAgentReadRoots()
{
    TArray<FString> Roots;
    const FString Project = FullDirectory(FPaths::ProjectDir());
    Roots.Add(Project);

    const FString Translations = FullDirectory(GetTranslationsBasePath());
    if (Translations.IsEmpty() || IsAtOrUnder(Translations, Project) || IsUnderDirectoryResolved(Translations, Project))
    {
        return Roots; // the project root already covers it
    }

    // A drive or file-system root, or a folder that holds the project or the user's home, would open far more than
    // translations: leave it out (the editor still writes translations there).
    const FString Resolved = ResolveRealPath(Translations);
    const bool bFileSystemRoot = FPaths::IsDrive(Translations) || Translations.Len() <= 3 || Translations == TEXT("/")
        || FPaths::IsDrive(Resolved) || Resolved.Len() <= 3;
    bool bTooBroad = bFileSystemRoot;
    for (const FString& Inside : { Project, FullDirectory(FPlatformProcess::UserHomeDir()), FullDirectory(FPlatformProcess::UserDir()) })
    {
        if (!Inside.IsEmpty() && (IsAtOrUnder(Inside, Translations) || IsUnderDirectoryResolved(Inside, Translations)))
        {
            bTooBroad = true;
        }
    }
    if (bTooBroad)
    {
        // Once per setting value, not on every read
        static FString LastWarned;
        if (LastWarned.Equals(Translations, ESearchCase::IgnoreCase))
        {
            return Roots;
        }
        LastWarned = Translations;
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("The translation output directory %s is too broad to be an MCP read root (a drive root, or it holds the project or your home folder); read-file and read-path cannot reach it"),
            *Translations));
        return Roots;
    }
    Roots.Add(Translations);
    return Roots;
}

bool FN2CPathUtils::ResolveAgentReadPath(const FString& RequestedPath, FString& OutAbsolutePath, FString& OutError)
{
    OutAbsolutePath.Reset();
    OutError.Reset();

    const TArray<FString> Roots = GetAgentReadRoots();
    FString Path = RequestedPath.TrimStartAndEnd();
    Path.ReplaceInline(TEXT("\\"), TEXT("/"));

    // Which root the path is relative to: absolute paths must name one of the roots, anything else is under the project
    FString Root;
    FString Candidate;
    bool bAbsolute = HasDriveOrUncPrefix(Path);
#if !PLATFORM_WINDOWS
    // On Mac and Linux "/Users/me/Project/Config" is absolute, while "/Config" still means the project's Config
    if (!bAbsolute && Path.StartsWith(TEXT("/")))
    {
        FString Collapsed = Path;
        FPaths::CollapseRelativeDirectories(Collapsed);
        for (const FString& ReadRoot : Roots)
        {
            bAbsolute |= IsAtOrUnder(Collapsed, ReadRoot);
        }
    }
#endif
    if (bAbsolute)
    {
        FString Collapsed = Path;
        FPaths::CollapseRelativeDirectories(Collapsed);
        Collapsed = FPaths::ConvertRelativePathToFull(Collapsed);
        for (const FString& R : Roots)
        {
            if (IsAtOrUnder(Collapsed, R))
            {
                Root = R;
                break;
            }
        }
        if (Root.IsEmpty())
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Blocked MCP read of an absolute path outside the read roots: %s"), *RequestedPath));
            OutError = TEXT("Access denied: an absolute path must be inside the project folder or the translation output directory; use a path relative to the project folder");
            return false;
        }
        Candidate = Path;
    }
    else
    {
        while (Path.StartsWith(TEXT("/")))
        {
            Path.RightChopInline(1);
        }
        if (Path == TEXT("."))
        {
            Path.Reset();
        }
        Root = Roots[0];
        Candidate = FPaths::Combine(Root, Path);
    }

    FString Normalized;
    if (!ValidatePathWithinBounds(Root, Candidate, Normalized))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Path traversal attempt blocked. Requested: %s, Base: %s"), *RequestedPath, *Root));
        OutError = TEXT("Access denied: Path traversal detected");
        return false;
    }

    // Credentials are never readable through MCP, whichever spelling (lexical or through a link) names them
    FString Lexical = Candidate;
    FPaths::CollapseRelativeDirectories(Lexical);
    if (IsDeniedForAgentAccess(Normalized) || IsDeniedForAgentAccess(Lexical))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Blocked MCP access to a credential path: %s"), *RequestedPath));
        OutError = TEXT("Access denied: this path holds credentials and is not readable through MCP");
        return false;
    }

    OutAbsolutePath = Normalized;
    return true;
}

FString FN2CPathUtils::MakeAgentDisplayPath(const FString& AbsolutePath)
{
    FString Full = FPaths::ConvertRelativePathToFull(AbsolutePath);
    FPaths::NormalizeFilename(Full);
    while (Full.EndsWith(TEXT("/")) && Full.Len() > 3)
    {
        Full.LeftChopInline(1);
    }

    const FString Project = FullDirectory(FPaths::ProjectDir());
    for (const FString& Base : { Project, ResolveRealPath(Project) })
    {
        if (Base.IsEmpty() || !IsAtOrUnder(Full, Base))
        {
            continue;
        }
        return Full.Len() <= Base.Len() ? FString() : Full.Mid(Base.Len() + 1);
    }
    return Full;
}
