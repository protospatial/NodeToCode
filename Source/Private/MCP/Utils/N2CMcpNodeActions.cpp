// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Utils/N2CMcpNodeActions.h"

#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintDragDropMenuItem.h"
#include "BlueprintFieldNodeSpawner.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintNodeSpawner.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace N2CNodeActions
{
	namespace
	{
		const TCHAR* const MenuIdPrefix = TEXT("menu:");

		/** Removes every control character (a signature or menu text never needs one; agents cannot round-trip them) */
		FString StripControlCharacters(const FString& In)
		{
			FString Out;
			Out.Reserve(In.Len());
			for (const TCHAR Ch : In)
			{
				if (Ch >= 0x20 && Ch != 0x7F)
				{
					Out.AppendChar(Ch);
				}
			}
			return Out;
		}

		/** "menu:<category>|<menu name>", for an action without a spawner signature */
		FString GetMenuId(const FEdGraphSchemaAction& Action)
		{
			return StripControlCharacters(FString(MenuIdPrefix) + Action.GetCategory().ToString() + TEXT("|")
				+ Action.GetMenuDescription().ToString());
		}

		/**
		 * Adds the template node's own object references and member references (properties declared below UK2Node)
		 * to Signature. Only references to objects outside the transient package count, so the result does not depend
		 * on the template cache, the session or the editor language.
		 */
		void AddTemplateFields(FBlueprintNodeSignature& Signature, const UEdGraphNode& Template)
		{
			for (TFieldIterator<FProperty> It(Template.GetClass()); It; ++It)
			{
				const FProperty* Property = *It;
				const UClass* Owner = Property->GetOwnerClass();
				if (!Owner || Owner == UK2Node::StaticClass() || !Owner->IsChildOf(UK2Node::StaticClass())
					|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || Property->ArrayDim != 1)
				{
					continue;
				}

				if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					const UObject* Object = ObjectProperty->GetObjectPropertyValue_InContainer(&Template);
					if (Object && Object->GetOutermost() != GetTransientPackage())
					{
						Signature.AddNamedValue(Property->GetFName(), Object->GetPathName());
					}
				}
				else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					if (StructProperty->Struct == FMemberReference::StaticStruct())
					{
						const FMemberReference* Reference = StructProperty->ContainerPtrToValuePtr<FMemberReference>(&Template);
						if (Reference && Reference->GetMemberName() != NAME_None)
						{
							const UClass* Parent = Reference->GetMemberParentClass();
							Signature.AddNamedValue(Property->GetFName(), (Parent && Parent->GetOutermost() != GetTransientPackage()
								? Parent->GetPathName() : FString()) + TEXT(":") + Reference->GetMemberName().ToString());
						}
					}
				}
			}
		}

		bool IsSignatureId(const FString& ActionId)
		{
			return ActionId.StartsWith(TEXT("(")) && ActionId.EndsWith(TEXT(")"));
		}

		/** Menu names compared without case and whitespace ("Print String" == "printstring") */
		FString NormalizeName(const FString& Name)
		{
			FString Out;
			for (const TCHAR Ch : Name)
			{
				if (!FChar::IsWhitespace(Ch))
				{
					Out.AppendChar(FChar::ToLower(Ch));
				}
			}
			return Out;
		}

		/**
		 * Cheap pre-check before the signature (which may build the spawner's template node): every signature names
		 * its node class as NodeName="<class path>", so a spawner whose node class is not in the ID cannot match
		 */
		bool MayMatchSignature(const FEdGraphSchemaAction& Action, const FString& ActionId)
		{
			const UBlueprintNodeSpawner* Spawner = GetSpawner(Action);
			if (!Spawner || !Spawner->NodeClass)
			{
				return true;
			}
			return ActionId.Contains(Spawner->NodeClass->GetPathName(), ESearchCase::CaseSensitive);
		}

		/** Picks one action among the matches of an ID, or explains why it cannot */
		TSharedPtr<FEdGraphSchemaAction> PickMatch(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Matches,
			const FString& ActionId, const FString& NodeName, FString& OutError)
		{
			if (Matches.Num() == 0)
			{
				return nullptr;
			}

			// The same spawner listed twice (it can appear in more than one menu section) is one action
			const UBlueprintNodeSpawner* FirstSpawner = GetSpawner(*Matches[0]);
			bool bAllSameSpawner = FirstSpawner != nullptr;
			for (const TSharedPtr<FEdGraphSchemaAction>& Match : Matches)
			{
				bAllSameSpawner &= GetSpawner(*Match) == FirstSpawner;
			}
			if (Matches.Num() == 1 || bAllSameSpawner)
			{
				return Matches[0];
			}

			// Different spawners with one signature: the node name breaks the tie
			const FString WantedName = NormalizeName(NodeName);
			TArray<TSharedPtr<FEdGraphSchemaAction>> Named;
			TArray<FString> Names;
			for (const TSharedPtr<FEdGraphSchemaAction>& Match : Matches)
			{
				const FString MenuName = Match->GetMenuDescription().ToString();
				Names.AddUnique(MenuName);
				if (!WantedName.IsEmpty() && NormalizeName(MenuName) == WantedName)
				{
					Named.Add(Match);
				}
			}
			if (Named.Num() >= 1)
			{
				return Named[0];
			}
			OutError = FString::Printf(TEXT("AMBIGUOUS_ACTION: %d actions have the actionIdentifier %s (%s). Pass the ")
				TEXT("one you want as nodeName."), Matches.Num(), *ActionId, *FString::Join(Names, TEXT(", ")));
			return nullptr;
		}
	}

	const UBlueprintNodeSpawner* GetSpawner(const FEdGraphSchemaAction& Action)
	{
		const FName TypeId = Action.GetTypeId();
		if (TypeId == FBlueprintActionMenuItem::StaticGetTypeId())
		{
			return static_cast<const FBlueprintActionMenuItem&>(Action).GetRawAction();
		}
		if (TypeId == FBlueprintDragDropMenuItem::StaticGetTypeId())
		{
			return static_cast<const FBlueprintDragDropMenuItem&>(Action).GetSampleAction();
		}
		return nullptr;
	}

	FString GetActionId(const TSharedPtr<FEdGraphSchemaAction>& Action)
	{
		if (!Action.IsValid())
		{
			return FString();
		}
		if (const UBlueprintNodeSpawner* Spawner = GetSpawner(*Action))
		{
			FBlueprintNodeSignature Signature = Spawner->GetSpawnerSignature();

			// UBlueprintVariableNodeSpawner's signature names only the node class for a member variable, so every Get
			// (or Set) of a member variable would share one ID: add the field, as function spawners' signatures have it
			if (const UBlueprintFieldNodeSpawner* FieldSpawner = Cast<UBlueprintFieldNodeSpawner>(Spawner))
			{
				const FFieldVariant Field = FieldSpawner->GetField();
				if (Field.IsValid() && !Signature.ToString().Contains(FString(TEXT("\"")) + Field.GetPathName() + TEXT("\"")))
				{
					Signature.AddSubObject(Field);
				}
			}

			// A signature that names only the node class (no '",' between elements) is shared by every spawner of that
			// class that a customize delegate sets up differently (Get <Subsystem>, the instanced-struct functions, the
			// Niagara data-channel context nodes): add what makes the template node differ
			if (!Signature.ToString().Contains(TEXT("\",")))
			{
				if (const UEdGraphNode* Template = Spawner->GetTemplateNode())
				{
					AddTemplateFields(Signature, *Template);
				}
			}

			if (Signature.IsValid())
			{
				const FString Id = StripControlCharacters(Signature.ToString());
				if (!Id.IsEmpty())
				{
					return Id;
				}
			}
		}
		return GetMenuId(*Action);
	}

	FString GetLegacyIdentifier(const FEdGraphSchemaAction& Action)
	{
		FString Id = Action.GetFullSearchText().Replace(TEXT("\r"), TEXT(""));
		return Id.Replace(TEXT("\n"), TEXT(">"));
	}

	void BuildActionList(FBlueprintActionMenuBuilder& MenuBuilder, UBlueprint* Blueprint, UEdGraph* Graph)
	{
		FBlueprintActionFilter Filter;
		if (Blueprint && Graph)
		{
			Filter.Context.Blueprints.Add(Blueprint);
			Filter.Context.Graphs.Add(Graph);
		}
		MenuBuilder.AddMenuSection(Filter, FText::GetEmpty(), 0);
		MenuBuilder.RebuildActionList();
	}

	TSharedPtr<FEdGraphSchemaAction> ResolveAction(FBlueprintActionMenuBuilder& MenuBuilder,
		const FString& InActionId, const FString& NodeName, FString& OutError, bool* bOutLegacy)
	{
		if (bOutLegacy)
		{
			*bOutLegacy = false;
		}
		const FString ActionId = InActionId.TrimStartAndEnd();
		if (ActionId.IsEmpty())
		{
			OutError = TEXT("ACTION_NOT_FOUND: actionIdentifier is empty");
			return nullptr;
		}

		const int32 NumActions = MenuBuilder.GetNumActions();
		TArray<TSharedPtr<FEdGraphSchemaAction>> Matches;

		if (IsSignatureId(ActionId) || ActionId.StartsWith(MenuIdPrefix))
		{
			const bool bSignature = IsSignatureId(ActionId);
			// First pass skips spawners whose node class the ID does not name; the second (only when the first finds
			// nothing) checks every action, for a spawner whose template node is of another class than its NodeClass
			for (int32 Pass = 0; Pass < 2 && Matches.Num() == 0; ++Pass)
			{
				for (int32 Index = 0; Index < NumActions; ++Index)
				{
					const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
					if (!Action.IsValid())
					{
						continue;
					}
					if (bSignature && Pass == 0 && !MayMatchSignature(*Action, ActionId))
					{
						continue;
					}
					if (GetActionId(Action) == ActionId)
					{
						Matches.Add(Action);
					}
				}
				if (!bSignature)
				{
					break;
				}
			}
		}
		else
		{
			// A pre-0277 identifier: the full search text with line breaks as '>'. Compared in that form, so a '>'
			// inside the text (comparison operators) matches too, and with every '\r' removed on both sides
			const FString Legacy = ActionId.Replace(TEXT("\r"), TEXT(""));
			for (int32 Index = 0; Index < NumActions; ++Index)
			{
				const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
				if (Action.IsValid() && GetLegacyIdentifier(*Action) == Legacy)
				{
					Matches.Add(Action);
				}
			}
			if (Matches.Num() > 0)
			{
				if (bOutLegacy)
				{
					*bOutLegacy = true;
				}
				// As before card 0277: the first action with that text
				return Matches[0];
			}
		}

		if (Matches.Num() == 0)
		{
			OutError = FString::Printf(TEXT("ACTION_NOT_FOUND: no action with the actionIdentifier %s can be added to ")
				TEXT("this graph. Use the spawnMetadata.actionIdentifier of a search-blueprint-nodes result for the ")
				TEXT("same graph."), *ActionId);
			return nullptr;
		}
		return PickMatch(Matches, ActionId, NodeName, OutError);
	}

	bool MatchesCategory(const FEdGraphSchemaAction& Action, const FString& Filter)
	{
		// Without case and spaces, so 'flowcontrol' finds "Utilities|Flow Control"
		const FString WantedCategory = NormalizeName(Filter);
		return WantedCategory.IsEmpty() || NormalizeName(Action.GetCategory().ToString()).Contains(WantedCategory);
	}

	bool IsExcludedVMFunction(const FEdGraphSchemaAction& Action)
	{
		const FString LowerSearchText = Action.GetFullSearchText().ToLower();

		// Type-specific math operations; Blueprint users use the promotable operators instead
		static const TCHAR* const ExcludedPatterns[] = {
			TEXT("_floatfloat"), TEXT("_intint"), TEXT("_int64int64"), TEXT("_vectorfloat"), TEXT("_vectorvector"),
			TEXT("_vectorint"), TEXT("_rotatorrotator"), TEXT("_transformtransform"), TEXT("_linearcolorfloat"),
			TEXT("_linearcolorlinearcolor"), TEXT("_bytebyte"), TEXT("_matrix"), TEXT("_quatquat"),
			TEXT("_timespantimespan"), TEXT("_datetimetimespan"),
		};
		for (const TCHAR* Pattern : ExcludedPatterns)
		{
			if (LowerSearchText.Contains(Pattern))
			{
				return true;
			}
		}
		return false;
	}

	bool MatchesSearchTerm(const FEdGraphSchemaAction& Action, const TArray<FString>& LowerTerms)
	{
		const FString LowerSearchText = Action.GetFullSearchText().ToLower();
		for (const FString& Term : LowerTerms)
		{
			if (!LowerSearchText.Contains(Term))
			{
				return false;
			}
		}
		return true;
	}

	TArray<FString> TokenizeSearchTerm(const FString& SearchTerm)
	{
		TArray<FString> Terms;
		SearchTerm.ParseIntoArray(Terms, TEXT(" "), true);
		for (FString& Term : Terms)
		{
			Term.ToLowerInline();
		}
		return Terms;
	}

	void AddDescriptionFields(const FEdGraphSchemaAction& Action, FJsonObject& Object)
	{
		Object.SetStringField(TEXT("menuName"), Action.GetMenuDescription().ToString());
		Object.SetStringField(TEXT("category"), Action.GetCategory().ToString());
		Object.SetStringField(TEXT("tooltip"), Action.GetTooltipDescription().ToString());
		Object.SetStringField(TEXT("keywords"), Action.GetKeywords().ToString());
	}

	UEdGraphNode* SpawnAction(const TSharedPtr<FEdGraphSchemaAction>& Action, UEdGraph* Graph, const FVector2f& Location,
		FString& OutError)
	{
		if (!Action.IsValid() || !Graph)
		{
			OutError = TEXT("Invalid schema action or graph");
			return nullptr;
		}

		const int32 PreSpawnNodeCount = Graph->Nodes.Num();
		UEdGraphNode* SpawnedNode = Action->PerformAction(Graph, nullptr, Location);
		if (!SpawnedNode && Graph->Nodes.Num() > PreSpawnNodeCount)
		{
			// Some actions add the node without returning it
			SpawnedNode = Graph->Nodes.Last();
		}
		if (!SpawnedNode)
		{
			OutError = TEXT("Failed to spawn node - action did not create a new node");
		}
		return SpawnedNode;
	}
}
