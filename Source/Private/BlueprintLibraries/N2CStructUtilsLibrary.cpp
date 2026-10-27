// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "BlueprintLibraries/N2CStructUtilsLibrary.h"
#include "Editor.h"
#include "Utils/N2CLogger.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/StructureFactory.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "N2CStructUtilsLibrary"

namespace
{
	/** True for the member FStructureEditorUtils::CreateUserDefinedStruct adds to every new struct: a bool named MemberVar_<n> */
	bool IsFactoryDefaultVariable(const FStructVariableDescription& Var)
	{
		return Var.Category == UEdGraphSchema_K2::PC_Boolean
			&& Var.ContainerType == EPinContainerType::None
			&& Var.FriendlyName.StartsWith(TEXT("MemberVar_"), ESearchCase::CaseSensitive);
	}
}

UUserDefinedStruct* UN2CStructUtilsLibrary::CreateUserDefinedStruct(
	const FString& PackagePath,
	const FString& StructName,
	bool& bSuccess,
	FString& ErrorMessage)
{
	bSuccess = false;

	if (PackagePath.IsEmpty())
	{
		ErrorMessage = TEXT("Package path cannot be empty");
		return nullptr;
	}

	if (StructName.IsEmpty())
	{
		ErrorMessage = TEXT("Struct name cannot be empty");
		return nullptr;
	}

	// Construct the full asset path
	FString FullPath = PackagePath / StructName;

	// Validate before touching the package system: LongPackageNameToFilename is fatal for an invalid or
	// unmounted path ("Game/Data", "/NoSuchMount/Data")
	FText NameReason;
	if (!FName::IsValidXName(StructName, INVALID_OBJECTNAME_CHARACTERS, &NameReason))
	{
		ErrorMessage = FString::Printf(TEXT("Invalid struct name '%s': %s"), *StructName, *NameReason.ToString());
		return nullptr;
	}
	FText PathReason;
	if (!FPackageName::IsValidLongPackageName(FullPath, false, &PathReason))
	{
		ErrorMessage = FString::Printf(TEXT("Invalid package path '%s': %s"), *FullPath, *PathReason.ToString());
		return nullptr;
	}
	FString UnusedFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(FullPath, UnusedFileName, FPackageName::GetAssetPackageExtension()))
	{
		ErrorMessage = FString::Printf(TEXT("Package path '%s' is not under a mounted content root (for example /Game)"), *FullPath);
		return nullptr;
	}

	// Never replace a live struct: during PIE the asset library reports false, and CreatePackage would return the
	// loaded package so the new struct replaced the one DataTables and Blueprints use
	if (GEditor && GEditor->PlayWorld)
	{
		ErrorMessage = TEXT("Cannot create a struct while Play In Editor is running");
		return nullptr;
	}
	if (FindPackage(nullptr, *FullPath) || FindObject<UObject>(nullptr, *(FullPath + TEXT(".") + StructName)))
	{
		ErrorMessage = FString::Printf(TEXT("Asset already exists at path: %s"), *FullPath);
		return nullptr;
	}
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Existing;
		AssetRegistryModule.Get().GetAssetsByPackageName(FName(*FullPath), Existing, /*bIncludeOnlyOnDiskAssets=*/false);
		if (Existing.Num() > 0)
		{
			ErrorMessage = FString::Printf(TEXT("Asset already exists at path: %s"), *FullPath);
			return nullptr;
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FText NotAllowedReason;
	if (!AssetTools.IsNameAllowed(StructName, &NotAllowedReason))
	{
		ErrorMessage = FString::Printf(TEXT("Struct name '%s' is not allowed: %s"), *StructName, *NotAllowedReason.ToString());
		return nullptr;
	}

	// Create through IAssetTools with the struct factory, as the Content Browser does (transactional flags, asset
	// registry notification, the package marked dirty). Every case it would ask about in a dialog was refused above.
	UStructureFactory* Factory = NewObject<UStructureFactory>();
	UUserDefinedStruct* NewStruct = Cast<UUserDefinedStruct>(AssetTools.CreateAsset(
		StructName, PackagePath, UUserDefinedStruct::StaticClass(), Factory, FName(TEXT("NodeToCode"))));

	if (!NewStruct)
	{
		ErrorMessage = FString::Printf(TEXT("Failed to create the struct asset at: %s"), *FullPath);
		return nullptr;
	}

	bSuccess = true;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Created UserDefinedStruct: %s"), *FullPath), EN2CLogSeverity::Info);

	return NewStruct;
}

void UN2CStructUtilsLibrary::AddStructVariable(
	UUserDefinedStruct* Struct,
	const FString& VariableName,
	const FString& VariableType,
	bool& bSuccess,
	FString& ErrorMessage)
{
	bSuccess = false;

	if (!Struct)
	{
		ErrorMessage = TEXT("Struct is null");
		return;
	}

	if (VariableName.IsEmpty())
	{
		ErrorMessage = TEXT("Variable name cannot be empty");
		return;
	}

	if (VariableType.IsEmpty())
	{
		ErrorMessage = TEXT("Variable type cannot be empty");
		return;
	}

	// Convert type string to FEdGraphPinType
	FEdGraphPinType PinType;
	if (!TypeStringToPinType(VariableType, PinType, ErrorMessage))
	{
		return;
	}

	// A member's display name must be unique in the struct; checked first so a taken name adds nothing
	if (!FStructureEditorUtils::IsUniqueVariableFriendlyName(Struct, VariableName))
	{
		ErrorMessage = FString::Printf(TEXT("Struct '%s' already has a variable named '%s'"), *Struct->GetName(), *VariableName);
		return;
	}

	// Add and rename as one undo step
	FScopedTransaction Transaction(LOCTEXT("AddStructVariable", "Add Struct Variable"));

	TSet<FGuid> GuidsBefore;
	for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
	{
		GuidsBefore.Add(Var.VarGuid);
	}

	if (!FStructureEditorUtils::AddVariable(Struct, PinType))
	{
		Transaction.Cancel();
		ErrorMessage = FString::Printf(TEXT("FStructureEditorUtils::AddVariable failed to create a '%s' variable (the type may not be allowed in a struct; see the log)"), *VariableType);
		return;
	}

	FGuid NewVarGuid;
	for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
	{
		if (!GuidsBefore.Contains(Var.VarGuid))
		{
			NewVarGuid = Var.VarGuid;
			break;
		}
	}
	if (!NewVarGuid.IsValid())
	{
		Transaction.Cancel();
		ErrorMessage = TEXT("FStructureEditorUtils::AddVariable reported success but no new variable was found");
		return;
	}

	// Rename the new member to the requested name; if that fails, take the member out again rather than leave an
	// unnamed MemberVar_<n> field in the struct
	if (!FStructureEditorUtils::RenameVariable(Struct, NewVarGuid, VariableName))
	{
		FStructureEditorUtils::RemoveVariable(Struct, NewVarGuid);
		Transaction.Cancel();
		ErrorMessage = FString::Printf(TEXT("Could not name the new variable '%s'; nothing was added"), *VariableName);
		return;
	}

	// Mark struct as modified
	Struct->MarkPackageDirty();

	bSuccess = true;
	FN2CLogger::Get().Log(FString::Printf(
		TEXT("Added variable '%s' of type '%s' to struct '%s'"),
		*VariableName, *VariableType, *Struct->GetName()), EN2CLogSeverity::Info);
}

int32 UN2CStructUtilsLibrary::AddStructVariablesBatch(
	UUserDefinedStruct* Struct,
	const TArray<FString>& VariableNames,
	const TArray<FString>& VariableTypes,
	bool& bSuccess,
	FString& ErrorMessage)
{
	bSuccess = false;

	if (!Struct)
	{
		ErrorMessage = TEXT("Struct is null");
		return 0;
	}

	if (VariableNames.Num() != VariableTypes.Num())
	{
		ErrorMessage = TEXT("VariableNames and VariableTypes arrays must have the same length");
		return 0;
	}

	if (VariableNames.Num() == 0)
	{
		ErrorMessage = TEXT("No variables provided");
		return 0;
	}

	int32 SuccessCount = 0;
	TArray<FString> FailedVariables;

	for (int32 i = 0; i < VariableNames.Num(); ++i)
	{
		bool bVarSuccess = false;
		FString VarError;
		AddStructVariable(Struct, VariableNames[i], VariableTypes[i], bVarSuccess, VarError);

		if (bVarSuccess)
		{
			SuccessCount++;
		}
		else
		{
			FailedVariables.Add(FString::Printf(TEXT("%s: %s"), *VariableNames[i], *VarError));
		}
	}

	if (FailedVariables.Num() > 0)
	{
		ErrorMessage = FString::Printf(TEXT("Failed to add %d variable(s): %s"),
			FailedVariables.Num(), *FString::Join(FailedVariables, TEXT("; ")));
	}

	bSuccess = (SuccessCount == VariableNames.Num());
	return SuccessCount;
}

void UN2CStructUtilsLibrary::RemoveDefaultVariable(
	UUserDefinedStruct* Struct,
	bool& bSuccess,
	FString& ErrorMessage)
{
	bSuccess = false;

	if (!Struct)
	{
		ErrorMessage = TEXT("Struct is null");
		return;
	}

	// Get all variable descriptions
	TArray<FStructVariableDescription>& Variables = FStructureEditorUtils::GetVarDesc(Struct);

	if (Variables.Num() == 0)
	{
		ErrorMessage = TEXT("Struct has no variables to remove");
		return;
	}

	// Only the member the struct was created with (a bool named MemberVar_<n>) is removed, never a field the user made
	const FStructVariableDescription* DefaultVar = Variables.FindByPredicate(&IsFactoryDefaultVariable);
	if (!DefaultVar)
	{
		ErrorMessage = FString::Printf(TEXT("Struct '%s' has no default variable (a bool named MemberVar_<n>) to remove; nothing was removed"), *Struct->GetName());
		return;
	}
	if (Variables.Num() < 2)
	{
		ErrorMessage = TEXT("The default variable is the struct's only member and a struct cannot be empty; add a variable first");
		return;
	}

	const FGuid VarToRemove = DefaultVar->VarGuid;
	if (!FStructureEditorUtils::RemoveVariable(Struct, VarToRemove))
	{
		ErrorMessage = TEXT("Failed to remove default variable");
		return;
	}

	Struct->MarkPackageDirty();
	bSuccess = true;
	FN2CLogger::Get().Log(FString::Printf(
		TEXT("Removed default variable from struct '%s'"), *Struct->GetName()), EN2CLogSeverity::Info);
}

TArray<FString> UN2CStructUtilsLibrary::GetStructVariableNames(UUserDefinedStruct* Struct)
{
	TArray<FString> Result;

	if (!Struct)
	{
		return Result;
	}

	const TArray<FStructVariableDescription>& Variables = FStructureEditorUtils::GetVarDesc(Struct);

	for (const FStructVariableDescription& Var : Variables)
	{
		Result.Add(Var.FriendlyName);
	}

	return Result;
}

bool UN2CStructUtilsLibrary::StructExists(const FString& AssetPath)
{
	return UEditorAssetLibrary::DoesAssetExist(AssetPath);
}

UUserDefinedStruct* UN2CStructUtilsLibrary::LoadUserDefinedStruct(const FString& AssetPath)
{
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		return nullptr;
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Cast<UUserDefinedStruct>(LoadedAsset);
}

bool UN2CStructUtilsLibrary::SaveStruct(UUserDefinedStruct* Struct)
{
	if (!Struct)
	{
		return false;
	}

	UPackage* Package = Struct->GetOutermost();
	if (!Package)
	{
		return false;
	}

	// Save the package. Errors go to the log (SAVE_NoError), never to GError, which would be fatal for a
	// read-only file or a rejected save.
	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("SaveStruct: package %s is not under a mounted content root"), *Package->GetName()));
		return false;
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GWarn;
	SaveArgs.SaveFlags = SAVE_NoError;

	bool bSaved = UPackage::SavePackage(Package, Struct, *PackageFileName, SaveArgs);

	if (bSaved)
	{
		FN2CLogger::Get().Log(FString::Printf(
			TEXT("Saved struct '%s' to %s"), *Struct->GetName(), *PackageFileName), EN2CLogSeverity::Info);
	}
	else
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("SaveStruct: could not save %s (read-only file, or the save was rejected)"), *PackageFileName));
	}

	return bSaved;
}

bool UN2CStructUtilsLibrary::TypeStringToPinType(
	const FString& TypeString,
	FEdGraphPinType& OutPinType,
	FString& OutErrorMessage)
{
	// Initialize with defaults
	OutPinType = FEdGraphPinType();
	OutPinType.ContainerType = EPinContainerType::None;

	FString TypeLower = TypeString.ToLower();

	// Primitive types
	if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		return true;
	}
	if (TypeLower == TEXT("byte") || TypeLower == TEXT("uint8"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		return true;
	}
	if (TypeLower == TEXT("int") || TypeLower == TEXT("int32") || TypeLower == TEXT("integer"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		return true;
	}
	if (TypeLower == TEXT("int64"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		return true;
	}
	if (TypeLower == TEXT("float"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		return true;
	}
	if (TypeLower == TEXT("double"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		return true;
	}
	if (TypeLower == TEXT("fstring") || TypeLower == TEXT("string"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		return true;
	}
	if (TypeLower == TEXT("fname") || TypeLower == TEXT("name"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		return true;
	}
	if (TypeLower == TEXT("ftext") || TypeLower == TEXT("text"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		return true;
	}

	// Common struct types
	if (TypeLower == TEXT("fvector") || TypeLower == TEXT("vector"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		return true;
	}
	if (TypeLower == TEXT("fvector2d") || TypeLower == TEXT("vector2d"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
		return true;
	}
	if (TypeLower == TEXT("frotator") || TypeLower == TEXT("rotator"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		return true;
	}
	if (TypeLower == TEXT("ftransform") || TypeLower == TEXT("transform"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		return true;
	}
	if (TypeLower == TEXT("fcolor") || TypeLower == TEXT("color"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FColor>::Get();
		return true;
	}
	if (TypeLower == TEXT("flinearcolor") || TypeLower == TEXT("linearcolor"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		return true;
	}

	// Try to find as a class (for object references)
	// Check if it's a full path like /Script/Engine.Actor
	if (TypeString.StartsWith(TEXT("/")))
	{
		// Try loading as class first
		UClass* FoundClass = LoadClass<UObject>(nullptr, *TypeString);
		if (FoundClass)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = FoundClass;
			return true;
		}

		// Try loading as struct
		UScriptStruct* FoundStruct = LoadObject<UScriptStruct>(nullptr, *TypeString);
		if (FoundStruct)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = FoundStruct;
			return true;
		}
	}

	// Try common class names without full path
	TArray<FString> CommonClassPaths = {
		FString::Printf(TEXT("/Script/Engine.%s"), *TypeString),
		FString::Printf(TEXT("/Script/CoreUObject.%s"), *TypeString),
	};

	for (const FString& ClassPath : CommonClassPaths)
	{
		UClass* FoundClass = LoadClass<UObject>(nullptr, *ClassPath);
		if (FoundClass)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = FoundClass;
			return true;
		}
	}

	// Try as struct in common paths
	for (const FString& StructPath : CommonClassPaths)
	{
		UScriptStruct* FoundStruct = LoadObject<UScriptStruct>(nullptr, *StructPath);
		if (FoundStruct)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = FoundStruct;
			return true;
		}
	}

	OutErrorMessage = FString::Printf(TEXT("Unknown type: '%s'. Supported types include: bool, int32, float, double, FString, FName, FText, FVector, FRotator, FTransform, FColor, or full class/struct paths like '/Script/Engine.Actor'"), *TypeString);
	return false;
}

#undef LOCTEXT_NAMESPACE
