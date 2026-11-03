// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "SceneTypes.h"
#include "Templates/SubclassOf.h"

class UMaterial;
class UMaterialExpression;
class UMaterialFunction;
class UMaterialGraph;
class UPackage;

namespace N2CTest
{
	/**
	 * Builds materials in code for material capture tests (epic 0442, card 0448), the material counterpart of
	 * FTestBlueprint. Each instance makes one UMaterial named exactly as asked inside its own transient package,
	 * <MountRoot>/N2CTest_<guid> (/Game by default), so names are stable from run to run while two instances never
	 * collide. Material functions made through it live in the same package. Nothing is saved or registered with the asset
	 * registry; the destructor clears the standalone and public flags of everything in the package so the garbage
	 * collector can take it.
	 *
	 * Engine and UnrealEd APIs only (NodeToCode does not link MaterialEditor): an expression is created as
	 * UMaterialEditingLibrary::CreateMaterialExpressionEx does (NewObject in the material or function, added to its
	 * expression collection, a new expression GUID), and BuildGraph makes the editor graph as the Material Editor does
	 * (FBlueprintEditorUtils::CreateNewGraph with UMaterialGraphSchema, then UMaterialGraph::RebuildGraph). Expressions
	 * are created in the order of the calls, which is the order of the collection. Every helper returns null, and
	 * Connect returns false, on failure; the failure text is kept in Errors() for the test to report.
	 */
	class FTestMaterial
	{
	public:
		explicit FTestMaterial(const FString& MaterialName, const FString& MountRoot = TEXT("/Game"));
		~FTestMaterial();

		FTestMaterial(const FTestMaterial&) = delete;
		FTestMaterial& operator=(const FTestMaterial&) = delete;

		UMaterial* GetMaterial() const { return Material; }
		UPackage* GetPackage() const { return Package; }
		/** The transient package's path, for tests that normalise it out of captured text */
		FString GetPackagePath() const;
		/** What failed */
		TArray<FString> Errors() const { return ErrorList; }

		/** A new UMaterialFunction named Name in this package */
		UMaterialFunction* AddFunction(const FString& Name);

		/**
		 * A new expression of Class in the material, or in Function when given (a function of this package), placed at
		 * (X, Y) in the editor graph, with a new expression GUID
		 */
		UMaterialExpression* AddExpression(TSubclassOf<UMaterialExpression> Class, UMaterialFunction* Function = nullptr,
			int32 X = 0, int32 Y = 0);

		template <typename TExpression>
		TExpression* Add(UMaterialFunction* Function = nullptr, int32 X = 0, int32 Y = 0)
		{
			return Cast<TExpression>(AddExpression(TExpression::StaticClass(), Function, X, Y));
		}

		/**
		 * Wires output OutputIndex of From into To's input named InputName: the FExpressionInput UPROPERTY of To's class
		 * with that name (for example "A" and "B" on Add, "Coordinates" on Texture Sample), never a display name
		 */
		bool Connect(UMaterialExpression* From, int32 OutputIndex, UMaterialExpression* To, const FName InputName);

		/** Wires output OutputIndex of From into the material's root input for Property (MP_BaseColor, ...) */
		bool ConnectToProperty(UMaterialExpression* From, int32 OutputIndex, EMaterialProperty Property);

		/**
		 * The material's editor graph, made as the Material Editor makes it (CreateNewGraph with UMaterialGraphSchema, then
		 * RebuildGraph from the expressions); rebuilt on every call so it reflects the expressions added so far
		 */
		UMaterialGraph* BuildGraph();

	private:
		UPackage* Package = nullptr;
		UMaterial* Material = nullptr;
		TArray<FString> ErrorList;

		void AddError(const FString& Error) { ErrorList.Add(Error); }
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
