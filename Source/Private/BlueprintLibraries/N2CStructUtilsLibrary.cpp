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

	// Create the package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		ErrorMessage = FString::Printf(TEXT("Failed to create package at: %s"), *FullPath);
		return nullptr;
	}

	// Create the UserDefinedStruct
	UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(
		Package,
		FName(*StructName),
		RF_Public | RF_Standalone
	);

	if (!NewStruct)
	{
		ErrorMessage = TEXT("FStructureEditorUtils::CreateUserDefinedStruct failed");
		return nullptr;
	}

	// Mark package dirty
	Package->MarkPackageDirty();

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(NewStruct);

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

	// Get variable count before adding
	TArray<FStructVariableDescription>& Variables = FStructureEditorUtils::GetVarDesc(Struct);
	int32 VarCountBefore = Variables.Num();

	// Add the variable using FStructureEditorUtils
	bool bAdded = FStructureEditorUtils::AddVariable(Struct, PinType);

	if (!bAdded)
	{
		ErrorMessage = TEXT("FStructureEditorUtils::AddVariable failed to create variable");
		return;
	}

	// Get the newly added variable's GUID (it's the last one)
	TArray<FStructVariableDescription>& UpdatedVariables = FStructureEditorUtils::GetVarDesc(Struct);
	if (UpdatedVariables.Num() > VarCountBefore)
	{
		FGuid NewVarGuid = UpdatedVariables.Last().VarGuid;

		// Rename the variable to the desired name
		bool bRenamed = FStructureEditorUtils::RenameVariable(Struct, NewVarGuid, VariableName);
		if (!bRenamed)
		{
			FN2CLogger::Get().LogWarning(FString::Printf(
				TEXT("Variable created but rename to '%s' failed. Using default name."), *VariableName));
		}
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

	// Find and remove the first (default) variable
	// The default variable typically has a generated name like "MemberVar_0_..."
	if (Variables.Num() > 0)
	{
		FGuid VarToRemove = Variables[0].VarGuid;
		bool bRemoved = FStructureEditorUtils::RemoveVariable(Struct, VarToRemove);

		if (bRemoved)
		{
			Struct->MarkPackageDirty();
			bSuccess = true;
			FN2CLogger::Get().Log(FString::Printf(
				TEXT("Removed default variable from struct '%s'"), *Struct->GetName()), EN2CLogSeverity::Info);
		}
		else
		{
			ErrorMessage = TEXT("Failed to remove default variable");
		}
	}
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
		Result.Add(Var.VarName.ToString());
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
