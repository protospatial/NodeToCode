// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpPythonStubResource.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FMcpResourceDefinition FN2CMcpPythonStubResource::GetDefinition()
{
	FMcpResourceDefinition Definition;
	Definition.Uri = Uri;
	Definition.Name = TEXT("Unreal Python API stub");
	Definition.Description = TEXT("unreal.py, the Python API stub of this editor (Intermediate/PythonStub, written by the Python Editor Script ")
		TEXT("Plugin when its Developer Mode is on): every class, function and signature of the unreal module for this engine and ")
		TEXT("project. Large (several MB): search it rather than reading it whole.");
	Definition.MimeType = TEXT("text/x-python");
	// MCP Annotations (card 0214): reference material for the model, read on demand
	Definition.Annotations = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Audience;
	Audience.Add(MakeShared<FJsonValueString>(TEXT("assistant")));
	Definition.Annotations->SetArrayField(TEXT("audience"), Audience);
	Definition.Annotations->SetNumberField(TEXT("priority"), 0.3);
	return Definition;
}

FString FN2CMcpPythonStubResource::StubPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("PythonStub") / TEXT("unreal.py"));
}

FMcpResourceContents FN2CMcpPythonStubResource::Read(const FString& RequestedUri)
{
	FMcpResourceContents Contents;
	Contents.Uri = RequestedUri;
	Contents.MimeType = TEXT("text/x-python");
	const FString Path = StubPath();
	if (!FFileHelper::LoadFileToString(Contents.Text, *Path))
	{
		Contents.Text = FString::Printf(TEXT("# No Python API stub at %s.\n")
			TEXT("# Generate it: Editor Preferences > Plugins > Python > Developer Mode, then restart the editor\n")
			TEXT("# (the Python Editor Script Plugin writes Intermediate/PythonStub/unreal.py at startup).\n"), *Path);
	}
	return Contents;
}
