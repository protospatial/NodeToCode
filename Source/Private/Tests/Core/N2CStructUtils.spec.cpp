// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintLibraries/N2CStructUtilsLibrary.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
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
 * entry per variable, by the name it was given. A variable name the struct already has is refused and adds nothing
 * (auth-settings-module-21: the rename failed and a MemberVar_<n> field stayed behind), the listing gives those names
 * rather than the internal ones (auth-settings-module-22), and removing the default variable never removes a field
 * the user made (auth-settings-module-24: it removed whatever came first). Structs are made under /Game/__N2CTestStructUtils_<guid>, which each case deletes with its files.
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
		// The names are the ones asked for, not the internal Health_2_<guid>
		TestEqual(TEXT("the names given, in order"), Names, TArray<FString>{ TEXT("Health"), TEXT("DisplayName"), TEXT("Location") });
	});

	It(TEXT("refuses a variable name the struct already has and leaves no unnamed field"), [this]()
	{
		bool bSuccess = false;
		FString Error;
		UUserDefinedStruct* Struct = Create(Folder, TEXT("F_Dup"), bSuccess, Error);
		if (!TestNotNull(FString::Printf(TEXT("created (%s)"), *Error), Struct)) { return; }
		UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("Health"), TEXT("int"), bSuccess, Error);
		TestTrue(TEXT("the first Health is added"), bSuccess);
		UN2CStructUtilsLibrary::RemoveDefaultVariable(Struct, bSuccess, Error);
		TestTrue(TEXT("the default variable is removed"), bSuccess);

		Error.Reset();
		UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("Health"), TEXT("float"), bSuccess, Error);
		TestFalse(TEXT("a second Health is refused"), bSuccess);
		TestTrue(FString::Printf(TEXT("saying the name is taken (%s)"), *Error), Error.Contains(TEXT("already has a variable named 'Health'")));
		UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("health"), TEXT("float"), bSuccess, Error);
		TestFalse(TEXT("names differing only in case are the same name"), bSuccess);

		const int32 Added = UN2CStructUtilsLibrary::AddStructVariablesBatch(Struct,
			{ TEXT("Armor"), TEXT("Health") }, { TEXT("int"), TEXT("int") }, bSuccess, Error);
		TestEqual(TEXT("a batch adds the new name only"), Added, 1);
		TestFalse(TEXT("and is not a success"), bSuccess);

		const TArray<FString> Names = UN2CStructUtilsLibrary::GetStructVariableNames(Struct);
		TestEqual(TEXT("Health once, then Armor"), Names, TArray<FString>{ TEXT("Health"), TEXT("Armor") });
		TestFalse(TEXT("no MemberVar field left behind"),
			Names.ContainsByPredicate([](const FString& Name) { return Name.StartsWith(TEXT("MemberVar")); }));
	});

	It(TEXT("undoes adding a variable in one step, name and all"), [this]()
	{
		UTransBuffer* TransBuffer = GEditor ? Cast<UTransBuffer>(GEditor->Trans) : nullptr;
		if (!TestNotNull(TEXT("the editor has an undo buffer"), TransBuffer)) { return; }
		bool bSuccess = false;
		FString Error;
		UUserDefinedStruct* Struct = Create(Folder, TEXT("F_Undo"), bSuccess, Error);
		if (!TestNotNull(FString::Printf(TEXT("created (%s)"), *Error), Struct)) { return; }
		const TArray<FString> Before = UN2CStructUtilsLibrary::GetStructVariableNames(Struct);

		UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("Speed"), TEXT("float"), bSuccess, Error);
		TestTrue(TEXT("Speed is added"), bSuccess);
		TestEqual(TEXT("as one undo step"), TransBuffer->GetUndoContext().Title.ToString(), FString(TEXT("Add Struct Variable")));
		TestTrue(TEXT("one undo"), GEditor->UndoTransaction());
		TestEqual(TEXT("leaves the struct as it was, with no unnamed field"), UN2CStructUtilsLibrary::GetStructVariableNames(Struct), Before);
	});

	It(TEXT("removes only the default variable, never a field the user made"), [this]()
	{
		bool bSuccess = true;
		FString Error;
		UN2CStructUtilsLibrary::RemoveDefaultVariable(nullptr, bSuccess, Error);
		TestFalse(TEXT("no struct is refused"), bSuccess);

		UUserDefinedStruct* Struct = Create(Folder, TEXT("F_Item"), bSuccess, Error);
		if (!TestNotNull(FString::Printf(TEXT("created (%s)"), *Error), Struct)) { return; }

		// While the default is the only member it cannot go: a struct cannot be empty
		UN2CStructUtilsLibrary::RemoveDefaultVariable(Struct, bSuccess, Error);
		TestFalse(TEXT("the only member is not removed"), bSuccess);
		TestFalse(TEXT("saying why"), Error.IsEmpty());
		TestEqual(TEXT("it is still there"), UN2CStructUtilsLibrary::GetStructVariableNames(Struct).Num(), 1);

		UN2CStructUtilsLibrary::AddStructVariablesBatch(Struct, { TEXT("ItemId"), TEXT("Count") }, { TEXT("name"), TEXT("int") }, bSuccess, Error);
		TestTrue(TEXT("ItemId and Count added"), bSuccess);
		UN2CStructUtilsLibrary::RemoveDefaultVariable(Struct, bSuccess, Error);
		TestTrue(TEXT("then the default is removed"), bSuccess);
		const TArray<FString> Expected = { TEXT("ItemId"), TEXT("Count") };
		TestEqual(TEXT("ItemId is now the first field"), UN2CStructUtilsLibrary::GetStructVariableNames(Struct), Expected);

		// A second call, as on a struct loaded from disk whose first field is the user's
		Error.Reset();
		UN2CStructUtilsLibrary::RemoveDefaultVariable(Struct, bSuccess, Error);
		TestFalse(TEXT("a second call removes nothing"), bSuccess);
		TestTrue(FString::Printf(TEXT("and says there is no default variable (%s)"), *Error), Error.Contains(TEXT("no default variable")));
		TestEqual(TEXT("ItemId and Count are kept"), UN2CStructUtilsLibrary::GetStructVariableNames(Struct), Expected);

		// A user's bool is not the default either, even first in the struct
		UUserDefinedStruct* Flags = Create(Folder, TEXT("F_Flags"), bSuccess, Error);
		if (!TestNotNull(TEXT("second struct created"), Flags)) { return; }
		UN2CStructUtilsLibrary::AddStructVariablesBatch(Flags, { TEXT("bEnabled"), TEXT("bVisible") }, { TEXT("bool"), TEXT("bool") }, bSuccess, Error);
		UN2CStructUtilsLibrary::RemoveDefaultVariable(Flags, bSuccess, Error);
		TestTrue(TEXT("the default goes"), bSuccess);
		UN2CStructUtilsLibrary::RemoveDefaultVariable(Flags, bSuccess, Error);
		TestFalse(TEXT("a user's bool does not"), bSuccess);
		TestEqual(TEXT("both bools kept"), UN2CStructUtilsLibrary::GetStructVariableNames(Flags), TArray<FString>{ TEXT("bEnabled"), TEXT("bVisible") });
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
