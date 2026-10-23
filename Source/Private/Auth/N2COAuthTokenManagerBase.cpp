// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Auth/N2COAuthTokenManagerBase.h"
#include "Core/N2CUserSecrets.h"
#include "Utils/N2CLogger.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "Async/TaskGraphInterfaces.h"

// Platform-specific SHA-256 for PKCE requirement
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "bcrypt.lib")
#elif PLATFORM_MAC
#include <CommonCrypto/CommonDigest.h>
#elif PLATFORM_LINUX
#include <openssl/sha.h>
#endif

// SHA-256 implementation for PKCE challenge generation (cross-platform)
namespace
{
	TArray<uint8> ComputeSHA256Base(const TArray<uint8>& Data)
	{
		TArray<uint8> Hash;
		Hash.SetNumUninitialized(32);

#if PLATFORM_WINDOWS
		BCRYPT_ALG_HANDLE hAlg = nullptr;
		NTSTATUS Status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (BCRYPT_SUCCESS(Status))
		{
			BCRYPT_HASH_HANDLE hHash = nullptr;
			Status = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
			if (BCRYPT_SUCCESS(Status))
			{
				BCryptHashData(hHash, (PUCHAR)Data.GetData(), Data.Num(), 0);
				BCryptFinishHash(hHash, Hash.GetData(), 32, 0);
				BCryptDestroyHash(hHash);
			}
			BCryptCloseAlgorithmProvider(hAlg, 0);
		}
#elif PLATFORM_MAC
		CC_SHA256(Data.GetData(), static_cast<CC_LONG>(Data.Num()), Hash.GetData());
#elif PLATFORM_LINUX
		SHA256(Data.GetData(), Data.Num(), Hash.GetData());
#endif

		return Hash;
	}
}

void UN2COAuthTokenManagerBase::Initialize()
{
	// Get or create user secrets
	UserSecrets = NewObject<UN2CUserSecrets>();
	UserSecrets->LoadSecrets();

	// Load existing tokens
	LoadTokensFromStorage();

	// If we have valid tokens, schedule refresh and allow provider-specific initialization
	if (IsAuthenticated() && !IsTokenExpired())
	{
		ScheduleTokenRefresh();
		OnInitializeWithTokens();
	}

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("%s OAuth Token Manager initialized"), *GetProviderName()),
		EN2CLogSeverity::Info);
}

FString UN2COAuthTokenManagerBase::GenerateAuthorizationUrl()
{
	// Generate PKCE values
	CurrentVerifier = GenerateVerifier();
	CurrentState = GenerateState();
	FString Challenge = GenerateChallenge(CurrentVerifier);

	const FN2COAuthProviderConfig& Config = GetProviderConfig();

	// Build authorization URL
	FString AuthUrl = FString::Printf(
		TEXT("%s?response_type=code&client_id=%s&redirect_uri=%s&scope=%s&code_challenge=%s&code_challenge_method=S256&state=%s%s"),
		*Config.AuthEndpoint,
		*Config.ClientId,
		*FGenericPlatformHttp::UrlEncode(Config.RedirectUri),
		*FGenericPlatformHttp::UrlEncode(Config.Scopes),
		*Challenge,
		*CurrentState,
		*GetAdditionalAuthUrlParams()
	);

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Generated %s OAuth authorization URL"), *GetProviderName()),
		EN2CLogSeverity::Debug);

	return AuthUrl;
}

bool UN2COAuthTokenManagerBase::ParseAuthorizationCode(const FString& Input, FString& OutCode, FString& OutState)
{
	// Default implementation: input is just the code
	OutCode = Input;
	OutState = FString();
	return !OutCode.IsEmpty();
}

void UN2COAuthTokenManagerBase::ExchangeCodeForTokensInternal(const FString& CodeInput, TFunction<void(bool)> OnComplete)
{
	// Parse the authorization code using provider-specific logic
	FString Code;
	FString State;
	if (!ParseAuthorizationCode(CodeInput, Code, State))
	{
		FN2CLogger::Get().LogError(TEXT("Failed to parse authorization code"));
		OnError.Broadcast(TEXT("Invalid authorization code format"));
		if (OnComplete) OnComplete(false);
		return;
	}

	// Validate state if provided
	if (!State.IsEmpty() && State != CurrentState)
	{
		FN2CLogger::Get().LogError(TEXT("OAuth state mismatch - possible CSRF attack"));
		OnError.Broadcast(TEXT("Security error: State mismatch. Please try again."));
		if (OnComplete) OnComplete(false);
		return;
	}

	if (Code.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("OAuth code is empty"));
		OnError.Broadcast(TEXT("Invalid authorization code"));
		if (OnComplete) OnComplete(false);
		return;
	}

	if (CurrentVerifier.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("No PKCE verifier found - auth flow may not have been initiated properly"));
		OnError.Broadcast(TEXT("Authentication error: Please start the login flow again."));
		if (OnComplete) OnComplete(false);
		return;
	}

	// Build token request payload using provider-specific formatting
	FString RequestBody = FormatTokenRequestBody(Code);
	const FN2COAuthProviderConfig& Config = GetProviderConfig();

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Token request to: %s"), *Config.TokenEndpoint),
		EN2CLogSeverity::Debug);

	// Create HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetTimeout(30.0f);
	Request->SetURL(Config.TokenEndpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), GetTokenRequestContentType());
	Request->SetContentAsString(RequestBody);

	// Weak reference for lambda capture
	TWeakObjectPtr<UN2COAuthTokenManagerBase> WeakThis(this);

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (UN2COAuthTokenManagerBase* This = WeakThis.Get())
			{
				This->HandleTokenResponse(Response, bConnectedSuccessfully, true, OnComplete);
			}
		});

	Request->ProcessRequest();

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Exchanging %s authorization code for tokens..."), *GetProviderName()),
		EN2CLogSeverity::Info);
}

void UN2COAuthTokenManagerBase::RefreshAccessTokenInternal(TFunction<void(bool)> OnComplete)
{
	if (CachedTokens.RefreshToken.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("No refresh token available"));
		OnError.Broadcast(TEXT("No refresh token. Please log in again."));
		if (OnComplete) OnComplete(false);
		return;
	}

	// Build refresh request payload using provider-specific formatting
	FString RequestBody = FormatRefreshRequestBody();
	const FN2COAuthProviderConfig& Config = GetProviderConfig();

	// Create HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetTimeout(30.0f);
	Request->SetURL(Config.TokenEndpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), GetTokenRequestContentType());
	Request->SetContentAsString(RequestBody);

	// Weak reference for lambda capture
	TWeakObjectPtr<UN2COAuthTokenManagerBase> WeakThis(this);

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (UN2COAuthTokenManagerBase* This = WeakThis.Get())
			{
				This->HandleTokenResponse(Response, bConnectedSuccessfully, false, OnComplete);
			}
		});

	Request->ProcessRequest();

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Refreshing %s access token..."), *GetProviderName()),
		EN2CLogSeverity::Info);
}

bool UN2COAuthTokenManagerBase::RefreshAccessTokenSync()
{
	// Use a blocking approach for synchronous refresh
	volatile bool bCompleted = false;
	volatile bool bSuccess = false;

	RefreshAccessTokenInternal(
		[&bCompleted, &bSuccess](bool bResult)
		{
			bSuccess = bResult;
			bCompleted = true;
		});

	// Wait for completion (with timeout)
	const double TimeoutSeconds = 30.0;
	const double StartTime = FPlatformTime::Seconds();

	while (!bCompleted)
	{
		if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
		{
			FN2CLogger::Get().LogError(TEXT("Token refresh timed out"));
			return false;
		}

		// Process pending tasks on game thread to allow HTTP callbacks to execute
		if (FTaskGraphInterface::IsRunning())
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		}
		FPlatformProcess::Sleep(0.05f);
	}

	return bSuccess;
}

FString UN2COAuthTokenManagerBase::GetAccessToken()
{
	if (!IsAuthenticated())
	{
		return FString();
	}

	// If token is expired, try to refresh synchronously
	if (IsTokenExpired())
	{
		FN2CLogger::Get().Log(TEXT("Access token expired, attempting refresh..."), EN2CLogSeverity::Info);
		if (!RefreshAccessTokenSync())
		{
			return FString();
		}
	}

	return CachedTokens.AccessToken;
}

bool UN2COAuthTokenManagerBase::IsAuthenticated() const
{
	return CachedTokens.HasTokens();
}

bool UN2COAuthTokenManagerBase::IsTokenExpired() const
{
	if (CachedTokens.ExpiresAt.IsEmpty())
	{
		return true;
	}

	FDateTime ExpiryTime;
	if (!FDateTime::ParseIso8601(*CachedTokens.ExpiresAt, ExpiryTime))
	{
		return true;
	}

	// Consider expired if less than 5 minutes remaining
	return (ExpiryTime - FDateTime::UtcNow()).GetTotalMinutes() < 5.0;
}

FString UN2COAuthTokenManagerBase::GetExpirationTimeString() const
{
	if (!IsAuthenticated())
	{
		return TEXT("Not authenticated");
	}

	FDateTime ExpiryTime;
	if (!FDateTime::ParseIso8601(*CachedTokens.ExpiresAt, ExpiryTime))
	{
		return TEXT("Unknown");
	}

	return ExpiryTime.ToString();
}

void UN2COAuthTokenManagerBase::Logout()
{
	CancelTokenRefresh();
	CachedTokens.Clear();
	CurrentVerifier.Empty();
	CurrentState.Empty();

	// Provider-specific cleanup
	OnLogoutCleanup();

	// Clear tokens from storage
	if (UserSecrets)
	{
		switch (GetProviderId())
		{
		case EN2COAuthProvider::Anthropic:
			UserSecrets->ClearOAuthTokens();
			break;
		case EN2COAuthProvider::Google:
			UserSecrets->ClearGoogleOAuthTokens();
			break;
		}
	}

	OnAuthStateChanged.Broadcast(false);

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("%s OAuth logout complete"), *GetProviderName()),
		EN2CLogSeverity::Info);
}

void UN2COAuthTokenManagerBase::OnTokenExchangeSuccess(TFunction<void(bool)> OnComplete)
{
	// Default: no post-auth actions needed
	if (OnComplete)
	{
		OnComplete(true);
	}
}

void UN2COAuthTokenManagerBase::LoadTokensFromStorage()
{
	if (UserSecrets)
	{
		switch (GetProviderId())
		{
		case EN2COAuthProvider::Anthropic:
			UserSecrets->GetOAuthTokens(CachedTokens);
			break;
		case EN2COAuthProvider::Google:
			UserSecrets->GetGoogleOAuthTokens(CachedTokens);
			break;
		}
	}
}

void UN2COAuthTokenManagerBase::SaveTokensToStorage()
{
	if (UserSecrets)
	{
		switch (GetProviderId())
		{
		case EN2COAuthProvider::Anthropic:
			UserSecrets->SetOAuthTokens(
				CachedTokens.AccessToken,
				CachedTokens.RefreshToken,
				CachedTokens.ExpiresAt,
				CachedTokens.Scope);
			break;
		case EN2COAuthProvider::Google:
			UserSecrets->SetGoogleOAuthTokens(
				CachedTokens.AccessToken,
				CachedTokens.RefreshToken,
				CachedTokens.ExpiresAt,
				CachedTokens.Scope);
			break;
		}
	}
}

// ============================================
// PKCE Helper Methods
// ============================================

FString UN2COAuthTokenManagerBase::GenerateVerifier()
{
	// Generate 32 random bytes
	TArray<uint8> RandomBytes;
	RandomBytes.SetNumUninitialized(32);

	for (int32 i = 0; i < 32; i++)
	{
		RandomBytes[i] = static_cast<uint8>(FMath::RandRange(0, 255));
	}

	return Base64UrlEncode(RandomBytes);
}

FString UN2COAuthTokenManagerBase::GenerateChallenge(const FString& Verifier)
{
	// Convert verifier to UTF-8 bytes
	FTCHARToUTF8 VerifierUtf8(*Verifier);
	TArray<uint8> VerifierBytes;
	VerifierBytes.SetNumUninitialized(VerifierUtf8.Length());
	FMemory::Memcpy(VerifierBytes.GetData(), VerifierUtf8.Get(), VerifierUtf8.Length());

	// SHA-256 hash of verifier
	TArray<uint8> HashBytes = ComputeSHA256Base(VerifierBytes);

	return Base64UrlEncode(HashBytes);
}

FString UN2COAuthTokenManagerBase::GenerateState()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}

FString UN2COAuthTokenManagerBase::Base64UrlEncode(const TArray<uint8>& Bytes)
{
	// Standard Base64 encode
	FString Base64 = FBase64::Encode(Bytes);

	// Convert to Base64URL (replace + with -, / with _, remove padding =)
	Base64.ReplaceInline(TEXT("+"), TEXT("-"));
	Base64.ReplaceInline(TEXT("/"), TEXT("_"));
	Base64.ReplaceInline(TEXT("="), TEXT(""));

	return Base64;
}

// ============================================
// Token Management
// ============================================

void UN2COAuthTokenManagerBase::ScheduleTokenRefresh()
{
	CancelTokenRefresh();

	if (CachedTokens.ExpiresAt.IsEmpty())
	{
		return;
	}

	FDateTime ExpiryTime;
	if (!FDateTime::ParseIso8601(*CachedTokens.ExpiresAt, ExpiryTime))
	{
		return;
	}

	// Refresh 5 minutes before expiry
	FTimespan TimeUntilRefresh = (ExpiryTime - FDateTime::UtcNow()) - FTimespan::FromMinutes(5);

	if (TimeUntilRefresh.GetTotalSeconds() > 0)
	{
		if (GEngine && GEngine->GetWorld())
		{
			GEngine->GetWorld()->GetTimerManager().SetTimer(
				RefreshTimerHandle,
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					RefreshAccessTokenInternal([this](bool bSuccess)
					{
						if (!bSuccess)
						{
							FN2CLogger::Get().LogError(
								FString::Printf(TEXT("Automatic %s token refresh failed"), *GetProviderName()));
						}
					});
				}),
				TimeUntilRefresh.GetTotalSeconds(),
				false
			);

			FN2CLogger::Get().Log(
				FString::Printf(TEXT("%s token refresh scheduled in %.0f minutes"),
					*GetProviderName(), TimeUntilRefresh.GetTotalMinutes()),
				EN2CLogSeverity::Debug
			);
		}
	}
}

void UN2COAuthTokenManagerBase::CancelTokenRefresh()
{
	if (GEngine && GEngine->GetWorld())
	{
		GEngine->GetWorld()->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
}

bool UN2COAuthTokenManagerBase::ParseTokenResponse(const FString& ResponseJson)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseJson);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("Failed to parse token response JSON"));
		return false;
	}

	// Check for error
	if (JsonObject->HasField(TEXT("error")))
	{
		FString Error = JsonObject->GetStringField(TEXT("error"));
		FString ErrorDescription = JsonObject->GetStringField(TEXT("error_description"));
		FN2CLogger::Get().LogError(FString::Printf(TEXT("OAuth error: %s - %s"), *Error, *ErrorDescription));
		OnError.Broadcast(ErrorDescription.IsEmpty() ? Error : ErrorDescription);
		return false;
	}

	// Extract tokens
	FString AccessToken = JsonObject->GetStringField(TEXT("access_token"));
	FString RefreshToken = JsonObject->GetStringField(TEXT("refresh_token"));
	FString Scope = JsonObject->GetStringField(TEXT("scope"));

	if (AccessToken.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("No access token in response"));
		return false;
	}

	// Calculate expiry time (use provider-specific default if not specified)
	int32 ExpiresIn = GetDefaultTokenExpirySeconds();
	if (JsonObject->HasField(TEXT("expires_in")))
	{
		ExpiresIn = static_cast<int32>(JsonObject->GetNumberField(TEXT("expires_in")));
	}

	FDateTime ExpiryTime = FDateTime::UtcNow() + FTimespan::FromSeconds(ExpiresIn);

	// Update cached tokens
	CachedTokens.AccessToken = AccessToken;
	if (!RefreshToken.IsEmpty())
	{
		CachedTokens.RefreshToken = RefreshToken;
	}
	CachedTokens.ExpiresAt = ExpiryTime.ToIso8601();
	CachedTokens.Scope = Scope;

	return true;
}

void UN2COAuthTokenManagerBase::HandleTokenResponse(FHttpResponsePtr Response, bool bSuccess,
	bool bIsExchange, TFunction<void(bool)> OnComplete)
{
	if (!bSuccess || !Response.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("Token request failed - no response"));
		OnError.Broadcast(TEXT("Network error. Please check your connection."));
		if (OnComplete) OnComplete(false);
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	FString ResponseContent = Response->GetContentAsString();

	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		FN2CLogger::Get().LogError(FString::Printf(
			TEXT("Token request failed with code %d: %s"), ResponseCode, *ResponseContent));

		// Try to parse error from response
		TSharedPtr<FJsonObject> ErrorJson;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
		if (FJsonSerializer::Deserialize(Reader, ErrorJson) && ErrorJson.IsValid())
		{
			FString ErrorMessage = ErrorJson->GetStringField(TEXT("error_description"));
			if (ErrorMessage.IsEmpty())
			{
				ErrorMessage = ErrorJson->GetStringField(TEXT("error"));
			}
			OnError.Broadcast(ErrorMessage.IsEmpty() ? TEXT("Authentication failed") : ErrorMessage);
		}
		else
		{
			OnError.Broadcast(FString::Printf(TEXT("Authentication failed (HTTP %d)"), ResponseCode));
		}

		if (OnComplete) OnComplete(false);
		return;
	}

	// Parse successful response
	if (!ParseTokenResponse(ResponseContent))
	{
		if (OnComplete) OnComplete(false);
		return;
	}

	// Save tokens and schedule refresh
	SaveTokensToStorage();
	ScheduleTokenRefresh();

	// Clear PKCE values after successful exchange
	CurrentVerifier.Empty();
	CurrentState.Empty();

	// Broadcast success
	OnAuthStateChanged.Broadcast(true);

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("%s OAuth authentication successful"), *GetProviderName()),
		EN2CLogSeverity::Info);

	// If this was a token exchange, call post-exchange hook
	if (bIsExchange)
	{
		OnTokenExchangeSuccess(OnComplete);
	}
	else
	{
		if (OnComplete) OnComplete(true);
	}
}
