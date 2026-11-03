// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/N2CTestMaterialFactory.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Kismet2/BlueprintEditorUtils.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace N2CTest
{
	FTestMaterial::FTestMaterial(const FString& MaterialName, const FString& MountRoot)
	{
		const FString PackageName = FString::Printf(TEXT("%s/N2CTest_%s"), *MountRoot, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Package = CreatePackage(*PackageName);
		if (!Package)
		{
			AddError(TEXT("could not create the transient package ") + PackageName);
			return;
		}
		Package->SetFlags(RF_Transient);
		// As UMaterialFactoryNew makes a new material asset
		Material = NewObject<UMaterial>(Package, FName(*MaterialName), RF_Public | RF_Standalone | RF_Transactional);
		if (!Material)
		{
			AddError(TEXT("could not create the material ") + MaterialName);
		}
	}

	FTestMaterial::~FTestMaterial()
	{
		if (!Package)
		{
			return;
		}
		// The material, its functions, expressions and editor graph all live in the package
		ForEachObjectWithPackage(Package, [](UObject* Object)
		{
			Object->ClearFlags(RF_Standalone | RF_Public);
			return true;
		});
		Package->ClearFlags(RF_Standalone);
	}

	FString FTestMaterial::GetPackagePath() const
	{
		return Package ? Package->GetName() : FString();
	}

	UMaterialFunction* FTestMaterial::AddFunction(const FString& Name)
	{
		if (!Package)
		{
			AddError(TEXT("no package for material function ") + Name);
			return nullptr;
		}
		UMaterialFunction* Function = NewObject<UMaterialFunction>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		if (!Function)
		{
			AddError(TEXT("could not create the material function ") + Name);
		}
		return Function;
	}

	UMaterialExpression* FTestMaterial::AddExpression(TSubclassOf<UMaterialExpression> Class, UMaterialFunction* Function,
		int32 X, int32 Y)
	{
		if (!Class || (!Function && !Material))
		{
			AddError(TEXT("no expression class, or no material or function to add it to"));
			return nullptr;
		}
		if (Function && Function->GetPackage() != Package)
		{
			AddError(FString::Printf(TEXT("material function %s is not in this test package"), *Function->GetName()));
			return nullptr;
		}
		// As UMaterialEditingLibrary::CreateMaterialExpressionEx does (MaterialEditor, which NodeToCode does not link)
		UObject* Outer = Function ? static_cast<UObject*>(Function) : static_cast<UObject*>(Material);
		UMaterialExpression* Expression = NewObject<UMaterialExpression>(Outer, Class.Get(), NAME_None, RF_Transactional);
		if (!Expression)
		{
			AddError(TEXT("could not create a ") + Class->GetName());
			return nullptr;
		}
		if (Function)
		{
			Function->GetExpressionCollection().AddExpression(Expression);
			Expression->Function = Function;
		}
		else
		{
			Material->GetExpressionCollection().AddExpression(Expression);
			Expression->Material = Material;
		}
		Expression->MaterialExpressionEditorX = X;
		Expression->MaterialExpressionEditorY = Y;
		Expression->UpdateMaterialExpressionGuid(true, false);
		return Expression;
	}

	UMaterialExpressionComment* FTestMaterial::AddComment(const FString& Text, int32 X, int32 Y, int32 SizeX, int32 SizeY)
	{
		if (!Material)
		{
			AddError(TEXT("AddComment: no material"));
			return nullptr;
		}
		UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material, NAME_None, RF_Transactional);
		Material->GetExpressionCollection().AddComment(Comment);
		Comment->Material = Material;
		Comment->Text = Text;
		Comment->MaterialExpressionEditorX = X;
		Comment->MaterialExpressionEditorY = Y;
		Comment->SizeX = SizeX;
		Comment->SizeY = SizeY;
		return Comment;
	}

	bool FTestMaterial::Connect(UMaterialExpression* From, int32 OutputIndex, UMaterialExpression* To, const FName InputName)
	{
		if (!From || !To)
		{
			AddError(TEXT("Connect: a null expression"));
			return false;
		}
		// A function call's inputs are its function's inputs, by their names
		if (UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(To))
		{
			for (FFunctionExpressionInput& FunctionInput : Call->FunctionInputs)
			{
				if (FunctionInput.Input.InputName == InputName && OutputIndex >= 0 && OutputIndex < From->GetOutputs().Num())
				{
					From->ConnectExpression(&FunctionInput.Input, OutputIndex);
					return true;
				}
			}
			AddError(FString::Printf(TEXT("Connect: the function call has no input named %s"), *InputName.ToString()));
			return false;
		}
		const FStructProperty* Property = FindFProperty<FStructProperty>(To->GetClass(), InputName);
		if (!Property || !Property->Struct || !Property->Struct->GetName().EndsWith(TEXT("ExpressionInput")))
		{
			AddError(FString::Printf(TEXT("Connect: %s has no expression input named %s"), *To->GetClass()->GetName(), *InputName.ToString()));
			return false;
		}
		if (OutputIndex < 0 || OutputIndex >= From->GetOutputs().Num())
		{
			AddError(FString::Printf(TEXT("Connect: %s has no output %d"), *From->GetClass()->GetName(), OutputIndex));
			return false;
		}
		From->ConnectExpression(Property->ContainerPtrToValuePtr<FExpressionInput>(To), OutputIndex);
		return true;
	}

	bool FTestMaterial::ConnectToProperty(UMaterialExpression* From, int32 OutputIndex, EMaterialProperty Property)
	{
		FExpressionInput* Input = Material ? Material->GetExpressionInputForProperty(Property) : nullptr;
		if (!From || !Input)
		{
			AddError(FString::Printf(TEXT("ConnectToProperty: no expression, or the material has no input for property %d"),
				static_cast<int32>(Property)));
			return false;
		}
		if (OutputIndex < 0 || OutputIndex >= From->GetOutputs().Num())
		{
			AddError(FString::Printf(TEXT("ConnectToProperty: %s has no output %d"), *From->GetClass()->GetName(), OutputIndex));
			return false;
		}
		From->ConnectExpression(Input, OutputIndex);
		return true;
	}

	UMaterialGraph* FTestMaterial::BuildGraph()
	{
		if (!Material)
		{
			AddError(TEXT("BuildGraph: no material"));
			return nullptr;
		}
		// As FMaterialEditor::InitMaterialEditor does (MaterialEditor.cpp, UE 5.6)
		if (!Material->MaterialGraph)
		{
			Material->MaterialGraph = CastChecked<UMaterialGraph>(FBlueprintEditorUtils::CreateNewGraph(Material, NAME_None,
				UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
		}
		Material->MaterialGraph->Material = Material;
		Material->MaterialGraph->MaterialFunction = nullptr;
		Material->MaterialGraph->RebuildGraph();
		return Material->MaterialGraph;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
