// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Server/N2CMcpClientConfig.h"

#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "Utils/N2CFileUtils.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

const TCHAR* const FN2CMcpClientConfig::ServerName = TEXT("nodetocode");
const TCHAR* const FN2CMcpClientConfig::TokenEnvVar = TEXT("NODETOCODE_MCP_TOKEN");

namespace N2CMcpClientConfigPrivate
{
	const TCHAR* const LogContext = TEXT("MCP");

	struct FDescriptor
	{
		const TCHAR* RelativePath;
		const TCHAR* ServersKey;
		const TCHAR* UrlField;
		bool bTypeField;
		/** The Authorization header value, in the client's own environment-variable syntax */
		const TCHAR* AuthHeaderFormat;
	};

	/** Field names from Epic's ModelContextProtocolClientConfig.cpp (UE 5.8); header syntax from each client's docs */
	FDescriptor Describe(EN2CMcpClient Client)
	{
		switch (Client)
		{
		case EN2CMcpClient::ClaudeCode: return { TEXT(".mcp.json"), TEXT("mcpServers"), TEXT("url"), true, TEXT("Bearer ${{var}}") };
		case EN2CMcpClient::Cursor:     return { TEXT(".cursor/mcp.json"), TEXT("mcpServers"), TEXT("url"), false, TEXT("Bearer ${env:{var}}") };
		case EN2CMcpClient::VSCode:     return { TEXT(".vscode/mcp.json"), TEXT("servers"), TEXT("url"), true, TEXT("Bearer ${env:{var}}") };
		case EN2CMcpClient::Gemini:     return { TEXT(".gemini/settings.json"), TEXT("mcpServers"), TEXT("httpUrl"), false, TEXT("Bearer ${var}") };
		case EN2CMcpClient::Codex:      return { TEXT(".codex/config.toml"), TEXT("mcp_servers"), TEXT("url"), false, nullptr };
		default:                        return { TEXT(""), TEXT(""), TEXT(""), false, nullptr };
		}
	}

	/** A TOML table header's name with whitespace and quotes removed ("mcp_servers.nodetocode"), else empty */
	FString TomlHeaderName(const FString& TrimmedLine)
	{
		if (!TrimmedLine.StartsWith(TEXT("[")))
		{
			return FString();
		}
		int32 Close = INDEX_NONE;
		if (!TrimmedLine.FindChar(TEXT(']'), Close))
		{
			return FString();
		}
		FString Name = TrimmedLine.Mid(1, Close - 1);
		Name.RemoveFromStart(TEXT("["));
		FString Out;
		for (const TCHAR Char : Name)
		{
			if (Char != TEXT(' ') && Char != TEXT('\t') && Char != TEXT('"') && Char != TEXT('\''))
			{
				Out.AppendChar(Char);
			}
		}
		return Out;
	}

	/** The key of a "key = value" line with whitespace and quotes removed, else empty (comments, blank lines) */
	FString TomlKey(const FString& TrimmedLine)
	{
		if (TrimmedLine.IsEmpty() || TrimmedLine.StartsWith(TEXT("#")))
		{
			return FString();
		}
		int32 Equals = INDEX_NONE;
		if (!TrimmedLine.FindChar(TEXT('='), Equals))
		{
			return FString();
		}
		FString Out;
		for (const TCHAR Char : TrimmedLine.Left(Equals))
		{
			if (Char != TEXT(' ') && Char != TEXT('\t') && Char != TEXT('"') && Char != TEXT('\''))
			{
				Out.AppendChar(Char);
			}
		}
		return Out;
	}

	bool IsOurs(const FString& Name, const FString& Prefix)
	{
		return Name == Prefix || Name.StartsWith(Prefix + TEXT("."));
	}

	FString JsonToString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out + TEXT("\n");
	}

	FAutoConsoleCommand WriteClientConfigCommand(
		TEXT("N2C.Mcp.WriteClientConfig"),
		TEXT("Writes or merges NodeToCode's MCP server into client configs in the project folder: N2C.Mcp.WriteClientConfig ")
		TEXT("[all|claude|cursor|vscode|gemini|codex|desktop ...] [token|notoken] [dir=<folder>, last]. Other servers in each file are kept; ")
		TEXT("with the bearer token on, the configs read it from NODETOCODE_MCP_TOKEN. desktop prints a Claude Desktop snippet."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FN2CMcpClientConfig::RunCommand(Args);
		}));
}

const TCHAR* FN2CMcpClientConfig::GetClientName(EN2CMcpClient Client)
{
	switch (Client)
	{
	case EN2CMcpClient::ClaudeCode: return TEXT("claude");
	case EN2CMcpClient::Cursor:     return TEXT("cursor");
	case EN2CMcpClient::VSCode:     return TEXT("vscode");
	case EN2CMcpClient::Gemini:     return TEXT("gemini");
	case EN2CMcpClient::Codex:      return TEXT("codex");
	default:                        return TEXT("");
	}
}

TOptional<EN2CMcpClient> FN2CMcpClientConfig::ParseClient(const FString& Name)
{
	const FString Lower = Name.ToLower();
	if (Lower == TEXT("claude") || Lower == TEXT("claudecode") || Lower == TEXT("claude-code")) { return EN2CMcpClient::ClaudeCode; }
	if (Lower == TEXT("cursor")) { return EN2CMcpClient::Cursor; }
	if (Lower == TEXT("vscode") || Lower == TEXT("code")) { return EN2CMcpClient::VSCode; }
	if (Lower == TEXT("gemini") || Lower == TEXT("gemini-cli")) { return EN2CMcpClient::Gemini; }
	if (Lower == TEXT("codex")) { return EN2CMcpClient::Codex; }
	return {};
}

FString FN2CMcpClientConfig::GetRelativePath(EN2CMcpClient Client)
{
	return N2CMcpClientConfigPrivate::Describe(Client).RelativePath;
}

FString FN2CMcpClientConfig::MakeServerUrl(int32 Port)
{
	return FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Port);
}

int32 FN2CMcpClientConfig::GetCurrentPort()
{
	const FN2CMcpHttpServerManager& Server = FN2CMcpHttpServerManager::Get();
	if (Server.IsServerRunning() && Server.GetServerPort() > 0)
	{
		return Server.GetServerPort();
	}
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return Settings ? Settings->McpServerPort : 27000;
}

FString FN2CMcpClientConfig::GetDefaultBaseDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
}

bool FN2CMcpClientConfig::MergeJson(EN2CMcpClient Client, const FString& Existing, const FString& ServerUrl, bool bWithToken,
	FString& Out, FString& OutError)
{
	using namespace N2CMcpClientConfigPrivate;
	const FDescriptor Descriptor = Describe(Client);
	if (Client == EN2CMcpClient::Codex || Client == EN2CMcpClient::Num)
	{
		OutError = TEXT("not a JSON client");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	if (!Existing.TrimStartAndEnd().IsEmpty())
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Existing);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("the file is not a JSON object (comments and trailing commas are not supported); it was left as it is, add the entry by hand");
			return false;
		}
	}
	else
	{
		Root = MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> Servers;
	if (const TSharedPtr<FJsonValue> ServersValue = Root->TryGetField(Descriptor.ServersKey))
	{
		const TSharedPtr<FJsonObject>* ServersObject = nullptr;
		if (!ServersValue->TryGetObject(ServersObject) || !ServersObject || !ServersObject->IsValid())
		{
			OutError = FString::Printf(TEXT("\"%s\" is not an object; the file was left as it is"), Descriptor.ServersKey);
			return false;
		}
		Servers = *ServersObject;
	}
	else
	{
		Servers = MakeShared<FJsonObject>();
	}

	const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	if (Descriptor.bTypeField)
	{
		Entry->SetStringField(TEXT("type"), TEXT("http"));
	}
	Entry->SetStringField(Descriptor.UrlField, ServerUrl);
	if (bWithToken && Descriptor.AuthHeaderFormat)
	{
		const TSharedRef<FJsonObject> Headers = MakeShared<FJsonObject>();
		Headers->SetStringField(TEXT("Authorization"), FString(Descriptor.AuthHeaderFormat).Replace(TEXT("{var}"), TokenEnvVar));
		Entry->SetObjectField(TEXT("headers"), Headers);
	}

	Servers->SetObjectField(ServerName, Entry);
	Root->SetObjectField(Descriptor.ServersKey, Servers);
	Out = JsonToString(Root.ToSharedRef());
	return true;
}

bool FN2CMcpClientConfig::MergeToml(const FString& Existing, const FString& ServerUrl, bool bWithToken, FString& Out, FString& OutError)
{
	using namespace N2CMcpClientConfigPrivate;
	const FString Eol = Existing.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	const FString OurTable = FString::Printf(TEXT("mcp_servers.%s"), ServerName);

	TArray<FString> Lines;
	Existing.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty*/ false);
	for (FString& Line : Lines)
	{
		Line.RemoveFromEnd(TEXT("\r"));
	}
	if (Lines.Num() > 0 && Lines.Last().IsEmpty())
	{
		Lines.Pop();
	}

	TArray<FString> Block;
	Block.Add(FString::Printf(TEXT("[%s]"), *OurTable));
	Block.Add(FString::Printf(TEXT("url = \"%s\""), *ServerUrl.ReplaceCharWithEscapedChar()));
	if (bWithToken)
	{
		Block.Add(FString::Printf(TEXT("bearer_token_env_var = \"%s\""), TokenEnvVar));
	}

	TArray<FString> Kept;
	bool bInserted = false;
	bool bInOurs = false;
	FString CurrentTable;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		const FString Trimmed = Line.TrimStartAndEnd();
		const FString Header = TomlHeaderName(Trimmed);
		if (!Header.IsEmpty())
		{
			CurrentTable = Header;
			bInOurs = IsOurs(Header, OurTable);
			if (bInOurs && !bInserted)
			{
				Kept.Append(Block);
				bInserted = true;
				// Keep one blank line before the next table, as the removed table had
				for (int32 Next = Index + 1; Next < Lines.Num(); ++Next)
				{
					if (!TomlHeaderName(Lines[Next].TrimStartAndEnd()).IsEmpty())
					{
						if (!IsOurs(TomlHeaderName(Lines[Next].TrimStartAndEnd()), OurTable))
						{
							Kept.Add(FString());
							break;
						}
					}
				}
			}
			if (bInOurs)
			{
				continue;
			}
		}
		else if (bInOurs)
		{
			continue;
		}
		else
		{
			const FString Key = TomlKey(Trimmed);
			if (!Key.IsEmpty() && ((CurrentTable.IsEmpty() && IsOurs(Key, OurTable))
				|| (CurrentTable == TEXT("mcp_servers") && IsOurs(Key, ServerName))))
			{
				OutError = FString::Printf(TEXT("line %d defines %s as %s, which this command cannot replace safely; ")
					TEXT("the file was left as it is, change it to a [%s] table or edit it by hand"),
					Index + 1, *OurTable, CurrentTable.IsEmpty() ? TEXT("dotted keys") : TEXT("an inline table"), *OurTable);
				return false;
			}
		}
		Kept.Add(Line);
	}

	if (!bInserted)
	{
		if (Kept.Num() > 0 && !Kept.Last().TrimStartAndEnd().IsEmpty())
		{
			Kept.Add(FString());
		}
		Kept.Append(Block);
	}
	// A kept blank line before a removed table that ended the file stays; trailing blank lines do not pile up
	while (Kept.Num() > 0 && Kept.Last().TrimStartAndEnd().IsEmpty())
	{
		Kept.Pop();
	}

	Out = FString::Join(Kept, *Eol) + Eol;
	return true;
}

FN2CMcpClientConfigResult FN2CMcpClientConfig::Write(EN2CMcpClient Client, const FString& BaseDir, const FString& ServerUrl, bool bWithToken)
{
	FN2CMcpClientConfigResult Result;
	Result.Client = Client;
	const FString Relative = GetRelativePath(Client);
	if (Relative.IsEmpty())
	{
		Result.Error = TEXT("unknown client");
		return Result;
	}
	Result.FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(BaseDir, Relative));

	FString Existing;
	if (FPaths::FileExists(Result.FilePath) && !FFileHelper::LoadFileToString(Existing, *Result.FilePath))
	{
		Result.Error = TEXT("the file could not be read");
		return Result;
	}

	FString Merged;
	const bool bMerged = Client == EN2CMcpClient::Codex
		? MergeToml(Existing, ServerUrl, bWithToken, Merged, Result.Error)
		: MergeJson(Client, Existing, ServerUrl, bWithToken, Merged, Result.Error);
	if (!bMerged)
	{
		return Result;
	}
	if (Merged.Equals(Existing, ESearchCase::CaseSensitive))
	{
		Result.bOk = true;
		Result.bUnchanged = true;
		return Result;
	}

	FN2CAtomicWriteOptions Options;
	Options.bKeepBackup = false;
	if (!FN2CFileUtils::SaveStringAtomically(Merged, Result.FilePath, Options))
	{
		Result.Error = TEXT("the file could not be written");
		return Result;
	}
	Result.bOk = true;
	return Result;
}

FString FN2CMcpClientConfig::MakeClaudeDesktopSnippet(const FString& LauncherPath, const FString& UProjectPath)
{
	TArray<TSharedPtr<FJsonValue>> Args;
	Args.Add(MakeShared<FJsonValueString>(TEXT("--project")));
	Args.Add(MakeShared<FJsonValueString>(UProjectPath));

	const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("command"), LauncherPath);
	Entry->SetArrayField(TEXT("args"), Args);
	const TSharedRef<FJsonObject> Servers = MakeShared<FJsonObject>();
	Servers->SetObjectField(ServerName, Entry);
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetObjectField(TEXT("mcpServers"), Servers);
	return N2CMcpClientConfigPrivate::JsonToString(Root);
}

FString FN2CMcpClientConfig::GetBridgeLauncherPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
	const FString BaseDir = Plugin.IsValid() ? Plugin->GetBaseDir() : FPaths::ProjectPluginsDir() / TEXT("NodeToCode");
#if PLATFORM_WINDOWS
	const TCHAR* const Launcher = TEXT("launch_bridge.bat");
#else
	const TCHAR* const Launcher = TEXT("launch_bridge.sh");
#endif
	FString Path = FPaths::ConvertRelativePathToFull(BaseDir / TEXT("Content/Python/mcp_bridge") / Launcher);
	FPaths::MakePlatformFilename(Path);
	return Path;
}

TArray<FN2CMcpClientConfigResult> FN2CMcpClientConfig::RunCommand(const TArray<FString>& Args)
{
	using namespace N2CMcpClientConfigPrivate;
	TArray<FN2CMcpClientConfigResult> Results;

	TArray<EN2CMcpClient> Clients;
	bool bDesktop = false;
	bool bWithToken = UN2CUserSettings::IsMcpBearerTokenRequired();
	FString BaseDir = GetDefaultBaseDir();
	for (int32 ArgIndex = 0; ArgIndex < Args.Num(); ++ArgIndex)
	{
		const FString& Arg = Args[ArgIndex];
		const FString Lower = Arg.ToLower();
		if (Lower == TEXT("all"))
		{
			for (uint8 Index = 0; Index < static_cast<uint8>(EN2CMcpClient::Num); ++Index)
			{
				Clients.AddUnique(static_cast<EN2CMcpClient>(Index));
			}
			bDesktop = true;
		}
		else if (Lower == TEXT("desktop") || Lower == TEXT("claude-desktop"))
		{
			bDesktop = true;
		}
		else if (Lower == TEXT("token") || Lower == TEXT("notoken"))
		{
			bWithToken = Lower == TEXT("token");
		}
		else if (Lower.StartsWith(TEXT("dir=")))
		{
			// The console splits arguments at spaces, so dir= takes the rest of the line: a folder may contain spaces
			TArray<FString> Rest(&Args[ArgIndex], Args.Num() - ArgIndex);
			BaseDir = FPaths::ConvertRelativePathToFull(FString::Join(Rest, TEXT(" ")).Mid(4).TrimQuotes());
			break;
		}
		else if (const TOptional<EN2CMcpClient> Client = ParseClient(Arg))
		{
			Clients.AddUnique(*Client);
		}
		else
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("N2C.Mcp.WriteClientConfig: unknown argument '%s'. Use all, claude, cursor, ")
				TEXT("vscode, gemini, codex, desktop, dir=<folder>, token or notoken. Nothing was written."), *Arg), LogContext);
			return Results;
		}
	}
	if (Clients.Num() == 0 && !bDesktop)
	{
		for (uint8 Index = 0; Index < static_cast<uint8>(EN2CMcpClient::Num); ++Index)
		{
			Clients.Add(static_cast<EN2CMcpClient>(Index));
		}
		bDesktop = true;
	}

	const FString Url = MakeServerUrl(GetCurrentPort());
	for (const EN2CMcpClient Client : Clients)
	{
		FN2CMcpClientConfigResult Result = Write(Client, BaseDir, Url, bWithToken);
		if (Result.bOk)
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("MCP client config for %s %s: %s (%s)"), GetClientName(Client),
				Result.bUnchanged ? TEXT("already up to date") : TEXT("written"), *Result.FilePath, *Url), EN2CLogSeverity::Info, LogContext);
		}
		else
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP client config for %s not written to %s: %s"),
				GetClientName(Client), *Result.FilePath, *Result.Error), LogContext);
		}
		Results.Add(MoveTemp(Result));
	}

	if (Clients.Num() > 0 && bWithToken)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("The configs send the MCP bearer token from the %s environment variable; the token ")
			TEXT("itself is not written to them, so they are safe to commit. Set %s in each client's environment (N2C.Mcp.CopyToken copies it)."),
			TokenEnvVar, TokenEnvVar), EN2CLogSeverity::Info, LogContext);
	}
	if (bDesktop)
	{
		FString UProject = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		FPaths::MakePlatformFilename(UProject);
		FN2CLogger::Get().Log(FString::Printf(TEXT("Claude Desktop runs the stdio bridge (it reads the bearer token file itself). Merge this ")
			TEXT("into claude_desktop_config.json (Settings > Developer > Edit Config):\n%s"),
			*MakeClaudeDesktopSnippet(GetBridgeLauncherPath(), UProject)), EN2CLogSeverity::Info, LogContext);
	}
	return Results;
}
