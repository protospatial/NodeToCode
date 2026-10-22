// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBatchTranslationConsolidator.h"

#include "Core/N2CSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"

namespace N2CBatchTranslationConsolidatorPrivate
{
FString SanitizeFileBaseName(const FString& InName)
{
    FString Result = InName.TrimStartAndEnd();
    if (Result.IsEmpty())
    {
        Result = TEXT("TranslatedBlueprint");
    }

    const TCHAR InvalidChars[] =
    {
        TEXT('<'), TEXT('>'), TEXT(':'), TEXT('"'),
        TEXT('/'), TEXT('\\'), TEXT('|'), TEXT('?'), TEXT('*')
    };

    for (TCHAR Ch : InvalidChars)
    {
        FString From;
        From.AppendChar(Ch);
        Result.ReplaceInline(*From, TEXT("_"), ESearchCase::CaseSensitive);
    }

    return Result;
}

TSharedPtr<FJsonObject> MakeTranslationObject(const FN2CGraphTranslation& Graph)
{
    TSharedPtr<FJsonObject> GraphObject = MakeShared<FJsonObject>();
    GraphObject->SetStringField(TEXT("graph_name"), Graph.GraphName);
    GraphObject->SetStringField(TEXT("graph_type"), Graph.GraphType);
    GraphObject->SetStringField(TEXT("graph_class"), Graph.GraphClass);

    TSharedPtr<FJsonObject> CodeObject = MakeShared<FJsonObject>();
    CodeObject->SetStringField(TEXT("graphDeclaration"), Graph.Code.GraphDeclaration);
    CodeObject->SetStringField(TEXT("graphImplementation"), Graph.Code.GraphImplementation);
    CodeObject->SetStringField(TEXT("implementationNotes"), Graph.Code.ImplementationNotes);
    GraphObject->SetObjectField(TEXT("code"), CodeObject);

    return GraphObject;
}
}

bool FN2CBatchTranslationConsolidator::BuildRequestPayload(
    const FN2CTranslationResponse& SessionResponse,
    const FN2CBlueprint& Blueprint,
    FString& OutPayload)
{
    OutPayload.Empty();

    if (SessionResponse.Graphs.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            TEXT("Cannot build final consolidation request without parsed graph responses"),
            TEXT("BatchConsolidation"));
        return false;
    }

    // Preserve the complete Blueprint as source-of-truth. The final LLM pass may need original pin,
    // flow, variable, component, struct, or enum information when independently generated graph
    // translations disagree about signatures or class structure.
    FN2CSerializer::SetPrettyPrint(false);
    const FString SourceBlueprintJson = FN2CSerializer::ToJson(Blueprint);
    if (SourceBlueprintJson.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to serialize source Blueprint for final consolidation"),
            TEXT("BatchConsolidation"));
        return false;
    }

    TSharedPtr<FJsonObject> SourceBlueprintObject;
    TSharedRef<TJsonReader<>> BlueprintReader = TJsonReaderFactory<>::Create(SourceBlueprintJson);
    if (!FJsonSerializer::Deserialize(BlueprintReader, SourceBlueprintObject) ||
        !SourceBlueprintObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to parse serialized source Blueprint while building consolidation request"),
            TEXT("BatchConsolidation"));
        return false;
    }

    TSharedPtr<FJsonObject> RequestObject = MakeShared<FJsonObject>();
    RequestObject->SetStringField(TEXT("task"), TEXT("consolidate_full_blueprint_cpp"));
    RequestObject->SetObjectField(TEXT("source_blueprint"), SourceBlueprintObject);

    TArray<TSharedPtr<FJsonValue>> TranslationArray;
    TranslationArray.Reserve(SessionResponse.Graphs.Num());
    for (const FN2CGraphTranslation& Graph : SessionResponse.Graphs)
    {
        TranslationArray.Add(MakeShared<FJsonValueObject>(
            N2CBatchTranslationConsolidatorPrivate::MakeTranslationObject(Graph)));
    }
    RequestObject->SetArrayField(TEXT("parsed_graph_translations"), TranslationArray);

    TSharedPtr<FJsonObject> UsageObject = MakeShared<FJsonObject>();
    UsageObject->SetNumberField(TEXT("input_tokens"), SessionResponse.Usage.InputTokens);
    UsageObject->SetNumberField(TEXT("output_tokens"), SessionResponse.Usage.OutputTokens);
    RequestObject->SetObjectField(TEXT("prior_translation_usage"), UsageObject);

    // Use a compact wire representation. This removes indentation/newline tokens only; the exact
    // source Blueprint and parsed translation fields remain present with identical JSON semantics.
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutPayload);
    if (!FJsonSerializer::Serialize(RequestObject.ToSharedRef(), Writer) || OutPayload.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to serialize final consolidation request payload"),
            TEXT("BatchConsolidation"));
        return false;
    }

    return true;
}

FString FN2CBatchTranslationConsolidator::GetSystemPrompt()
{
    return TEXT(
        "You are the final integration and reconciliation pass for Node to Code.\n"
        "You are given one complete Unreal Engine Blueprint source representation and multiple "
        "independently generated, already-parsed C++ translations for graphs belonging to that "
        "same Blueprint. These translations may conflict. Produce one authoritative, internally "
        "consistent Unreal Engine C++ class.\n\n"
        "SOURCE AUTHORITY AND RECONCILIATION RULES:\n"
        "1. The source_blueprint object is the ground truth for Blueprint behavior, pins, flows, "
        "variables, components, structs, enums, and ownership.\n"
        "2. Treat every parsed_graph_translations entry as a candidate implementation of one part "
        "of the same class, not as an independent class or file.\n"
        "3. A ClassItSelf translation is the preferred authority for class skeleton, UCLASS/GENERATED_BODY, "
        "members, components, constructor/destructor, and class-level initialization.\n"
        "4. A dedicated Function/EventGraph translation is the preferred authority for the detailed "
        "body and signature of that function/event when it conflicts with a placeholder emitted by ClassItSelf.\n"
        "5. If translations disagree, resolve the disagreement using source_blueprint. Do not preserve "
        "both conflicting versions.\n"
        "6. Normalize all declarations and definitions to one canonical class name. Every .cpp definition "
        "must exactly match its .h declaration.\n"
        "7. Remove duplicate declarations, duplicate definitions, placeholder bodies, duplicate includes, "
        "and contradictory UFUNCTION/UPROPERTY declarations.\n"
        "8. Ensure every member referenced by an implementation exists in the class, and merge required "
        "includes or forward declarations.\n"
        "9. Every graph present in source_blueprint must be represented in the final class. If an earlier "
        "per-graph request failed and therefore has no parsed_graph_translations entry, synthesize that graph "
        "directly from source_blueprint rather than omitting it.\n"
        "10. Preserve the behavior represented by every successfully translated graph while correcting "
        "inconsistencies against source_blueprint.\n"
        "11. Return complete compilable header and implementation text, not fragments.\n\n"
        "OUTPUT CONTRACT:\n"
        "Return JSON only. Do not wrap it in markdown fences or explanatory prose. Use exactly the normal "
        "Node to Code response shape below and return exactly ONE graph entry:\n"
        "{\n"
        "  \"graphs\": [\n"
        "    {\n"
        "      \"graph_name\": \"CanonicalClassName\",\n"
        "      \"graph_type\": \"ClassItSelf\",\n"
        "      \"graph_class\": \"CanonicalClassName\",\n"
        "      \"code\": {\n"
        "        \"graphDeclaration\": \"complete .h contents\",\n"
        "        \"graphImplementation\": \"complete .cpp contents\",\n"
        "        \"implementationNotes\": \"brief reconciliation notes, including important conflicts resolved\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n"
        "The final response must contain exactly one coherent class-level result."
    );
}

bool FN2CBatchTranslationConsolidator::ValidateFinalResponse(
    const FN2CTranslationResponse& Response,
    FString& OutError)
{
    OutError.Empty();

    if (Response.Graphs.Num() != 1)
    {
        OutError = FString::Printf(
            TEXT("Expected exactly one consolidated graph, received %d"),
            Response.Graphs.Num());
        return false;
    }

    const FN2CGraphTranslation& Graph = Response.Graphs[0];
    if (!Graph.GraphType.Equals(TEXT("ClassItSelf"), ESearchCase::IgnoreCase))
    {
        OutError = FString::Printf(
            TEXT("Final consolidated graph_type must be ClassItSelf, received '%s'"),
            *Graph.GraphType);
        return false;
    }

    if (Graph.GraphClass.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT("Final consolidated response has no graph_class");
        return false;
    }

    if (Graph.Code.GraphDeclaration.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT("Final consolidated response has an empty header/declaration");
        return false;
    }

    if (Graph.Code.GraphImplementation.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT("Final consolidated response has an empty implementation");
        return false;
    }

    return true;
}

bool FN2CBatchTranslationConsolidator::SaveCppFiles(
    const FN2CTranslationResponse& ConsolidatedResponse,
    const FString& RootPath)
{
    FString ValidationError;
    if (!ValidateFinalResponse(ConsolidatedResponse, ValidationError))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Refusing to save invalid final consolidation: %s"), *ValidationError),
            TEXT("BatchConsolidation"));
        return false;
    }

    if (!IFileManager::Get().DirectoryExists(*RootPath) &&
        !IFileManager::Get().MakeDirectory(*RootPath, true))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to create consolidated translation directory: %s"), *RootPath),
            TEXT("BatchConsolidation"));
        return false;
    }

    const FN2CGraphTranslation& Graph = ConsolidatedResponse.Graphs[0];
    const FString FileBaseName = N2CBatchTranslationConsolidatorPrivate::SanitizeFileBaseName(
        Graph.GraphClass.IsEmpty() ? Graph.GraphName : Graph.GraphClass);

    const FString HeaderPath = FPaths::Combine(RootPath, FileBaseName + TEXT(".h"));
    const FString SourcePath = FPaths::Combine(RootPath, FileBaseName + TEXT(".cpp"));

    bool bSuccess = true;
    if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to save consolidated header: %s"), *HeaderPath),
            TEXT("BatchConsolidation"));
        bSuccess = false;
    }

    if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *SourcePath))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to save consolidated implementation: %s"), *SourcePath),
            TEXT("BatchConsolidation"));
        bSuccess = false;
    }

    if (!Graph.Code.ImplementationNotes.TrimStartAndEnd().IsEmpty())
    {
        const FString NotesPath = FPaths::Combine(RootPath, FileBaseName + TEXT("_Notes.txt"));
        if (!FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath))
        {
            FN2CLogger::Get().LogWarning(
                FString::Printf(TEXT("Failed to save consolidated notes: %s"), *NotesPath),
                TEXT("BatchConsolidation"));
        }
    }

    if (bSuccess)
    {
        FN2CLogger::Get().Log(
            FString::Printf(
                TEXT("Saved final LLM-consolidated Blueprint translation: %s.h / %s.cpp"),
                *FileBaseName,
                *FileBaseName),
            EN2CLogSeverity::Info,
            TEXT("BatchConsolidation"));
    }

    return bSuccess;
}
