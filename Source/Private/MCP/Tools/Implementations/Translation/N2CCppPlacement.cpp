// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CCppPlacement.h"

#include "Utils/N2CFileUtils.h"
#include "Utils/N2CPathUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProjectDescriptor.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace N2CCppPlacementPrivate
{
	bool IsIdentifierChar(TCHAR Ch)
	{
		return FChar::IsAlnum(Ch) || Ch == TEXT('_');
	}

	bool IsIdentifier(const FString& Name)
	{
		if (Name.IsEmpty() || Name.Len() > 64 || FChar::IsDigit(Name[0]))
		{
			return false;
		}
		for (const TCHAR Ch : Name)
		{
			if (!IsIdentifierChar(Ch))
			{
				return false;
			}
		}
		return true;
	}

	/** Every identifier X written as "X::" in Text, in order of first use */
	TArray<FString> FindQualifiers(const FString& Text)
	{
		TArray<FString> Out;
		int32 Index = 0;
		while ((Index = Text.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index)) != INDEX_NONE)
		{
			int32 Start = Index;
			while (Start > 0 && IsIdentifierChar(Text[Start - 1]))
			{
				--Start;
			}
			if (Start < Index && (Start == 0 || Text[Start - 1] != TEXT(':')))
			{
				Out.AddUnique(Text.Mid(Start, Index - Start));
			}
			Index += 2;
		}
		return Out;
	}

	/**
	 * The class qualifiers of the function definitions in Implementation: a line at column 0 that does not end in ';' and
	 * has Qualifier::Name( in it
	 */
	TArray<FString> FindDefinitionQualifiers(const FString& Implementation)
	{
		TArray<FString> Out;
		TArray<FString> Lines;
		Implementation.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
		for (const FString& Line : Lines)
		{
			if (Line.IsEmpty() || FChar::IsWhitespace(Line[0]) || Line.TrimEnd().EndsWith(TEXT(";")) || Line.StartsWith(TEXT("#"))
				|| Line.StartsWith(TEXT("//")))
			{
				continue;
			}
			const int32 Colons = Line.Find(TEXT("::"), ESearchCase::CaseSensitive);
			const int32 Paren = Line.Find(TEXT("("), ESearchCase::CaseSensitive);
			if (Colons == INDEX_NONE || Paren == INDEX_NONE || Colons > Paren)
			{
				continue;
			}
			int32 Start = Colons;
			while (Start > 0 && IsIdentifierChar(Line[Start - 1]))
			{
				--Start;
			}
			if (Start < Colons)
			{
				Out.AddUnique(Line.Mid(Start, Colons - Start));
			}
		}
		return Out;
	}

	/** Replaces "From::" with "To::" where From is a whole identifier */
	FString RenameQualifier(const FString& Text, const FString& From, const FString& To)
	{
		FString Out;
		Out.Reserve(Text.Len());
		const FString Needle = From + TEXT("::");
		int32 Index = 0;
		while (true)
		{
			const int32 Found = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
			if (Found == INDEX_NONE)
			{
				Out.Append(Text.Mid(Index));
				break;
			}
			const bool bWholeWord = Found == 0 || !IsIdentifierChar(Text[Found - 1]);
			Out.Append(Text.Mid(Index, Found - Index));
			Out.Append(bWholeWord ? To + TEXT("::") : Needle);
			Index = Found + Needle.Len();
		}
		return Out;
	}

	FString Indent(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArray(Lines, TEXT("\n"), /*bCullEmpty*/ false);
		for (FString& Line : Lines)
		{
			Line.RemoveFromEnd(TEXT("\r"));
			if (!Line.TrimStartAndEnd().IsEmpty())
			{
				Line = TEXT("\t") + Line;
			}
		}
		return FString::Join(Lines, TEXT("\n")).TrimEnd();
	}

	/** The include path UHT uses for a native type: IncludePath metadata, else ModuleRelativePath without Public/Classes/Private */
	FString IncludeOf(const UStruct* Type)
	{
#if WITH_METADATA
		if (!Type)
		{
			return FString();
		}
		FString Include = Type->GetMetaData(TEXT("IncludePath"));
		if (Include.IsEmpty())
		{
			Include = Type->GetMetaData(TEXT("ModuleRelativePath"));
			for (const TCHAR* Prefix : { TEXT("Public/"), TEXT("Classes/"), TEXT("Private/") })
			{
				if (Include.RemoveFromStart(Prefix))
				{
					break;
				}
			}
		}
		return Include;
#else
		return FString();
#endif
	}

	/** "Engine" for a type in /Script/Engine */
	FString ModuleOf(const UObject* Type)
	{
		FString Package = Type ? Type->GetOutermost()->GetName() : FString();
		return Package.RemoveFromStart(TEXT("/Script/")) ? Package : FString();
	}

	/** The C++ name of a native class or struct: AActor, UKismetSystemLibrary, FVector */
	FString CppNameOf(const UStruct* Type)
	{
		if (const UClass* Class = Cast<UClass>(Type))
		{
			return (Class->IsChildOf(AActor::StaticClass()) ? TEXT("A") : TEXT("U")) + Class->GetName();
		}
		return Type ? TEXT("F") + Type->GetName() : FString();
	}

	/** A native class or struct named Name: as given, without a one-letter C++ prefix, or by object path */
	const UStruct* FindNativeType(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}
		auto Find = [](const FString& Candidate) -> const UStruct*
		{
			if (Candidate.StartsWith(TEXT("/Script/")))
			{
				if (const UStruct* ByPath = FindObject<UStruct>(nullptr, *Candidate))
				{
					return ByPath;
				}
				return nullptr;
			}
			if (const UClass* Class = FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::NativeFirst))
			{
				return Class->HasAnyClassFlags(CLASS_Native) ? Class : nullptr;
			}
			if (const UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*Candidate, EFindFirstObjectOptions::NativeFirst))
			{
				return Struct->IsNative() ? Struct : nullptr;
			}
			return nullptr;
		};
		if (const UStruct* Found = Find(Name))
		{
			return Found;
		}
		if (Name.Len() > 1 && (Name[0] == TEXT('U') || Name[0] == TEXT('A') || Name[0] == TEXT('F')) && FChar::IsUpper(Name[1]))
		{
			return Find(Name.Mid(1));
		}
		return nullptr;
	}

	/** A subfolder below Public/ and Private/: empty, or segments of letters, digits and _ (at most 64 each) joined by / */
	bool IsSafeSubfolder(const FString& Subfolder)
	{
		if (Subfolder.IsEmpty())
		{
			return true;
		}
		TArray<FString> Segments;
		Subfolder.ParseIntoArray(Segments, TEXT("/"), /*bCullEmpty*/ false);
		for (const FString& Segment : Segments)
		{
			if (Segment.IsEmpty() || Segment.Len() > 64)
			{
				return false;
			}
			for (const TCHAR Ch : Segment)
			{
				if (!IsIdentifierChar(Ch))
				{
					return false;
				}
			}
		}
		return true;
	}
}

bool FN2CCppPlacement::IsInside(const FString& Path, const FString& Dir)
{
	FString FullPath = FPaths::ConvertRelativePathToFull(Path);
	FString FullDir = FPaths::ConvertRelativePathToFull(Dir);
	FPaths::NormalizeFilename(FullPath);
	FPaths::NormalizeDirectoryName(FullDir);
	if (!FPaths::CollapseRelativeDirectories(FullPath) || !FPaths::CollapseRelativeDirectories(FullDir))
	{
		return false;
	}
	return FullPath.StartsWith(FullDir + TEXT("/"), ESearchCase::IgnoreCase) && FN2CPathUtils::IsUnderDirectoryResolved(FullPath, FullDir)
		&& !FN2CPathUtils::IsDeniedForAgentAccess(FullPath);
}

bool FN2CCppPlacement::FindModule(const FString& ModuleName, FString& OutSourceDir, FString& OutError)
{
	using namespace N2CCppPlacementPrivate;
	if (!IsIdentifier(ModuleName))
	{
		OutError = FString::Printf(TEXT("'%s' is not a module name"), *ModuleName.Left(80));
		return false;
	}

	TArray<FString> Known;
	// Name: the descriptor's spelling, so the folder, the Build.cs and the _API macro match on case-sensitive file systems
	auto Accept = [&](const FString& BaseSourceDir, const FString& Name) -> bool
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(BaseSourceDir, Name));
		if (FPaths::FileExists(FPaths::Combine(Dir, Name + TEXT(".Build.cs"))))
		{
			OutSourceDir = Dir;
			return true;
		}
		OutError = FString::Printf(TEXT("Module '%s' has no %s.Build.cs in %s"), *Name, *Name, *Dir);
		return false;
	};

	if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
	{
		for (const FModuleDescriptor& Module : Project->Modules)
		{
			Known.Add(Module.Name.ToString());
			if (Module.Name.ToString().Equals(ModuleName, ESearchCase::IgnoreCase))
			{
				return Accept(FPaths::GameSourceDir(), Module.Name.ToString());
			}
		}
	}
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
	{
		if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Project)
		{
			continue;
		}
		for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
		{
			Known.Add(Module.Name.ToString());
			if (Module.Name.ToString().Equals(ModuleName, ESearchCase::IgnoreCase))
			{
				if (Plugin->GetName() == TEXT("NodeToCode"))
				{
					OutError = TEXT("NodeToCode's own module cannot be a target");
					return false;
				}
				return Accept(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source")), Module.Name.ToString());
			}
		}
	}
	Known.Remove(TEXT("NodeToCode"));
	OutError = FString::Printf(TEXT("'%s' is not a module of this project or of a plugin in it (engine and marketplace modules are never written to). Modules: %s"),
		*ModuleName, Known.Num() > 0 ? *FString::Join(Known, TEXT(", ")) : TEXT("none (a Blueprint-only project has no C++ module: add one with Tools > New C++ Class first)"));
	return false;
}

bool FN2CCppPlacement::ResolveType(const FString& TypeName, FString& OutInclude, FString& OutModule, FString* OutCppName)
{
	using namespace N2CCppPlacementPrivate;
	const UStruct* Type = FindNativeType(TypeName);
	if (!Type)
	{
		return false;
	}
	OutInclude = IncludeOf(Type);
	OutModule = ModuleOf(Type);
	if (OutCppName)
	{
		*OutCppName = CppNameOf(Type);
	}
	return !OutInclude.IsEmpty() && !OutModule.IsEmpty();
}

FString FN2CCppPlacement::FindBlueprintNativeParent(const FString& BlueprintName, FString& OutError)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses*/ true);
	TArray<FAssetData> Matches = Assets.FilterByPredicate([&BlueprintName](const FAssetData& Asset)
	{
		return Asset.AssetName.ToString() == BlueprintName;
	});
	if (Matches.Num() != 1)
	{
		OutError = Matches.Num() == 0
			? FString::Printf(TEXT("No Blueprint named '%s' was found to take its parent class from; pass parent_class"), *BlueprintName)
			: FString::Printf(TEXT("%d Blueprints are named '%s'; pass parent_class"), Matches.Num(), *BlueprintName);
		return FString();
	}
	const UBlueprint* Blueprint = Cast<UBlueprint>(Matches[0].GetAsset());
	const UClass* Class = Blueprint ? Blueprint->ParentClass.Get() : nullptr;
	while (Class && !Class->HasAnyClassFlags(CLASS_Native))
	{
		Class = Class->GetSuperClass();
	}
	if (!Class)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' has no native parent class; pass parent_class"), *BlueprintName);
		return FString();
	}
	return Class->GetPathName();
}

TSet<FString> FN2CCppPlacement::GetListedModules(const FString& BuildCs)
{
	TSet<FString> Out;
	int32 Index = 0;
	while ((Index = BuildCs.Find(TEXT("DependencyModuleNames"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index)) != INDEX_NONE)
	{
		const int32 End = BuildCs.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
		const FString Statement = BuildCs.Mid(Index, End == INDEX_NONE ? BuildCs.Len() - Index : End - Index);
		int32 Quote = 0;
		while ((Quote = Statement.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Quote)) != INDEX_NONE)
		{
			const int32 Close = Statement.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Quote + 1);
			if (Close == INDEX_NONE)
			{
				break;
			}
			Out.Add(Statement.Mid(Quote + 1, Close - Quote - 1));
			Quote = Close + 1;
		}
		Index = End == INDEX_NONE ? BuildCs.Len() : End;
	}
	return Out;
}

bool FN2CCppPlacement::AddModulesToBuildCs(const FString& BuildCs, const TArray<FString>& Modules, FString& OutBuildCs, FString& OutError)
{
	if (Modules.Num() == 0)
	{
		OutBuildCs = BuildCs;
		return true;
	}
	for (const TCHAR* List : { TEXT("PrivateDependencyModuleNames.AddRange("), TEXT("PublicDependencyModuleNames.AddRange(") })
	{
		const int32 At = BuildCs.Find(List, ESearchCase::CaseSensitive);
		if (At == INDEX_NONE)
		{
			continue;
		}
		const int32 Open = BuildCs.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
		const int32 Close = Open == INDEX_NONE ? INDEX_NONE : BuildCs.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
		const int32 Semicolon = BuildCs.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
		if (Open == INDEX_NONE || Close == INDEX_NONE || (Semicolon != INDEX_NONE && Close > Semicolon))
		{
			continue;
		}
		// Insert after the last entry, keeping a trailing comma style when the list has one
		int32 Insert = Close;
		while (Insert > Open + 1 && FChar::IsWhitespace(BuildCs[Insert - 1]))
		{
			--Insert;
		}
		const bool bTrailingComma = BuildCs[Insert - 1] == TEXT(',');
		const bool bEmpty = Insert == Open + 1;
		TArray<FString> Quoted;
		for (const FString& Module : Modules)
		{
			Quoted.Add(FString::Printf(TEXT("\"%s\""), *Module));
		}
		const FString Added = bEmpty || bTrailingComma
			? TEXT(" ") + FString::Join(Quoted, TEXT(", ")) + (bTrailingComma ? TEXT(",") : TEXT(" "))
			: TEXT(", ") + FString::Join(Quoted, TEXT(", "));
		OutBuildCs = BuildCs.Left(Insert) + Added + BuildCs.Mid(Insert);
		return true;
	}
	OutError = TEXT("the Build.cs has no PrivateDependencyModuleNames.AddRange(...) or PublicDependencyModuleNames.AddRange(...) list to add to; add the modules by hand");
	return false;
}

bool FN2CCppPlacement::BuildPlan(const FN2CCppPlacementRequest& Request, FN2CCppPlacementPlan& OutPlan, FString& OutError)
{
	using namespace N2CCppPlacementPrivate;
	OutPlan = FN2CCppPlacementPlan();

	// The module
	OutPlan.ModuleName = Request.ModuleName;
	if (!Request.ModuleSourceDirOverride.IsEmpty())
	{
		OutPlan.ModuleSourceDir = FPaths::ConvertRelativePathToFull(Request.ModuleSourceDirOverride);
	}
	else if (!FindModule(Request.ModuleName, OutPlan.ModuleSourceDir, OutError))
	{
		return false;
	}
	FPaths::NormalizeDirectoryName(OutPlan.ModuleSourceDir);
	if (Request.ModuleSourceDirOverride.IsEmpty())
	{
		OutPlan.ModuleName = FPaths::GetCleanFilename(OutPlan.ModuleSourceDir);
	}
	OutPlan.BuildCsPath = FPaths::Combine(OutPlan.ModuleSourceDir, OutPlan.ModuleName + TEXT(".Build.cs"));
	FString BuildCs;
	if (!FFileHelper::LoadFileToString(BuildCs, *OutPlan.BuildCsPath))
	{
		OutError = FString::Printf(TEXT("Cannot read %s"), *OutPlan.BuildCsPath);
		return false;
	}
	if (!IsSafeSubfolder(Request.Subfolder))
	{
		OutError = FString::Printf(TEXT("subfolder '%s' is not a relative folder of letters, digits, _ and /"), *Request.Subfolder.Left(80));
		return false;
	}

	// The parent class
	FString ParentName = Request.ParentClass;
	if (ParentName.IsEmpty())
	{
		const FString BlueprintName = Request.Blueprint.IsSet() ? Request.Blueprint->Metadata.Name : FString();
		if (BlueprintName.IsEmpty())
		{
			OutError = TEXT("The translation does not name its Blueprint; pass parent_class");
			return false;
		}
		ParentName = FindBlueprintNativeParent(BlueprintName, OutError);
		if (ParentName.IsEmpty())
		{
			return false;
		}
	}
	const UClass* Parent = Cast<UClass>(FindNativeType(ParentName));
	if (!Parent)
	{
		OutError = FString::Printf(TEXT("parent_class '%s' is not a native class"), *ParentName.Left(120));
		return false;
	}
	OutPlan.ParentClassName = CppNameOf(Parent);
	OutPlan.ParentInclude = IncludeOf(Parent);
	const TCHAR* Prefix = Parent->IsChildOf(AActor::StaticClass()) ? TEXT("A") : TEXT("U");

	// The class name: MyActor or AMyActor
	FString Stem = Request.ClassName;
	if (Stem.Len() > 1 && Stem[0] == Prefix[0] && FChar::IsUpper(Stem[1]))
	{
		Stem.RightChopInline(1);
	}
	if (!IsIdentifier(Stem))
	{
		OutError = FString::Printf(TEXT("class_name '%s' is not a C++ identifier"), *Request.ClassName.Left(80));
		return false;
	}
	OutPlan.ClassName = Prefix + Stem;

	// Files
	const FString Sub = Request.Subfolder;
	const bool bSplit = FPaths::DirectoryExists(FPaths::Combine(OutPlan.ModuleSourceDir, TEXT("Public")))
		&& FPaths::DirectoryExists(FPaths::Combine(OutPlan.ModuleSourceDir, TEXT("Private")));
	const FString HeaderDir = FPaths::Combine(OutPlan.ModuleSourceDir, bSplit ? TEXT("Public") : TEXT(""), Sub);
	const FString SourceDir = FPaths::Combine(OutPlan.ModuleSourceDir, bSplit ? TEXT("Private") : TEXT(""), Sub);
	OutPlan.Header.Path = FPaths::Combine(HeaderDir, Stem + TEXT(".h"));
	OutPlan.Source.Path = FPaths::Combine(SourceDir, Stem + TEXT(".cpp"));
	for (FN2CPlacedFile* File : { &OutPlan.Header, &OutPlan.Source })
	{
		FPaths::NormalizeFilename(File->Path);
		if (!IsInside(File->Path, OutPlan.ModuleSourceDir))
		{
			OutError = FString::Printf(TEXT("Refused: %s is outside the module's Source folder %s"), *File->Path, *OutPlan.ModuleSourceDir);
			return false;
		}
		File->bExists = FPaths::FileExists(File->Path);
	}
	const FString HeaderInclude = Sub.IsEmpty() ? Stem + TEXT(".h") : Sub + TEXT("/") + Stem + TEXT(".h");

	// The code: struct and enum entries before the class, graph declarations inside it, implementations in the .cpp
	TArray<FString> TopDeclarations;
	TArray<FString> ClassDeclarations;
	TArray<FString> Implementations;
	TArray<FString> Notes;
	for (const FN2CGraphTranslation& Graph : Request.Translation.Graphs)
	{
		const FString Declaration = Graph.Code.GraphDeclaration.TrimStartAndEnd();
		const FString Implementation = Graph.Code.GraphImplementation.TrimStartAndEnd();
		const bool bType = Graph.GraphType.Equals(TEXT("Struct"), ESearchCase::IgnoreCase) || Graph.GraphType.Equals(TEXT("Enum"), ESearchCase::IgnoreCase);
		if (!Declaration.IsEmpty())
		{
			(bType ? TopDeclarations : ClassDeclarations).Add(FString::Printf(TEXT("// %s (%s)\n%s"), *Graph.GraphName, *Graph.GraphType, *Declaration));
		}
		if (!Implementation.IsEmpty())
		{
			Implementations.Add(FString::Printf(TEXT("// %s (%s)\n%s"), *Graph.GraphName, *Graph.GraphType, *Implementation));
		}
		if (!Graph.Code.ImplementationNotes.TrimStartAndEnd().IsEmpty())
		{
			Notes.Add(FString::Printf(TEXT("%s: %s"), *Graph.GraphName, *Graph.Code.ImplementationNotes.TrimStartAndEnd()));
		}
	}
	if (ClassDeclarations.Num() == 0 && Implementations.Num() == 0 && TopDeclarations.Num() == 0)
	{
		OutError = TEXT("The translation has no C++ declarations or implementations to place");
		return false;
	}
	FString ImplementationText = FString::Join(Implementations, TEXT("\n\n"));

	// Definitions qualified with a class that is not a native type (the Blueprint's class) become the new class's
	for (const FString& Qualifier : FindDefinitionQualifiers(ImplementationText))
	{
		FString Include, Module;
		if (Qualifier != OutPlan.ClassName && !ResolveType(Qualifier, Include, Module))
		{
			ImplementationText = RenameQualifier(ImplementationText, Qualifier, OutPlan.ClassName);
			OutPlan.Warnings.Add(FString::Printf(TEXT("Definitions of %s:: were renamed to %s::"), *Qualifier, *OutPlan.ClassName));
		}
	}

	// Includes and modules: every node's member_parent, and every native type the implementation names as X::
	TArray<FString> TypeNames;
	if (Request.Blueprint.IsSet())
	{
		for (const FN2CGraph& Graph : Request.Blueprint->Graphs)
		{
			for (const FN2CNodeDefinition& Node : Graph.Nodes)
			{
				if (!Node.MemberParent.IsEmpty())
				{
					TypeNames.AddUnique(Node.MemberParent);
				}
			}
		}
	}
	for (const FString& Qualifier : FindQualifiers(ImplementationText + TEXT("\n") + FString::Join(ClassDeclarations, TEXT("\n"))))
	{
		TypeNames.AddUnique(Qualifier);
	}
	TSet<FString> Modules = { TEXT("Core"), TEXT("CoreUObject") };
	if (const FString ParentModule = ModuleOf(Parent); !ParentModule.IsEmpty())
	{
		Modules.Add(ParentModule);
	}
	for (const FString& TypeName : TypeNames)
	{
		FString Include, Module;
		if (TypeName == OutPlan.ClassName || TypeName == Stem)
		{
			continue;
		}
		if (ResolveType(TypeName, Include, Module))
		{
			OutPlan.Includes.AddUnique(Include);
			Modules.Add(Module);
		}
		else if (Request.Blueprint.IsSet() && Request.Blueprint->Graphs.ContainsByPredicate([&TypeName](const FN2CGraph& Graph)
			{
				return Graph.Nodes.ContainsByPredicate([&TypeName](const FN2CNodeDefinition& Node) { return Node.MemberParent == TypeName; });
			}))
		{
			OutPlan.Warnings.Add(FString::Printf(TEXT("member_parent '%s' is not a native type (a Blueprint, or not loaded): no include for it"), *TypeName));
		}
	}
	OutPlan.Includes.Remove(OutPlan.ParentInclude);
	OutPlan.Includes.Sort();
	Modules.Remove(OutPlan.ModuleName);
	OutPlan.RequiredModules = Modules.Array();
	OutPlan.RequiredModules.Sort();

	const TSet<FString> Listed = GetListedModules(BuildCs);
	for (const FString& Module : OutPlan.RequiredModules)
	{
		if (!Listed.Contains(Module))
		{
			OutPlan.MissingModules.Add(Module);
		}
	}
	if (OutPlan.MissingModules.Num() > 0 && !AddModulesToBuildCs(BuildCs, OutPlan.MissingModules, OutPlan.BuildCsContent, OutPlan.BuildCsProblem))
	{
		OutPlan.BuildCsContent.Reset();
	}

	const FString Api = OutPlan.ModuleName.ToUpper() + TEXT("_API");
	const FString Banner = FString::Printf(TEXT("// Generated by NodeToCode (place-cpp-translation) from a translation of %s. Review it before you compile.\n"),
		Request.Blueprint.IsSet() && !Request.Blueprint->Metadata.Name.IsEmpty() ? *Request.Blueprint->Metadata.Name : TEXT("a Blueprint"));

	FString Header = Banner + TEXT("\n#pragma once\n\n#include \"CoreMinimal.h\"\n");
	if (!OutPlan.ParentInclude.IsEmpty())
	{
		Header += FString::Printf(TEXT("#include \"%s\"\n"), *OutPlan.ParentInclude);
	}
	Header += FString::Printf(TEXT("#include \"%s.generated.h\"\n\n"), *Stem);
	if (TopDeclarations.Num() > 0)
	{
		Header += FString::Join(TopDeclarations, TEXT("\n\n")) + TEXT("\n\n");
	}
	Header += FString::Printf(TEXT("UCLASS()\nclass %s %s : public %s\n{\n\tGENERATED_BODY()\n\npublic:\n"), *Api, *OutPlan.ClassName, *OutPlan.ParentClassName);
	Header += Indent(FString::Join(ClassDeclarations, TEXT("\n\n"))) + TEXT("\n};\n");
	OutPlan.Header.Content = Header;

	FString Source = Banner + FString::Printf(TEXT("\n#include \"%s\"\n"), *HeaderInclude);
	for (const FString& Include : OutPlan.Includes)
	{
		Source += FString::Printf(TEXT("#include \"%s\"\n"), *Include);
	}
	Source += TEXT("\n") + ImplementationText.TrimEnd() + TEXT("\n");
	if (Notes.Num() > 0)
	{
		Source += TEXT("\n/*\nImplementation notes from the translation:\n\n") + FString::Join(Notes, TEXT("\n\n")).Replace(TEXT("*/"), TEXT("* /")) + TEXT("\n*/\n");
	}
	OutPlan.Source.Content = Source;
	return true;
}

bool FN2CCppPlacement::Apply(const FN2CCppPlacementPlan& Plan, TArray<FString>& OutWritten, TArray<FString>& OutBackups, FString& OutError)
{
	TArray<TPair<FString, FString>> Files = { { Plan.Header.Path, Plan.Header.Content }, { Plan.Source.Path, Plan.Source.Content } };
	if (!Plan.BuildCsContent.IsEmpty())
	{
		Files.Emplace(Plan.BuildCsPath, Plan.BuildCsContent);
	}
	for (const TPair<FString, FString>& File : Files)
	{
		if (!IsInside(File.Key, Plan.ModuleSourceDir))
		{
			OutError = FString::Printf(TEXT("Refused: %s is outside the module's Source folder %s"), *File.Key, *Plan.ModuleSourceDir);
			return false;
		}
	}
	for (const TPair<FString, FString>& File : Files)
	{
		const bool bExisted = FPaths::FileExists(File.Key);
		FN2CAtomicWriteOptions Options;
		Options.bKeepBackup = true;
		if (!FN2CFileUtils::SaveStringAtomically(File.Value, File.Key, Options))
		{
			OutError = FString::Printf(TEXT("Could not write %s"), *File.Key);
			return false;
		}
		OutWritten.Add(File.Key);
		if (bExisted)
		{
			OutBackups.Add(FN2CFileUtils::BackupPathFor(File.Key));
		}
	}
	return true;
}
