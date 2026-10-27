// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Server/N2CMcpAuthToken.h"

#include "Utils/N2CFileUtils.h"
#include "Utils/N2CLogger.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace N2CMcpAuthTokenPrivate
{
	const TCHAR* const LogContext = TEXT("MCP");

	FCriticalSection Lock;

	/** The token last read or written (empty: not loaded yet) */
	FString Cached;

	/** A spec's token file, else empty */
	FString PathOverride;

	/** The token file; call with Lock held */
	FString PathLocked()
	{
		return !PathOverride.IsEmpty() ? PathOverride
			: FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("NodeToCode"), TEXT("mcp-token")));
	}

	/** Fills Bytes from the operating system's CSPRNG: BCryptGenRandom on Windows, /dev/urandom elsewhere */
	bool FillRandom(uint8* Bytes, int32 Num)
	{
#if PLATFORM_WINDOWS
		return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, Bytes, static_cast<ULONG>(Num), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
		const int Fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		if (Fd < 0)
		{
			return false;
		}
		int32 Done = 0;
		while (Done < Num)
		{
			const ssize_t Read = read(Fd, Bytes + Done, Num - Done);
			if (Read <= 0)
			{
				break;
			}
			Done += static_cast<int32>(Read);
		}
		close(Fd);
		return Done == Num;
#endif
	}

	FString ToLowerHex(const uint8* Bytes, int32 Num)
	{
		static const TCHAR Digits[] = TEXT("0123456789abcdef");
		FString Hex;
		Hex.Reserve(Num * 2);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Hex.AppendChar(Digits[Bytes[Index] >> 4]);
			Hex.AppendChar(Digits[Bytes[Index] & 0x0F]);
		}
		return Hex;
	}

	const FString* FirstHeader(const FHttpServerRequest& Request, const TCHAR* Name)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
			{
				return &Pair.Value[0];
			}
		}
		return nullptr;
	}

	FAutoConsoleCommand CopyTokenCommand(
		TEXT("N2C.Mcp.CopyToken"),
		TEXT("Copies NodeToCode's MCP bearer token to the clipboard, for an MCP client that connects over HTTP (creates the token on first use)"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const FString Token = FN2CMcpAuthToken::Get();
			if (Token.IsEmpty())
			{
				return;
			}
			FPlatformApplicationMisc::ClipboardCopy(*Token);
			FN2CLogger::Get().Log(FString::Printf(TEXT("MCP bearer token copied to the clipboard (it is kept in %s)"),
				*FN2CMcpAuthToken::GetTokenFilePath()), EN2CLogSeverity::Info, LogContext);
		}));

	FAutoConsoleCommand RegenerateTokenCommand(
		TEXT("N2C.Mcp.RegenerateToken"),
		TEXT("Replaces NodeToCode's MCP bearer token: clients that send the old one get 401 until they use the new one (the stdio bridge rereads it)"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (!FN2CMcpAuthToken::Regenerate().IsEmpty())
			{
				FN2CLogger::Get().Log(FString::Printf(TEXT("MCP bearer token regenerated in %s: the old token no longer works. ")
					TEXT("Update the clients that send it by hand (N2C.Mcp.CopyToken); the stdio bridge reads the new one itself."),
					*FN2CMcpAuthToken::GetTokenFilePath()), EN2CLogSeverity::Info, LogContext);
			}
		}));
}

FString FN2CMcpAuthToken::GetTokenFilePath()
{
	using namespace N2CMcpAuthTokenPrivate;
	FScopeLock ScopeLock(&Lock);
	return PathLocked();
}

bool FN2CMcpAuthToken::IsWellFormed(const FString& Token)
{
	if (Token.Len() != TokenBytes * 2)
	{
		return false;
	}
	for (const TCHAR Char : Token)
	{
		if (!((Char >= TEXT('0') && Char <= TEXT('9')) || (Char >= TEXT('a') && Char <= TEXT('f'))))
		{
			return false;
		}
	}
	return true;
}

bool FN2CMcpAuthToken::LoadLocked()
{
	using namespace N2CMcpAuthTokenPrivate;
	const FString Path = PathLocked();
	FString Text;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return false;
	}
	Text.TrimStartAndEndInline();
	if (!IsWellFormed(Text))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("The MCP token file %s does not hold a token (64 lowercase hex characters): it is ignored, and a new token is written when one is needed"), *Path), LogContext);
		return false;
	}
	Cached = MoveTemp(Text);
	return true;
}

bool FN2CMcpAuthToken::GenerateLocked()
{
	using namespace N2CMcpAuthTokenPrivate;
	uint8 Bytes[TokenBytes];
	if (!FillRandom(Bytes, TokenBytes))
	{
		FMemory::Memzero(Bytes, sizeof(Bytes));
		FN2CLogger::Get().LogError(TEXT("Could not get random bytes from the operating system for the MCP bearer token"), LogContext);
		return false;
	}
	const FString Token = ToLowerHex(Bytes, TokenBytes);
	FMemory::Memzero(Bytes, sizeof(Bytes));

	const FString Path = PathLocked();
	FN2CAtomicWriteOptions Options;
	Options.bKeepBackup = false;
	Options.bOwnerOnly = true;
	if (!FN2CFileUtils::SaveStringAtomically(Token, Path, Options))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Could not write the MCP bearer token to %s"), *Path), LogContext);
		return false;
	}
	Cached = Token;
	return true;
}

FString FN2CMcpAuthToken::Get()
{
	using namespace N2CMcpAuthTokenPrivate;
	FScopeLock ScopeLock(&Lock);
	if (Cached.IsEmpty() && !LoadLocked())
	{
		GenerateLocked();
	}
	return Cached;
}

FString FN2CMcpAuthToken::Regenerate()
{
	using namespace N2CMcpAuthTokenPrivate;
	FScopeLock ScopeLock(&Lock);
	Cached.Reset();
	GenerateLocked();
	return Cached;
}

FString FN2CMcpAuthToken::ParseBearer(const FString& Authorization)
{
	FString Value = Authorization.TrimStartAndEnd();
	static const FString Scheme = TEXT("Bearer");
	if (Value.Len() <= Scheme.Len() || !Value.StartsWith(Scheme, ESearchCase::IgnoreCase) || !FChar::IsWhitespace(Value[Scheme.Len()]))
	{
		return FString();
	}
	return Value.RightChop(Scheme.Len()).TrimStartAndEnd();
}

bool FN2CMcpAuthToken::ConstantTimeEquals(const FString& A, const FString& B)
{
	if (A.Len() != B.Len())
	{
		return false;
	}
	uint32 Difference = 0;
	for (int32 Index = 0; Index < A.Len(); ++Index)
	{
		Difference |= static_cast<uint32>(A[Index]) ^ static_cast<uint32>(B[Index]);
	}
	return Difference == 0;
}

bool FN2CMcpAuthToken::IsAuthorized(const FString* Authorization)
{
	using namespace N2CMcpAuthTokenPrivate;
	const FString Presented = Authorization ? ParseBearer(*Authorization) : FString();
	if (Presented.IsEmpty())
	{
		return false;
	}
	FScopeLock ScopeLock(&Lock);
	if (Cached.IsEmpty() && !LoadLocked() && !GenerateLocked())
	{
		return false;
	}
	if (ConstantTimeEquals(Presented, Cached))
	{
		return true;
	}
	// Another editor of this user may have regenerated the shared token since it was read
	const FString Previous = Cached;
	return LoadLocked() && Cached != Previous && ConstantTimeEquals(Presented, Cached);
}

bool FN2CMcpAuthToken::Authorize(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	using namespace N2CMcpAuthTokenPrivate;
	const FString* Authorization = FirstHeader(Request, TEXT("Authorization"));
	if (IsAuthorized(Authorization))
	{
		return true;
	}
	const TCHAR* const Reason = Authorization ? TEXT("wrong or malformed bearer token") : TEXT("no Authorization header");
	FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP request rejected (401): %s"), Reason), LogContext);
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString::Printf(
		TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,\"message\":\"Unauthorized: this NodeToCode MCP server requires ")
		TEXT("'Authorization: Bearer <token>' (%s). The token is in NodeToCode's mcp-token file in the user settings folder; ")
		TEXT("the console command N2C.Mcp.CopyToken copies it.\"}}"), Reason), TEXT("application/json"));
	Response->Code = EHttpServerResponseCodes::Denied;
	Response->Headers.Add(TEXT("WWW-Authenticate"), { TEXT("Bearer") });
	OnComplete(MoveTemp(Response));
	return false;
}

void FN2CMcpAuthToken::SetTokenFilePathForTesting(const FString& Path)
{
	using namespace N2CMcpAuthTokenPrivate;
	FScopeLock ScopeLock(&Lock);
	PathOverride = Path;
	Cached.Reset();
}
