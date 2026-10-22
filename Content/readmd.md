{
		"version": "1.0.0",
		"metadata":
		{
			"name": "BP_MCGameInstance",
			"blueprint_type": "Normal",
			"blueprint_class": "BP_MCGameInstance"
		},
		"graphs": [
			{
				"name": "EventGraph",
				"graph_type": "EventGraph",
				"nodes": [
					{
						"id": "N1",
						"type": "CustomEvent",
						"name": "CreateServer",
						"member_name": "None",
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Output Delegate",
								"type": "Delegate"
							},
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N3",
						"type": "CallFunction",
						"name": "Open Level (by Name)",
						"member_parent": "GameplayStatics",
						"member_name": "OpenLevel",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Level Name",
								"type": "Name",
								"default_value": "None",
								"connected": true
							},
							{
								"id": "P4",
								"name": "Absolute",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Options",
								"type": "String",
								"default_value": "Listen"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N5",
						"type": "CallFunction",
						"name": "Print String",
						"member_parent": "KismetSystemLibrary",
						"member_name": "PrintString",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "In String",
								"type": "String",
								"default_value": "Creating Game Session..."
							},
							{
								"id": "P4",
								"name": "Print to Screen",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Print to Log",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P6",
								"name": "Text Color",
								"type": "Struct",
								"sub_type": "LinearColor",
								"default_value": "(R=0.000000,G=1.000000,B=0.007393,A=1.000000)"
							},
							{
								"id": "P7",
								"name": "Duration",
								"type": "Real",
								"sub_type": "float",
								"default_value": "10"
							},
							{
								"id": "P8",
								"name": "Key",
								"type": "Name",
								"default_value": "None",
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N7",
						"type": "CallFunction",
						"name": "Print String",
						"member_parent": "KismetSystemLibrary",
						"member_name": "PrintString",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "In String",
								"type": "String",
								"default_value": "Failed to Create Game Session..."
							},
							{
								"id": "P4",
								"name": "Print to Screen",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Print to Log",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P6",
								"name": "Text Color",
								"type": "Struct",
								"sub_type": "LinearColor",
								"default_value": "(R=0.000000,G=1.000000,B=0.007393,A=1.000000)"
							},
							{
								"id": "P7",
								"name": "Duration",
								"type": "Real",
								"sub_type": "float",
								"default_value": "10"
							},
							{
								"id": "P8",
								"name": "Key",
								"type": "Name",
								"default_value": "None",
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N8",
						"type": "CallFunction",
						"name": "Get Player Controller",
						"member_parent": "GameplayStatics",
						"member_name": "GetPlayerController",
						"pure": true,
						"input_pins": [
							{
								"id": "P1",
								"name": "Player Index",
								"type": "Integer",
								"default_value": "0"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "Return Value",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							}
						]
					},
					{
						"id": "N9",
						"type": "CustomEvent",
						"name": "OpenMainMenu",
						"member_name": "None",
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Output Delegate",
								"type": "Delegate"
							},
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Actor",
								"type": "Object",
								"sub_type": "Actor",
								"connected": true
							}
						]
					},
					{
						"id": "N10",
						"type": "MacroInstance",
						"name": "Can Execute Cosmetic Events",
						"member_parent": "StandardMacros",
						"member_name": "Can Execute Cosmetic Events",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "True",
								"connected": true
							},
							{
								"id": "P3",
								"name": "False"
							}
						]
					},
					{
						"id": "N12",
						"type": "CallFunction",
						"name": "Get Player Controller",
						"member_parent": "GameplayStatics",
						"member_name": "GetPlayerController",
						"pure": true,
						"input_pins": [
							{
								"id": "P1",
								"name": "Player Index",
								"type": "Integer",
								"default_value": "0"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "Return Value",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							}
						]
					},
					{
						"id": "N13",
						"type": "VariableSet",
						"name": "bShowMouseCursor",
						"member_parent": "bool",
						"member_name": "bShowMouseCursor",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Show Mouse Cursor",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Target",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P4",
								"name": "",
								"type": "Boolean",
								"default_value": "false"
							}
						]
					},
					{
						"id": "N15",
						"type": "AsyncAction",
						"name": "Join Session",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P5",
								"name": "Player Controller",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							},
							{
								"id": "P6",
								"name": "Search Result",
								"type": "Struct",
								"sub_type": "BlueprintSessionResult",
								"connected": true,
								"is_reference": true,
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "On Success",
								"connected": true
							},
							{
								"id": "P4",
								"name": "On Failure",
								"connected": true
							}
						]
					},
					{
						"id": "N19",
						"type": "CallFunction",
						"name": "Print String",
						"member_parent": "KismetSystemLibrary",
						"member_name": "PrintString",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "In String",
								"type": "String",
								"default_value": "Joining Game Session..."
							},
							{
								"id": "P4",
								"name": "Print to Screen",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Print to Log",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P6",
								"name": "Text Color",
								"type": "Struct",
								"sub_type": "LinearColor",
								"default_value": "(R=0.000000,G=1.000000,B=0.007393,A=1.000000)"
							},
							{
								"id": "P7",
								"name": "Duration",
								"type": "Real",
								"sub_type": "float",
								"default_value": "5"
							},
							{
								"id": "P8",
								"name": "Key",
								"type": "Name",
								"default_value": "None",
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							}
						]
					},
					{
						"id": "N17",
						"type": "CallFunction",
						"name": "Print String",
						"member_parent": "KismetSystemLibrary",
						"member_name": "PrintString",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "In String",
								"type": "String",
								"default_value": "Joined Game Session..."
							},
							{
								"id": "P4",
								"name": "Print to Screen",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Print to Log",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P6",
								"name": "Text Color",
								"type": "Struct",
								"sub_type": "LinearColor",
								"default_value": "(R=0.000000,G=1.000000,B=0.007393,A=1.000000)"
							},
							{
								"id": "P7",
								"name": "Duration",
								"type": "Real",
								"sub_type": "float",
								"default_value": "5"
							},
							{
								"id": "P8",
								"name": "Key",
								"type": "Name",
								"default_value": "None",
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N18",
						"type": "CallFunction",
						"name": "Print String",
						"member_parent": "KismetSystemLibrary",
						"member_name": "PrintString",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "In String",
								"type": "String",
								"default_value": "Failed to Join Game Session..."
							},
							{
								"id": "P4",
								"name": "Print to Screen",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Print to Log",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P6",
								"name": "Text Color",
								"type": "Struct",
								"sub_type": "LinearColor",
								"default_value": "(R=1.000000,G=0.000000,B=0.059079,A=1.000000)"
							},
							{
								"id": "P7",
								"name": "Duration",
								"type": "Real",
								"sub_type": "float",
								"default_value": "5"
							},
							{
								"id": "P8",
								"name": "Key",
								"type": "Name",
								"default_value": "None",
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N21",
						"type": "CustomEvent",
						"name": "Event_JoinGameSession",
						"member_name": "None",
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Output Delegate",
								"type": "Delegate"
							},
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Join Session",
								"type": "Struct",
								"sub_type": "BlueprintSessionResult",
								"connected": true
							}
						]
					},
					{
						"id": "N23",
						"type": "CallFunction",
						"name": "Get Player Controller",
						"member_parent": "GameplayStatics",
						"member_name": "GetPlayerController",
						"pure": true,
						"input_pins": [
							{
								"id": "P1",
								"name": "Player Index",
								"type": "Integer",
								"default_value": "0"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "Return Value",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							}
						]
					},
					{
						"id": "N14",
						"type": "CallFunction",
						"name": "Set View Target with Blend",
						"member_parent": "PlayerController",
						"member_name": "SetViewTargetWithBlend",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Target",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							},
							{
								"id": "P4",
								"name": "New View Target",
								"type": "Object",
								"sub_type": "Actor",
								"connected": true
							},
							{
								"id": "P5",
								"name": "Blend Time",
								"type": "Real",
								"sub_type": "float",
								"default_value": "0.000000"
							},
							{
								"id": "P6",
								"name": "Blend Func",
								"type": "Byte",
								"sub_type": "EViewTargetBlendFunction",
								"default_value": "VTBlend_Linear"
							},
							{
								"id": "P7",
								"name": "Blend Exp",
								"type": "Real",
								"sub_type": "float",
								"default_value": "0.000000"
							},
							{
								"id": "P8",
								"name": "Lock Outgoing",
								"type": "Boolean",
								"default_value": "false"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N24",
						"type": "CallFunction",
						"name": "Set Input Mode Game And UI",
						"member_parent": "WidgetBlueprintLibrary",
						"member_name": "SetInputMode_GameAndUIEx",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Player Controller",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							},
							{
								"id": "P4",
								"name": "In Widget to Focus",
								"type": "Object",
								"sub_type": "Widget"
							},
							{
								"id": "P5",
								"name": "In Mouse Lock Mode",
								"type": "Byte",
								"sub_type": "EMouseLockMode",
								"default_value": "DoNotLock"
							},
							{
								"id": "P6",
								"name": "Hide Cursor During Capture",
								"type": "Boolean",
								"default_value": "false"
							},
							{
								"id": "P7",
								"name": "Flush Input",
								"type": "Boolean",
								"default_value": "false",
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							}
						]
					},
					{
						"id": "N25",
						"type": "AsyncAction",
						"name": "Create Advanced Session",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P5",
								"name": "Extra Settings",
								"type": "Struct",
								"sub_type": "SessionPropertyKeyPair",
								"is_reference": true,
								"is_const": true,
								"is_array": true
							},
							{
								"id": "P6",
								"name": "Player Controller",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							},
							{
								"id": "P7",
								"name": "Public Connections",
								"type": "Integer",
								"default_value": "100"
							},
							{
								"id": "P8",
								"name": "Private Connections",
								"type": "Integer",
								"default_value": "0"
							},
							{
								"id": "P9",
								"name": "Use LAN",
								"type": "Boolean",
								"default_value": "false"
							},
							{
								"id": "P10",
								"name": "Allow Invites",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P11",
								"name": "Is Dedicated Server",
								"type": "Boolean",
								"default_value": "false"
							},
							{
								"id": "P12",
								"name": "Use Presence",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P13",
								"name": "Use Lobbies if Available",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P14",
								"name": "Allow Join Via Presence",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P15",
								"name": "Allow Join Via Presence Friends Only",
								"type": "Boolean",
								"default_value": "false"
							},
							{
								"id": "P16",
								"name": "Anti Cheat Protected",
								"type": "Boolean",
								"default_value": "false"
							},
							{
								"id": "P17",
								"name": "Uses Stats",
								"type": "Boolean",
								"default_value": "false"
							},
							{
								"id": "P18",
								"name": "Should Advertise",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P19",
								"name": "Use Lobbies Voice Chat if Available",
								"type": "Boolean",
								"default_value": "false"
							},
							{
								"id": "P20",
								"name": "Start After Create",
								"type": "Boolean",
								"default_value": "true"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "On Success",
								"connected": true
							},
							{
								"id": "P4",
								"name": "On Failure",
								"connected": true
							}
						]
					},
					{
						"id": "N27",
						"type": "AsyncAction",
						"name": "Destroy Session",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P5",
								"name": "Player Controller",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							},
							{
								"id": "P3",
								"name": "On Success",
								"connected": true
							},
							{
								"id": "P4",
								"name": "On Failure",
								"connected": true
							}
						]
					},
					{
						"id": "N29",
						"type": "CallFunction",
						"name": "Get Player Controller",
						"member_parent": "GameplayStatics",
						"member_name": "GetPlayerController",
						"pure": true,
						"input_pins": [
							{
								"id": "P1",
								"name": "Player Index",
								"type": "Integer",
								"default_value": "0"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "Return Value",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							}
						]
					},
					{
						"id": "N30",
						"type": "AsyncAction",
						"name": "Destroy Session",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P5",
								"name": "Player Controller",
								"type": "Object",
								"sub_type": "PlayerController",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							},
							{
								"id": "P3",
								"name": "On Success",
								"connected": true
							},
							{
								"id": "P4",
								"name": "On Failure",
								"connected": true
							}
						]
					},
					{
						"id": "N31",
						"type": "CustomEvent",
						"name": "HideMainMenu",
						"member_name": "None",
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Output Delegate",
								"type": "Delegate"
							},
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N32",
						"type": "MacroInstance",
						"name": "Can Execute Cosmetic Events",
						"member_parent": "StandardMacros",
						"member_name": "Can Execute Cosmetic Events",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "True",
								"connected": true
							},
							{
								"id": "P3",
								"name": "False"
							}
						]
					},
					{
						"id": "N34",
						"type": "VariableGet",
						"name": "MenuRef",
						"member_parent": "object/W_StartLogo",
						"member_name": "MenuRef",
						"pure": true,
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Menu Ref",
								"type": "Object",
								"sub_type": "W_StartLogo",
								"connected": true
							}
						]
					},
					{
						"id": "N33",
						"type": "MacroInstance",
						"name": "Is Valid",
						"member_parent": "StandardMacros",
						"member_name": "IsValid",
						"input_pins": [
							{
								"id": "P1",
								"name": "Exec",
								"connected": true
							},
							{
								"id": "P2",
								"name": "Input Object",
								"type": "Object",
								"sub_type": "Object",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P3",
								"name": "Is Valid",
								"connected": true
							},
							{
								"id": "P4",
								"name": "Is Not Valid"
							}
						]
					},
					{
						"id": "N35",
						"type": "CallFunction",
						"name": "Remove from Parent",
						"member_parent": "Widget",
						"member_name": "RemoveFromParent",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Target",
								"type": "Object",
								"sub_type": "Widget",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							}
						]
					},
					{
						"id": "N36",
						"type": "CustomEvent",
						"name": "ShowMainMenu",
						"member_name": "None",
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Output Delegate",
								"type": "Delegate"
							},
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "With Anim",
								"type": "Boolean",
								"connected": true
							}
						]
					},
					{
						"id": "N37",
						"type": "MacroInstance",
						"name": "Can Execute Cosmetic Events",
						"member_parent": "StandardMacros",
						"member_name": "Can Execute Cosmetic Events",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "True",
								"connected": true
							},
							{
								"id": "P3",
								"name": "False"
							}
						]
					},
					{
						"id": "N39",
						"type": "ConstructObjectFromClass",
						"name": "Create Widget",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Class",
								"type": "Class",
								"sub_type": "UserWidget",
								"default_value": "/Game/MagicCard/UI/Hall/Menu/W_StartLogo.W_StartLogo_C"
							},
							{
								"id": "P5",
								"name": "Owning Player",
								"type": "Object",
								"sub_type": "PlayerController"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P4",
								"name": "Return Value",
								"type": "Object",
								"sub_type": "W_StartLogo",
								"connected": true
							}
						]
					},
					{
						"id": "N41",
						"type": "CallFunction",
						"name": "Add to Viewport",
						"member_parent": "UserWidget",
						"member_name": "AddToViewport",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Target",
								"type": "Object",
								"sub_type": "UserWidget",
								"connected": true
							},
							{
								"id": "P4",
								"name": "ZOrder",
								"type": "Integer",
								"default_value": "5"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							}
						]
					},
					{
						"id": "N40",
						"type": "VariableSet",
						"name": "MenuRef",
						"member_parent": "object/W_StartLogo",
						"member_name": "MenuRef",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Menu Ref",
								"type": "Object",
								"sub_type": "W_StartLogo",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P4",
								"name": "",
								"type": "Object",
								"sub_type": "W_StartLogo",
								"connected": true
							}
						]
					},
					{
						"id": "N38",
						"type": "CallFunction",
						"name": "Hide Main Menu",
						"member_parent": "BP_MCGameInstance",
						"member_name": "HideMainMenu",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Target",
								"type": "Object",
								"sub_type": "self"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N42",
						"type": "CallFunction",
						"name": "Play Menu Anim",
						"member_parent": "W_StartLogo",
						"member_name": "PlayMenuAnim",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Target",
								"type": "Object",
								"sub_type": "W_StartLogo",
								"connected": true
							},
							{
								"id": "P4",
								"name": "Play Anim",
								"type": "Boolean",
								"default_value": "false",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N11",
						"type": "CallFunction",
						"name": "Show Main Menu",
						"member_parent": "BP_MCGameInstance",
						"member_name": "ShowMainMenu",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Target",
								"type": "Object",
								"sub_type": "self"
							},
							{
								"id": "P4",
								"name": "With Anim",
								"type": "Boolean",
								"default_value": "true"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							}
						]
					},
					{
						"id": "N16",
						"type": "VariableSet",
						"name": "IsJoiningSession",
						"member_parent": "bool",
						"member_name": "IsJoiningSession",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Is Joining Session",
								"type": "Boolean",
								"default_value": "true"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P4",
								"name": "",
								"type": "Boolean",
								"default_value": "false"
							}
						]
					},
					{
						"id": "N22",
						"type": "Branch",
						"name": "Branch",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P2",
								"name": "Condition",
								"type": "Boolean",
								"default_value": "true",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P3",
								"name": "True",
								"connected": true
							},
							{
								"id": "P4",
								"name": "False",
								"connected": true
							}
						]
					},
					{
						"id": "N44",
						"type": "VariableGet",
						"name": "IsJoiningSession",
						"member_parent": "bool",
						"member_name": "IsJoiningSession",
						"pure": true,
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Is Joining Session",
								"type": "Boolean",
								"default_value": "false",
								"connected": true
							}
						]
					},
					{
						"id": "N20",
						"type": "VariableSet",
						"name": "IsJoiningSession",
						"member_parent": "bool",
						"member_name": "IsJoiningSession",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Is Joining Session",
								"type": "Boolean",
								"default_value": "false"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							},
							{
								"id": "P4",
								"name": "",
								"type": "Boolean",
								"default_value": "false"
							}
						]
					},
					{
						"id": "N45",
						"type": "VariableGet",
						"name": "IsJoiningSession",
						"member_parent": "bool",
						"member_name": "IsJoiningSession",
						"pure": true,
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Is Joining Session",
								"type": "Boolean",
								"default_value": "false",
								"connected": true
							}
						]
					},
					{
						"id": "N2",
						"type": "Branch",
						"name": "Branch",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P2",
								"name": "Condition",
								"type": "Boolean",
								"default_value": "true",
								"connected": true
							}
						],
						"output_pins": [
							{
								"id": "P3",
								"name": "True"
							},
							{
								"id": "P4",
								"name": "False",
								"connected": true
							}
						]
					},
					{
						"id": "N6",
						"type": "VariableSet",
						"name": "IsJoiningSession",
						"member_parent": "bool",
						"member_name": "IsJoiningSession",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Is Joining Session",
								"type": "Boolean",
								"default_value": "true"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							},
							{
								"id": "P4",
								"name": "",
								"type": "Boolean",
								"default_value": "false"
							}
						]
					},
					{
						"id": "N4",
						"type": "VariableSet",
						"name": "IsJoiningSession",
						"member_parent": "bool",
						"member_name": "IsJoiningSession",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Is Joining Session",
								"type": "Boolean",
								"default_value": "false"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							},
							{
								"id": "P4",
								"name": "",
								"type": "Boolean",
								"default_value": "false"
							}
						]
					},
					{
						"id": "N26",
						"type": "VariableSet",
						"name": "IsLocalGame",
						"member_parent": "bool",
						"member_name": "IsLocalGame",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Is Local Game",
								"type": "Boolean",
								"default_value": "false"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P4",
								"name": "",
								"type": "Boolean",
								"default_value": "false"
							}
						]
					},
					{
						"id": "N28",
						"type": "VariableSet",
						"name": "IsLocalGame",
						"member_parent": "bool",
						"member_name": "IsLocalGame",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "Is Local Game",
								"type": "Boolean",
								"default_value": "false"
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": "",
								"connected": true
							},
							{
								"id": "P4",
								"name": "",
								"type": "Boolean",
								"default_value": "false"
							}
						]
					},
					{
						"id": "N43",
						"type": "CallFunction",
						"name": "Print String",
						"member_parent": "KismetSystemLibrary",
						"member_name": "PrintString",
						"input_pins": [
							{
								"id": "P1",
								"name": "",
								"connected": true
							},
							{
								"id": "P3",
								"name": "In String",
								"type": "String",
								"default_value": "Already Joining Session"
							},
							{
								"id": "P4",
								"name": "Print to Screen",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P5",
								"name": "Print to Log",
								"type": "Boolean",
								"default_value": "true"
							},
							{
								"id": "P6",
								"name": "Text Color",
								"type": "Struct",
								"sub_type": "LinearColor",
								"default_value": "(R=0.000000,G=0.660000,B=1.000000,A=1.000000)"
							},
							{
								"id": "P7",
								"name": "Duration",
								"type": "Real",
								"sub_type": "float",
								"default_value": "2.000000"
							},
							{
								"id": "P8",
								"name": "Key",
								"type": "Name",
								"default_value": "None",
								"is_const": true
							}
						],
						"output_pins": [
							{
								"id": "P2",
								"name": ""
							}
						]
					},
					{
						"id": "N46",
						"type": "VariableGet",
						"name": "Level Name",
						"member_parent": "name",
						"member_name": "Level Name",
						"pure": true,
						"input_pins": [],
						"output_pins": [
							{
								"id": "P1",
								"name": "Level Name",
								"type": "Name",
								"default_value": "None",
								"connected": true
							}
						]
					}
				],
				"flows":
				{
					"execution": [
						"N1->N2",
						"N3->N4",
						"N5->N6",
						"N7->N4",
						"N9->N10",
						"N10->N11",
						"N13->N14",
						"N15->N16",
						"N15->N17",
						"N15->N18",
						"N17->N20",
						"N18->N20",
						"N21->N22",
						"N14->N24",
						"N25->N5",
						"N25->N26",
						"N25->N7",
						"N27->N28",
						"N30->N25",
						"N31->N32",
						"N32->N33",
						"N33->N35",
						"N36->N37",
						"N37->N38",
						"N39->N40",
						"N40->N42",
						"N38->N39",
						"N42->N41",
						"N11->N13",
						"N16->N19",
						"N22->N43",
						"N22->N27",
						"N2->N30",
						"N26->N3",
						"N28->N15"
					],
					"data":
					{
						"N12.P2": "N24.P3",
						"N21.P3": "N15.P6",
						"N23.P2": "N27.P5",
						"N9.P3": "N14.P4",
						"N8.P2": "N25.P6",
						"N29.P2": "N30.P5",
						"N34.P1": "N35.P3",
						"N39.P4": "N40.P3",
						"N40.P4": "N42.P3",
						"N36.P3": "N42.P4",
						"N44.P1": "N22.P2",
						"N45.P1": "N2.P2",
						"N46.P1": "N3.P3"
					}
				}
			}
		],
		"structs": [],
		"enums": []
	}