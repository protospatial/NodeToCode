// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintLibraries/N2CStructUtilsLibrary.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

/**
 * UN2CStructUtilsLibrary is Blueprint- and Python-callable, so an MCP agent's run-python reaches it with any string.
 * Bad package paths and names are refused with a message and create nothing (auth-settings-module-19: an unmounted or
 * malformed path was a fatal error), a duplicate name never replaces the struct already there, a failed save returns
 * false (auth-settings-module-20: SavePackage errors went to GError, which is fatal), and the variable listing has one
 * entry per variable. Structs are made under /Game/__N2CTestStructUtils_<guid>, which each case deletes with its files.
 */
BEGIN_DEFINE_SPEC(FN2CStructUtilsSpec, "NodeToCode.StructUtils", N2C_TEST_FLAGS)

	FString Folder;
	FString ReadOnlyFile;

	/** Deletes every struct in Folder, its files and the folder */
	void DeleteFolder()
	{
		if (!ReadOnlyFile.IsEmpty())
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ReadOnlyFile, false);
			ReadOnlyFile.Reset();
		}
		TArray<UObject*> Structs;
		for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
		{
			if (It->GetPathName().StartsWith(Folder + TEXT("/")))
			{
				It->GetOutermost()->SetDirtyFlag(false);
				Structs.Add(*It);
			}
		}
		if (Structs.Num() > 0)
		{
			ObjectTools::ForceDeleteObjects(Structs, /*ShowConfirmation*/ false);
		}
		if (UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr)
		{
			if (AssetSubsystem->DoesDirectoryExist(Folder))
			{
				AssetSubsystem->DeleteDirectory(Folder);
			}
		}
		FString Filename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Folder, Filename))
		{
			IFileManager::Get().DeleteDirectory(*Filename, /*RequireExists*/ false, /*Tree*/ true);
		}
	}

	UUserDefinedStruct* Create(const FString& Path, const FString& Name, bool& bSuccess, FString& Error)
	{
		bSuccess = false;
		Error.Reset();
		return UN2CStructUtilsLibrary::CreateUserDefinedStruct(Path, Name, bSuccess, Error);
	}

END_DEFINE_SPEC(FN2CStructUtilsSpec)

void FN2CStructUtilsSpec::Define()
{
	BeforeEach([this]()
	{
		Folder = FString::Printf(TEXT("/Game/__N2CTestStructUtils_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	});

	AfterEach([this]()
	{
		DeleteFolder();
	});

	It(TEXT("refuses a bad package path or struct name with a message and creates nothing"), [this]()
	{
		struct FCase { FString Path; FString Name; const TCHAR* Why; };
		const TArray<FCase> Cases = {
			{ FString(), TEXT("F_Row"), TEXT("empty path") },
			{ Folder, FString(), TEXT("empty name") },
			{ TEXT("Game/Data"), TEXT("F_Row"), TEXT("no leading slash") },
			{ TEXT("/NoSuchMount/Data"), TEXT("F_Row"), TEXT("a root nothing is mounted at") },
			{ TEXT("/"), TEXT("F_Row"), TEXT("the bare root") },
			{ TEXT("/Game//Data"), TEXT("F_Row"), TEXT("an empty folder name") },
			{ TEXT("/Game/Has Space"), TEXT("F_Row"), TEXT("a space in the path") },
			{ TEXT("\\Game\\Data"), TEXT("F_Row"), TEXT("backslashes") },
			{ TEXT("C:/Temp/Data"), TEXT("F_Row"), TEXT("a file system path") },
			{ Folder, TEXT("F Row!"), TEXT("a space and ! in the name") },
			{ Folder, TEXT("F.Row"), TEXT("a dot in the name") },
			{ Folder, TEXT("Sub/F_Row"), TEXT("a slash in the name") },
		};
		for (const FCase& Case : Cases)
		{
			bool bSuccess = true;
			FString Error;
			UUserDefinedStruct* Struct = Create(Case.Path, Case.Name, bSuccess, Error);
			TestNull(FString::Printf(TEXT("%s: no struct"), Case.Why), Struct);
			TestFalse(FString::Printf(TEXT("%s: not a success"), Case.Why), bSuccess);
			TestFalse(FString::Printf(TEXT("%s: says why"), Case.Why), Error.IsEmpty());
			const FString Full = Case.Path / Case.Name;
			TestNull(FString::Printf(TEXT("%s: no package %s"), Case.Why, *Full), FindPackage(nullptr, *Full));
		}
		TestFalse(TEXT("the folder was not made"), UN2CStructUtilsLibrary::StructExists(Folder / TEXT("F_Row")));
	});

	It(TEXT("refuses a name already taken and keeps the struct that has it"), [this]()
	{
		bool bSuccess = false;
		FString Error;
		UUserDefinedStruct* Original = Create(Folder, TEXT("F_Row"), bSuccess, Error);
		if (!TestTrue(FString::Printf(TEXT("first create succeeds (%s)"), *Error), bSuccess && Original != nullptr)) { return; }
		UN2CStructUtilsLibrary::AddStructVariable(Original, TEXT("Health"), TEXT("int"), bSuccess, Error);
		TestTrue(TEXT("a variable added"), bSuccess);
		const TArray<FString> NamesBefore = UN2CStructUtilsLibrary::GetStructVariableNames(Original);

		UUserDefinedStruct* Again = Create(Folder, TEXT("F_Row"), bSuccess, Error);
		TestNull(TEXT("the second create returns nothing"), Again);
		TestFalse(TEXT("and is not a success"), bSuccess);
		TestTrue(TEXT("and says the asset exists"), Error.Contains(TEXT("already exists")));
		TestEqual(TEXT("the struct at the path is the original"),
			FindObject<UUserDefinedStruct>(nullptr, *(Folder / TEXT("F_Row.F_Row"))), Original);
		TestEqual(TEXT("with its variables"), UN2CStructUtilsLibrary::GetStructVariableNames(Original), NamesBefore);
		TestEqual(TEXT("loading by path gives it"), UN2CStructUtilsLibrary::LoadUserDefinedStruct(Folder / TEXT("F_Row.F_Row")), Original);

		// Saved and found by the asset registry, the name is still taken
		TestTrue(TEXT("the struct saves"), UN2CStructUtilsLibrary::SaveStruct(Original));
		TestTrue(TEXT("it exists"), UN2CStructUtilsLibrary::StructExists(Folder / TEXT("F_Row")));
		TestNull(TEXT("a third create returns nothing"), Create(Folder, TEXT("F_Row"), bSuccess, Error));
		TestTrue(TEXT("another name in the folder is fine"), Create(Folder, TEXT("F_Row2"), bSuccess, Error) != nullptr && bSuccess);
	});

	It(TEXT("lists one name per variable, after adds, a refused type and removing the default"), [this]()
	{
		TestEqual(TEXT("no struct lists nothing"), UN2CStructUtilsLibrary::GetStructVariableNames(nullptr).Num(), 0);

		bool bSuccess = false;
		FString Error;
		UUserDefinedStruct* Struct = Create(Folder, TEXT("F_List"), bSuccess, Error);
		if (!TestNotNull(FString::Printf(TEXT("created (%s)"), *Error), Struct)) { return; }
		TestEqual(TEXT("a new struct has its one default variable"), UN2CStructUtilsLibrary::GetStructVariableNames(Struct).Num(), 1);

		const int32 Added = UN2CStructUtilsLibrary::AddStructVariablesBatch(Struct,
			{ TEXT("Health"), TEXT("DisplayName"), TEXT("Location") }, { TEXT("int"), TEXT("string"), TEXT("vector") }, bSuccess, Error);
		TestEqual(TEXT("three added"), Added, 3);
		TestTrue(TEXT("batch succeeded"), bSuccess);

		UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("Broken"), TEXT("NoSuchType_N2C"), bSuccess, Error);
		TestFalse(TEXT("an unknown type is refused"), bSuccess);
		TestTrue(TEXT("naming the type"), Error.Contains(TEXT("NoSuchType_N2C")));
		UN2CStructUtilsLibrary::AddStructVariablesBatch(Struct, { TEXT("A") }, {}, bSuccess, Error);
		TestFalse(TEXT("names and types of different lengths are refused"), bSuccess);
		UN2CStructUtilsLibrary::AddStructVariable(nullptr, TEXT("A"), TEXT("int"), bSuccess, Error);
		TestFalse(TEXT("no struct is refused"), bSuccess);

		UN2CStructUtilsLibrary::RemoveDefaultVariable(Struct, bSuccess, Error);
		TestTrue(TEXT("the default variable is removed"), bSuccess);

		const TArray<FString> Names = UN2CStructUtilsLibrary::GetStructVariableNames(Struct);
		if (TestEqual(TEXT("three names"), Names.Num(), 3))
		{
			// A struct member's name is the display name with a unique suffix (Health_2_<guid>)
			TestTrue(TEXT("Health first"), Names[0].StartsWith(TEXT("Health")));
			TestTrue(TEXT("DisplayName second"), Names[1].StartsWith(TEXT("DisplayName")));
			TestTrue(TEXT("Location third"), Names[2].StartsWith(TEXT("Location")));
		}
		TSet<FString> Unique(Names);
		TestEqual(TEXT("no name twice"), Unique.Num(), Names.Num());

		// A second variable with a display name already used gets a name of its own
		UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("Health"), TEXT("float"), bSuccess, Error);
		const TArray<FString> After = UN2CStructUtilsLibrary::GetStructVariableNames(Struct);
		TestEqual(TEXT("four names"), After.Num(), 4);
		TestEqual(TEXT("still no name twice"), TSet<FString>(After).Num(), After.Num());
	});

	It(TEXT("returns false instead of a fatal error when the save fails"), [this]()
	{
		TestFalse(TEXT("no struct"), UN2CStructUtilsLibrary::SaveStruct(nullptr));

		bool bSuccess = false;
		FString Error;
		UUserDefinedStruct* Struct = Create(Folder, TEXT("F_Saved"), bSuccess, Error);
		if (!TestNotNull(FString::Printf(TEXT("created (%s)"), *Error), Struct)) { return; }
		if (!TestTrue(TEXT("first save succeeds"), UN2CStructUtilsLibrary::SaveStruct(Struct))) { return; }

		FString Filename;
		FPackageName::TryConvertLongPackageNameToFilename(Struct->GetOutermost()->GetName(), Filename, FPackageName::GetAssetPackageExtension());
		Filename = FPaths::ConvertRelativePathToFull(Filename);
		if (!TestTrue(TEXT("the file is on disk"), FPaths::FileExists(Filename))) { return; }
		ReadOnlyFile = Filename;
		TestTrue(TEXT("file made read-only, as a source-controlled file that is not checked out"),
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, true));

		UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("Extra"), TEXT("bool"), bSuccess, Error);
		AddExpectedError(TEXT("SaveStruct: could not save"), EAutomationExpectedErrorFlags::Contains, 1);
		// The engine's own line for the refused write (LogSavePackage, an error but not fatal)
		AddExpectedError(TEXT("as it is read only"), EAutomationExpectedErrorFlags::Contains, 0);
		TestFalse(TEXT("the save of a read-only file returns false"), UN2CStructUtilsLibrary::SaveStruct(Struct));
		TestTrue(TEXT("the struct is still loaded"), IsValid(Struct));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
