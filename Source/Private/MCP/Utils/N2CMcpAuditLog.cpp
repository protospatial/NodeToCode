// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpAuditLog.h"
#include "Utils/N2CLogger.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CMcpAuditLogPrivate
{
	N2CMcpAuditLog::FCaller& Current()
	{
		static N2CMcpAuditLog::FCaller Caller;
		return Caller;
	}

	FString& PathOverride()
	{
		static FString Path;
		return Path;
	}
}

N2CMcpAuditLog::FCaller N2CMcpAuditLog::CurrentCaller()
{
	return N2CMcpAuditLogPrivate::Current();
}

N2CMcpAuditLog::FScopedCaller::FScopedCaller(const FString& SessionId, const TSharedPtr<FJsonObject>& ClientInfo)
	: Previous(N2CMcpAuditLogPrivate::Current())
{
	FCaller Caller;
	Caller.SessionId = SessionId;
	if (ClientInfo.IsValid())
	{
		ClientInfo->TryGetStringField(TEXT("name"), Caller.ClientName);
		ClientInfo->TryGetStringField(TEXT("version"), Caller.ClientVersion);
	}
	N2CMcpAuditLogPrivate::Current() = Caller;
}

N2CMcpAuditLog::FScopedCaller::~FScopedCaller()
{
	N2CMcpAuditLogPrivate::Current() = Previous;
}

FString N2CMcpAuditLog::FilePath()
{
	const FString& Override = N2CMcpAuditLogPrivate::PathOverride();
	return !Override.IsEmpty() ? Override
		: FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("Logs"), TEXT("mcp-audit.jsonl")));
}

FString N2CMcpAuditLog::TruncateResult(const FString& Text)
{
	if (Text.Len() <= MaxResultChars)
	{
		return Text;
	}
	return Text.Left(MaxResultChars) + FString::Printf(TEXT("... [%d more characters]"), Text.Len() - MaxResultChars);
}

void N2CMcpAuditLog::Append(const FString& Tool, const TSharedRef<FJsonObject>& Fields)
{
	const FCaller Caller = CurrentCaller();
	const TSharedRef<FJsonObject> Line = MakeShared<FJsonObject>();
	Line->SetStringField(TEXT("time"), FDateTime::UtcNow().ToIso8601());
	Line->SetStringField(TEXT("session"), Caller.SessionId);
	const TSharedRef<FJsonObject> Client = MakeShared<FJsonObject>();
	Client->SetStringField(TEXT("name"), Caller.ClientName);
	Client->SetStringField(TEXT("version"), Caller.ClientVersion);
	Line->SetObjectField(TEXT("client"), Client);
	Line->SetStringField(TEXT("tool"), Tool);
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Fields->Values)
	{
		Line->SetField(Field.Key, Field.Value);
	}

	FString Text;
	FJsonSerializer::Serialize(Line, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	Text += TEXT("\n");

	const FString Path = FilePath();
	IFileManager& Files = IFileManager::Get();
	Files.MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
	if (Files.FileSize(*Path) >= MaxBytes)
	{
		const FString Older = FPaths::Combine(FPaths::GetPath(Path), FPaths::GetBaseFilename(Path) + TEXT(".1.jsonl"));
		Files.Delete(*Older, false, true, true);
		Files.Move(*Older, *Path, true, true, false, true);
	}
	if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP audit: could not append to %s"), *Path), TEXT("MCP"));
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void N2CMcpAuditLog::SetFilePathForTests(const FString& Path)
{
	N2CMcpAuditLogPrivate::PathOverride() = Path;
}
#endif
