// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSearchBlueprintClassesTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "Blueprint/BlueprintSupport.h"

REGISTER_MCP_TOOL(FN2CMcpSearchBlueprintClassesTool)

// Define common class lists
const TArray<FString> FN2CMcpSearchBlueprintClassesTool::CommonActorClasses = {
	TEXT("/Script/Engine.Actor"),
	TEXT("/Script/Engine.Pawn"),
	TEXT("/Script/Engine.Character"),
	TEXT("/Script/Engine.GameModeBase"),
	TEXT("/Script/Engine.GameStateBase"),
	TEXT("/Script/Engine.PlayerController"),
	TEXT("/Script/Engine.PlayerState"),
	TEXT("/Script/Engine.HUD")
};

const TArray<FString> FN2CMcpSearchBlueprintClassesTool::CommonComponentClasses = {
	TEXT("/Script/Engine.ActorComponent"),
	TEXT("/Script/Engine.SceneComponent"),
	TEXT("/Script/Engine.PrimitiveComponent"),
	TEXT("/Script/Engine.StaticMeshComponent"),
	TEXT("/Script/Engine.SkeletalMeshComponent"),
	TEXT("/Script/Engine.CapsuleComponent"),
	TEXT("/Script/Engine.SphereComponent"),
	TEXT("/Script/Engine.BoxComponent")
};

const TArray<FString> FN2CMcpSearchBlueprintClassesTool::CommonObjectClasses = {
	TEXT("/Script/CoreUObject.Object"),
	TEXT("/Script/Engine.DataAsset"),
	TEXT("/Script/Engine.SaveGame")
};

const TArray<FString> FN2CMcpSearchBlueprintClassesTool::CommonWidgetClasses = {
	TEXT("/Script/UMG.UserWidget"),
	TEXT("/Script/UMG.Button"),
	TEXT("/Script/UMG.TextBlock"),
	TEXT("/Script/UMG.Image")
};

FMcpToolDefinition FN2CMcpSearchBlueprintClassesTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("search-blueprint-classes"),
		TEXT("Searches for available parent classes for Blueprint creation, similar to the 'Pick Parent Class' dialog. Each class is listed once, native and Blueprint classes alike, and every filter applies to all of them. Common parent classes (Actor, Pawn, Character, ActorComponent, UserWidget, ...) score higher. totalFound counts the classes that match, before maxResults is applied"),
		N2CMcpToolsets::BlueprintClass
	);

	// Build input schema manually
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// searchTerm property
	TSharedPtr<FJsonObject> SearchTermProp = MakeShareable(new FJsonObject);
	SearchTermProp->SetStringField(TEXT("type"), TEXT("string"));
	SearchTermProp->SetStringField(TEXT("description"), TEXT("Text query to search for class names"));
	Properties->SetObjectField(TEXT("searchTerm"), SearchTermProp);

	// classType filter
	TSharedPtr<FJsonObject> ClassTypeProp = MakeShareable(new FJsonObject);
	ClassTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> ClassTypeEnum;
	ClassTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("all"))));
	ClassTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("actor"))));
	ClassTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("actorComponent"))));
	ClassTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("object"))));
	ClassTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("userWidget"))));
	ClassTypeProp->SetArrayField(TEXT("enum"), ClassTypeEnum);
	ClassTypeProp->SetStringField(TEXT("default"), TEXT("all"));
	ClassTypeProp->SetStringField(TEXT("description"), TEXT("Filter by base class type"));
	Properties->SetObjectField(TEXT("classType"), ClassTypeProp);

	// includeEngineClasses
	TSharedPtr<FJsonObject> IncludeEngineProp = MakeShareable(new FJsonObject);
	IncludeEngineProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeEngineProp->SetBoolField(TEXT("default"), true);
	IncludeEngineProp->SetStringField(TEXT("description"), TEXT("Include engine-provided classes in results"));
	Properties->SetObjectField(TEXT("includeEngineClasses"), IncludeEngineProp);

	// includePluginClasses
	TSharedPtr<FJsonObject> IncludePluginProp = MakeShareable(new FJsonObject);
	IncludePluginProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludePluginProp->SetBoolField(TEXT("default"), true);
	IncludePluginProp->SetStringField(TEXT("description"), TEXT("Include plugin-provided classes in results"));
	Properties->SetObjectField(TEXT("includePluginClasses"), IncludePluginProp);

	// includeDeprecated
	TSharedPtr<FJsonObject> IncludeDeprecatedProp = MakeShareable(new FJsonObject);
	IncludeDeprecatedProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeDeprecatedProp->SetBoolField(TEXT("default"), false);
	IncludeDeprecatedProp->SetStringField(TEXT("description"), TEXT("Include deprecated classes in results"));
	Properties->SetObjectField(TEXT("includeDeprecated"), IncludeDeprecatedProp);

	// includeAbstract
	TSharedPtr<FJsonObject> IncludeAbstractProp = MakeShareable(new FJsonObject);
	IncludeAbstractProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeAbstractProp->SetBoolField(TEXT("default"), true);
	IncludeAbstractProp->SetStringField(TEXT("description"), TEXT("Include abstract classes in results; abstract classes such as UserWidget and PrimaryDataAsset are valid parents"));
	Properties->SetObjectField(TEXT("includeAbstract"), IncludeAbstractProp);

	// maxResults
	TSharedPtr<FJsonObject> MaxResultsProp = MakeShareable(new FJsonObject);
	MaxResultsProp->SetStringField(TEXT("type"), TEXT("integer"));
	MaxResultsProp->SetNumberField(TEXT("default"), 50);
	MaxResultsProp->SetNumberField(TEXT("minimum"), 1);
	MaxResultsProp->SetNumberField(TEXT("maximum"), 200);
	MaxResultsProp->SetStringField(TEXT("description"), TEXT("Maximum number of results to return"));
	Properties->SetObjectField(TEXT("maxResults"), MaxResultsProp);

	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required fields
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("searchTerm"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	// Read-only: reads editor or project state and changes nothing
	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
	
	return Definition;
}

FMcpToolCallResult FN2CMcpSearchBlueprintClassesTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FString SearchTerm;
		if (!Arguments->TryGetStringField(TEXT("searchTerm"), SearchTerm))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: searchTerm"));
		}

		FString ClassTypeFilter = TEXT("all");
		Arguments->TryGetStringField(TEXT("classType"), ClassTypeFilter);

		bool bIncludeEngineClasses = true;
		Arguments->TryGetBoolField(TEXT("includeEngineClasses"), bIncludeEngineClasses);

		bool bIncludePluginClasses = true;
		Arguments->TryGetBoolField(TEXT("includePluginClasses"), bIncludePluginClasses);

		bool bIncludeDeprecated = false;
		Arguments->TryGetBoolField(TEXT("includeDeprecated"), bIncludeDeprecated);

		bool bIncludeAbstract = true;
		Arguments->TryGetBoolField(TEXT("includeAbstract"), bIncludeAbstract);

		int32 MaxResults = 50;
		Arguments->TryGetNumberField(TEXT("maxResults"), MaxResults);
		MaxResults = FMath::Clamp(MaxResults, 1, 200);

		// Collect every eligible class once: native classes (the common ones marked), then Blueprint classes from the
		// asset registry. Before card 0288 the common classes were added a second time ahead of every filter, and
		// loaded Blueprint classes came through the native pass as well.
		TArray<FBlueprintClassInfo> AllClasses;
		CollectNativeClasses(AllClasses, ClassTypeFilter, bIncludeEngineClasses, bIncludePluginClasses, bIncludeDeprecated, bIncludeAbstract);
		CollectBlueprintClasses(AllClasses, ClassTypeFilter, bIncludeEngineClasses, bIncludePluginClasses, bIncludeDeprecated, bIncludeAbstract);

		TSet<FString> SeenPaths;
		AllClasses.RemoveAll([&SeenPaths](const FBlueprintClassInfo& Info)
		{
			bool bSeen = false;
			SeenPaths.Add(Info.ClassPath, &bSeen);
			return bSeen;
		});

		// Filter and score based on search term; totalFound is the number of matches
		int32 TotalMatches = 0;
		TArray<FBlueprintClassInfo> FilteredClasses = FilterAndScoreClasses(AllClasses, SearchTerm, MaxResults, TotalMatches);

		// Build JSON result
		TSharedPtr<FJsonObject> Result = BuildJsonResult(FilteredClasses, TotalMatches);

		// Convert result to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(OutputString);
	});
}

void FN2CMcpSearchBlueprintClassesTool::CollectNativeClasses(TArray<FBlueprintClassInfo>& OutClasses, const FString& ClassTypeFilter, 
	bool bIncludeEngineClasses, bool bIncludePluginClasses, bool bIncludeDeprecated, bool bIncludeAbstract) const
{
	// Iterate through all loaded native classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class || Class->HasAnyClassFlags(CLASS_Interface))
		{
			continue;
		}

		// Native classes only: Blueprint classes (and their SKEL_ and REINST_ copies) come from the asset registry.
		// IsChildOf(UBlueprintGeneratedClass) tested the class's parents, not the class itself, so it never matched one.
		if (!Class->HasAnyClassFlags(CLASS_Native) || Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
			|| Cast<UBlueprintGeneratedClass>(Class) || Class->ClassGeneratedBy)
		{
			continue;
		}

		// Skip deprecated if not included
		if (!bIncludeDeprecated && Class->HasAnyClassFlags(CLASS_Deprecated))
		{
			continue;
		}

		// Skip abstract if not included
		if (!bIncludeAbstract && Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		// Check if class can be used as Blueprint parent
		if (!CanCreateBlueprintOfClass(Class))
		{
			continue;
		}

		// Apply class type filter
		if (!PassesClassTypeFilter(Class, ClassTypeFilter))
		{
			continue;
		}

		// Check module filter
		FString ClassPath = Class->GetPathName();
		if (!bIncludeEngineClasses && IsEngineClass(ClassPath))
		{
			continue;
		}
		if (!bIncludePluginClasses && IsPluginClass(ClassPath))
		{
			continue;
		}

		// Create class info
		FBlueprintClassInfo ClassInfo;
		ClassInfo.ClassName = Class->GetName();
		ClassInfo.ClassPath = ClassPath;
		ClassInfo.DisplayName = GetClassDisplayName(Class);
		ClassInfo.bIsCommonClass = IsCommonClassPath(ClassPath);
		ClassInfo.Category = ClassInfo.bIsCommonClass ? FString(TEXT("Common")) : GetClassCategory(Class);
		ClassInfo.Description = GetClassDescription(Class);
		ClassInfo.ParentClass = Class->GetSuperClass() ? Class->GetSuperClass()->GetName() : TEXT("");
		ClassInfo.Module = GetClassModule(Class);
		ClassInfo.Icon = GetClassIcon(Class);
		ClassInfo.bIsAbstract = Class->HasAnyClassFlags(CLASS_Abstract);
		ClassInfo.bIsDeprecated = Class->HasAnyClassFlags(CLASS_Deprecated);
		ClassInfo.bIsBlueprintable = true; // Already checked via CanCreateBlueprintOfClass
		ClassInfo.bIsNative = true;

		OutClasses.Add(ClassInfo);
	}
}

void FN2CMcpSearchBlueprintClassesTool::CollectBlueprintClasses(TArray<FBlueprintClassInfo>& OutClasses, const FString& ClassTypeFilter,
	bool bIncludeEngineClasses, bool bIncludePluginClasses, bool bIncludeDeprecated, bool bIncludeAbstract) const
{
	// Get the asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Search for Blueprint assets
	TArray<FAssetData> BlueprintAssets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BlueprintAssets, true);

	// The class tags are export text (/Script/Engine.BlueprintGeneratedClass'/Game/BP_Door.BP_Door_C'): use their
	// object path, and the object name after its last '.'
	auto ObjectNameOf = [](const FString& ObjectPath)
	{
		int32 Dot = INDEX_NONE;
		return ObjectPath.FindLastChar(TEXT('.'), Dot) ? ObjectPath.Mid(Dot + 1) : FPackageName::GetShortName(ObjectPath);
	};

	for (const FAssetData& AssetData : BlueprintAssets)
	{
		// Skip if module filter doesn't match
		FString PackagePath = AssetData.PackagePath.ToString();
		if (!bIncludeEngineClasses && IsEngineClass(PackagePath))
		{
			continue;
		}
		if (!bIncludePluginClasses && IsPluginClass(PackagePath))
		{
			continue;
		}

		// Only normal and const Blueprints are classes to derive from (not macro or function libraries or level scripts)
		FString BlueprintTypeTag;
		if (AssetData.GetTagValue(FBlueprintTags::BlueprintType, BlueprintTypeTag) && !BlueprintTypeTag.IsEmpty()
			&& BlueprintTypeTag != TEXT("BPTYPE_Normal") && BlueprintTypeTag != TEXT("BPTYPE_Const"))
		{
			continue;
		}

		// The generated class's object path
		FString GeneratedClassTag;
		AssetData.GetTagValue(FBlueprintTags::GeneratedClassPath, GeneratedClassTag);
		const FString GeneratedClassPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassTag);
		if (GeneratedClassPath.IsEmpty() || GeneratedClassPath == TEXT("None"))
		{
			continue;
		}

		// The parent class, and the first native class above it
		FString ParentClassTag, NativeParentTag;
		AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassTag);
		AssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParentTag);
		const FString ParentClassPath = FPackageName::ExportTextPathToObjectPath(ParentClassTag);
		if (ParentClassPath.IsEmpty() || ParentClassPath == TEXT("None"))
		{
			continue;
		}

		// The generated class's flags (interface, abstract, deprecated). The IsDeprecated tag read before card 0288 is
		// not written by UBlueprint.
		FString ClassFlagsTag;
		const uint32 ClassFlags = AssetData.GetTagValue(FBlueprintTags::ClassFlags, ClassFlagsTag)
			? static_cast<uint32>(FCString::Strtoui64(*ClassFlagsTag, nullptr, 10)) : 0;
		if ((ClassFlags & CLASS_Interface) != 0)
		{
			continue;
		}
		if (!bIncludeDeprecated && (ClassFlags & CLASS_Deprecated) != 0)
		{
			continue;
		}
		if (!bIncludeAbstract && (ClassFlags & CLASS_Abstract) != 0)
		{
			continue;
		}

		// classType tests the native parent with IsChildOf, as for native classes (before card 0288 it matched a
		// substring of the parent's export text, so a Character Blueprint was not an actor)
		if (ClassTypeFilter != TEXT("all"))
		{
			const FString NativeParentPath = FPackageName::ExportTextPathToObjectPath(NativeParentTag);
			const UClass* NativeParent = NativeParentPath.IsEmpty() || NativeParentPath == TEXT("None")
				? nullptr : FindObject<UClass>(nullptr, *NativeParentPath);
			if (!NativeParent || !PassesClassTypeFilter(NativeParent, ClassTypeFilter))
			{
				continue;
			}
		}

		// Create class info
		FBlueprintClassInfo ClassInfo;
		ClassInfo.ClassName = ObjectNameOf(GeneratedClassPath);
		ClassInfo.ClassPath = GeneratedClassPath;
		ClassInfo.DisplayName = AssetData.AssetName.ToString();
		ClassInfo.Category = FPackageName::GetShortName(AssetData.PackagePath.ToString());
		ClassInfo.ParentClass = ObjectNameOf(ParentClassPath);
		ClassInfo.Description = FString::Printf(TEXT("Blueprint class derived from %s"), *ClassInfo.ParentClass);
		ClassInfo.Module = TEXT("Game"); // Blueprint classes are typically in game module
		ClassInfo.Icon = TEXT("Blueprint");
		ClassInfo.bIsAbstract = (ClassFlags & CLASS_Abstract) != 0;
		ClassInfo.bIsDeprecated = (ClassFlags & CLASS_Deprecated) != 0;
		ClassInfo.bIsBlueprintable = true;
		ClassInfo.bIsNative = false;
		ClassInfo.bIsCommonClass = false;

		OutClasses.Add(ClassInfo);
	}
}

bool FN2CMcpSearchBlueprintClassesTool::IsCommonClassPath(const FString& ClassPath) const
{
	return CommonActorClasses.Contains(ClassPath) || CommonComponentClasses.Contains(ClassPath)
		|| CommonObjectClasses.Contains(ClassPath) || CommonWidgetClasses.Contains(ClassPath);
}

bool FN2CMcpSearchBlueprintClassesTool::PassesClassTypeFilter(const UClass* Class, const FString& ClassTypeFilter) const
{
	if (ClassTypeFilter == TEXT("all"))
	{
		return true;
	}
	else if (ClassTypeFilter == TEXT("actor"))
	{
		return Class->IsChildOf(AActor::StaticClass());
	}
	else if (ClassTypeFilter == TEXT("actorComponent"))
	{
		return Class->IsChildOf(UActorComponent::StaticClass());
	}
	else if (ClassTypeFilter == TEXT("userWidget"))
	{
		return Class->IsChildOf(UUserWidget::StaticClass());
	}
	else if (ClassTypeFilter == TEXT("object"))
	{
		// For object filter, accept anything not in the other categories
		return !Class->IsChildOf(AActor::StaticClass()) && 
		       !Class->IsChildOf(UActorComponent::StaticClass()) && 
		       !Class->IsChildOf(UUserWidget::StaticClass());
	}

	return false;
}

bool FN2CMcpSearchBlueprintClassesTool::CanCreateBlueprintOfClass(const UClass* Class) const
{
	// Use the same logic as FKismetEditorUtilities::CanCreateBlueprintOfClass
	return FKismetEditorUtilities::CanCreateBlueprintOfClass(Class);
}

FString FN2CMcpSearchBlueprintClassesTool::GetClassModule(const UClass* Class) const
{
	if (Class)
	{
		UPackage* Package = Class->GetOutermost();
		if (Package)
		{
			FString PackageName = Package->GetName();
			
			// Extract module name from package path
			if (PackageName.StartsWith(TEXT("/Script/")))
			{
				return PackageName.RightChop(8); // Remove "/Script/"
			}
			else if (PackageName.StartsWith(TEXT("/Game/")))
			{
				return TEXT("Game");
			}
			else
			{
				// Try to extract module from path
				int32 FirstSlash = PackageName.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 1);
				if (FirstSlash != INDEX_NONE)
				{
					return PackageName.Mid(1, FirstSlash - 1);
				}
			}
		}
	}
	
	return TEXT("Unknown");
}

FString FN2CMcpSearchBlueprintClassesTool::GetClassCategory(const UClass* Class) const
{
	if (Class)
	{
		// Try to get category from metadata
		if (Class->HasMetaData(TEXT("Category")))
		{
			return Class->GetMetaData(TEXT("Category"));
		}

		// Generate category based on class hierarchy
		if (Class->IsChildOf(AActor::StaticClass()))
		{
			if (Class->IsChildOf(APawn::StaticClass()))
			{
				if (Class->IsChildOf(ACharacter::StaticClass()))
				{
					return TEXT("Actor|Pawn|Character");
				}
				return TEXT("Actor|Pawn");
			}
			else if (Class->IsChildOf(AGameModeBase::StaticClass()))
			{
				return TEXT("Actor|GameMode");
			}
			return TEXT("Actor");
		}
		else if (Class->IsChildOf(UActorComponent::StaticClass()))
		{
			if (Class->IsChildOf(USceneComponent::StaticClass()))
			{
				return TEXT("Component|Scene");
			}
			return TEXT("Component");
		}
		else if (Class->IsChildOf(UUserWidget::StaticClass()))
		{
			return TEXT("Widget");
		}
		
		// Default to module name
		return GetClassModule(Class);
	}
	
	return TEXT("Misc");
}

FString FN2CMcpSearchBlueprintClassesTool::GetClassDescription(const UClass* Class) const
{
	if (Class)
	{
		// Try tooltip metadata first
		if (Class->HasMetaData(TEXT("ToolTip")))
		{
			return Class->GetMetaData(TEXT("ToolTip"));
		}

		// Try short tooltip
		if (Class->HasMetaData(TEXT("ShortTooltip")))
		{
			return Class->GetMetaData(TEXT("ShortTooltip"));
		}

		// Generate a basic description
		FString ParentName = Class->GetSuperClass() ? Class->GetSuperClass()->GetName() : TEXT("Object");
		return FString::Printf(TEXT("Class derived from %s"), *ParentName);
	}
	
	return TEXT("");
}

FString FN2CMcpSearchBlueprintClassesTool::GetClassDisplayName(const UClass* Class) const
{
	// The editor's display name: the DisplayName metadata, otherwise the name split at capitals. UClass names have no
	// C++ prefix, so nothing is stripped (before card 0288 Actor showed as 'ctor').
	return Class ? Class->GetDisplayNameText().ToString() : FString();
}

FString FN2CMcpSearchBlueprintClassesTool::GetClassIcon(const UClass* Class) const
{
	if (Class)
	{
		// Determine icon based on class type
		if (Class->IsChildOf(ACharacter::StaticClass()))
		{
			return TEXT("Character");
		}
		else if (Class->IsChildOf(APawn::StaticClass()))
		{
			return TEXT("Pawn");
		}
		else if (Class->IsChildOf(AGameModeBase::StaticClass()))
		{
			return TEXT("GameMode");
		}
		else if (Class->IsChildOf(AActor::StaticClass()))
		{
			return TEXT("Actor");
		}
		else if (Class->IsChildOf(USceneComponent::StaticClass()))
		{
			return TEXT("SceneComponent");
		}
		else if (Class->IsChildOf(UActorComponent::StaticClass()))
		{
			return TEXT("Component");
		}
		else if (Class->IsChildOf(UUserWidget::StaticClass()))
		{
			return TEXT("Widget");
		}
	}
	
	return TEXT("Object");
}

bool FN2CMcpSearchBlueprintClassesTool::IsEngineClass(const FString& ClassPath) const
{
	return ClassPath.StartsWith(TEXT("/Script/Engine")) || 
	       ClassPath.StartsWith(TEXT("/Engine/")) ||
	       ClassPath.StartsWith(TEXT("/Script/CoreUObject")) ||
	       ClassPath.StartsWith(TEXT("/Script/UMG"));
}

bool FN2CMcpSearchBlueprintClassesTool::IsPluginClass(const FString& ClassPath) const
{
	return ClassPath.Contains(TEXT("/Plugins/")) || 
	       (ClassPath.StartsWith(TEXT("/Script/")) && 
	        !IsEngineClass(ClassPath) && 
	        !ClassPath.StartsWith(FString(TEXT("/Script/")) + FApp::GetProjectName()));
}

TArray<FN2CMcpSearchBlueprintClassesTool::FBlueprintClassInfo> FN2CMcpSearchBlueprintClassesTool::FilterAndScoreClasses(
	const TArray<FBlueprintClassInfo>& AllClasses, const FString& SearchTerm, int32 MaxResults, int32& OutTotalMatches) const
{
	TArray<FBlueprintClassInfo> ScoredClasses;

	// Score all classes
	for (const FBlueprintClassInfo& ClassInfo : AllClasses)
	{
		int32 Score = CalculateRelevanceScore(ClassInfo, SearchTerm);
		if (Score > 0)
		{
			FBlueprintClassInfo ScoredInfo = ClassInfo;
			ScoredInfo.RelevanceScore = Score;
			ScoredClasses.Add(ScoredInfo);
		}
	}

	OutTotalMatches = ScoredClasses.Num();

	// Sort by score (descending): the common-class bonus is part of the score, not a tier above it
	ScoredClasses.Sort([](const FBlueprintClassInfo& A, const FBlueprintClassInfo& B)
	{
		// Sort by score
		if (A.RelevanceScore != B.RelevanceScore)
		{
			return A.RelevanceScore > B.RelevanceScore;
		}
		
		// Finally sort alphabetically
		return A.DisplayName < B.DisplayName;
	});

	// Limit results
	if (ScoredClasses.Num() > MaxResults)
	{
		ScoredClasses.SetNum(MaxResults);
	}

	return ScoredClasses;
}

int32 FN2CMcpSearchBlueprintClassesTool::CalculateRelevanceScore(const FBlueprintClassInfo& ClassInfo, const FString& SearchTerm) const
{
	int32 Score = 0;
	FString SearchLower = SearchTerm.ToLower();

	// No search term: every class gets the base score
	if (SearchTerm.IsEmpty())
	{
		Score = 100;
	}
	// Exact match on class name
	else if (ClassInfo.ClassName.ToLower() == SearchLower)
	{
		Score += 1000;
	}
	// Exact match on display name
	else if (ClassInfo.DisplayName.ToLower() == SearchLower)
	{
		Score += 900;
	}
	// Class name starts with search term
	else if (ClassInfo.ClassName.ToLower().StartsWith(SearchLower))
	{
		Score += 500;
	}
	// Display name starts with search term
	else if (ClassInfo.DisplayName.ToLower().StartsWith(SearchLower))
	{
		Score += 400;
	}
	// Class name contains search term
	else if (ClassInfo.ClassName.ToLower().Contains(SearchLower))
	{
		Score += 200;
	}
	// Display name contains search term
	else if (ClassInfo.DisplayName.ToLower().Contains(SearchLower))
	{
		Score += 150;
	}
	// Description contains search term
	else if (ClassInfo.Description.ToLower().Contains(SearchLower))
	{
		Score += 50;
	}
	// Category contains search term
	else if (ClassInfo.Category.ToLower().Contains(SearchLower))
	{
		Score += 25;
	}

	// Bonus for common classes
	if (ClassInfo.bIsCommonClass && Score > 0)
	{
		Score += 100;
	}

	// Penalty for deprecated classes
	if (ClassInfo.bIsDeprecated)
	{
		Score = FMath::Max(1, Score / 2);
	}

	return Score;
}

TSharedPtr<FJsonObject> FN2CMcpSearchBlueprintClassesTool::BuildJsonResult(const TArray<FBlueprintClassInfo>& FilteredClasses, int32 TotalFound) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	// Build classes array
	TArray<TSharedPtr<FJsonValue>> ClassesArray;
	for (const FBlueprintClassInfo& ClassInfo : FilteredClasses)
	{
		ClassesArray.Add(MakeShareable(new FJsonValueObject(ClassInfoToJson(ClassInfo))));
	}
	
	Result->SetArrayField(TEXT("classes"), ClassesArray);
	Result->SetNumberField(TEXT("totalFound"), TotalFound);
	Result->SetBoolField(TEXT("hasMore"), FilteredClasses.Num() < TotalFound);
	
	return Result;
}

TSharedPtr<FJsonObject> FN2CMcpSearchBlueprintClassesTool::ClassInfoToJson(const FBlueprintClassInfo& ClassInfo) const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("className"), ClassInfo.ClassName);
	JsonObject->SetStringField(TEXT("classPath"), ClassInfo.ClassPath);
	JsonObject->SetStringField(TEXT("displayName"), ClassInfo.DisplayName);
	JsonObject->SetStringField(TEXT("category"), ClassInfo.Category);
	JsonObject->SetStringField(TEXT("description"), ClassInfo.Description);
	JsonObject->SetStringField(TEXT("parentClass"), ClassInfo.ParentClass);
	JsonObject->SetBoolField(TEXT("isAbstract"), ClassInfo.bIsAbstract);
	JsonObject->SetBoolField(TEXT("isDeprecated"), ClassInfo.bIsDeprecated);
	JsonObject->SetBoolField(TEXT("isBlueprintable"), ClassInfo.bIsBlueprintable);
	JsonObject->SetBoolField(TEXT("isNative"), ClassInfo.bIsNative);
	JsonObject->SetStringField(TEXT("module"), ClassInfo.Module);
	JsonObject->SetStringField(TEXT("icon"), ClassInfo.Icon);
	JsonObject->SetBoolField(TEXT("commonClass"), ClassInfo.bIsCommonClass);
	
	return JsonObject;
}