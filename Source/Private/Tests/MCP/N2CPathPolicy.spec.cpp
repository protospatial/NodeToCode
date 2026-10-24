// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Utils/N2CPathUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * The path jail behind the MCP file-system tools (read-file, read-path): FN2CPathUtils::ValidatePathWithinBounds,
 * which must keep a request inside its base whatever the spelling (.., backslashes, drive letters, UNC, case, a
 * trailing slash on the base) and after following junctions, and FN2CPathUtils::IsDeniedForAgentAccess, which keeps
 * credentials out of reach even inside the base. The read-file and read-path cases call the registered tools.
 * Review ids: mcp-tools-contentbrowser-fs-1 (secrets readable), mcp-tools-contentbrowser-fs-16 (lexical-only jail).
 */
BEGIN_DEFINE_SPEC(FN2CPathPolicySpec, "NodeToCode.PathPolicy", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString Base;
	FString Outside;

	bool Validate(const FString& InBase, const FString& Path) const
	{
		FString Normalized;
		return FN2CPathUtils::ValidatePathWithinBounds(InBase, Path, Normalized);
	}

	/** Creates a directory junction (no admin rights needed); false when the platform cannot. */
	bool MakeJunction(const FString& Link, const FString& Target)
	{
#if PLATFORM_WINDOWS
		const FString Args = FString::Printf(TEXT("/c mklink /J \"%s\" \"%s\""),
			*FPaths::ConvertRelativePathToFull(Link).Replace(TEXT("/"), TEXT("\\")),
			*FPaths::ConvertRelativePathToFull(Target).Replace(TEXT("/"), TEXT("\\")));
		int32 ReturnCode = -1;
		FString StdOut, StdErr;
		FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *Args, &ReturnCode, &StdOut, &StdErr);
		return ReturnCode == 0 && IFileManager::Get().DirectoryExists(*Link);
#else
		return false;
#endif
	}

	/** Removes a junction itself, never the tree it points at. */
	static void RemoveLink(const FString& Link)
	{
		IFileManager::Get().DeleteDirectory(*Link, false, false);
	}

END_DEFINE_SPEC(FN2CPathPolicySpec)

void FN2CPathPolicySpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CPathPolicy"));
		Base = *Scratch / TEXT("Base");
		Outside = *Scratch / TEXT("Outside");
		IFileManager::Get().MakeDirectory(*(Base / TEXT("Sub")), true);
		IFileManager::Get().MakeDirectory(*Outside, true);
		FFileHelper::SaveStringToFile(TEXT("inside"), *(Base / TEXT("Sub/inside.txt")));
		FFileHelper::SaveStringToFile(TEXT("outside"), *(Outside / TEXT("secret.txt")));
	});

	AfterEach([this]()
	{
		RemoveLink(Base / TEXT("LinkOut"));
		RemoveLink(Base / TEXT("LinkIn"));
		Scratch.Reset();
	});

	Describe(TEXT("ValidatePathWithinBounds"), [this]()
	{
		It(TEXT("accepts the base itself and paths inside it, including ones that do not exist yet"), [this]()
		{
			TestTrue(TEXT("base"), Validate(Base, Base));
			TestTrue(TEXT("existing file"), Validate(Base, Base / TEXT("Sub/inside.txt")));
			TestTrue(TEXT("missing file"), Validate(Base, Base / TEXT("Sub/new/file.txt")));
			TestTrue(TEXT(".. that stays inside"), Validate(Base, Base / TEXT("Sub/../Sub/inside.txt")));
		});

		It(TEXT("rejects .. that climbs out of the base"), [this]()
		{
			TestFalse(TEXT("one level"), Validate(Base, Base / TEXT("../Outside/secret.txt")));
			TestFalse(TEXT("through a subfolder"), Validate(Base, Base / TEXT("Sub/../../Outside/secret.txt")));
			TestFalse(TEXT("many levels"), Validate(Base, Base / TEXT("../../../../../../../../Windows/win.ini")));
		});

		It(TEXT("treats backslashes like slashes"), [this]()
		{
			const FString BackslashBase = Base.Replace(TEXT("/"), TEXT("\\"));
			TestTrue(TEXT("inside with backslashes"), Validate(Base, BackslashBase + TEXT("\\Sub\\inside.txt")));
			TestTrue(TEXT("backslash base"), Validate(BackslashBase, Base / TEXT("Sub/inside.txt")));
			TestFalse(TEXT("..\\ escape"), Validate(Base, BackslashBase + TEXT("\\..\\Outside\\secret.txt")));
			TestFalse(TEXT("mixed separators escape"), Validate(Base, Base + TEXT("/Sub\\..\\..\\Outside/secret.txt")));
		});

		It(TEXT("rejects absolute paths on another drive or another tree"), [this]()
		{
			TestFalse(TEXT("C: root"), Validate(Base, TEXT("C:/Windows/win.ini")));
			TestFalse(TEXT("C: with backslashes"), Validate(Base, TEXT("C:\\Windows\\win.ini")));
			TestFalse(TEXT("Z: drive"), Validate(Base, TEXT("Z:/secret.txt")));
			TestFalse(TEXT("sibling folder"), Validate(Base, Outside / TEXT("secret.txt")));
		});

		It(TEXT("rejects UNC paths"), [this]()
		{
			TestFalse(TEXT("//server/share"), Validate(Base, TEXT("//server/share/file.txt")));
			TestFalse(TEXT("\\\\server\\share"), Validate(Base, TEXT("\\\\server\\share\\file.txt")));
			TestFalse(TEXT("\\\\?\\ device path"), Validate(Base, TEXT("\\\\?\\C:\\Windows\\win.ini")));
		});

		It(TEXT("gives the same answers whether or not the base ends with a slash"), [this]()
		{
			for (const FString& B : { Base, Base + TEXT("/") })
			{
				TestTrue(FString::Printf(TEXT("inside, base '%s'"), *B), Validate(B, Base / TEXT("Sub/inside.txt")));
				TestFalse(FString::Printf(TEXT("escape, base '%s'"), *B), Validate(B, Base / TEXT("../Outside/secret.txt")));
				// A sibling whose name starts with the base's name passes a plain StartsWith on the base
				TestFalse(FString::Printf(TEXT("name-prefix sibling, base '%s'"), *B), Validate(B, Base + TEXT("Evil/x.txt")));
			}
		});

#if PLATFORM_WINDOWS
		It(TEXT("matches the base case-insensitively on Windows and still rejects escapes in any case"), [this]()
		{
			TestTrue(TEXT("upper-case path"), Validate(Base, (Base / TEXT("Sub/inside.txt")).ToUpper()));
			TestTrue(TEXT("upper-case base"), Validate(Base.ToUpper(), Base / TEXT("Sub/inside.txt")));
			TestFalse(TEXT("upper-case escape"), Validate(Base, (Base / TEXT("../Outside/secret.txt")).ToUpper()));
			TestFalse(TEXT("lower-case sibling"), Validate(Base, (Outside / TEXT("secret.txt")).ToLower()));
		});

		It(TEXT("rejects a junction under Saved that points outside the base, and accepts one that points inside"), [this]()
		{
			if (!TestTrue(TEXT("created the outward junction"), MakeJunction(Base / TEXT("LinkOut"), Outside)))
			{
				return;
			}
			TestTrue(TEXT("the junction really leads outside"), FPaths::FileExists(Base / TEXT("LinkOut/secret.txt")));
			TestFalse(TEXT("file through the outward junction"), Validate(Base, Base / TEXT("LinkOut/secret.txt")));
			TestFalse(TEXT("the outward junction itself"), Validate(Base, Base / TEXT("LinkOut")));
			TestFalse(TEXT("missing file under the outward junction"), Validate(Base, Base / TEXT("LinkOut/new.txt")));

			if (TestTrue(TEXT("created the inward junction"), MakeJunction(Base / TEXT("LinkIn"), Base / TEXT("Sub"))))
			{
				TestTrue(TEXT("file through the inward junction"), Validate(Base, Base / TEXT("LinkIn/inside.txt")));
			}
		});
#endif
	});

	Describe(TEXT("IsDeniedForAgentAccess"), [this]()
	{
		It(TEXT("denies Saved/NodeToCode/User, the per-user secret store, .git, secrets.json and .env files"), [this]()
		{
			const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
			TestTrue(TEXT("Saved/NodeToCode/User"), FN2CPathUtils::IsDeniedForAgentAccess(Saved / TEXT("NodeToCode/User")));
			TestTrue(TEXT("secrets.json under User"), FN2CPathUtils::IsDeniedForAgentAccess(Saved / TEXT("NodeToCode/User/secrets.json")));
			TestTrue(TEXT("other file under User"), FN2CPathUtils::IsDeniedForAgentAccess(Saved / TEXT("NodeToCode/User/notes.txt")));
			TestTrue(TEXT("User in another case"), FN2CPathUtils::IsDeniedForAgentAccess((Saved / TEXT("nodetocode/user/x.json")).ToLower()));
			TestTrue(TEXT("User with backslashes"), FN2CPathUtils::IsDeniedForAgentAccess((Saved / TEXT("NodeToCode/User/secrets.json")).Replace(TEXT("/"), TEXT("\\"))));
			TestTrue(TEXT("User reached through .."), FN2CPathUtils::IsDeniedForAgentAccess(Saved / TEXT("NodeToCode/Translations/../User/secrets.json")));
			TestTrue(TEXT("per-user store"), FN2CPathUtils::IsDeniedForAgentAccess(FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("NodeToCode"), TEXT("secrets.json"))));
			TestTrue(TEXT(".git"), FN2CPathUtils::IsDeniedForAgentAccess(Project / TEXT(".git/config")));
			TestTrue(TEXT("secrets.json anywhere"), FN2CPathUtils::IsDeniedForAgentAccess(Project / TEXT("Config/secrets.json")));
			TestTrue(TEXT("secrets.json backup"), FN2CPathUtils::IsDeniedForAgentAccess(Project / TEXT("Config/secrets.json.bak")));
			TestTrue(TEXT(".env"), FN2CPathUtils::IsDeniedForAgentAccess(Project / TEXT(".env")));
			TestTrue(TEXT("named .env"), FN2CPathUtils::IsDeniedForAgentAccess(Project / TEXT("Tools/local.env")));
		});

		It(TEXT("allows ordinary project files"), [this]()
		{
			const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
			TestFalse(TEXT("Config ini"), FN2CPathUtils::IsDeniedForAgentAccess(Project / TEXT("Config/DefaultEngine.ini")));
			TestFalse(TEXT("translations"), FN2CPathUtils::IsDeniedForAgentAccess(Saved / TEXT("NodeToCode/Translations/a.cpp")));
			TestFalse(TEXT("a folder named Users"), FN2CPathUtils::IsDeniedForAgentAccess(Saved / TEXT("NodeToCode/Users/a.txt")));
			TestFalse(TEXT("environment.h"), FN2CPathUtils::IsDeniedForAgentAccess(Project / TEXT("Source/environment.h")));
		});
	});

	Describe(TEXT("read-file and read-path"), [this]()
	{
		It(TEXT("refuse Saved/NodeToCode/User and its secrets.json however the path is spelled"), [this]()
		{
			AddExpectedErrorPlain(TEXT("Blocked MCP access to a credential path"), EAutomationExpectedErrorFlags::Contains, 0);
			for (const TCHAR* Path : { TEXT("Saved/NodeToCode/User/secrets.json"), TEXT("Saved\\NodeToCode\\User\\secrets.json"),
				TEXT("saved/nodetocode/user/secrets.json"), TEXT("Saved/NodeToCode/Translations/../User/secrets.json"),
				TEXT("./Saved/NodeToCode/User/secrets.json") })
			{
				const FMcpToolCallResult Result = N2CTest::CallTool(TEXT("read-file"), { { TEXT("relativePath"), Path } });
				TestTrue(FString::Printf(TEXT("read-file '%s' is an error"), Path), Result.bIsError);
				TestTrue(FString::Printf(TEXT("read-file '%s' names credentials"), Path), N2CTest::ResultText(Result).Contains(TEXT("credentials")));
			}
			for (const TCHAR* Path : { TEXT("Saved/NodeToCode/User"), TEXT("Saved\\NodeToCode\\User\\") })
			{
				const FMcpToolCallResult Result = N2CTest::CallTool(TEXT("read-path"), { { TEXT("relativePath"), Path } });
				TestTrue(FString::Printf(TEXT("read-path '%s' is an error"), Path), Result.bIsError);
				TestTrue(FString::Printf(TEXT("read-path '%s' names credentials"), Path), N2CTest::ResultText(Result).Contains(TEXT("credentials")));
			}
		});

		It(TEXT("refuse paths that leave the project"), [this]()
		{
			AddExpectedErrorPlain(TEXT("Path traversal attempt blocked"), EAutomationExpectedErrorFlags::Contains, 0);
			for (const TCHAR* Path : { TEXT("../../../../../../../../Windows/win.ini"), TEXT("..\\..\\..\\..\\..\\..\\Windows\\win.ini") })
			{
				const FMcpToolCallResult File = N2CTest::CallTool(TEXT("read-file"), { { TEXT("relativePath"), Path } });
				TestTrue(FString::Printf(TEXT("read-file '%s' is an error"), Path), File.bIsError);
				TestTrue(FString::Printf(TEXT("read-file '%s' says traversal"), Path), N2CTest::ResultText(File).Contains(TEXT("traversal")));
				const FMcpToolCallResult Dir = N2CTest::CallTool(TEXT("read-path"), { { TEXT("relativePath"), Path } });
				TestTrue(FString::Printf(TEXT("read-path '%s' is an error"), Path), Dir.bIsError);
			}
			// An absolute path is joined under the project, so it names a file that does not exist there
			const FMcpToolCallResult Drive = N2CTest::CallTool(TEXT("read-file"), { { TEXT("relativePath"), TEXT("C:/Windows/win.ini") } });
			TestTrue(TEXT("read-file with a drive letter is an error"), Drive.bIsError);
		});

#if PLATFORM_WINDOWS
		It(TEXT("refuse a junction inside the project that points outside it"), [this]()
		{
			// The scratch folder is inside the project, so this target lives in the OS temp folder instead
			const FString OutsideProject = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::UserTempDir(),
				FString::Printf(TEXT("N2CPathPolicy_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			FFileHelper::SaveStringToFile(TEXT("outside"), *(OutsideProject / TEXT("secret.txt")));
			ON_SCOPE_EXIT
			{
				RemoveLink(Base / TEXT("LinkOut"));
				IFileManager::Get().DeleteDirectory(*OutsideProject, false, true);
			};
			TestFalse(TEXT("the target is outside the project"),
				FPaths::IsUnderDirectory(OutsideProject, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())));
			if (!TestTrue(TEXT("created the outward junction"), MakeJunction(Base / TEXT("LinkOut"), OutsideProject)))
			{
				return;
			}
			AddExpectedErrorPlain(TEXT("Path traversal attempt blocked"), EAutomationExpectedErrorFlags::Contains, 0);
			FString Relative = Base / TEXT("LinkOut/secret.txt");
			FPaths::MakePathRelativeTo(Relative, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
			const FMcpToolCallResult File = N2CTest::CallTool(TEXT("read-file"), { { TEXT("relativePath"), Relative } });
			TestTrue(TEXT("read-file through the junction is an error"), File.bIsError);
			TestFalse(TEXT("read-file did not return the outside file"), N2CTest::ResultText(File).Contains(TEXT("\"content\":\"outside\"")));

			FString RelativeDir = Base / TEXT("LinkOut");
			FPaths::MakePathRelativeTo(RelativeDir, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
			const FMcpToolCallResult Dir = N2CTest::CallTool(TEXT("read-path"), { { TEXT("relativePath"), RelativeDir } });
			TestTrue(TEXT("read-path through the junction is an error"), Dir.bIsError);
		});
#endif

		It(TEXT("still read an ordinary file inside the project"), [this]()
		{
			FString Relative = Base / TEXT("Sub/inside.txt");
			FPaths::MakePathRelativeTo(Relative, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
			const FMcpToolCallResult Result = N2CTest::CallTool(TEXT("read-file"), { { TEXT("relativePath"), Relative } });
			TestFalse(FString::Printf(TEXT("read-file '%s' succeeds"), *Relative), Result.bIsError);
			const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
			FString Content;
			TestTrue(TEXT("result has the file content"), Json.IsValid() && Json->TryGetStringField(TEXT("content"), Content) && Content == TEXT("inside"));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
