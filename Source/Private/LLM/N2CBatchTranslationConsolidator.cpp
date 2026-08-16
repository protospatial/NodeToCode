// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBatchTranslationConsolidator.h"

<<<<<<< HEAD
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
=======
#include "Core/N2CSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
>>>>>>> 38cd82a23ba8e51bf03ff6b70ec83cbd1acc7efe
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

<<<<<<< HEAD
FString ExtractDeclarationFunctionName(const FString& Declaration)
{
    const int32 OpenParen = Declaration.Find(
        TEXT("("),
        ESearchCase::CaseSensitive,
        ESearchDir::FromEnd);
    if (OpenParen == INDEX_NONE)
    {
        return FString();
    }

    int32 End = OpenParen - 1;
    while (End >= 0 && FChar::IsWhitespace(Declaration[End]))
    {
        --End;
    }

    int32 Start = End;
    while (Start >= 0 && (FChar::IsAlnum(Declaration[Start]) || Declaration[Start] == TEXT('_')))
    {
        --Start;
    }

    return End >= Start + 1
        ? Declaration.Mid(Start + 1, End - Start)
        : FString();
}

bool ReplaceFunctionDeclaration(
    FString& Header,
    const FString& FunctionName,
    const FString& ReplacementDeclaration)
{
    if (FunctionName.IsEmpty() || ReplacementDeclaration.IsEmpty())
    {
        return false;
    }

    const int32 FunctionIndex = Header.Find(
        FunctionName + TEXT("("),
        ESearchCase::CaseSensitive);
    if (FunctionIndex == INDEX_NONE)
    {
        return false;
    }

    const int32 SemicolonIndex = Header.Find(
        TEXT(";"),
        ESearchCase::CaseSensitive,
        ESearchDir::FromStart,
        FunctionIndex);
    if (SemicolonIndex == INDEX_NONE)
    {
        return false;
    }

    int32 ReplaceStart = FunctionIndex;
    while (ReplaceStart > 0 && Header[ReplaceStart - 1] != TEXT('\n'))
    {
        --ReplaceStart;
    }

    // Include the immediately associated UFUNCTION macro when replacing a method declaration.
    // A semicolon or closing brace between the macro and method means the macro belongs to an
    // earlier declaration and must not be consumed.
    const int32 UFunctionIndex = Header.Find(
        TEXT("UFUNCTION("),
        ESearchCase::CaseSensitive,
        ESearchDir::FromEnd,
        FunctionIndex);
    if (UFunctionIndex != INDEX_NONE)
    {
        const FString BetweenMacroAndFunction = Header.Mid(
            UFunctionIndex,
            FunctionIndex - UFunctionIndex);
        if (!BetweenMacroAndFunction.Contains(TEXT(";")) &&
            !BetweenMacroAndFunction.Contains(TEXT("}")))
        {
            ReplaceStart = UFunctionIndex;
        }
    }

    Header = Header.Left(ReplaceStart) +
        ReplacementDeclaration.TrimStartAndEnd() +
        Header.Mid(SemicolonIndex + 1);
    return true;
}

FString NormalizeDefinitionOwner(
    const FString& Source,
    const FString& FunctionName,
    const FString& ClassName)
{
    if (Source.IsEmpty() || FunctionName.IsEmpty() || ClassName.IsEmpty())
    {
        return Source;
    }

    const int32 FunctionIndex = Source.Find(
        FunctionName + TEXT("("),
        ESearchCase::CaseSensitive);
    if (FunctionIndex == INDEX_NONE)
    {
        return Source;
    }

    const int32 ScopeIndex = Source.Find(
        TEXT("::"),
        ESearchCase::CaseSensitive,
        ESearchDir::FromEnd,
        FunctionIndex);
    if (ScopeIndex == INDEX_NONE || ScopeIndex >= FunctionIndex)
    {
        return Source;
    }

    int32 OwnerEnd = ScopeIndex - 1;
    while (OwnerEnd >= 0 && FChar::IsWhitespace(Source[OwnerEnd]))
    {
        --OwnerEnd;
    }

    int32 OwnerStart = OwnerEnd;
    while (OwnerStart >= 0 &&
           (FChar::IsAlnum(Source[OwnerStart]) || Source[OwnerStart] == TEXT('_')))
    {
        --OwnerStart;
    }
    ++OwnerStart;

    if (OwnerStart > OwnerEnd)
    {
        return Source;
    }

    const FString ExistingOwner = Source.Mid(OwnerStart, OwnerEnd - OwnerStart + 1);
    if (ExistingOwner.Equals(ClassName, ESearchCase::CaseSensitive))
    {
        return Source;
    }

    return Source.Left(OwnerStart) + ClassName + Source.Mid(OwnerEnd + 1);
}

int32 FindMatchingClosingBrace(const FString& Text, int32 OpenBraceIndex)
{
    if (OpenBraceIndex == INDEX_NONE || OpenBraceIndex >= Text.Len() || Text[OpenBraceIndex] != TEXT('{'))
    {
        return INDEX_NONE;
    }

    int32 Depth = 0;
    for (int32 Index = OpenBraceIndex; Index < Text.Len(); ++Index)
    {
        if (Text[Index] == TEXT('{'))
        {
            ++Depth;
        }
        else if (Text[Index] == TEXT('}'))
        {
            --Depth;
            if (Depth == 0)
            {
                return Index;
            }
        }
    }

    return INDEX_NONE;
}

FString IndentBlock(const FString& Block)
{
    TArray<FString> Lines;
    Block.ParseIntoArrayLines(Lines, false);

    FString Result;
    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
        if (Index > 0)
        {
            Result += TEXT("\n");
        }

        if (!Lines[Index].IsEmpty())
        {
            Result += TEXT("    ");
        }
        Result += Lines[Index];
    }
    return Result;
}

void AppendNamedSection(FString& Target, const FString& Name, const FString& Content)
{
    const FString Trimmed = Content.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return;
    }

    if (!Target.IsEmpty())
    {
        Target += TEXT("\n\n");
    }

    if (!Name.IsEmpty())
    {
        Target += FString::Printf(TEXT("// ----- %s -----\n"), *Name);
    }
    Target += Trimmed;
}

void AppendDefinitionBlocksForToken(
    const FString& Source,
    const FString& SignatureToken,
    FString& OutImplementation)
{
    int32 SearchFrom = 0;
    while (SearchFrom < Source.Len())
    {
        const int32 TokenIndex = Source.Find(
            SignatureToken,
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            SearchFrom);
        if (TokenIndex == INDEX_NONE)
        {
            break;
        }

        const int32 OpenBrace = Source.Find(
            TEXT("{"),
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            TokenIndex + SignatureToken.Len());
        if (OpenBrace == INDEX_NONE)
        {
            break;
        }

        const int32 CloseBrace = FindMatchingClosingBrace(Source, OpenBrace);
        if (CloseBrace == INDEX_NONE)
        {
            break;
        }

        int32 BlockStart = TokenIndex;
        while (BlockStart > 0 && Source[BlockStart - 1] != TEXT('\n'))
        {
            --BlockStart;
        }

        AppendNamedSection(
            OutImplementation,
            FString(),
            Source.Mid(BlockStart, CloseBrace - BlockStart + 1));

        SearchFrom = CloseBrace + 1;
    }
}

FString ExtractClassLifetimeImplementation(const FString& Source, const FString& ClassName)
{
    if (Source.IsEmpty() || ClassName.IsEmpty())
    {
        return FString();
    }

    FString Result;
    AppendDefinitionBlocksForToken(
        Source,
        ClassName + TEXT("::") + ClassName + TEXT("("),
        Result);
    AppendDefinitionBlocksForToken(
        Source,
        ClassName + TEXT("::~") + ClassName + TEXT("("),
        Result);
    return Result;
}

FString ResolveClassName(
    const FN2CTranslationResponse& SessionResponse,
    const FN2CBlueprint& Blueprint,
    const FN2CGraphTranslation*& OutClassGraph)
{
    OutClassGraph = nullptr;

    for (const FN2CGraphTranslation& Graph : SessionResponse.Graphs)
    {
        if (Graph.GraphType.Equals(TEXT("ClassItSelf"), ESearchCase::IgnoreCase))
        {
            OutClassGraph = &Graph;
            if (!Graph.GraphClass.TrimStartAndEnd().IsEmpty())
            {
                return Graph.GraphClass.TrimStartAndEnd();
            }
            break;
        }
    }

    for (const FN2CGraphTranslation& Graph : SessionResponse.Graphs)
    {
        if (!Graph.GraphClass.TrimStartAndEnd().IsEmpty())
        {
            return Graph.GraphClass.TrimStartAndEnd();
        }
    }

    if (!Blueprint.Metadata.BlueprintClass.TrimStartAndEnd().IsEmpty())
    {
        return Blueprint.Metadata.BlueprintClass.TrimStartAndEnd();
    }

    return Blueprint.Metadata.Name.TrimStartAndEnd();
}
}

bool FN2CBatchTranslationConsolidator::BuildCppResponse(
    const FN2CTranslationResponse& SessionResponse,
    const FN2CBlueprint& Blueprint,
    FN2CTranslationResponse& OutResponse)
{
    OutResponse = FN2CTranslationResponse();
    OutResponse.Usage = SessionResponse.Usage;

    if (SessionResponse.Graphs.IsEmpty())
    {
        return false;
    }

    const FN2CGraphTranslation* ClassGraph = nullptr;
    FString ClassName = N2CBatchTranslationConsolidatorPrivate::ResolveClassName(
        SessionResponse,
        Blueprint,
        ClassGraph);
    if (ClassName.IsEmpty())
    {
        ClassName = TEXT("TranslatedBlueprint");
    }

    FString Header;
    FString Implementation;
    FString Notes;

    if (ClassGraph)
    {
        Header = ClassGraph->Code.GraphDeclaration.TrimStartAndEnd();

        // ClassItSelf is the class-structure authority. Its implementation is expected to contain
        // constructor/destructor/class-lifetime setup only. Some models also emit placeholder
        // implementations for graph functions there; retain only constructor/destructor blocks so
        // the graph-specific responses remain authoritative and duplicate definitions are avoided.
        Implementation = N2CBatchTranslationConsolidatorPrivate::ExtractClassLifetimeImplementation(
            ClassGraph->Code.GraphImplementation,
            ClassName);

        if (!ClassGraph->Code.ImplementationNotes.TrimStartAndEnd().IsEmpty())
        {
            N2CBatchTranslationConsolidatorPrivate::AppendNamedSection(
                Notes,
                ClassGraph->GraphName,
                ClassGraph->Code.ImplementationNotes);
        }
    }

    TArray<TPair<FString, FString>> AdditionalDeclarations;

    for (const FN2CGraphTranslation& Graph : SessionResponse.Graphs)
    {
        if (&Graph == ClassGraph)
        {
            continue;
        }

        FString FunctionName;
        const FString Declaration = Graph.Code.GraphDeclaration.TrimStartAndEnd();
        if (!Declaration.IsEmpty())
        {
            FunctionName = N2CBatchTranslationConsolidatorPrivate::ExtractDeclarationFunctionName(
                Declaration);

            // A dedicated graph response is more specific than the ClassItSelf summary. Replace an
            // existing ClassItSelf method declaration (including its UFUNCTION macro) so parameter
            // qualifiers/signatures stay consistent with the detailed implementation.
            const bool bReplacedExisting = !FunctionName.IsEmpty() &&
                N2CBatchTranslationConsolidatorPrivate::ReplaceFunctionDeclaration(
                    Header,
                    FunctionName,
                    Declaration);

            if (!bReplacedExisting &&
                !Header.Contains(Declaration, ESearchCase::CaseSensitive))
            {
                AdditionalDeclarations.Emplace(Graph.GraphName, Declaration);
            }
        }

        FString GraphImplementation = Graph.Code.GraphImplementation.TrimStartAndEnd();
        if (!GraphImplementation.IsEmpty())
        {
            GraphImplementation = N2CBatchTranslationConsolidatorPrivate::NormalizeDefinitionOwner(
                GraphImplementation,
                FunctionName,
                ClassName);

            if (!Implementation.Contains(GraphImplementation, ESearchCase::CaseSensitive))
            {
                N2CBatchTranslationConsolidatorPrivate::AppendNamedSection(
                    Implementation,
                    Graph.GraphName,
                    GraphImplementation);
            }
        }

        if (!Graph.Code.ImplementationNotes.TrimStartAndEnd().IsEmpty())
        {
            N2CBatchTranslationConsolidatorPrivate::AppendNamedSection(
                Notes,
                Graph.GraphName,
                Graph.Code.ImplementationNotes);
        }
    }

    if (!AdditionalDeclarations.IsEmpty())
    {
        FString DeclarationBlock;
        for (const TPair<FString, FString>& Entry : AdditionalDeclarations)
        {
            N2CBatchTranslationConsolidatorPrivate::AppendNamedSection(
                DeclarationBlock,
                Entry.Key,
                Entry.Value);
        }

        // If ClassItSelf returned a full class declaration, place missing declarations inside that
        // class body. If it returned only an extension/snippet (as in older prompts/models), append
        // the missing declarations to that snippet rather than inventing a class skeleton.
        const int32 ClassNameIndex = Header.Find(ClassName, ESearchCase::CaseSensitive);
        const int32 OpenBrace = ClassNameIndex != INDEX_NONE
            ? Header.Find(
                TEXT("{"),
                ESearchCase::CaseSensitive,
                ESearchDir::FromStart,
                ClassNameIndex + ClassName.Len())
            : INDEX_NONE;
        const int32 CloseBrace = N2CBatchTranslationConsolidatorPrivate::FindMatchingClosingBrace(
            Header,
            OpenBrace);

        if (CloseBrace != INDEX_NONE)
        {
            const FString Insertion = TEXT("\n\n") +
                N2CBatchTranslationConsolidatorPrivate::IndentBlock(DeclarationBlock) +
                TEXT("\n");
            Header = Header.Left(CloseBrace) + Insertion + Header.Mid(CloseBrace);
        }
        else
        {
            N2CBatchTranslationConsolidatorPrivate::AppendNamedSection(
                Header,
                TEXT("Additional graph declarations"),
                DeclarationBlock);
        }
    }

    // If there was no ClassItSelf graph, preserve all available implementation/declaration content
    // rather than failing the entire batch. The output remains one pair and the manifest/raw
    // responses still expose the individual graph results for diagnosis.
    if (!ClassGraph && Header.IsEmpty())
    {
        for (const FN2CGraphTranslation& Graph : SessionResponse.Graphs)
        {
            N2CBatchTranslationConsolidatorPrivate::AppendNamedSection(
                Header,
                Graph.GraphName,
                Graph.Code.GraphDeclaration);
        }
    }

    FN2CGraphTranslation ConsolidatedGraph;
    ConsolidatedGraph.GraphName = ClassName;
    ConsolidatedGraph.GraphType = TEXT("ClassItSelf");
    ConsolidatedGraph.GraphClass = ClassName;
    ConsolidatedGraph.Code.GraphDeclaration = Header;
    ConsolidatedGraph.Code.GraphImplementation = Implementation;
    ConsolidatedGraph.Code.ImplementationNotes = Notes;
    OutResponse.Graphs.Add(MoveTemp(ConsolidatedGraph));
=======
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

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutPayload);
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
>>>>>>> 38cd82a23ba8e51bf03ff6b70ec83cbd1acc7efe

    return true;
}

bool FN2CBatchTranslationConsolidator::SaveCppFiles(
    const FN2CTranslationResponse& ConsolidatedResponse,
    const FString& RootPath)
{
<<<<<<< HEAD
    if (ConsolidatedResponse.Graphs.Num() != 1)
    {
        FN2CLogger::Get().LogError(
            TEXT("Expected exactly one consolidated graph when saving full Blueprint C++ output"),
=======
    FString ValidationError;
    if (!ValidateFinalResponse(ConsolidatedResponse, ValidationError))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Refusing to save invalid final consolidation: %s"), *ValidationError),
>>>>>>> 38cd82a23ba8e51bf03ff6b70ec83cbd1acc7efe
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
<<<<<<< HEAD
                TEXT("Saved full Blueprint translation as one C++ pair: %s.h / %s.cpp"),
=======
                TEXT("Saved final LLM-consolidated Blueprint translation: %s.h / %s.cpp"),
>>>>>>> 38cd82a23ba8e51bf03ff6b70ec83cbd1acc7efe
                *FileBaseName,
                *FileBaseName),
            EN2CLogSeverity::Info,
            TEXT("BatchConsolidation"));
    }

    return bSuccess;
}
