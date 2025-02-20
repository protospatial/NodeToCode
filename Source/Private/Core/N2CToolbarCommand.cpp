// Copyright ProtoSpatial. All Rights Reserved.

#include "Core/N2CToolbarCommand.h"

#include "Styling/AppStyle.h"
#include "Framework/Commands/UICommandList.h"
#include "Utils/N2CLogger.h"

#define LOCTEXT_NAMESPACE "NodeToCode"

const FName FN2CToolbarCommand::CommandName_Open = TEXT("NodeToCode_OpenWindow");
const FName FN2CToolbarCommand::CommandName_Collect = TEXT("NodeToCode_CollectNodes");
const FText FN2CToolbarCommand::CommandLabel_Open = NSLOCTEXT("NodeToCode", "OpenWindow", "Open Node to Code");
const FText FN2CToolbarCommand::CommandLabel_Collect = NSLOCTEXT("NodeToCode", "CollectNodes", "Collect and Translate Nodes");
const FText FN2CToolbarCommand::CommandTooltip_Open = NSLOCTEXT("NodeToCode", "OpenWindowTooltip", "Open the Node to Code window");
const FText FN2CToolbarCommand::CommandTooltip_Collect = NSLOCTEXT("NodeToCode", "CollectNodesTooltip", "Collect nodes from current Blueprint graph and translate to code");

FN2CToolbarCommand::FN2CToolbarCommand()
    : TCommands<FN2CToolbarCommand>(
        TEXT("NodeToCode"), // Context name for fast lookup
        NSLOCTEXT("NodeToCode", "NodeToCode", "Node to Code"), // Localized context name
        NAME_None, // Parent
        FAppStyle::GetAppStyleSetName() // Icon style
    )
{
}

void FN2CToolbarCommand::RegisterCommands()
{
    FN2CLogger::Get().Log(TEXT("Registering N2C toolbar commands"), EN2CLogSeverity::Debug);
    
    UI_COMMAND(
        OpenWindowCommand,
        "Open Node to Code Editor",
        "Open the Node to Code Editor window",
        EUserInterfaceActionType::Button,
        FInputChord()
    );

    UI_COMMAND(
        CollectNodesCommand,
        "Translate Blueprint Graph to Code",
        "Translate current Blueprint graph to code.\nResults will be in the Node to Code Editor window.",
        EUserInterfaceActionType::Button,
        FInputChord()
    );
    
    FN2CLogger::Get().Log(TEXT("N2C toolbar commands registered"), EN2CLogSeverity::Debug);
}

#undef LOCTEXT_NAMESPACE
