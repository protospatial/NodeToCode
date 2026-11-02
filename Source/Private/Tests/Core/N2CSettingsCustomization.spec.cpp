// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/N2CSettings.h"
#include "Core/N2CSettingsCustomization.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PropertyEditorModule.h"

/**
 * Card 0507: the Write MCP Client Config row in Project Settings (MCP Server category). Its button runs the console
 * command's code (FN2CMcpClientConfig::RunCommand) with the chosen client, the token box and the project folder; here
 * the folder is a scratch folder with a space in its name, never a real project or home folder. Each choice must
 * write exactly its own client's file, All every file, and Claude Desktop no file but the snippet.
 */
BEGIN_DEFINE_SPEC(FN2CSettingsCustomizationSpec, "NodeToCode.Settings.WriteClientConfig", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString BaseDir;
	const FString Url = TEXT("http://127.0.0.1:27000/mcp");

	FString ReadFile(EN2CMcpClient Client) const
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *(BaseDir / FN2CMcpClientConfig::GetRelativePath(Client)));
		return Text;
	}

	bool Exists(EN2CMcpClient Client) const
	{
		return IFileManager::Get().FileExists(*(BaseDir / FN2CMcpClientConfig::GetRelativePath(Client)));
	}

	static TOptional<EN2CMcpClient> ClientOf(EN2CClientConfigChoice Choice)
	{
		switch (Choice)
		{
		case EN2CClientConfigChoice::ClaudeCode: return EN2CMcpClient::ClaudeCode;
		case EN2CClientConfigChoice::Cursor:     return EN2CMcpClient::Cursor;
		case EN2CClientConfigChoice::VSCode:     return EN2CMcpClient::VSCode;
		case EN2CClientConfigChoice::Gemini:     return EN2CMcpClient::Gemini;
		case EN2CClientConfigChoice::Codex:      return EN2CMcpClient::Codex;
		default:                                 return {};
		}
	}

END_DEFINE_SPEC(FN2CSettingsCustomizationSpec)

void FN2CSettingsCustomizationSpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CSettingsWriteClientConfig"));
		BaseDir = *Scratch / TEXT("My Project");
		IFileManager::Get().MakeDirectory(*BaseDir, true);
	});

	AfterEach([this]()
	{
		Scratch.Reset();
	});

	It(TEXT("is the MCP Client Config row of the MCP Server settings, which saves nothing"), [this]()
	{
		const FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		TestTrue(TEXT("a property layout is registered for FN2CMcpClientConfigWriter"),
			PropertyEditor.IsCustomizedStruct(FN2CMcpClientConfigWriter::StaticStruct(), FCustomPropertyTypeLayoutMap()));
		const FProperty* Row = UN2CSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UN2CSettings, McpClientConfigWriter));
		if (TestNotNull(TEXT("the settings have the row's property"), Row))
		{
			TestEqual(TEXT("in the MCP Server category"), Row->GetMetaData(TEXT("Category")), FString(TEXT("Node to Code | MCP Server")));
			TestFalse(TEXT("not saved to the config"), Row->HasAnyPropertyFlags(CPF_Config));
			TestTrue(TEXT("transient"), Row->HasAnyPropertyFlags(CPF_Transient));
		}
	});

	It(TEXT("passes the console command the client, the token box and the folder, the folder last"), [this]()
	{
		const TArray<FString> Cursor = FN2CSettingsCustomization::MakeCommandArgs(EN2CClientConfigChoice::Cursor, true, BaseDir);
		TestTrue(TEXT("cursor with token"), Cursor == TArray<FString>{ TEXT("cursor"), TEXT("token"), TEXT("dir=") + BaseDir });
		const TArray<FString> All = FN2CSettingsCustomization::MakeCommandArgs(EN2CClientConfigChoice::All, false, BaseDir);
		TestTrue(TEXT("all without token"), All == TArray<FString>{ TEXT("all"), TEXT("notoken"), TEXT("dir=") + BaseDir });
		const TArray<FString> Desktop = FN2CSettingsCustomization::MakeCommandArgs(EN2CClientConfigChoice::ClaudeDesktop, false, BaseDir);
		TestEqual(TEXT("desktop"), Desktop[0], FString(TEXT("desktop")));
	});

	for (uint8 Index = static_cast<uint8>(EN2CClientConfigChoice::ClaudeCode); Index <= static_cast<uint8>(EN2CClientConfigChoice::Codex); ++Index)
	{
		const EN2CClientConfigChoice Choice = static_cast<EN2CClientConfigChoice>(Index);
		It(FString::Printf(TEXT("writes only the %s file"), *FN2CSettingsCustomization::GetChoiceLabel(Choice).ToString()), [this, Choice]()
		{
			const EN2CMcpClient Client = *ClientOf(Choice);
			const FN2CClientConfigOutcome Outcome = FN2CSettingsCustomization::WriteClientConfig(Choice, false, BaseDir);
			if (!TestEqual(TEXT("one file"), Outcome.Results.Num(), 1))
			{
				return;
			}
			const FN2CMcpClientConfigResult& Result = Outcome.Results[0];
			TestTrue(TEXT("written"), Result.bOk);
			TestTrue(TEXT("the chosen client"), Result.Client == Client);
			TestEqual(TEXT("in the folder, at the client's path"), FPaths::ConvertRelativePathToFull(Result.FilePath),
				FPaths::ConvertRelativePathToFull(BaseDir / FN2CMcpClientConfig::GetRelativePath(Client)));
			const FString Text = ReadFile(Client);
			TestTrue(TEXT("names the nodetocode server"), Text.Contains(FN2CMcpClientConfig::ServerName));
			TestTrue(TEXT("points at the server URL"), Text.Contains(TEXT("http://127.0.0.1:")) && Text.Contains(TEXT("/mcp")));
			TestFalse(TEXT("no token header without the box"), Text.Contains(FN2CMcpClientConfig::TokenEnvVar));
			TestTrue(TEXT("no Claude Desktop snippet"), Outcome.DesktopSnippet.IsEmpty());
			for (uint8 Other = 0; Other < static_cast<uint8>(EN2CMcpClient::Num); ++Other)
			{
				if (static_cast<EN2CMcpClient>(Other) != Client)
				{
					TestFalse(FString::Printf(TEXT("no %s file"), FN2CMcpClientConfig::GetClientName(static_cast<EN2CMcpClient>(Other))),
						Exists(static_cast<EN2CMcpClient>(Other)));
				}
			}
		});
	}

	It(TEXT("with the token box, the file reads the token from the environment variable and never holds it"), [this]()
	{
		const FN2CClientConfigOutcome Outcome = FN2CSettingsCustomization::WriteClientConfig(EN2CClientConfigChoice::ClaudeCode, true, BaseDir);
		TestEqual(TEXT("one file"), Outcome.Results.Num(), 1);
		TestTrue(TEXT("references NODETOCODE_MCP_TOKEN"), ReadFile(EN2CMcpClient::ClaudeCode).Contains(FN2CMcpClientConfig::TokenEnvVar));
		TestTrue(TEXT("the notification says where the token comes from"),
			FN2CSettingsCustomization::Summarize(Outcome, true).ToString().Contains(FN2CMcpClientConfig::TokenEnvVar));
	});

	It(TEXT("All writes every client's file, and a second click changes nothing"), [this]()
	{
		const FN2CClientConfigOutcome First = FN2CSettingsCustomization::WriteClientConfig(EN2CClientConfigChoice::All, false, BaseDir);
		TestEqual(TEXT("one result per client"), First.Results.Num(), static_cast<int32>(EN2CMcpClient::Num));
		for (uint8 Index = 0; Index < static_cast<uint8>(EN2CMcpClient::Num); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s written"), FN2CMcpClientConfig::GetClientName(static_cast<EN2CMcpClient>(Index))),
				Exists(static_cast<EN2CMcpClient>(Index)));
		}
		const FN2CClientConfigOutcome Second = FN2CSettingsCustomization::WriteClientConfig(EN2CClientConfigChoice::All, false, BaseDir);
		for (const FN2CMcpClientConfigResult& Result : Second.Results)
		{
			TestTrue(FString::Printf(TEXT("%s unchanged"), FN2CMcpClientConfig::GetClientName(Result.Client)), Result.bOk && Result.bUnchanged);
		}
		TestTrue(TEXT("the notification lists the files"),
			FN2CSettingsCustomization::Summarize(First, false).ToString().Contains(TEXT("config.toml")));
	});

	It(TEXT("Claude Desktop writes no file and returns the bridge snippet"), [this]()
	{
		const FN2CClientConfigOutcome Outcome = FN2CSettingsCustomization::WriteClientConfig(EN2CClientConfigChoice::ClaudeDesktop, false, BaseDir);
		TestEqual(TEXT("no client file"), Outcome.Results.Num(), 0);
		TestTrue(TEXT("a snippet that runs the bridge launcher"), Outcome.DesktopSnippet.Contains(TEXT("launch_bridge")));
		TestTrue(TEXT("it names the server"), Outcome.DesktopSnippet.Contains(FN2CMcpClientConfig::ServerName));
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *BaseDir, TEXT("*"), true, false);
		TestEqual(TEXT("the folder stays empty"), Files.Num(), 0);
		TestTrue(TEXT("the notification says it was copied"),
			FN2CSettingsCustomization::Summarize(Outcome, false).ToString().Contains(TEXT("clipboard")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
