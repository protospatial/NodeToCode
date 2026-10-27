// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSearchVariableTypesTool.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node.h"
#include "EdGraphSchema_K2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Blueprint/BlueprintSupport.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformTime.h"

REGISTER_MCP_TOOL(FN2CMcpSearchVariableTypesTool)

FMcpToolDefinition FN2CMcpSearchVariableTypesTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("search-variable-types"),
		TEXT("Searches for available variable types (primitives, classes, structs, enums) by name and returns matches with unique type identifiers")
	,
		N2CMcpToolsets::BlueprintVariable
	);

	// Build input schema manually (similar to search blueprint nodes tool)
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// searchTerm property
	TSharedPtr<FJsonObject> SearchTermProp = MakeShareable(new FJsonObject);
	SearchTermProp->SetStringField(TEXT("type"), TEXT("string"));
	SearchTermProp->SetStringField(TEXT("description"), TEXT("The text query to search for type names"));
	Properties->SetObjectField(TEXT("searchTerm"), SearchTermProp);

	// Category filter
	TSharedPtr<FJsonObject> CategoryProp = MakeShareable(new FJsonObject);
	CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> CategoryEnum;
	CategoryEnum.Add(MakeShareable(new FJsonValueString(TEXT("all"))));
	CategoryEnum.Add(MakeShareable(new FJsonValueString(TEXT("primitive"))));
	CategoryEnum.Add(MakeShareable(new FJsonValueString(TEXT("class"))));
	CategoryEnum.Add(MakeShareable(new FJsonValueString(TEXT("struct"))));
	CategoryEnum.Add(MakeShareable(new FJsonValueString(TEXT("enum"))));
	CategoryProp->SetArrayField(TEXT("enum"), CategoryEnum);
	CategoryProp->SetStringField(TEXT("default"), TEXT("all"));
	CategoryProp->SetStringField(TEXT("description"), TEXT("Filter results by type category"));
	Properties->SetObjectField(TEXT("category"), CategoryProp);

	// Include engine types
	TSharedPtr<FJsonObject> IncludeEngineProp = MakeShareable(new FJsonObject);
	IncludeEngineProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeEngineProp->SetBoolField(TEXT("default"), true);
	IncludeEngineProp->SetStringField(TEXT("description"), TEXT("Include engine-provided types in results"));
	Properties->SetObjectField(TEXT("includeEngineTypes"), IncludeEngineProp);

	// Max results
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

namespace N2CSearchVariableTypes
{
	using FTypeInfo = FN2CMcpSearchVariableTypesTool::FVariableTypeInfo;

	/**
	 * The native and asset types every search reads, built once without loading any asset (mcp-tools-variables-18,
	 * imp-mcp-tools-variables-7). Native types are rebuilt when a module loads or unloads, asset types when the asset
	 * registry adds, removes, renames or updates an asset. Both rebuilds happen on the next search, on the game thread.
	 */
	struct FTypeIndex
	{
		TArray<FTypeInfo> NativeTypes;
		TArray<FTypeInfo> AssetTypes;
		bool bNativeStale = true;
		bool bAssetsStale = true;
		bool bSubscribed = false;
		FDelegateHandle AssetAddedHandle;
		FDelegateHandle AssetRemovedHandle;
		FDelegateHandle AssetRenamedHandle;
		FDelegateHandle AssetUpdatedHandle;
		FDelegateHandle FilesLoadedHandle;
		FDelegateHandle ModulesChangedHandle;
		FDelegateHandle PreExitHandle;
	};

	FTypeIndex& Index()
	{
		static FTypeIndex Instance;
		return Instance;
	}

	void Unsubscribe()
	{
		FTypeIndex& State = Index();
		if (!State.bSubscribed)
		{
			return;
		}
		if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
		{
			AssetRegistry->OnAssetAdded().Remove(State.AssetAddedHandle);
			AssetRegistry->OnAssetRemoved().Remove(State.AssetRemovedHandle);
			AssetRegistry->OnAssetRenamed().Remove(State.AssetRenamedHandle);
			AssetRegistry->OnAssetUpdated().Remove(State.AssetUpdatedHandle);
			AssetRegistry->OnFilesLoaded().Remove(State.FilesLoadedHandle);
		}
		FModuleManager::Get().OnModulesChanged().Remove(State.ModulesChangedHandle);
		FCoreDelegates::OnPreExit.Remove(State.PreExitHandle);
		State.bSubscribed = false;
	}

	void Subscribe(IAssetRegistry& AssetRegistry)
	{
		FTypeIndex& State = Index();
		if (State.bSubscribed)
		{
			return;
		}
		State.AssetAddedHandle = AssetRegistry.OnAssetAdded().AddLambda([](const FAssetData&) { Index().bAssetsStale = true; });
		State.AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddLambda([](const FAssetData&) { Index().bAssetsStale = true; });
		State.AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddLambda([](const FAssetData&, const FString&) { Index().bAssetsStale = true; });
		State.AssetUpdatedHandle = AssetRegistry.OnAssetUpdated().AddLambda([](const FAssetData&) { Index().bAssetsStale = true; });
		State.FilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddLambda([]() { Index().bAssetsStale = true; });
		State.ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddLambda([](FName, EModuleChangeReason) { Index().bNativeStale = true; });
		// The lambdas live in this module: drop them before modules shut down
		State.PreExitHandle = FCoreDelegates::OnPreExit.AddLambda([]() { Unsubscribe(); });
		State.bSubscribed = true;
	}

	/** True when a native module's binary is under the engine directory (engine modules and engine plugins) */
	bool IsEngineModule(const FString& ModuleName, TMap<FString, bool>& Cache)
	{
		if (const bool* Known = Cache.Find(ModuleName))
		{
			return *Known;
		}
		// A module the manager does not know is compiled into the executable: the engine's
		bool bEngine = true;
		FModuleStatus Status;
		if (FModuleManager::Get().QueryModule(FName(*ModuleName), Status) && !Status.FilePath.IsEmpty())
		{
			bEngine = FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Status.FilePath),
				FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
		}
		Cache.Add(ModuleName, bEngine);
		return bEngine;
	}

	/**
	 * Classifies a type by where it comes from (mcp-tools-variables-23): /Script/<Module> by the module's location,
	 * /Engine content as engine, /Game as the project's, and another mount point by the plugin it belongs to. A
	 * project's own native types (/Script/MyGame) and its plugins' are not engine types.
	 */
	bool IsEngineTypePath(const FString& ObjectPath, TMap<FString, bool>& ModuleCache)
	{
		FString Rest = ObjectPath;
		Rest.RemoveFromStart(TEXT("/"));
		int32 End = INDEX_NONE;
		Rest.FindChar(TEXT('/'), End);
		const FString Root = End == INDEX_NONE ? Rest : Rest.Left(End);
		if (Root == TEXT("Script"))
		{
			FString Module = Rest.Mid(End + 1);
			int32 Dot = INDEX_NONE;
			if (Module.FindChar(TEXT('.'), Dot))
			{
				Module.LeftInline(Dot);
			}
			return IsEngineModule(Module, ModuleCache);
		}
		if (Root == TEXT("Engine"))
		{
			return true;
		}
		if (Root == TEXT("Game"))
		{
			return false;
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Root);
		return Plugin.IsValid() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Engine;
	}

	/** Engine-internal class copies a Blueprint compile leaves in memory, never a type to offer */
	bool IsCompilerCopy(const UObject* Object)
	{
		const FString Name = Object->GetName();
		return Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASH_"))
			|| Name.StartsWith(TEXT("HOTRELOADED_"));
	}

	void Finish(FTypeInfo& Info, bool bIsEngine)
	{
		Info.bIsEngine = bIsEngine;
		Info.LowerName = Info.TypeName.ToLower();
		Info.LowerDescription = Info.Description.ToLower();
	}

	/** An object path from a GeneratedClass or ParentClass tag, which holds export text (Class'/Game/BP.BP_C') */
	FString TagObjectPath(const FAssetData& AssetData, const FName Tag)
	{
		FString Value;
		if (!AssetData.GetTagValue(Tag, Value) || Value.IsEmpty() || Value == TEXT("None"))
		{
			return FString();
		}
		return FPackageName::ExportTextPathToObjectPath(Value);
	}
}

FMcpToolCallResult FN2CMcpSearchVariableTypesTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// The same focused Blueprint editor every focus-based tool uses (card 0150)
		const TSharedPtr<FBlueprintEditor> BlueprintEditor = FN2CBlueprintEditorTracker::Get().GetFocusedEditor();
		if (!BlueprintEditor.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("No active Blueprint editor found"));
		}

		// Parse arguments
		FN2CMcpArgumentParser ArgParser(Arguments);
		FString ErrorMsg;

		FString SearchTerm;
		if (!ArgParser.TryGetRequiredString(TEXT("searchTerm"), SearchTerm, ErrorMsg))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}

		if (SearchTerm.IsEmpty())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("searchTerm cannot be empty"));
		}

		// Optional parameters
		FString Category = ArgParser.GetOptionalString(TEXT("category"), TEXT("all"));
		bool bIncludeEngineTypes = ArgParser.GetOptionalBool(TEXT("includeEngineTypes"), true);
		int32 MaxResults = FMath::Clamp(ArgParser.GetOptionalInt(TEXT("maxResults"), 50), 1, 200);

		// Log the search request
		FN2CLogger& Logger = FN2CLogger::Get();
		Logger.Log(FString::Printf(TEXT("Searching variable types: '%s', Category: %s, MaxResults: %d"),
			*SearchTerm, *Category, MaxResults), EN2CLogSeverity::Info, TEXT("SearchVariableTypes"));

		// Every candidate once: primitives, the cached native and asset types, and user types loaded in memory that
		// the asset registry does not list (a new struct not yet saved, a Blueprint in a transient package)
		TArray<FVariableTypeInfo> Candidates;
		CollectPrimitiveTypes(Candidates);
		BuildTypeIndex();
		const N2CSearchVariableTypes::FTypeIndex& State = N2CSearchVariableTypes::Index();
		Candidates.Append(State.NativeTypes);
		Candidates.Append(State.AssetTypes);
		CollectLoadedUserTypes(Candidates);

		TArray<FVariableTypeInfo> Filtered;
		for (FVariableTypeInfo& Info : Candidates)
		{
			if ((Category == TEXT("all") || Info.Category == Category) && (bIncludeEngineTypes || !Info.bIsEngine))
			{
				Filtered.Add(MoveTemp(Info));
			}
		}

		// Filter by search term
		int32 TotalMatches = 0;
		TArray<FVariableTypeInfo> FilteredTypes = FilterTypesBySearchTerm(Filtered, SearchTerm, MaxResults, TotalMatches);

		// Build JSON result
		TSharedPtr<FJsonObject> Result = BuildJsonResult(FilteredTypes, TotalMatches);

		// Convert result to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		Logger.Log(FString::Printf(TEXT("Found %d variable types matching '%s', returned %d"),
			TotalMatches, *SearchTerm, FilteredTypes.Num()), EN2CLogSeverity::Info, TEXT("SearchVariableTypes"));

		return FMcpToolCallResult::CreateTextResult(OutputString);
	});
}

void FN2CMcpSearchVariableTypesTool::CollectPrimitiveTypes(TArray<FVariableTypeInfo>& OutTypes) const
{
	struct FPrimitive { const TCHAR* Name; const TCHAR* Identifier; const TCHAR* Description; const TCHAR* Icon; };
	static const FPrimitive Primitives[] = {
		{ TEXT("Boolean"), TEXT("bool"), TEXT("True/False value"), TEXT("boolean") },
		{ TEXT("Byte"), TEXT("uint8"), TEXT("8-bit unsigned integer (0-255)"), TEXT("byte") },
		{ TEXT("Integer"), TEXT("int32"), TEXT("32-bit signed integer"), TEXT("integer") },
		{ TEXT("Integer64"), TEXT("int64"), TEXT("64-bit signed integer"), TEXT("integer64") },
		{ TEXT("Float"), TEXT("float"), TEXT("Single precision decimal"), TEXT("float") },
		{ TEXT("Double"), TEXT("double"), TEXT("Double precision decimal"), TEXT("double") },
		{ TEXT("String"), TEXT("FString"), TEXT("Text string"), TEXT("string") },
		{ TEXT("Text"), TEXT("FText"), TEXT("Localized text"), TEXT("text") },
		{ TEXT("Name"), TEXT("FName"), TEXT("Lightweight name identifier"), TEXT("name") },
	};
	for (const FPrimitive& Primitive : Primitives)
	{
		FVariableTypeInfo Info;
		Info.TypeName = Primitive.Name;
		Info.TypeIdentifier = Primitive.Identifier;
		Info.Category = TEXT("primitive");
		Info.Description = Primitive.Description;
		Info.Icon = Primitive.Icon;
		N2CSearchVariableTypes::Finish(Info, /*bIsEngine: always offered*/ false);
		OutTypes.Add(MoveTemp(Info));
	}
}

void FN2CMcpSearchVariableTypesTool::BuildTypeIndex() const
{
	using namespace N2CSearchVariableTypes;
	FTypeIndex& State = Index();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Subscribe(AssetRegistry);
	TMap<FString, bool> ModuleCache;

	if (State.bNativeStale)
	{
		State.bNativeStale = false;
		State.NativeTypes.Reset();
		const double Start = FPlatformTime::Seconds();

		// Native classes: Blueprint and Python classes come from the asset registry or CollectLoadedUserTypes
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const UClass* Class = *It;
			if (!Class->HasAnyClassFlags(CLASS_Native) || Class->HasAnyClassFlags(CLASS_NewerVersionExists)
				|| !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Class))
			{
				continue;
			}
			FVariableTypeInfo Info;
			Info.TypeName = Class->GetDisplayNameText().ToString();
			if (Info.TypeName.IsEmpty())
			{
				Info.TypeName = Class->GetName();
			}
			Info.TypeIdentifier = Class->GetPathName();
			Info.Category = TEXT("class");
			Info.Description = GetTypeDescription(Class);
			Info.bIsAbstract = Class->HasAnyClassFlags(CLASS_Abstract);
			if (Class->GetSuperClass())
			{
				Info.ParentClass = Class->GetSuperClass()->GetPathName();
			}
			Finish(Info, IsEngineTypePath(Info.TypeIdentifier, ModuleCache));
			State.NativeTypes.Add(MoveTemp(Info));
		}

		// Native structs and enums: those in compiled-in packages; user structs and enums are assets
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			const UScriptStruct* Struct = *It;
			if (!Struct->GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn) || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
			{
				continue;
			}
			FVariableTypeInfo Info;
			Info.TypeName = Struct->GetDisplayNameText().ToString();
			if (Info.TypeName.IsEmpty())
			{
				Info.TypeName = Struct->GetName();
			}
			Info.TypeIdentifier = Struct->GetPathName();
			Info.Category = TEXT("struct");
			Info.Description = GetTypeDescription(Struct);
			Finish(Info, IsEngineTypePath(Info.TypeIdentifier, ModuleCache));
			State.NativeTypes.Add(MoveTemp(Info));
		}
		for (TObjectIterator<UEnum> It; It; ++It)
		{
			const UEnum* Enum = *It;
			if (!Enum->GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn) || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
			{
				continue;
			}
			FVariableTypeInfo Info;
			Info.TypeName = Enum->GetDisplayNameText().ToString();
			if (Info.TypeName.IsEmpty())
			{
				Info.TypeName = Enum->GetName();
			}
			Info.TypeIdentifier = Enum->GetPathName();
			Info.Category = TEXT("enum");
			Info.Description = GetTypeDescription(Enum);
			Finish(Info, IsEngineTypePath(Info.TypeIdentifier, ModuleCache));
			State.NativeTypes.Add(MoveTemp(Info));
		}
		FN2CLogger::Get().Log(FString::Printf(TEXT("Indexed %d native variable types in %.0f ms"),
			State.NativeTypes.Num(), (FPlatformTime::Seconds() - Start) * 1000.0), EN2CLogSeverity::Debug, TEXT("SearchVariableTypes"));
	}

	if (State.bAssetsStale)
	{
		State.bAssetsStale = false;
		State.AssetTypes.Reset();
		TSet<FString> Seen;

		// Class paths from the classes themselves: UserDefinedStruct is in CoreUObject since 5.5, and the asset
		// registry applies no class redirects to a filter (mcp-tools-variables-17). Subclasses of UBlueprint
		// (Widget and Anim Blueprints) are included. Nothing is loaded.
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses*/ true);
		for (const FAssetData& AssetData : Assets)
		{
			FVariableTypeInfo Info;
			Info.TypeName = AssetData.AssetName.ToString();
			Info.TypeIdentifier = AssetData.GetObjectPathString();
			Info.Category = TEXT("struct");
			Info.Description = TEXT("User-defined struct");
			if (!Seen.Contains(Info.TypeIdentifier))
			{
				Seen.Add(Info.TypeIdentifier);
				Finish(Info, IsEngineTypePath(Info.TypeIdentifier, ModuleCache));
				State.AssetTypes.Add(MoveTemp(Info));
			}
		}

		Assets.Reset();
		AssetRegistry.GetAssetsByClass(UUserDefinedEnum::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses*/ true);
		for (const FAssetData& AssetData : Assets)
		{
			FVariableTypeInfo Info;
			Info.TypeName = AssetData.AssetName.ToString();
			Info.TypeIdentifier = AssetData.GetObjectPathString();
			Info.Category = TEXT("enum");
			Info.Description = TEXT("User-defined enumeration");
			if (!Seen.Contains(Info.TypeIdentifier))
			{
				Seen.Add(Info.TypeIdentifier);
				Finish(Info, IsEngineTypePath(Info.TypeIdentifier, ModuleCache));
				State.AssetTypes.Add(MoveTemp(Info));
			}
		}

		Assets.Reset();
		AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses*/ true);
		for (const FAssetData& AssetData : Assets)
		{
			// The generated class's object path (/Game/BP_Enemy.BP_Enemy_C), the form the type resolver accepts;
			// the tag itself holds export text (mcp-tools-variables-23)
			FVariableTypeInfo Info;
			Info.TypeIdentifier = TagObjectPath(AssetData, FBlueprintTags::GeneratedClassPath);
			if (Info.TypeIdentifier.IsEmpty() || Seen.Contains(Info.TypeIdentifier))
			{
				continue;
			}
			// Macro libraries, function libraries and level scripts have a generated class, but no variable can
			// have it as type (UEdGraphSchema_K2::IsAllowableBlueprintVariableType refuses them); the tag says so
			// without loading. A loaded class is checked directly.
			FString BlueprintType;
			if (AssetData.GetTagValue(FBlueprintTags::BlueprintType, BlueprintType)
				&& (BlueprintType.Contains(TEXT("MacroLibrary")) || BlueprintType.Contains(TEXT("FunctionLibrary")) || BlueprintType.Contains(TEXT("LevelScript"))))
			{
				continue;
			}
			if (const UClass* LoadedClass = FindObject<UClass>(nullptr, *Info.TypeIdentifier))
			{
				if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(LoadedClass))
				{
					continue;
				}
			}
			Info.TypeName = AssetData.AssetName.ToString();
			Info.Category = TEXT("class");
			Info.Description = TEXT("Blueprint class");
			Info.ParentClass = TagObjectPath(AssetData, FBlueprintTags::ParentClassPath);
			Seen.Add(Info.TypeIdentifier);
			Finish(Info, IsEngineTypePath(Info.TypeIdentifier, ModuleCache));
			State.AssetTypes.Add(MoveTemp(Info));
		}

		// While the first scan runs, every search rebuilds the asset part; the added-asset events say so too
		if (AssetRegistry.IsLoadingAssets())
		{
			State.bAssetsStale = true;
		}
	}
}

void FN2CMcpSearchVariableTypesTool::CollectLoadedUserTypes(TArray<FVariableTypeInfo>& InOutTypes) const
{
	using namespace N2CSearchVariableTypes;
	TSet<FString> Seen;
	Seen.Reserve(InOutTypes.Num());
	for (const FVariableTypeInfo& Info : InOutTypes)
	{
		Seen.Add(Info.TypeIdentifier);
	}
	TMap<FString, bool> ModuleCache;
	UPackage* const TransientPackage = GetTransientPackage();

	auto Add = [&](const UObject* Object, const TCHAR* Category, FVariableTypeInfo&& Info)
	{
		Info.TypeIdentifier = Object->GetPathName();
		if (Seen.Contains(Info.TypeIdentifier))
		{
			return;
		}
		Seen.Add(Info.TypeIdentifier);
		if (Info.TypeName.IsEmpty())
		{
			Info.TypeName = Object->GetName();
		}
		Info.Category = Category;
		Finish(Info, IsEngineTypePath(Info.TypeIdentifier, ModuleCache));
		InOutTypes.Add(MoveTemp(Info));
	};

	for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
	{
		const UUserDefinedStruct* Struct = *It;
		if (Struct->GetOutermost() == TransientPackage || IsCompilerCopy(Struct) || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
		{
			continue;
		}
		FVariableTypeInfo Info;
		Info.TypeName = Struct->GetName();
		Info.Description = TEXT("User-defined struct");
		Add(Struct, TEXT("struct"), MoveTemp(Info));
	}
	for (TObjectIterator<UUserDefinedEnum> It; It; ++It)
	{
		const UUserDefinedEnum* Enum = *It;
		if (Enum->GetOutermost() == TransientPackage || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
		{
			continue;
		}
		FVariableTypeInfo Info;
		Info.TypeName = Enum->GetName();
		Info.Description = TEXT("User-defined enumeration");
		Add(Enum, TEXT("enum"), MoveTemp(Info));
	}
	for (TObjectIterator<UBlueprintGeneratedClass> It; It; ++It)
	{
		const UBlueprintGeneratedClass* Class = *It;
		if (Class->GetOutermost() == TransientPackage || IsCompilerCopy(Class) || Class->HasAnyClassFlags(CLASS_NewerVersionExists)
			|| !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Class))
		{
			continue;
		}
		FVariableTypeInfo Info;
		const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
		Info.TypeName = Blueprint ? Blueprint->GetName() : Class->GetName();
		Info.Description = TEXT("Blueprint class");
		Info.bIsAbstract = Class->HasAnyClassFlags(CLASS_Abstract);
		if (Class->GetSuperClass())
		{
			Info.ParentClass = Class->GetSuperClass()->GetPathName();
		}
		Add(Class, TEXT("class"), MoveTemp(Info));
	}
}

TArray<FN2CMcpSearchVariableTypesTool::FVariableTypeInfo> FN2CMcpSearchVariableTypesTool::FilterTypesBySearchTerm(
	const TArray<FVariableTypeInfo>& AllTypes, const FString& SearchTerm, int32 MaxResults, int32& OutTotalMatches) const
{
	TArray<FVariableTypeInfo> Results;

	// Tokenize search term for better matching
	TArray<FString> SearchTokens;
	SearchTerm.ParseIntoArray(SearchTokens, TEXT(" "), true);

	// Convert tokens to lowercase for case-insensitive search
	for (FString& Token : SearchTokens)
	{
		Token = Token.ToLower();
	}

	// Score-based filtering for better results
	struct FScoredType
	{
		const FVariableTypeInfo* TypeInfo;
		int32 Score;
	};
	TArray<FScoredType> ScoredTypes;

	for (const FVariableTypeInfo& TypeInfo : AllTypes)
	{
		const FString& LowerTypeName = TypeInfo.LowerName;
		const FString& LowerDescription = TypeInfo.LowerDescription;

		int32 Score = 0;
		bool bMatchesAll = true;

		// Check if all search tokens match
		for (const FString& Token : SearchTokens)
		{
			bool bTokenFound = false;

			// Exact match in type name gets highest score
			if (LowerTypeName == Token)
			{
				Score += 100;
				bTokenFound = true;
			}
			// Type name starts with token
			else if (LowerTypeName.StartsWith(Token))
			{
				Score += 50;
				bTokenFound = true;
			}
			// Type name contains token
			else if (LowerTypeName.Contains(Token))
			{
				Score += 25;
				bTokenFound = true;
			}
			// Description contains token
			else if (LowerDescription.Contains(Token))
			{
				Score += 10;
				bTokenFound = true;
			}

			if (!bTokenFound)
			{
				bMatchesAll = false;
				break;
			}
		}

		if (bMatchesAll && Score > 0)
		{
			ScoredTypes.Add({&TypeInfo, Score});
		}
	}

	// Sort by score (highest first), then by name so equal scores come in a stable order
	ScoredTypes.Sort([](const FScoredType& A, const FScoredType& B)
	{
		return A.Score != B.Score ? A.Score > B.Score : A.TypeInfo->LowerName < B.TypeInfo->LowerName;
	});

	// Every match is counted; the top MaxResults are returned
	OutTotalMatches = ScoredTypes.Num();
	for (int32 i = 0; i < FMath::Min(ScoredTypes.Num(), MaxResults); ++i)
	{
		Results.Add(*ScoredTypes[i].TypeInfo);
	}

	return Results;
}

TSharedPtr<FJsonObject> FN2CMcpSearchVariableTypesTool::BuildJsonResult(const TArray<FVariableTypeInfo>& FilteredTypes, int32 TotalMatches) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> TypesArray;

	for (const FVariableTypeInfo& TypeInfo : FilteredTypes)
	{
		TSharedPtr<FJsonObject> TypeObject = MakeShareable(new FJsonObject);

		TypeObject->SetStringField(TEXT("typeName"), TypeInfo.TypeName);
		TypeObject->SetStringField(TEXT("typeIdentifier"), TypeInfo.TypeIdentifier);
		TypeObject->SetStringField(TEXT("category"), TypeInfo.Category);
		TypeObject->SetStringField(TEXT("description"), TypeInfo.Description);

		if (!TypeInfo.Icon.IsEmpty())
		{
			TypeObject->SetStringField(TEXT("icon"), TypeInfo.Icon);
		}

		if (TypeInfo.Category == TEXT("class"))
		{
			if (!TypeInfo.ParentClass.IsEmpty())
			{
				TypeObject->SetStringField(TEXT("parentClass"), TypeInfo.ParentClass);
			}
			TypeObject->SetBoolField(TEXT("isAbstract"), TypeInfo.bIsAbstract);
		}
		else if (TypeInfo.Category == TEXT("enum"))
		{
			// Enumerators only for the enums returned: a user enum is loaded here, never while searching.
			// values are the names a default value takes (NewEnumerator0); displayNames what the editor shows.
			const UEnum* Enum = FindObject<UEnum>(nullptr, *TypeInfo.TypeIdentifier);
			if (!Enum && !FPackageName::IsScriptPackage(TypeInfo.TypeIdentifier))
			{
				Enum = LoadObject<UEnum>(nullptr, *TypeInfo.TypeIdentifier, nullptr, LOAD_NoWarn);
			}
			if (Enum)
			{
				TArray<TSharedPtr<FJsonValue>> Values;
				TArray<TSharedPtr<FJsonValue>> DisplayNames;
				for (int32 i = 0; i < Enum->NumEnums() - 1; ++i) // -1 to skip MAX value
				{
					Values.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(i)));
					DisplayNames.Add(MakeShared<FJsonValueString>(Enum->GetDisplayNameTextByIndex(i).ToString()));
				}
				TypeObject->SetArrayField(TEXT("values"), Values);
				TypeObject->SetArrayField(TEXT("displayNames"), DisplayNames);
			}
		}

		TypesArray.Add(MakeShareable(new FJsonValueObject(TypeObject)));
	}

	Result->SetArrayField(TEXT("types"), TypesArray);
	// Every match, before maxResults cut the list; truncated says the list was cut
	Result->SetNumberField(TEXT("totalMatches"), TotalMatches);
	Result->SetBoolField(TEXT("truncated"), TotalMatches > FilteredTypes.Num());

	return Result;
}

FString FN2CMcpSearchVariableTypesTool::GetTypeDescription(const UObject* TypeObject) const
{
	if (!TypeObject)
	{
		return TEXT("");
	}

	if (const UClass* Class = Cast<UClass>(TypeObject))
	{
		// Try to get tooltip metadata
		if (Class->HasMetaData(TEXT("ToolTip")))
		{
			return Class->GetMetaData(TEXT("ToolTip"));
		}
		
		// Default descriptions for common classes
		if (Class == AActor::StaticClass())
		{
			return TEXT("Base class for all Actors that can be placed in a level");
		}
		else if (Class == UActorComponent::StaticClass())
		{
			return TEXT("Base class for components that can be attached to Actors");
		}
		
		return FString::Printf(TEXT("%s class"), Class->IsNative() ? TEXT("Native") : TEXT("Blueprint"));
	}
	else if (const UScriptStruct* Struct = Cast<UScriptStruct>(TypeObject))
	{
		if (Struct->HasMetaData(TEXT("ToolTip")))
		{
			return Struct->GetMetaData(TEXT("ToolTip"));
		}
		
		// Common struct descriptions
		FString StructName = Struct->GetName();
		if (StructName == TEXT("Vector"))
		{
			return TEXT("3D vector with X, Y, Z components");
		}
		else if (StructName == TEXT("Rotator"))
		{
			return TEXT("Rotation in 3D space (Pitch, Yaw, Roll)");
		}
		else if (StructName == TEXT("Transform"))
		{
			return TEXT("3D transformation (Location, Rotation, Scale)");
		}
		
		return TEXT("Structure");
	}
	else if (const UEnum* Enum = Cast<UEnum>(TypeObject))
	{
		if (Enum->HasMetaData(TEXT("ToolTip")))
		{
			return Enum->GetMetaData(TEXT("ToolTip"));
		}
		
		return TEXT("Enumeration");
	}

	return TEXT("");
}