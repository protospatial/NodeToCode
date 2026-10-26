// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Utils/N2CStableNames.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Models/N2CJsonContract.h"

namespace N2CStableNames
{
	FString ClassName(const UClass* Class)
	{
		if (!Class)
		{
			return FString();
		}
#if WITH_EDITORONLY_DATA
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
		{
			return Blueprint->GetName();
		}
#endif
		FString Name = Class->GetName();
		if (Cast<UBlueprintGeneratedClass>(Class))
		{
			// A generated class without its Blueprint (a cooked or orphaned class): BP_Enemy_C, SKEL_BP_Enemy_C
			Name.RemoveFromStart(TEXT("SKEL_"), ESearchCase::CaseSensitive);
			Name.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);
		}
		return Name;
	}

	FString TypeObjectName(const UObject* Object)
	{
		if (const UClass* Class = Cast<UClass>(Object))
		{
			return ClassName(Class);
		}
		return Object ? Object->GetName() : FString();
	}

	EN2CPinType PinType(const FName& Category, const FName& SubCategory, const UObject* SubCategoryObject)
	{
		// Subcategories first: the schema uses each on more than one category (PSC_Self on object and class, PSC_Bitmask
		// on int and byte, PSC_Index on wildcard and int)
		if (SubCategory == UEdGraphSchema_K2::PSC_Self)
		{
			return EN2CPinType::Self;
		}
		if (SubCategory == UEdGraphSchema_K2::PSC_Bitmask)
		{
			return EN2CPinType::Bitmask;
		}
		if (SubCategory == UEdGraphSchema_K2::PSC_Index)
		{
			return EN2CPinType::Index;
		}

		// GetPropertyCategoryInfo gives FByteProperty-with-enum and FEnumProperty PC_Byte with the UEnum as the object
		if (Category == UEdGraphSchema_K2::PC_Byte)
		{
			return Cast<UEnum>(SubCategoryObject) ? EN2CPinType::Enum : EN2CPinType::Byte;
		}

		static const TMap<FName, EN2CPinType> Categories = {
			{ UEdGraphSchema_K2::PC_Exec, EN2CPinType::Exec },
			{ UEdGraphSchema_K2::PC_Boolean, EN2CPinType::Boolean },
			{ UEdGraphSchema_K2::PC_Int, EN2CPinType::Integer },
			{ UEdGraphSchema_K2::PC_Int64, EN2CPinType::Integer64 },
			{ UEdGraphSchema_K2::PC_Float, EN2CPinType::Float },
			{ UEdGraphSchema_K2::PC_Double, EN2CPinType::Double },
			{ UEdGraphSchema_K2::PC_Real, EN2CPinType::Real },
			{ UEdGraphSchema_K2::PC_String, EN2CPinType::String },
			{ UEdGraphSchema_K2::PC_Name, EN2CPinType::Name },
			{ UEdGraphSchema_K2::PC_Text, EN2CPinType::Text },
			{ UEdGraphSchema_K2::PC_Object, EN2CPinType::Object },
			{ UEdGraphSchema_K2::PC_Class, EN2CPinType::Class },
			{ UEdGraphSchema_K2::PC_Interface, EN2CPinType::Interface },
			{ UEdGraphSchema_K2::PC_Struct, EN2CPinType::Struct },
			{ UEdGraphSchema_K2::PC_Enum, EN2CPinType::Enum },
			{ UEdGraphSchema_K2::PC_Delegate, EN2CPinType::Delegate },
			{ UEdGraphSchema_K2::PC_MCDelegate, EN2CPinType::MulticastDelegate },
			{ UEdGraphSchema_K2::PC_FieldPath, EN2CPinType::FieldPath },
			{ UEdGraphSchema_K2::PC_Wildcard, EN2CPinType::Wildcard },
			{ UEdGraphSchema_K2::PC_SoftObject, EN2CPinType::SoftObject },
			{ UEdGraphSchema_K2::PC_SoftClass, EN2CPinType::SoftClass },
		};
		const EN2CPinType* Found = Categories.Find(Category);
		return Found ? *Found : EN2CPinType::Wildcard;
	}

	FString PinSubType(const FName& SubCategory, const UObject* SubCategoryObject)
	{
		if (SubCategoryObject)
		{
			return TypeObjectName(SubCategoryObject);
		}
		return SubCategory.IsNone() ? FString() : SubCategory.ToString();
	}

	FString SourceText(const FText& Text)
	{
		return Text.BuildSourceString();
	}

	FString NodeTitle(const UEdGraphNode* Node)
	{
		return Node ? SourceText(Node->GetNodeTitle(ENodeTitleType::MenuTitle)) : FString();
	}

	FString PinLabel(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return FString();
		}
		FString Label;
		const UEdGraphNode* Node = Pin->GetOwningNodeUnchecked();
		if (Node && Node->ShouldOverridePinNames())
		{
			Label = SourceText(Node->GetPinNameOverride(*Pin));
		}
		else if (!Pin->PinFriendlyName.IsEmpty())
		{
			Label = SourceText(Pin->PinFriendlyName);
		}
		Label.TrimStartAndEndInline();
		return N2CJsonContract::IsRedundantHint(Label, Pin->PinName.ToString()) ? FString() : Label;
	}

	FString NodeSearchText(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FString();
		}
		const FText FullTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle);
		FString MemberName;
		if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			MemberName = CallNode->FunctionReference.GetMemberName().ToString();
		}
		else if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
		{
			MemberName = VariableNode->VariableReference.GetMemberName().ToString();
		}
		return FString::Join(TArray<FString>{ FullTitle.ToString(), SourceText(FullTitle), Node->GetClass()->GetName(), MemberName }, TEXT(" "));
	}
}
