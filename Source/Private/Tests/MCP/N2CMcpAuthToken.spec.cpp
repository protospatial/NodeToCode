// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Server/N2CMcpAuthToken.h"
#include "Core/N2CUserSettings.h"
#include "Utils/N2CPathUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * Card 0183: the MCP endpoint's optional bearer token. The token is 64 hex characters from the OS CSPRNG, kept in a
 * per-user file outside the project, and /mcp accepts a request only with "Authorization: Bearer <token>" when Require
 * MCP Bearer Token is on (401 with WWW-Authenticate: Bearer otherwise). Every case keeps the token in a scratch file,
 * never the user's real one. The route itself (on, off, regenerate) is Tests/mcp_probe/live_bearer_token_0183.py.
 */
BEGIN_DEFINE_SPEC(FN2CMcpAuthTokenSpec, "NodeToCode.MCP.AuthToken", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString TokenFile;

	FString ReadFile() const
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *TokenFile);
		return Text.TrimStartAndEnd();
	}

	/** A request to /mcp with these headers */
	static FHttpServerRequest Request(const TArray<TPair<FString, FString>>& Headers)
	{
		FHttpServerRequest Out;
		Out.Verb = EHttpServerRequestVerbs::VERB_POST;
		for (const TPair<FString, FString>& Header : Headers)
		{
			Out.Headers.FindOrAdd(Header.Key).Add(Header.Value);
		}
		return Out;
	}

END_DEFINE_SPEC(FN2CMcpAuthTokenSpec)

void FN2CMcpAuthTokenSpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CMcpAuthToken"));
		TokenFile = *Scratch / TEXT("NodeToCode/mcp-token");
		FN2CMcpAuthToken::SetTokenFilePathForTesting(TokenFile);
	});

	AfterEach([this]()
	{
		FN2CMcpAuthToken::SetTokenFilePathForTesting(FString());
		Scratch.Reset();
	});

	It(TEXT("creates a 64-character hex token in the token file on first use, and keeps it"), [this]()
	{
		TestFalse(TEXT("no token file before first use"), FPaths::FileExists(TokenFile));
		const FString Token = FN2CMcpAuthToken::Get();
		TestTrue(TEXT("64 lowercase hex characters: ") + FString::FromInt(Token.Len()), FN2CMcpAuthToken::IsWellFormed(Token));
		TestEqual(TEXT("the file holds it"), ReadFile(), Token);
		TestEqual(TEXT("the next Get returns the same token"), FN2CMcpAuthToken::Get(), Token);

		// A new editor session reads it back instead of making another
		FN2CMcpAuthToken::SetTokenFilePathForTesting(TokenFile);
		TestEqual(TEXT("read back from the file"), FN2CMcpAuthToken::Get(), Token);

		FN2CMcpAuthToken::SetTokenFilePathForTesting(*Scratch / TEXT("other/mcp-token"));
		TestNotEqual(TEXT("two token files never share a token"), FN2CMcpAuthToken::Get(), Token);
	});

	It(TEXT("replaces a token file that does not hold a token"), [this]()
	{
		FFileHelper::SaveStringToFile(TEXT("not-a-token"), *TokenFile);
		const FString Token = FN2CMcpAuthToken::Get();
		TestTrue(TEXT("a new token"), FN2CMcpAuthToken::IsWellFormed(Token));
		TestEqual(TEXT("written over the bad file"), ReadFile(), Token);
	});

	It(TEXT("accepts only the current token as a Bearer credential"), [this]()
	{
		const FString Token = FN2CMcpAuthToken::Get();
		FString Wrong = Token;
		Wrong[0] = Token[0] == TEXT('a') ? TEXT('b') : TEXT('a');
		const FString Accepted[] = { TEXT("Bearer ") + Token, TEXT("bearer ") + Token, TEXT("BEARER  ") + Token + TEXT(" ") };
		for (const FString& Header : Accepted)
		{
			TestTrue(FString::Printf(TEXT("accepts '%s...'"), *Header.Left(9)), FN2CMcpAuthToken::IsAuthorized(&Header));
		}
		const FString Refused[] = { FString(), TEXT("Bearer"), TEXT("Bearer "), Token, TEXT("Bearer") + Token, TEXT("Basic ") + Token,
			TEXT("Bearer ") + Wrong, TEXT("Bearer ") + Token.Left(63), TEXT("Bearer ") + Token + TEXT("0"), TEXT("Bearer ") + Token.ToUpper() };
		for (const FString& Header : Refused)
		{
			TestFalse(FString::Printf(TEXT("refuses '%s' (%d chars)"), *Header.Left(12), Header.Len()), FN2CMcpAuthToken::IsAuthorized(&Header));
		}
		TestFalse(TEXT("refuses a request with no Authorization header"), FN2CMcpAuthToken::IsAuthorized(nullptr));
		TestEqual(TEXT("a refused header does not replace the token"), FN2CMcpAuthToken::Get(), Token);
	});

	It(TEXT("compares in constant time and matches only equal strings"), [this]()
	{
		TestTrue(TEXT("equal"), FN2CMcpAuthToken::ConstantTimeEquals(TEXT("abc"), TEXT("abc")));
		TestFalse(TEXT("last character differs"), FN2CMcpAuthToken::ConstantTimeEquals(TEXT("abc"), TEXT("abd")));
		TestFalse(TEXT("lengths differ"), FN2CMcpAuthToken::ConstantTimeEquals(TEXT("abc"), TEXT("abcd")));
		TestTrue(TEXT("both empty"), FN2CMcpAuthToken::ConstantTimeEquals(FString(), FString()));
	});

	It(TEXT("stops accepting the old token when the token is regenerated, here or by another editor"), [this]()
	{
		const FString Old = FN2CMcpAuthToken::Get();
		const FString New = FN2CMcpAuthToken::Regenerate();
		TestTrue(TEXT("a new token"), FN2CMcpAuthToken::IsWellFormed(New) && New != Old);
		TestEqual(TEXT("written to the file"), ReadFile(), New);
		const FString OldHeader = TEXT("Bearer ") + Old;
		const FString NewHeader = TEXT("Bearer ") + New;
		TestFalse(TEXT("the old token is refused"), FN2CMcpAuthToken::IsAuthorized(&OldHeader));
		TestTrue(TEXT("the new token is accepted"), FN2CMcpAuthToken::IsAuthorized(&NewHeader));

		// Another editor of the same user writes a new token to the shared file: it counts at once, the cached one does not
		const FString Other = TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
		FFileHelper::SaveStringToFile(Other, *TokenFile);
		const FString OtherHeader = TEXT("Bearer ") + Other;
		TestTrue(TEXT("the other editor's token is accepted"), FN2CMcpAuthToken::IsAuthorized(&OtherHeader));
		TestFalse(TEXT("the token this editor had is refused"), FN2CMcpAuthToken::IsAuthorized(&NewHeader));
	});

	It(TEXT("answers a request without the token with 401, WWW-Authenticate: Bearer and an id-less JSON-RPC error"), [this]()
	{
		const FString Token = FN2CMcpAuthToken::Get();
		for (const TArray<TPair<FString, FString>>& Headers : {
			TArray<TPair<FString, FString>>{ { TEXT("Content-Type"), TEXT("application/json") } },
			TArray<TPair<FString, FString>>{ { TEXT("authorization"), TEXT("Bearer 0000") } } })
		{
			TUniquePtr<FHttpServerResponse> Response;
			const bool bAllowed = FN2CMcpAuthToken::Authorize(Request(Headers), [&Response](TUniquePtr<FHttpServerResponse>&& R) { Response = MoveTemp(R); });
			TestFalse(TEXT("refused"), bAllowed);
			if (!TestTrue(TEXT("answered"), Response.IsValid()))
			{
				continue;
			}
			TestEqual(TEXT("401"), static_cast<int32>(Response->Code), 401);
			const TArray<FString>* Challenge = Response->Headers.Find(TEXT("WWW-Authenticate"));
			TestTrue(TEXT("WWW-Authenticate: Bearer"), Challenge && Challenge->Num() == 1 && (*Challenge)[0] == TEXT("Bearer"));
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Response->Body.GetData()), Response->Body.Num());
			const FString Body(Converted.Length(), Converted.Get());
			TSharedPtr<FJsonObject> Message;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Message);
			if (TestTrue(TEXT("a JSON-RPC message"), Message.IsValid()))
			{
				TestTrue(TEXT("id null"), Message->HasTypedField<EJson::Null>(TEXT("id")));
				TestTrue(TEXT("an error"), Message->HasTypedField<EJson::Object>(TEXT("error")));
				TestFalse(TEXT("the body never shows the token"), Body.Contains(Token));
			}
		}

		bool bAnswered = false;
		TestTrue(TEXT("the token passes"), FN2CMcpAuthToken::Authorize(Request({ { TEXT("Authorization"), TEXT("Bearer ") + Token } }),
			[&bAnswered](TUniquePtr<FHttpServerResponse>&&) { bAnswered = true; }));
		TestFalse(TEXT("and leaves the answer to the protocol core"), bAnswered);
	});

	It(TEXT("keeps the real token file outside the project, where the file tools cannot read it, and is off by default"), [this]()
	{
		FN2CMcpAuthToken::SetTokenFilePathForTesting(FString());
		const FString Path = FN2CMcpAuthToken::GetTokenFilePath();
		TestTrue(TEXT("under the user settings folder: ") + Path,
			FPaths::IsUnderDirectory(Path, FPaths::ConvertRelativePathToFull(FPlatformProcess::UserSettingsDir())));
		TestFalse(TEXT("not under the project"), FPaths::IsUnderDirectory(Path, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())));
		TestTrue(TEXT("denied to read-path and read-file"), FN2CPathUtils::IsDeniedForAgentAccess(Path));
		TestFalse(TEXT("Require MCP Bearer Token is off by default (2.0-beta)"), UN2CUserSettings::bDefaultRequireMcpBearerToken);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
