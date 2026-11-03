// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpAppResources.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace N2CMcpAppResourcesPrivate
{
	struct FApp
	{
		const TCHAR* Uri;
		const TCHAR* File;
		const TCHAR* Name;
		const TCHAR* Description;
	};

	const FApp Apps[] = {
		{ FN2CMcpAppResources::TranslationViewerUri, TEXT("translation-viewer.html"), TEXT("NodeToCode translation viewer"),
			TEXT("MCP App for translate-focused-blueprint: the translated code, syntax-highlighted, with Declaration and ")
			TEXT("Implementation tabs and the implementation notes.") },
		{ FN2CMcpAppResources::GraphPreviewUri, TEXT("graph-preview.html"), TEXT("NodeToCode graph preview"),
			TEXT("MCP App for get-focused-blueprint: the focused graph's N2C JSON drawn as nodes with execution and data wires.") },
	};

	const FApp* Find(const FString& Uri)
	{
		for (const FApp& App : Apps)
		{
			if (Uri.Equals(App.Uri, ESearchCase::CaseSensitive))
			{
				return &App;
			}
		}
		return nullptr;
	}
}

TArray<FMcpResourceDefinition> FN2CMcpAppResources::GetDefinitions()
{
	TArray<FMcpResourceDefinition> Definitions;
	for (const N2CMcpAppResourcesPrivate::FApp& App : N2CMcpAppResourcesPrivate::Apps)
	{
		FMcpResourceDefinition Definition;
		Definition.Uri = App.Uri;
		Definition.Name = App.Name;
		Definition.Description = App.Description;
		Definition.MimeType = MimeType;
		// Shown to the user by the client; the model gets the tool result's text and structuredContent
		Definition.Annotations = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Audience;
		Audience.Add(MakeShared<FJsonValueString>(TEXT("user")));
		Definition.Annotations->SetArrayField(TEXT("audience"), Audience);
		Definitions.Add(MoveTemp(Definition));
	}
	return Definitions;
}

FString FN2CMcpAppResources::HtmlPath(const FString& Uri)
{
	const N2CMcpAppResourcesPrivate::FApp* App = N2CMcpAppResourcesPrivate::Find(Uri);
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
	if (!App || !Plugin.IsValid())
	{
		return FString();
	}
	return FPaths::ConvertRelativePathToFull(Plugin->GetContentDir() / TEXT("MCPApps") / App->File);
}

FMcpResourceContents FN2CMcpAppResources::Read(const FString& RequestedUri)
{
	FMcpResourceContents Contents;
	Contents.Uri = RequestedUri;
	Contents.MimeType = MimeType;
	const FString Path = HtmlPath(RequestedUri);
	if (Path.IsEmpty())
	{
		Contents.SetError(JsonRpcErrorCodes::ResourceNotFound, FString::Printf(TEXT("No NodeToCode MCP App at %s"), *RequestedUri));
	}
	else if (!FFileHelper::LoadFileToString(Contents.Text, *Path))
	{
		Contents.SetError(JsonRpcErrorCodes::ResourceNotFound, FString::Printf(TEXT("The MCP App file %s is missing from the plugin"), *Path));
	}
	return Contents;
}

TSharedRef<FJsonObject> FN2CMcpAppResources::ToolMeta(const TCHAR* Uri)
{
	const TSharedRef<FJsonObject> Ui = MakeShared<FJsonObject>();
	Ui->SetStringField(TEXT("resourceUri"), Uri);
	const TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetObjectField(TEXT("ui"), Ui);
	return Meta;
}
