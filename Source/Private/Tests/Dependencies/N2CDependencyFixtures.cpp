// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/Dependencies/N2CDependencyFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestBlueprintFactory.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace N2CTest::DependencyFixtures
{
	FString ObjectPath(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), FolderPath, AssetName, AssetName);
	}

	bool ShouldGenerate()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_GENERATE_DEPENDENCY_FIXTURES")) == TEXT("1")
			|| FParse::Param(FCommandLine::Get(), TEXT("N2CGenerateDependencyFixtures"));
	}

	namespace
	{
		UPackage* NewAssetPackage(const TCHAR* AssetName, TArray<FString>& OutErrors)
		{
			const FString PackageName = FString::Printf(TEXT("%s/%s"), FolderPath, AssetName);
			if (FindPackage(nullptr, *PackageName))
			{
				OutErrors.Add(PackageName + TEXT(" is already loaded: regenerate in a fresh editor (only NodeToCode.Dependencies in the filter)"));
				return nullptr;
			}
			const FString File = FPaths::ConvertRelativePathToFull(
				FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
			// The new package replaces the file: delete the old one first (saving over it fails)
			if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true))
			{
				OutErrors.Add(TEXT("could not delete the old ") + File);
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				OutErrors.Add(TEXT("could not create ") + PackageName);
			}
			return Package;
		}

		bool Compile(UBlueprint* Blueprint, TArray<FString>& OutErrors)
		{
			FCompilerResultsLog Results;
			Results.bSilentMode = true;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			if (Results.NumErrors == 0 && Blueprint->Status != BS_Error)
			{
				return true;
			}
			for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
			{
				if (Message->GetSeverity() == EMessageSeverity::Error)
				{
					OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Blueprint->GetName(), *Message->ToText().ToString()));
				}
			}
			OutErrors.Add(FString::Printf(TEXT("%s: %d compile errors"), *Blueprint->GetName(), Results.NumErrors));
			return false;
		}

		bool Save(UObject* Asset, TArray<FString>& OutErrors, TArray<FString>& OutNotes)
		{
			UPackage* Package = Asset->GetPackage();
			const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			Package->MarkPackageDirty();
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Package, Asset, *FPaths::ConvertRelativePathToFull(File), Args))
			{
				OutErrors.Add(TEXT("could not save ") + File);
				return false;
			}
			OutNotes.Add(FString::Printf(TEXT("saved %s"), *FPaths::ConvertRelativePathToFull(File)));
			return true;
		}

		UBlueprint* CreateBlueprint(UClass* Parent, UPackage* Package, const TCHAR* Name, EBlueprintType Type, TArray<FString>& OutErrors)
		{
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(Name), Type,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			if (!Blueprint)
			{
				OutErrors.Add(FString::Printf(TEXT("could not create %s"), Name));
			}
			return Blueprint;
		}
	}

	bool GenerateAll(TArray<FString>& OutErrors, TArray<FString>& OutNotes)
	{
#if ENGINE_MAJOR_VERSION != 5 || ENGINE_MINOR_VERSION != 6
		OutErrors.Add(TEXT("regenerate the dependency fixtures on UE 5.6 only: a package saved by a later engine does not load in 5.6"));
		return false;
#else
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCodeTestContent"));
		if (!Plugin.IsValid() || !Plugin->IsEnabled() || !FPackageName::MountPointExists(TEXT("/NodeToCodeTestContent/")))
		{
			OutErrors.Add(TEXT("the NodeToCodeTestContent plugin is not mounted: build.ps1 junctions Tests/NodeToCodeTestContent into the host's Plugins folder"));
			return false;
		}

		UPackage* EnumPackage = NewAssetPackage(EnumName, OutErrors);
		UPackage* StructPackage = NewAssetPackage(StructName, OutErrors);
		UPackage* InterfacePackage = NewAssetPackage(InterfaceName, OutErrors);
		UPackage* MacroPackage = NewAssetPackage(MacroLibraryName, OutErrors);
		UPackage* APackage = NewAssetPackage(AName, OutErrors);
		UPackage* BPackage = NewAssetPackage(BName, OutErrors);
		UPackage* CPackage = NewAssetPackage(CName, OutErrors);
		UPackage* ChildPackage = NewAssetPackage(ChildName, OutErrors);
		if (OutErrors.Num() > 0)
		{
			return false;
		}

		TUniquePtr<FTestBlueprint> A = FTestBlueprint::InPackage(AName, APackage);
		TUniquePtr<FTestBlueprint> B = FTestBlueprint::InPackage(BName, BPackage);
		TUniquePtr<FTestBlueprint> C = FTestBlueprint::InPackage(CName, CPackage);
		auto Failed = [&]()
		{
			for (const FTestBlueprint* Each : { A.Get(), B.Get(), C.Get() })
			{
				OutErrors.Append(Each->Errors());
			}
			return OutErrors.Num() > 0;
		};

		// The types: the struct has a member of the enum's type
		UUserDefinedEnum* Enum = A->AddUserEnum(EnumName, { TEXT("Low"), TEXT("High") }, EnumPackage);
		UUserDefinedStruct* Struct = Enum ? A->AddUserStruct(StructName, {
			{ TEXT("Level"), FTestBlueprint::EnumPinType(Enum) }, { TEXT("Count"), FTestBlueprint::IntPinType() } }, StructPackage) : nullptr;

		// The interface, with one function, and the macro library, with one macro
		UBlueprint* Interface = CreateBlueprint(UInterface::StaticClass(), InterfacePackage, InterfaceName, BPTYPE_Interface, OutErrors);
		UBlueprint* Macros = CreateBlueprint(UObject::StaticClass(), MacroPackage, MacroLibraryName, BPTYPE_MacroLibrary, OutErrors);
		UEdGraph* Macro = nullptr;
		if (Interface && Macros)
		{
			UEdGraph* Ping = FBlueprintEditorUtils::CreateNewGraph(Interface, TEXT("Ping"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Interface, Ping, /*bIsUserCreated*/ true, nullptr);
			Macro = FBlueprintEditorUtils::CreateNewGraph(Macros, TEXT("DepMacro"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddMacroGraph(Macros, Macro, /*bIsUserCreated*/ true, nullptr);
		}
		if (!Struct || !Interface || !Macro || Failed() || !Compile(Interface, OutErrors) || !Compile(Macros, OutErrors))
		{
			Failed();
			return false;
		}

		// B and C reference each other: both classes first, then the variables and the cast
		B->AddFunction(TEXT("DoB"));
		if (Failed() || !Compile(B->GetBlueprint(), OutErrors) || !Compile(C->GetBlueprint(), OutErrors))
		{
			return false;
		}
		B->AddMemberVariable(TEXT("OtherC"), FTestBlueprint::ObjectPinType(C->GetBlueprint()->GeneratedClass));
		C->AddMemberVariable(TEXT("OtherB"), FTestBlueprint::ObjectPinType(B->GetBlueprint()->GeneratedClass));
		{
			FGraphNodeCreator<UK2Node_DynamicCast> Creator(*C->EventGraph());
			UK2Node_DynamicCast* CastNode = Creator.CreateNode();
			CastNode->TargetType = B->GetBlueprint()->GeneratedClass;
			Creator.Finalize();
		}
		if (Failed() || !Compile(B->GetBlueprint(), OutErrors) || !Compile(C->GetBlueprint(), OutErrors))
		{
			return false;
		}

		// A: the interface, a struct variable, a BP_N2CDep_B variable whose DoB it calls, and the library's macro
		if (!FBlueprintEditorUtils::ImplementNewInterface(A->GetBlueprint(), Interface->GeneratedClass->GetClassPathName()))
		{
			OutErrors.Add(TEXT("BP_N2CDep_A could not implement BPI_N2CDep"));
			return false;
		}
		A->AddMemberVariable(TEXT("Data"), FTestBlueprint::StructPinType(Struct));
		A->AddMemberVariable(TEXT("TargetB"), FTestBlueprint::ObjectPinType(B->GetBlueprint()->GeneratedClass));
		UK2Node_CustomEvent* Go = A->AddCustomEvent(A->EventGraph(), TEXT("Go"));
		UK2Node_CallFunction* CallDoB = A->AddCall(A->EventGraph(), B->GetBlueprint()->GeneratedClass, TEXT("DoB"));
		UK2Node_VariableGet* GetB = A->AddVariableGet(A->EventGraph(), TEXT("TargetB"));
		A->AddMacroInstance(A->EventGraph(), Macro);
		if (!Go || !CallDoB || !GetB || !A->Link(Go, UEdGraphSchema_K2::PN_Then, CallDoB, UEdGraphSchema_K2::PN_Execute)
			|| !A->Link(GetB, TEXT("TargetB"), CallDoB, UEdGraphSchema_K2::PN_Self) || Failed() || !Compile(A->GetBlueprint(), OutErrors))
		{
			Failed();
			return false;
		}

		// The child of A
		UBlueprint* Child = CreateBlueprint(A->GetBlueprint()->GeneratedClass, ChildPackage, ChildName, BPTYPE_Normal, OutErrors);
		if (!Child || !Compile(Child, OutErrors))
		{
			return false;
		}

		for (UObject* Asset : TArray<UObject*>{ Enum, Struct, Interface, Macros, B->GetBlueprint(), C->GetBlueprint(), A->GetBlueprint(), Child })
		{
			if (!Save(Asset, OutErrors, OutNotes))
			{
				return false;
			}
		}
		return true;
#endif
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
