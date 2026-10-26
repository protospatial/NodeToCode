// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CGraphStateTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ============================================================================
// FN2CTranslationSummary
// ============================================================================

TSharedPtr<FJsonObject> FN2CTranslationSummary::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("declarationPreview"), DeclarationPreview);
	JsonObject->SetNumberField(TEXT("implementationLines"), ImplementationLines);
	JsonObject->SetBoolField(TEXT("hasNotes"), bHasNotes);
	return JsonObject;
}

FN2CTranslationSummary FN2CTranslationSummary::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FN2CTranslationSummary Summary;
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("declarationPreview"), Summary.DeclarationPreview);
		JsonObject->TryGetNumberField(TEXT("implementationLines"), Summary.ImplementationLines);
		JsonObject->TryGetBoolField(TEXT("hasNotes"), Summary.bHasNotes);
	}
	return Summary;
}

// ============================================================================
// FN2CTranslationState
// ============================================================================

TSharedPtr<FJsonObject> FN2CTranslationState::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetBoolField(TEXT("exists"), bExists);
	JsonObject->SetStringField(TEXT("outputPath"), OutputPath);
	JsonObject->SetStringField(TEXT("timestamp"), Timestamp.ToIso8601());
	JsonObject->SetStringField(TEXT("provider"), Provider);
	JsonObject->SetStringField(TEXT("model"), Model);
	JsonObject->SetStringField(TEXT("language"), Language);
	JsonObject->SetObjectField(TEXT("summary"), Summary.ToJson());
	return JsonObject;
}

FN2CTranslationState FN2CTranslationState::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FN2CTranslationState State;
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetBoolField(TEXT("exists"), State.bExists);
		JsonObject->TryGetStringField(TEXT("outputPath"), State.OutputPath);

		FString TimestampStr;
		if (JsonObject->TryGetStringField(TEXT("timestamp"), TimestampStr))
		{
			FDateTime::ParseIso8601(*TimestampStr, State.Timestamp);
		}

		JsonObject->TryGetStringField(TEXT("provider"), State.Provider);
		JsonObject->TryGetStringField(TEXT("model"), State.Model);
		JsonObject->TryGetStringField(TEXT("language"), State.Language);

		const TSharedPtr<FJsonObject>* SummaryObject;
		if (JsonObject->TryGetObjectField(TEXT("summary"), SummaryObject))
		{
			State.Summary = FN2CTranslationSummary::FromJson(*SummaryObject);
		}
	}
	return State;
}

// ============================================================================
// FN2CJsonExportState
// ============================================================================

TSharedPtr<FJsonObject> FN2CJsonExportState::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetBoolField(TEXT("exists"), bExists);
	JsonObject->SetStringField(TEXT("outputPath"), OutputPath);
	JsonObject->SetStringField(TEXT("timestamp"), Timestamp.ToIso8601());
	JsonObject->SetBoolField(TEXT("minified"), bMinified);
	return JsonObject;
}

FN2CJsonExportState FN2CJsonExportState::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FN2CJsonExportState State;
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetBoolField(TEXT("exists"), State.bExists);
		JsonObject->TryGetStringField(TEXT("outputPath"), State.OutputPath);

		FString TimestampStr;
		if (JsonObject->TryGetStringField(TEXT("timestamp"), TimestampStr))
		{
			FDateTime::ParseIso8601(*TimestampStr, State.Timestamp);
		}

		JsonObject->TryGetBoolField(TEXT("minified"), State.bMinified);
	}
	return State;
}

// ============================================================================
// FN2CTagEntry
// ============================================================================

TSharedPtr<FJsonObject> FN2CTagEntry::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("tag"), Tag);
	JsonObject->SetStringField(TEXT("category"), Category);
	JsonObject->SetStringField(TEXT("description"), Description);
	JsonObject->SetStringField(TEXT("timestamp"), Timestamp.ToIso8601());
	return JsonObject;
}

FN2CTagEntry FN2CTagEntry::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FN2CTagEntry Entry;
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("tag"), Entry.Tag);
		JsonObject->TryGetStringField(TEXT("category"), Entry.Category);
		JsonObject->TryGetStringField(TEXT("description"), Entry.Description);

		FString TimestampStr;
		if (JsonObject->TryGetStringField(TEXT("timestamp"), TimestampStr))
		{
			FDateTime::ParseIso8601(*TimestampStr, Entry.Timestamp);
		}
	}
	return Entry;
}

bool FN2CTagEntry::MatchesTag(const FString& InTag, const FString& InCategory) const
{
	bool bTagMatches = Tag.Equals(InTag, ESearchCase::IgnoreCase);
	bool bCategoryMatches = InCategory.IsEmpty() || Category.Equals(InCategory, ESearchCase::IgnoreCase);
	return bTagMatches && bCategoryMatches;
}

bool FN2CTagEntry::MatchesTagExactly(const FString& InTag, const FString& InCategory) const
{
	return Tag.Equals(InTag, ESearchCase::IgnoreCase) && Category.Equals(InCategory, ESearchCase::IgnoreCase);
}

FString FN2CTagEntry::NormalizeCategory(const FString& InCategory)
{
	return InCategory.TrimStartAndEnd().IsEmpty() ? FString(TEXT("Default")) : InCategory;
}

// ============================================================================
// FN2CGraphState
// ============================================================================

TSharedPtr<FJsonObject> FN2CGraphState::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("graphGuid"), GraphGuid.ToString());
	JsonObject->SetStringField(TEXT("graphName"), GraphName);
	JsonObject->SetStringField(TEXT("owningBlueprint"), OwningBlueprint.ToString());

	// Tags array
	TArray<TSharedPtr<FJsonValue>> TagsArray;
	for (const FN2CTagEntry& TagEntry : Tags)
	{
		TagsArray.Add(MakeShared<FJsonValueObject>(TagEntry.ToJson()));
	}
	JsonObject->SetArrayField(TEXT("tags"), TagsArray);

	// Translation state
	JsonObject->SetObjectField(TEXT("translation"), Translation.ToJson());

	// JSON export state
	JsonObject->SetObjectField(TEXT("jsonExport"), JsonExport.ToJson());

	return JsonObject;
}

FN2CGraphState FN2CGraphState::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FN2CGraphState State;
	if (JsonObject.IsValid())
	{
		FString GuidStr;
		if (JsonObject->TryGetStringField(TEXT("graphGuid"), GuidStr))
		{
			FGuid::Parse(GuidStr, State.GraphGuid);
		}

		JsonObject->TryGetStringField(TEXT("graphName"), State.GraphName);

		FString BlueprintPath;
		if (JsonObject->TryGetStringField(TEXT("owningBlueprint"), BlueprintPath))
		{
			State.OwningBlueprint = FSoftObjectPath(BlueprintPath);
		}

		// Tags array
		const TArray<TSharedPtr<FJsonValue>>* TagsArray;
		if (JsonObject->TryGetArrayField(TEXT("tags"), TagsArray))
		{
			for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
			{
				const TSharedPtr<FJsonObject>* TagObject;
				if (TagValue->TryGetObject(TagObject))
				{
					State.Tags.Add(FN2CTagEntry::FromJson(*TagObject));
				}
			}
		}

		// Translation state
		const TSharedPtr<FJsonObject>* TranslationObject;
		if (JsonObject->TryGetObjectField(TEXT("translation"), TranslationObject))
		{
			State.Translation = FN2CTranslationState::FromJson(*TranslationObject);
		}

		// JSON export state
		const TSharedPtr<FJsonObject>* JsonExportObject;
		if (JsonObject->TryGetObjectField(TEXT("jsonExport"), JsonExportObject))
		{
			State.JsonExport = FN2CJsonExportState::FromJson(*JsonExportObject);
		}
	}
	return State;
}

bool FN2CGraphState::HasTag(const FString& InTag, const FString& InCategory) const
{
	for (const FN2CTagEntry& TagEntry : Tags)
	{
		if (TagEntry.MatchesTag(InTag, InCategory))
		{
			return true;
		}
	}
	return false;
}

TArray<FN2CTagEntry> FN2CGraphState::GetTagsInCategory(const FString& InCategory) const
{
	TArray<FN2CTagEntry> Result;
	for (const FN2CTagEntry& TagEntry : Tags)
	{
		if (TagEntry.Category.Equals(InCategory, ESearchCase::IgnoreCase))
		{
			Result.Add(TagEntry);
		}
	}
	return Result;
}

// ============================================================================
// FN2CGraphStateFile
// ============================================================================

FString FN2CGraphStateFile::ToJsonString(bool bPrettyPrint) const
{
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("version"), Version);
	RootObject->SetStringField(TEXT("lastSaved"), LastSaved.ToIso8601());

	// Graphs array
	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (const FN2CGraphState& GraphState : Graphs)
	{
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphState.ToJson()));
	}
	RootObject->SetArrayField(TEXT("graphs"), GraphsArray);

	FString OutputString;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	}

	return OutputString;
}

bool FN2CGraphStateFile::FromJsonString(const FString& JsonString, FN2CGraphStateFile& OutFile)
{
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	RootObject->TryGetStringField(TEXT("version"), OutFile.Version);

	FString LastSavedStr;
	if (RootObject->TryGetStringField(TEXT("lastSaved"), LastSavedStr))
	{
		FDateTime::ParseIso8601(*LastSavedStr, OutFile.LastSaved);
	}

	// Graphs array
	OutFile.Graphs.Empty();
	const TArray<TSharedPtr<FJsonValue>>* GraphsArray;
	if (RootObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
		{
			const TSharedPtr<FJsonObject>* GraphObject;
			if (GraphValue->TryGetObject(GraphObject))
			{
				OutFile.Graphs.Add(FN2CGraphState::FromJson(*GraphObject));
			}
		}
	}

	return true;
}
