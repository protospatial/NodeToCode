// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CEditorIntegration.h"

#include "BlueprintEditorModes.h"
#include "Core/N2CNodeCollector.h"
#include "BlueprintEditorModule.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CEditorWindow.h"
#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CToolbarCommand.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMTypes.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformApplicationMisc.h"
#endif

#if PLATFORM_MAC
#include "Mac/MacPlatformApplicationMisc.h"
#endif

FN2CEditorIntegration& FN2CEditorIntegration::Get()
{
    static FN2CEditorIntegration Instance;
    return Instance;
}

void FN2CEditorIntegration::ExecuteTranslateEntireBlueprintForEditor(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Check if translation is already in progress
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing)
    {
        FN2CLogger::Get().LogWarning(TEXT("Translation already in progress, please wait"));
        return;
    }

    // Show the window as a tab
    FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);

    // Get the editor pointer
    TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
    if (!Editor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
        return;
    }

    // Get focused graph to resolve owning blueprint
    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
        return;
    }

    UBlueprint* OwnerBP = Cast<UBlueprint>(FocusedGraph->GetOuter());
    if (!OwnerBP)
    {
        FN2CLogger::Get().LogError(TEXT("Focused graph has no owning Blueprint"));
        return;
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const bool bIncludeVariables = Settings ? Settings->bIncludeVariables : true;

    const FString BlueprintName = OwnerBP->GetName();

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Starting full Blueprint translation for: %s"), *BlueprintName),
        EN2CLogSeverity::Info
    );

    if (!LLMModule || !LLMModule->Initialize())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to initialize LLM Module"));
        return;
    }

    // Begin batch translation - all graphs in this Blueprint will share the same root directory
    LLMModule->BeginBatchTranslation(BlueprintName);

    // Use a single Blueprint-wide translation to build the full FN2CBlueprint,
    // which already contains all graphs (including the synthetic ClassItSelf),
    // then emit one JSON per graph by slicing the Graphs array.
    FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();

    if (!Translator.GenerateFromBlueprint(OwnerBP, bIncludeVariables))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to generate Blueprint-wide translation for Translate Entire Blueprint"));
        // End batch translation on error
        if (LLMModule)
        {
            LLMModule->EndBatchTranslation();
        }
        return;
    }

    const FN2CBlueprint& FullBlueprint = Translator.GetN2CBlueprint();
    if (!FullBlueprint.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Blueprint-wide node translation validation failed for Translate Entire Blueprint"));
        // End batch translation on error
        if (LLMModule)
        {
            LLMModule->EndBatchTranslation();
        }
        return;
    }

    FN2CLogger::Get().Log(TEXT("Blueprint-wide translation successful for Translate Entire Blueprint"), EN2CLogSeverity::Info);

    // First pass: build per-graph JSON payloads so we know how many requests
    // will be sent for this Blueprint (used for batch completion logging).
    TArray<TPair<FString, FString>> PendingRequests; // Json, GraphName
    TArray<FString> SerializationFailedGraphs; // Track graphs that failed JSON serialization

    for (const FN2CGraph& Graph : FullBlueprint.Graphs)
    {
        const FString GraphName = Graph.Name;
        if (GraphName.IsEmpty())
        {
            continue;
        }

        FN2CBlueprint PerGraphBlueprint;
        PerGraphBlueprint.Version    = FullBlueprint.Version;
        PerGraphBlueprint.Metadata   = FullBlueprint.Metadata;
        PerGraphBlueprint.Variables  = FullBlueprint.Variables;
        PerGraphBlueprint.Components = FullBlueprint.Components;
        PerGraphBlueprint.Structs    = FullBlueprint.Structs;
        PerGraphBlueprint.Enums      = FullBlueprint.Enums;
        PerGraphBlueprint.Graphs.Reset(1);
        PerGraphBlueprint.Graphs.Add(Graph);

        FN2CSerializer::SetPrettyPrint(false);
        FString JsonOutput = FN2CSerializer::ToJson(PerGraphBlueprint);
        if (JsonOutput.IsEmpty())
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("JSON serialization failed for graph: %s"), *GraphName));
            SerializationFailedGraphs.Add(GraphName);
            continue;
        }

        PendingRequests.Emplace(JsonOutput, GraphName);
    }

    if (PendingRequests.Num() == 0)
    {
        FN2CLogger::Get().LogWarning(TEXT("No valid graphs to translate for this Blueprint"));
        // End batch translation since there are no requests
        if (LLMModule)
        {
            LLMModule->EndBatchTranslation();
        }
        return;
    }

    // Shared counter and result tracking for batch completion logging.
    // We keep these local to the editor integration and update them from the per-request callback.
    const int32 TotalRequests = PendingRequests.Num();
    const int32 TotalGraphs = FullBlueprint.Graphs.Num(); // Total including serialization failures
    TSharedRef<int32> RemainingResponses = MakeShared<int32>(TotalRequests);
    TSharedRef<TArray<FString>> SuccessfulGraphs = MakeShared<TArray<FString>>();
    TSharedRef<TArray<FString>> FailedGraphs = MakeShared<TArray<FString>>(SerializationFailedGraphs); // Include serialization failures

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Starting batch translation: %d graphs to translate for Blueprint: %s (%d requests queued, %d failed serialization)"),
            TotalGraphs, *BlueprintName, TotalRequests, SerializationFailedGraphs.Num()),
        EN2CLogSeverity::Info
    );

    // Second pass: actually send requests to the LLM
    for (const TPair<FString, FString>& Request : PendingRequests)
    {
        const FString& JsonOutput = Request.Key;
        const FString& GraphName  = Request.Value;

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Sending translation request for graph: %s"), *GraphName),
            EN2CLogSeverity::Debug
        );
        FN2CLogger::Get().Log(TEXT("JSON Output:"), EN2CLogSeverity::Debug);
        FN2CLogger::Get().Log(JsonOutput, EN2CLogSeverity::Debug);

        LLMModule->ProcessN2CJson(JsonOutput, FOnLLMResponseReceived::CreateLambda(
            [GraphName, RemainingResponses, BlueprintName, SuccessfulGraphs, FailedGraphs, TotalGraphs](const FString& Response)
            {
                bool bSuccess = false;
                FString ErrorMessage;

                FN2CLogger::Get().Log(
                    FString::Printf(TEXT("Received LLM response for graph: %s"), *GraphName),
                    EN2CLogSeverity::Debug
                );
                FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response for graph %s:\n\n%s"), *GraphName, *Response), EN2CLogSeverity::Debug);
                
                FN2CTranslationResponse TranslationResponse;
                TScriptInterface<IN2CLLMService> ActiveService = UN2CLLMModule::Get()->GetActiveService();
                if (ActiveService.GetInterface())
                {
                    UN2CResponseParserBase* Parser = ActiveService->GetResponseParser();
                    if (Parser)
                    {
                        if (Parser->ParseLLMResponse(Response, TranslationResponse))
                        {
                            FN2CLogger::Get().Log(
                                FString::Printf(TEXT("Successfully parsed LLM response for graph: %s"), *GraphName),
                                EN2CLogSeverity::Info
                            );
                            bSuccess = true;
                            SuccessfulGraphs->Add(GraphName);
                        }
                        else
                        {
                            ErrorMessage = TEXT("Failed to parse LLM response");
                            FN2CLogger::Get().LogError(
                                FString::Printf(TEXT("Failed to parse LLM response for graph: %s"), *GraphName)
                            );
                            FailedGraphs->Add(GraphName);
                        }
                    }
                    else
                    {
                        ErrorMessage = TEXT("No response parser available");
                        FN2CLogger::Get().LogError(
                            FString::Printf(TEXT("No response parser available for graph: %s"), *GraphName)
                        );
                        FailedGraphs->Add(GraphName);
                    }
                }
                else
                {
                    ErrorMessage = TEXT("No active LLM service");
                    FN2CLogger::Get().LogError(
                        FString::Printf(TEXT("No active LLM service for graph: %s"), *GraphName)
                    );
                    FailedGraphs->Add(GraphName);
                }

                // Decrement remaining counter and log summary when the batch completes.
                const int32 NewRemaining = --(*RemainingResponses);
                if (NewRemaining <= 0)
                {
                    // Build summary message
                    FString Summary = FString::Printf(
                        TEXT("Full Blueprint translation complete for: %s\n")
                        TEXT("  Total graphs: %d\n")
                        TEXT("  Successful: %d\n")
                        TEXT("  Failed: %d"),
                        *BlueprintName,
                        TotalGraphs,
                        SuccessfulGraphs->Num(),
                        FailedGraphs->Num()
                    );

                    if (FailedGraphs->Num() > 0)
                    {
                        Summary += TEXT("\n  Failed graphs: ");
                        for (int32 i = 0; i < FailedGraphs->Num(); ++i)
                        {
                            if (i > 0)
                            {
                                Summary += TEXT(", ");
                            }
                            Summary += (*FailedGraphs)[i];
                        }
                    }

                    if (SuccessfulGraphs->Num() > 0)
                    {
                        Summary += TEXT("\n  Successful graphs: ");
                        for (int32 i = 0; i < SuccessfulGraphs->Num(); ++i)
                        {
                            if (i > 0)
                            {
                                Summary += TEXT(", ");
                            }
                            Summary += (*SuccessfulGraphs)[i];
                        }
                    }

                    // Log summary with appropriate severity
                    if (FailedGraphs->Num() > 0)
                    {
                        FN2CLogger::Get().LogWarning(Summary);
                    }
                    else
                    {
                        FN2CLogger::Get().Log(Summary, EN2CLogSeverity::Info);
                    }
                    
                    // End batch translation - clear the batch root path
                    UN2CLLMModule* BatchLLMModule = UN2CLLMModule::Get();
                    if (BatchLLMModule)
                    {
                        BatchLLMModule->EndBatchTranslation();
                    }
                }
            }
        ));
    }
}

void FN2CEditorIntegration::ExecuteCopyJsonForEditor(TWeakPtr<FBlueprintEditor> InEditor)
{
    FN2CLogger::Get().Log(TEXT("ExecuteCopyJsonForEditor called"), EN2CLogSeverity::Debug);

    // Get the editor pointer
    TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
    if (!Editor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
        return;
    }
    FN2CLogger::Get().Log(TEXT("Successfully obtained Blueprint Editor pointer"), EN2CLogSeverity::Info);

    // Get focused graph
    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
        return;
    }

    FString GraphName = FocusedGraph->GetName();
    FString BlueprintName = TEXT("Unknown");
    if (UBlueprint* Blueprint = Cast<UBlueprint>(FocusedGraph->GetOuter()))
    {
        BlueprintName = Blueprint->GetName();
    }
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Found focused graph: %s in Blueprint: %s"), 
        *GraphName, *BlueprintName), 
        EN2CLogSeverity::Info
    );

    // Get collector instance
    FN2CNodeCollector& Collector = FN2CNodeCollector::Get();

    // Collect nodes using the specific editor
    TArray<UK2Node*> CollectedNodes;
    if (Collector.CollectNodesFromGraph(FocusedGraph, CollectedNodes))
    {
        FString Context = FString::Printf(TEXT("Collected %d nodes"), CollectedNodes.Num());
        FN2CLogger::Get().Log(TEXT("Node collection successful"), EN2CLogSeverity::Info, Context);

        // Get translator instance                                                                                                                                                                        
        FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();

        // Generate N2CStruct from collected nodes
        if (Translator.GenerateN2CStruct(CollectedNodes))
        {
            FN2CLogger::Get().Log(TEXT("Node translation successful"), EN2CLogSeverity::Info);

            // Get the Blueprint structure
            const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();

            // Validate the generated Blueprint
            if (Blueprint.IsValid())
            {
                FN2CLogger::Get().Log(TEXT("Node translation validation successful"), EN2CLogSeverity::Info);

                // Serialize to JSON with pretty printing enabled for clipboard                                                                                                                                   
                FN2CSerializer::SetPrettyPrint(true);
                FString JsonOutput = FN2CSerializer::ToJson(Blueprint);                                                                                                                                       

                // Copy JSON to clipboard if not empty                                                                                                                                                         
                if (!JsonOutput.IsEmpty())                                                                                                                                                                    
                {                                                                                                                                                                                             
                    FPlatformApplicationMisc::ClipboardCopy(*JsonOutput);

                    // Show notification
                    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "BlueprintJsonCopied", "Blueprint JSON copied to clipboard"));
                    Info.bFireAndForget = true;
                    Info.FadeInDuration = 0.2f;
                    Info.FadeOutDuration = 0.5f;
                    Info.ExpireDuration = 2.0f;
                    FSlateNotificationManager::Get().AddNotification(Info);

                    FN2CLogger::Get().Log(TEXT("Blueprint JSON copied to clipboard successfully"), EN2CLogSeverity::Info);
                }
                else
                {
                    FN2CLogger::Get().LogError(TEXT("JSON serialization failed"));
                }
            }
            else
            {
                FN2CLogger::Get().LogError(TEXT("Node translation validation failed"));
            }
        }
        else
        {
            FN2CLogger::Get().LogError(TEXT("Failed to translate nodes"));
        }
    }
}

void FN2CEditorIntegration::Initialize()
{
    // Register commands
    FN2CToolbarCommand::Register();

    // Register tab spawner
    SN2CEditorWindow::RegisterTabSpawner();

    // Subscribe to asset editor opened events
    if (GEditor)
    {
        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (ensure(AssetEditorSubsystem))
        {
            AssetEditorSubsystem->OnAssetEditorOpened().AddLambda([this](UObject* Asset)
            {
                if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Asset, false))
                {
                    HandleAssetEditorOpened(Asset, EditorInstance);
                }
            });
            FN2CLogger::Get().Log(TEXT("N2C Editor Integration: Subscribed to OnAssetEditorOpened via AssetEditorSubsystem"), EN2CLogSeverity::Info);
        }
    }

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration initialized"), EN2CLogSeverity::Info);
}

void FN2CEditorIntegration::Shutdown()
{
    // Unregister tab spawner
    SN2CEditorWindow::UnregisterTabSpawner();

    // Clear editor command lists
    EditorCommandLists.Empty();

    // Unsubscribe from asset editor events
    if (GEditor)
    {
        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (AssetEditorSubsystem)
        {
            AssetEditorSubsystem->OnAssetEditorOpened().RemoveAll(this);
        }
    }

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration shutdown"), EN2CLogSeverity::Info);
}


TSharedPtr<FBlueprintEditor> FN2CEditorIntegration::GetBlueprintEditorFromTab() const
{
    // Mark as deprecated
    FN2CLogger::Get().LogWarning(TEXT("GetBlueprintEditorFromTab is deprecated - editors should be accessed directly"));
    return nullptr;
}

void FN2CEditorIntegration::HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance)
{
    if (!Asset || !EditorInstance)
    {
        return;
    }

    // Check if the asset is a Blueprint or a child class of Blueprint
    UBlueprint* OpenedBlueprint = Cast<UBlueprint>(Asset);
    if (!OpenedBlueprint)
    {
        return; // Not a Blueprint, so ignore
    }

    // Convert the EditorInstance to the correct type
    FBlueprintEditor* BlueprintEditorPtr = static_cast<FBlueprintEditor*>(EditorInstance);
    if (!BlueprintEditorPtr)
    {
        return;
    }

    // Convert to SharedPtr so it matches our existing RegisterToolbarForEditor() signature
    TSharedPtr<FBlueprintEditor> BlueprintEditorShared = StaticCastSharedRef<FBlueprintEditor>(BlueprintEditorPtr->AsShared());
    if (BlueprintEditorShared.IsValid())
    {
        // Check if we already have this editor registered
        TWeakPtr<FBlueprintEditor> WeakEditor(BlueprintEditorShared);
        if (!EditorCommandLists.Contains(WeakEditor))
        {
            FString BlueprintPath = OpenedBlueprint->GetPathName();
            
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Registering toolbar for Blueprint Editor: %s"), 
                *BlueprintPath), 
                EN2CLogSeverity::Info
            );
            
            RegisterToolbarForEditor(BlueprintEditorShared);
        }
        else
        {
            FN2CLogger::Get().Log(
                TEXT("Blueprint Editor already registered"), 
                EN2CLogSeverity::Debug
            );
        }
    }
}


void FN2CEditorIntegration::RegisterToolbarForEditor(TSharedPtr<FBlueprintEditor> InEditor)
{
    FN2CLogger::Get().Log(TEXT("Starting toolbar registration for editor"), EN2CLogSeverity::Info);

    if (!InEditor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid editor pointer provided to RegisterToolbarForEditor"));
        return;
    }

    // Get Blueprint name for context
    FString BlueprintName = TEXT("Unknown");
    if (InEditor->GetBlueprintObj())
    {
        BlueprintName = InEditor->GetBlueprintObj()->GetName();
    }

    // Check if we already have a command list for this editor
    TWeakPtr<FBlueprintEditor> WeakEditor(InEditor);
    if (EditorCommandLists.Contains(WeakEditor))
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Editor already has command list registered: %s"), *BlueprintName),
            EN2CLogSeverity::Warning
        );
        return;
    }

    // Create command list for this editor
    TSharedPtr<FUICommandList> CommandList = MakeShareable(new FUICommandList);
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Created command list for Blueprint: %s"), *BlueprintName), 
        EN2CLogSeverity::Info
    );

    // Map the Open Window command
    CommandList->MapAction(
        FN2CToolbarCommand::Get().OpenWindowCommand,
        FExecuteAction::CreateLambda([this]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
            FN2CLogger::Get().Log(TEXT("Node to Code window opened"), EN2CLogSeverity::Info);
        }),
        FCanExecuteAction::CreateLambda([]() { return true; })
    );

    // Map the Collect Nodes command
    CommandList->MapAction(
        FN2CToolbarCommand::Get().CollectNodesCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Node to Code collection triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteCollectNodesForEditor(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    // Map the Translate Entire Blueprint command
    CommandList->MapAction(
        FN2CToolbarCommand::Get().TranslateEntireBlueprintCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Translate Entire Blueprint triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteTranslateEntireBlueprintForEditor(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );
    
    // Map the Copy JSON command
    CommandList->MapAction(
        FN2CToolbarCommand::Get().CopyJsonCommand,
        FExecuteAction::CreateLambda([this, WeakEditor, BlueprintName]()
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Copy Blueprint JSON triggered for Blueprint: %s"), *BlueprintName),
                EN2CLogSeverity::Info
            );
            ExecuteCopyJsonForEditor(WeakEditor);
        }),
        FCanExecuteAction::CreateLambda([WeakEditor]()
        {
            TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (!Editor.IsValid())
            {
                return false;
            }
            return Editor->GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode;
        })
    );

    // Store in our map
    EditorCommandLists.Add(WeakEditor, CommandList);
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Added command list to map for Blueprint: %s"), *BlueprintName),
        EN2CLogSeverity::Info
    );

    // Add toolbar extension
    TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
    ToolbarExtender->AddToolBarExtension(
        "Asset",
        EExtensionHook::After,
        CommandList,
        FToolBarExtensionDelegate::CreateLambda([CommandList](FToolBarBuilder& Builder)
        {
            Builder.BeginSection("NodeToCode");
            
            // Add dropdown button
            Builder.AddComboButton(
                FUIAction(),
                FOnGetContent::CreateLambda([CommandList]() -> TSharedRef<SWidget>
                {
                    FMenuBuilder MenuBuilder(true, CommandList);
                    
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().OpenWindowCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().CollectNodesCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().CopyJsonCommand);
                    MenuBuilder.AddMenuEntry(FN2CToolbarCommand::Get().TranslateEntireBlueprintCommand);

                    return MenuBuilder.MakeWidget();
                }),
                NSLOCTEXT("NodeToCode", "NodeToCodeActions", "Node to Code"),
                NSLOCTEXT("NodeToCode", "NodeToCodeTooltip", "Node to Code Actions"),
                FSlateIcon("NodeToCodeStyle", "NodeToCode.ToolbarButton")
            );
            
            Builder.EndSection();
        })
    );

    // Add the extender to this specific editor
    InEditor->AddToolbarExtender(ToolbarExtender);

    InEditor->RegenerateMenusAndToolbars();
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Completed toolbar registration for Blueprint: %s"), *BlueprintName), 
        EN2CLogSeverity::Info
    );
}

TArray<FName> FN2CEditorIntegration::GetAvailableThemes(EN2CCodeLanguage Language) const
{
    TArray<FName> ThemeNames;
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            Settings->CPPThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::Python:
            Settings->PythonThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::JavaScript:
            Settings->JavaScriptThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::CSharp:
            Settings->CSharpThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::Swift:
            Settings->SwiftThemes.Themes.GetKeys(ThemeNames);
            break;
    }
    
    return ThemeNames;
}

FName FN2CEditorIntegration::GetDefaultTheme(EN2CCodeLanguage Language) const
{
    return TEXT("Unreal Engine");
}

void FN2CEditorIntegration::ExecuteCollectNodesForEditor(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Check if translation is already in progress
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing)
    {
        FN2CLogger::Get().LogWarning(TEXT("Translation already in progress, please wait"));
        return;
    }

    FN2CLogger::Get().Log(TEXT("ExecuteCollectNodesForEditor called"), EN2CLogSeverity::Debug);

    // Show the window as a tab
    FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
    FN2CLogger::Get().Log(TEXT("Node to Code window shown"), EN2CLogSeverity::Debug);

    // Get the editor pointer
    TSharedPtr<FBlueprintEditor> Editor = InEditor.Pin();
    if (!Editor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Invalid Blueprint Editor pointer"));
        return;
    }
    FN2CLogger::Get().Log(TEXT("Successfully obtained Blueprint Editor pointer"), EN2CLogSeverity::Info);

    // Get focused graph
    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        FN2CLogger::Get().LogError(TEXT("No focused graph in Blueprint Editor"));
        return;
    }

    FString GraphName = FocusedGraph->GetName();
    FString BlueprintName = TEXT("Unknown");
    if (UBlueprint* Blueprint = Cast<UBlueprint>(FocusedGraph->GetOuter()))
    {
        BlueprintName = Blueprint->GetName();
    }
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Found focused graph: %s in Blueprint: %s"), 
        *GraphName, *BlueprintName), 
        EN2CLogSeverity::Info
    );

    // Get collector instance
    FN2CNodeCollector& Collector = FN2CNodeCollector::Get();
    
    // Collect nodes using the specific editor
    TArray<UK2Node*> CollectedNodes;
    if (Collector.CollectNodesFromGraph(FocusedGraph, CollectedNodes))
    {
        FString Context = FString::Printf(TEXT("Collected %d nodes"), CollectedNodes.Num());
        FN2CLogger::Get().Log(TEXT("Node collection successful"), EN2CLogSeverity::Info, Context);
        
        // Get translator instance                                                                                                                                                                        
        FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();

        // Generate N2CStruct from collected nodes
        if (Translator.GenerateN2CStruct(CollectedNodes))
        {
            FN2CLogger::Get().Log(TEXT("Node translation successful"), EN2CLogSeverity::Info);

            // Get the Blueprint structure
            const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
            
            // Validate the generated Blueprint
            if (Blueprint.IsValid())
            {
                FN2CLogger::Get().Log(TEXT("Node translation validation successful"), EN2CLogSeverity::Info);

                // Serialize to JSON with pretty printing enabled                                                                                                                                             
                FN2CSerializer::SetPrettyPrint(false);
                FString JsonOutput = FN2CSerializer::ToJson(Blueprint);                                                                                                                                       
                                                                                                                                                                                                           
                // Log the JSON output                                                                                                                                                                        
                if (!JsonOutput.IsEmpty())                                                                                                                                                                    
                {                                                                                                                                                                                             
                    FN2CLogger::Get().Log(TEXT("JSON Output:"), EN2CLogSeverity::Debug);                                                                                                                       
                    FN2CLogger::Get().Log(JsonOutput, EN2CLogSeverity::Debug);
                    
                    if (LLMModule->Initialize())
                    {
                        // Send JSON to LLM service                                                                                                                                                        
                        LLMModule->ProcessN2CJson(JsonOutput, FOnLLMResponseReceived::CreateLambda(
                            [](const FString& Response)                                                                                                                                                   
                            {                                                                                                                                                                             
                                FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response:\n\n%s"), *Response), EN2CLogSeverity::Debug);                                                                                                      
                            
                                // Create translation response struct
                                FN2CTranslationResponse TranslationResponse;
                            
                                // Get active service's response parser
                                TScriptInterface<IN2CLLMService> ActiveService = UN2CLLMModule::Get()->GetActiveService();
                                if (ActiveService.GetInterface())
                                {
                                    UN2CResponseParserBase* Parser = ActiveService->GetResponseParser();
                                    if (Parser)
                                    {
                                        if (Parser->ParseLLMResponse(Response, TranslationResponse))
                                        {
                                            // Log successful parsing
                                            FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                                        }
                                        else
                                        {
                                            FN2CLogger::Get().LogError(TEXT("Failed to parse LLM response"));
                                        }
                                    }
                                    else
                                    {
                                        FN2CLogger::Get().LogError(TEXT("No response parser available"));
                                    }
                                }
                                else
                                {
                                    FN2CLogger::Get().LogError(TEXT("No active LLM service"));
                                }
                            }));
                    }
                    else
                    {
                        FN2CLogger::Get().LogError(TEXT("Failed to initialize LLM Module"));
                    }
                }
                else
                {
                    FN2CLogger::Get().LogError(TEXT("JSON serialization failed"));
                }
            }
            else
            {
                FN2CLogger::Get().LogError(TEXT("Node translation validation failed"));
            }
        }
        else
        {
            FN2CLogger::Get().LogError(TEXT("Failed to translate nodes"));
        }
    }
}
