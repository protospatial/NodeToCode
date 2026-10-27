// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Server/N2CMcpClientConfig.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * Card 0206: N2C.Mcp.WriteClientConfig writes or merges NodeToCode's server into the config files of Claude Code,
 * Cursor, VS Code, Gemini CLI and Codex. Every case writes into a scratch folder that already holds another server's
 * entry and asserts the merged file parses (JSON) or keeps its tables (TOML), keeps that entry, and that a second run
 * changes nothing. The token itself never lands in a file: the configs read NODETOCODE_MCP_TOKEN.
 */
BEGIN_DEFINE_SPEC(FN2CMcpClientConfigSpec, "NodeToCode.MCP.ClientConfig", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	const FString Url = TEXT("http://127.0.0.1:27123/mcp");

	FString Path(EN2CMcpClient Client) const
	{
		return *Scratch / FN2CMcpClientConfig::GetRelativePath(Client);
	}

	FString ReadFile(const FString& File) const
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *File);
		return Text;
	}

	void WriteFile(const FString& File, const FString& Text) const
	{
		FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSharedPtr<FJsonObject> Parse(const FString& Text) const
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	static const TCHAR* ServersKey(EN2CMcpClient Client)
	{
		return Client == EN2CMcpClient::VSCode ? TEXT("servers") : TEXT("mcpServers");
	}

	static const TCHAR* UrlKey(EN2CMcpClient Client)
	{
		return Client == EN2CMcpClient::Gemini ? TEXT("httpUrl") : TEXT("url");
	}

	/** The nodetocode entry of a merged JSON file, after checking that the other server and top-level key survived */
	TSharedPtr<FJsonObject> CheckMergedJson(EN2CMcpClient Client)
	{
		const FString Name = FN2CMcpClientConfig::GetClientName(Client);
		const TSharedPtr<FJsonObject> Root = Parse(ReadFile(Path(Client)));
		if (!TestTrue(Name + TEXT(": the merged file parses as JSON"), Root.IsValid()))
		{
			return nullptr;
		}
		TestEqual(Name + TEXT(": the unrelated top-level key is kept"), Root->GetStringField(TEXT("theme")), TEXT("dark"));
		const TSharedPtr<FJsonObject>* Servers = nullptr;
		if (!TestTrue(Name + TEXT(": has the servers object"), Root->TryGetObjectField(ServersKey(Client), Servers)))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* Other = nullptr;
		TestTrue(Name + TEXT(": the other server's entry is kept"), (*Servers)->TryGetObjectField(TEXT("other"), Other));
		if (Other)
		{
			TestEqual(Name + TEXT(": the other server's command is kept"), (*Other)->GetStringField(TEXT("command")), TEXT("other-mcp"));
		}
		const TSharedPtr<FJsonObject>* Ours = nullptr;
		TestTrue(Name + TEXT(": has the nodetocode entry"), (*Servers)->TryGetObjectField(TEXT("nodetocode"), Ours));
		return Ours ? *Ours : nullptr;
	}

END_DEFINE_SPEC(FN2CMcpClientConfigSpec)

void FN2CMcpClientConfigSpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CMcpClientConfig"));
	});

	AfterEach([this]()
	{
		Scratch.Reset();
	});

	It(TEXT("merges into each JSON client file, keeping another server's entry, and a second run changes nothing"), [this]()
	{
		for (const EN2CMcpClient Client : { EN2CMcpClient::ClaudeCode, EN2CMcpClient::Cursor, EN2CMcpClient::VSCode, EN2CMcpClient::Gemini })
		{
			const FString Name = FN2CMcpClientConfig::GetClientName(Client);
			WriteFile(Path(Client), FString::Printf(TEXT("{ \"theme\": \"dark\", \"%s\": { \"other\": { \"command\": \"other-mcp\", \"args\": [\"--x\"] }, ")
				TEXT("\"nodetocode\": { \"url\": \"http://127.0.0.1:1/old\" } } }"), ServersKey(Client)));

			const FN2CMcpClientConfigResult First = FN2CMcpClientConfig::Write(Client, Scratch->Path, Url, false);
			TestTrue(Name + TEXT(": written: ") + First.Error, First.bOk && !First.bUnchanged);
			const TSharedPtr<FJsonObject> Ours = CheckMergedJson(Client);
			if (!Ours.IsValid())
			{
				continue;
			}
			TestEqual(Name + TEXT(": the URL replaces the old one"), Ours->GetStringField(UrlKey(Client)), Url);
			const bool bWantsType = Client == EN2CMcpClient::ClaudeCode || Client == EN2CMcpClient::VSCode;
			TestEqual(Name + TEXT(": type http only where the client wants it"), Ours->HasField(TEXT("type")), bWantsType);
			if (bWantsType)
			{
				TestEqual(Name + TEXT(": type"), Ours->GetStringField(TEXT("type")), TEXT("http"));
			}
			TestFalse(Name + TEXT(": no headers without the token"), Ours->HasField(TEXT("headers")));
			if (Client == EN2CMcpClient::Gemini)
			{
				TestFalse(TEXT("gemini: no url field next to httpUrl"), Ours->HasField(TEXT("url")));
			}

			const FString Before = ReadFile(Path(Client));
			const FN2CMcpClientConfigResult Second = FN2CMcpClientConfig::Write(Client, Scratch->Path, Url, false);
			TestTrue(Name + TEXT(": the second run reports nothing to change"), Second.bOk && Second.bUnchanged);
			TestTrue(Name + TEXT(": the second run leaves the file byte for byte"), ReadFile(Path(Client)).Equals(Before, ESearchCase::CaseSensitive));
		}
	});

	It(TEXT("with the token on, each JSON client reads it from NODETOCODE_MCP_TOKEN in its own syntax"), [this]()
	{
		const TMap<EN2CMcpClient, FString> Expected = {
			{ EN2CMcpClient::ClaudeCode, TEXT("Bearer ${NODETOCODE_MCP_TOKEN}") },
			{ EN2CMcpClient::Cursor, TEXT("Bearer ${env:NODETOCODE_MCP_TOKEN}") },
			{ EN2CMcpClient::VSCode, TEXT("Bearer ${env:NODETOCODE_MCP_TOKEN}") },
			{ EN2CMcpClient::Gemini, TEXT("Bearer $NODETOCODE_MCP_TOKEN") },
		};
		for (const TPair<EN2CMcpClient, FString>& Pair : Expected)
		{
			const FString Name = FN2CMcpClientConfig::GetClientName(Pair.Key);
			WriteFile(Path(Pair.Key), FString::Printf(TEXT("{ \"theme\": \"dark\", \"%s\": { \"other\": { \"command\": \"other-mcp\" } } }"), ServersKey(Pair.Key)));
			TestTrue(Name + TEXT(": written"), FN2CMcpClientConfig::Write(Pair.Key, Scratch->Path, Url, true).bOk);
			const TSharedPtr<FJsonObject> Ours = CheckMergedJson(Pair.Key);
			const TSharedPtr<FJsonObject>* Headers = nullptr;
			if (TestTrue(Name + TEXT(": has headers"), Ours.IsValid() && Ours->TryGetObjectField(TEXT("headers"), Headers)))
			{
				TestEqual(Name + TEXT(": Authorization"), (*Headers)->GetStringField(TEXT("Authorization")), Pair.Value);
			}

			// Turning the token off again drops the header
			TestTrue(Name + TEXT(": rewritten without the token"), FN2CMcpClientConfig::Write(Pair.Key, Scratch->Path, Url, false).bOk);
			const TSharedPtr<FJsonObject> Plain = CheckMergedJson(Pair.Key);
			TestFalse(Name + TEXT(": header gone"), Plain.IsValid() && Plain->HasField(TEXT("headers")));
		}
	});

	It(TEXT("creates each file and its folder when none exists"), [this]()
	{
		for (uint8 Index = 0; Index < static_cast<uint8>(EN2CMcpClient::Num); ++Index)
		{
			const EN2CMcpClient Client = static_cast<EN2CMcpClient>(Index);
			const FN2CMcpClientConfigResult Result = FN2CMcpClientConfig::Write(Client, Scratch->Path, Url, false);
			TestTrue(FString(FN2CMcpClientConfig::GetClientName(Client)) + TEXT(": written ") + Result.Error, Result.bOk);
			TestTrue(FString(FN2CMcpClientConfig::GetClientName(Client)) + TEXT(": file exists"), FPaths::FileExists(Path(Client)));
		}
		TestTrue(TEXT("the Claude Code file parses"), Parse(ReadFile(Path(EN2CMcpClient::ClaudeCode))).IsValid());
		TestEqual(TEXT("a new Codex file holds just the table"), ReadFile(Path(EN2CMcpClient::Codex)).Replace(TEXT("\r\n"), TEXT("\n")),
			FString::Printf(TEXT("[mcp_servers.nodetocode]\nurl = \"%s\"\n"), *Url));
	});

	It(TEXT("leaves a JSON file it cannot parse as it is and says why"), [this]()
	{
		const FString Broken = TEXT("{ \"mcpServers\": { // a comment\n \"other\": {} }, }");
		WriteFile(Path(EN2CMcpClient::ClaudeCode), Broken);
		const FN2CMcpClientConfigResult Result = FN2CMcpClientConfig::Write(EN2CMcpClient::ClaudeCode, Scratch->Path, Url, false);
		TestFalse(TEXT("refused"), Result.bOk);
		TestTrue(TEXT("the error says the file is not JSON: ") + Result.Error, Result.Error.Contains(TEXT("not a JSON object")));
		TestEqual(TEXT("the file is untouched"), ReadFile(Path(EN2CMcpClient::ClaudeCode)), Broken);

		WriteFile(Path(EN2CMcpClient::VSCode), TEXT("{ \"servers\": [] }"));
		const FN2CMcpClientConfigResult NotObject = FN2CMcpClientConfig::Write(EN2CMcpClient::VSCode, Scratch->Path, Url, false);
		TestFalse(TEXT("a servers key that is not an object is refused"), NotObject.bOk);
		TestEqual(TEXT("and left as it is"), ReadFile(Path(EN2CMcpClient::VSCode)), FString(TEXT("{ \"servers\": [] }")));
	});

	It(TEXT("replaces the Codex nodetocode table and its sub-tables in place, keeping every other line"), [this]()
	{
		const FString Existing = FString(TEXT("model = \"gpt-5.5\"\n"))
			+ TEXT("# my servers\n")
			+ TEXT("[mcp_servers.other]\n")
			+ TEXT("command = \"other-mcp\"\n")
			+ TEXT("\n")
			+ TEXT("[mcp_servers.nodetocode]\n")
			+ TEXT("url = \"http://127.0.0.1:1/old\"\n")
			+ TEXT("startup_timeout_sec = 5\n")
			+ TEXT("\n")
			+ TEXT("[mcp_servers.nodetocode.env]\n")
			+ TEXT("FOO = \"bar\"\n")
			+ TEXT("\n")
			+ TEXT("[profiles.fast]\n")
			+ TEXT("model = \"gpt-5.5-mini\"\n");
		WriteFile(Path(EN2CMcpClient::Codex), Existing);

		const FN2CMcpClientConfigResult First = FN2CMcpClientConfig::Write(EN2CMcpClient::Codex, Scratch->Path, Url, true);
		TestTrue(TEXT("written: ") + First.Error, First.bOk && !First.bUnchanged);
		const FString Expected = FString(TEXT("model = \"gpt-5.5\"\n"))
			+ TEXT("# my servers\n")
			+ TEXT("[mcp_servers.other]\n")
			+ TEXT("command = \"other-mcp\"\n")
			+ TEXT("\n")
			+ TEXT("[mcp_servers.nodetocode]\n")
			+ FString::Printf(TEXT("url = \"%s\"\n"), *Url)
			+ TEXT("bearer_token_env_var = \"NODETOCODE_MCP_TOKEN\"\n")
			+ TEXT("\n")
			+ TEXT("[profiles.fast]\n")
			+ TEXT("model = \"gpt-5.5-mini\"\n");
		TestEqual(TEXT("the merged file"), ReadFile(Path(EN2CMcpClient::Codex)), Expected);

		const FN2CMcpClientConfigResult Second = FN2CMcpClientConfig::Write(EN2CMcpClient::Codex, Scratch->Path, Url, true);
		TestTrue(TEXT("the second run changes nothing"), Second.bOk && Second.bUnchanged);
		TestEqual(TEXT("byte for byte"), ReadFile(Path(EN2CMcpClient::Codex)), Expected);
	});

	It(TEXT("appends the Codex table after the existing tables, and keeps CRLF line endings"), [this]()
	{
		WriteFile(Path(EN2CMcpClient::Codex), TEXT("[mcp_servers.other]\r\ncommand = \"other-mcp\"\r\n"));
		TestTrue(TEXT("written"), FN2CMcpClientConfig::Write(EN2CMcpClient::Codex, Scratch->Path, Url, false).bOk);
		const FString Expected = FString::Printf(TEXT("[mcp_servers.other]\r\ncommand = \"other-mcp\"\r\n\r\n[mcp_servers.nodetocode]\r\nurl = \"%s\"\r\n"), *Url);
		TestEqual(TEXT("appended"), ReadFile(Path(EN2CMcpClient::Codex)), Expected);
		TestTrue(TEXT("the second run changes nothing"), FN2CMcpClientConfig::Write(EN2CMcpClient::Codex, Scratch->Path, Url, false).bUnchanged);
		TestEqual(TEXT("byte for byte"), ReadFile(Path(EN2CMcpClient::Codex)), Expected);
	});

	It(TEXT("refuses a Codex file that defines nodetocode as an inline table or dotted keys"), [this]()
	{
		for (const FString& Existing : { FString(TEXT("[mcp_servers]\nnodetocode = { url = \"http://x\" }\n")),
			FString(TEXT("mcp_servers.nodetocode.url = \"http://x\"\n")) })
		{
			WriteFile(Path(EN2CMcpClient::Codex), Existing);
			const FN2CMcpClientConfigResult Result = FN2CMcpClientConfig::Write(EN2CMcpClient::Codex, Scratch->Path, Url, false);
			TestFalse(TEXT("refused: ") + Existing, Result.bOk);
			TestTrue(TEXT("names the line: ") + Result.Error, Result.Error.Contains(TEXT("cannot replace safely")));
			TestEqual(TEXT("left as it is"), ReadFile(Path(EN2CMcpClient::Codex)), Existing);
		}
	});

	It(TEXT("RunCommand writes every client into dir= and a Claude Desktop snippet that runs the bridge"), [this]()
	{
		const TArray<FN2CMcpClientConfigResult> Results = FN2CMcpClientConfig::RunCommand({ TEXT("all"), TEXT("notoken"), TEXT("dir=") + Scratch->Path });
		TestEqual(TEXT("five files"), Results.Num(), static_cast<int32>(EN2CMcpClient::Num));
		for (const FN2CMcpClientConfigResult& Result : Results)
		{
			TestTrue(FString(FN2CMcpClientConfig::GetClientName(Result.Client)) + TEXT(" ok: ") + Result.Error, Result.bOk);
			TestTrue(TEXT("under dir="), FPaths::IsUnderDirectory(Result.FilePath, Scratch->Path));
		}
		// The console splits at spaces: dir= takes the rest of the line, so a folder with a space works
		const FString Spaced = *Scratch / TEXT("My Project");
		const TArray<FN2CMcpClientConfigResult> SpacedResults = FN2CMcpClientConfig::RunCommand({ TEXT("claude"), TEXT("dir=") + *Scratch / TEXT("My"), TEXT("Project") });
		TestTrue(TEXT("one file in the folder with a space"), SpacedResults.Num() == 1 && SpacedResults[0].bOk
			&& FPaths::FileExists(Spaced / TEXT(".mcp.json")));

		AddExpectedMessage(TEXT("unknown argument 'emacs'"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
		TestEqual(TEXT("an unknown argument writes nothing"), FN2CMcpClientConfig::RunCommand({ TEXT("emacs"), TEXT("dir=") + Scratch->Path }).Num(), 0);

		const TSharedPtr<FJsonObject> Snippet = Parse(FN2CMcpClientConfig::MakeClaudeDesktopSnippet(TEXT("C:\\P\\launch_bridge.bat"), TEXT("C:\\P\\P.uproject")));
		const TSharedPtr<FJsonObject>* Servers = nullptr;
		const TSharedPtr<FJsonObject>* Ours = nullptr;
		if (TestTrue(TEXT("the snippet parses and has mcpServers.nodetocode"), Snippet.IsValid()
			&& Snippet->TryGetObjectField(TEXT("mcpServers"), Servers) && (*Servers)->TryGetObjectField(TEXT("nodetocode"), Ours)))
		{
			TestEqual(TEXT("command"), (*Ours)->GetStringField(TEXT("command")), TEXT("C:\\P\\launch_bridge.bat"));
			const TArray<TSharedPtr<FJsonValue>>& Args = (*Ours)->GetArrayField(TEXT("args"));
			TestTrue(TEXT("args: --project <uproject>"), Args.Num() == 2 && Args[0]->AsString() == TEXT("--project") && Args[1]->AsString() == TEXT("C:\\P\\P.uproject"));
		}
		TestTrue(TEXT("the launcher path names launch_bridge"), FN2CMcpClientConfig::GetBridgeLauncherPath().Contains(TEXT("launch_bridge")));
	});

	It(TEXT("parses client names"), [this]()
	{
		TestTrue(TEXT("claude"), FN2CMcpClientConfig::ParseClient(TEXT("Claude")) == EN2CMcpClient::ClaudeCode);
		TestTrue(TEXT("code"), FN2CMcpClientConfig::ParseClient(TEXT("code")) == EN2CMcpClient::VSCode);
		TestTrue(TEXT("gemini-cli"), FN2CMcpClientConfig::ParseClient(TEXT("gemini-cli")) == EN2CMcpClient::Gemini);
		TestFalse(TEXT("unknown"), FN2CMcpClientConfig::ParseClient(TEXT("emacs")).IsSet());
		TestEqual(TEXT("URL"), FN2CMcpClientConfig::MakeServerUrl(27001), FString(TEXT("http://127.0.0.1:27001/mcp")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
