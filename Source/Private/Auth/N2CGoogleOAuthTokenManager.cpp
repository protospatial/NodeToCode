// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Auth/N2CGoogleOAuthTokenManager.h"
#include "Utils/N2CLogger.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/TaskGraphInterfaces.h"

// Singleton instance
UN2CGoogleOAuthTokenManager* UN2CGoogleOAuthTokenManager::Instance = nullptr;

UN2CGoogleOAuthTokenManager* UN2CGoogleOAuthTokenManager::Get()
{
	if (!Instance)
	{
		Instance = NewObject<UN2CGoogleOAuthTokenManager>();
		Instance->AddToRoot(); // Prevent garbage collection
		Instance->Initialize();
	}
	return Instance;
}

// ============================================
// Provider Configuration
// ============================================

const FN2COAuthProviderConfig& UN2CGoogleOAuthTokenManager::GetProviderConfig() const
{
	if (!bConfigInitialized)
	{
		ProviderConfig = FN2COAuthProviderConfig::CreateGoogleConfig();
		bConfigInitialized = true;
	}
	return ProviderConfig;
}

// ============================================
// Google-Specific Token Request Formatting
// ============================================

FString UN2CGoogleOAuthTokenManager::FormatTokenRequestBody(const FString& Code) const
{
	const FN2COAuthProviderConfig& Config = GetProviderConfig();

	// Google requires form-encoded request body
	return FString::Printf(
		TEXT("grant_type=authorization_code&client_id=%s&client_secret=%s&code=%s&redirect_uri=%s&code_verifier=%s"),
		*FGenericPlatformHttp::UrlEncode(Config.ClientId),
		*FGenericPlatformHttp::UrlEncode(Config.ClientSecret),
		*FGenericPlatformHttp::UrlEncode(Code),
		*FGenericPlatformHttp::UrlEncode(Config.RedirectUri),
		*FGenericPlatformHttp::UrlEncode(CurrentVerifier)
	);
}

FString UN2CGoogleOAuthTokenManager::FormatRefreshRequestBody() const
{
	const FN2COAuthProviderConfig& Config = GetProviderConfig();

	// Google requires form-encoded refresh body
	return FString::Printf(
		TEXT("grant_type=refresh_token&client_id=%s&client_secret=%s&refresh_token=%s"),
		*FGenericPlatformHttp::UrlEncode(Config.ClientId),
		*FGenericPlatformHttp::UrlEncode(Config.ClientSecret),
		*FGenericPlatformHttp::UrlEncode(CachedTokens.RefreshToken)
	);
}

// ============================================
// Public Token Exchange Method
// ============================================

void UN2CGoogleOAuthTokenManager::ExchangeCodeForTokens(const FString& Code, const FOnTokenExchangeComplete& OnComplete)
{
	// Google sends just the code, not code#state format
	// Wrap the delegate in a lambda for TFunction compatibility
	ExchangeCodeForTokensInternal(Code, [OnComplete](bool bSuccess)
	{
		OnComplete.ExecuteIfBound(bSuccess);
	});
}

void UN2CGoogleOAuthTokenManager::RefreshAccessToken(const FOnTokenRefreshComplete& OnComplete)
{
	// Use the base class implementation
	// Wrap the delegate in a lambda for TFunction compatibility
	RefreshAccessTokenInternal([OnComplete](bool bSuccess)
	{
		OnComplete.ExecuteIfBound(bSuccess);
	});
}

// ============================================
// Token Exchange Lifecycle Hooks
// ============================================

void UN2CGoogleOAuthTokenManager::OnTokenExchangeSuccess(TFunction<void(bool)> OnComplete)
{
	// Initialize Code Assist session after successful token exchange
	InitializeCodeAssistSession([OnComplete](bool bSessionSuccess)
	{
		if (!bSessionSuccess)
		{
			FN2CLogger::Get().LogWarning(TEXT("Code Assist session initialization failed, but authentication succeeded"));
		}
		// Authentication still succeeded even if session init failed
		if (OnComplete)
		{
			OnComplete(true);
		}
	});
}

void UN2CGoogleOAuthTokenManager::OnLogoutCleanup()
{
	// Clear Code Assist session state
	CachedProjectId.Empty();
	bSessionInitialized = false;
}

void UN2CGoogleOAuthTokenManager::OnInitializeWithTokens()
{
	// Initialize Code Assist session (required for API access)
	// This is done asynchronously - requests may need to wait for it
	InitializeCodeAssistSession([](bool bSuccess)
	{
		if (bSuccess)
		{
			FN2CLogger::Get().Log(TEXT("Code Assist session restored from saved tokens"), EN2CLogSeverity::Info);
		}
		else
		{
			FN2CLogger::Get().LogWarning(TEXT("Failed to restore Code Assist session - may need to re-login"));
		}
	});
}

// ============================================
// Code Assist Session Methods (Google-specific)
// ============================================

FString UN2CGoogleOAuthTokenManager::GetProjectId()
{
	// Ensure session is initialized before returning project ID
	if (!bSessionInitialized && IsAuthenticated())
	{
		EnsureSessionInitialized();
	}
	return CachedProjectId;
}

bool UN2CGoogleOAuthTokenManager::EnsureSessionInitialized()
{
	// Already initialized
	if (bSessionInitialized)
	{
		return true;
	}

	// Not authenticated, can't initialize
	if (!IsAuthenticated())
	{
		return false;
	}

	FN2CLogger::Get().Log(TEXT("Synchronously initializing Code Assist session..."), EN2CLogSeverity::Info);

	// Use a blocking approach for synchronous initialization
	volatile bool bCompleted = false;
	volatile bool bSuccess = false;

	InitializeCodeAssistSession([&bCompleted, &bSuccess](bool bResult)
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
			FN2CLogger::Get().LogError(TEXT("Code Assist session initialization timed out"));
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

void UN2CGoogleOAuthTokenManager::InitializeCodeAssistSession(TFunction<void(bool)> OnComplete)
{
	if (!IsAuthenticated())
	{
		FN2CLogger::Get().LogError(TEXT("Cannot initialize Code Assist session - not authenticated"));
		if (OnComplete) OnComplete(false);
		return;
	}

	FN2CLogger::Get().Log(TEXT("Initializing Code Assist session..."), EN2CLogSeverity::Info);

	// Build loadCodeAssist request payload
	TSharedPtr<FJsonObject> RequestObject = MakeShareable(new FJsonObject());

	// Metadata object
	TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject());
	MetadataObject->SetStringField(TEXT("ideType"), TEXT("IDE_UNSPECIFIED"));
	MetadataObject->SetStringField(TEXT("platform"), TEXT("WINDOWS_AMD64"));
	MetadataObject->SetStringField(TEXT("pluginType"), TEXT("GEMINI"));

	RequestObject->SetObjectField(TEXT("metadata"), MetadataObject);

	// Serialize request
	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(RequestObject.ToSharedRef(), Writer);

	// Create HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetTimeout(30.0f);
	Request->SetURL(TEXT("https://cloudcode-pa.googleapis.com/v1internal:loadCodeAssist"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *CachedTokens.AccessToken));
	Request->SetContentAsString(RequestBody);

	// Weak reference for lambda capture
	TWeakObjectPtr<UN2CGoogleOAuthTokenManager> WeakThis(this);

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (!WeakThis.IsValid())
			{
				if (OnComplete) OnComplete(false);
				return;
			}

			UN2CGoogleOAuthTokenManager* This = WeakThis.Get();

			if (!bConnectedSuccessfully || !Response.IsValid())
			{
				FN2CLogger::Get().LogError(TEXT("loadCodeAssist request failed - no response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			int32 ResponseCode = Response->GetResponseCode();
			FString ResponseContent = Response->GetContentAsString();

			FN2CLogger::Get().Log(FString::Printf(TEXT("loadCodeAssist response (%d): %s"),
				ResponseCode, *ResponseContent), EN2CLogSeverity::Debug);

			if (ResponseCode >= 200 && ResponseCode < 300)
			{
				if (This->ParseLoadCodeAssistResponse(ResponseContent))
				{
					This->bSessionInitialized = true;
					FN2CLogger::Get().Log(TEXT("Code Assist session initialized successfully"), EN2CLogSeverity::Info);
					if (OnComplete) OnComplete(true);
				}
				else
				{
					// Even if we can't parse, the session may still work
					This->bSessionInitialized = true;
					FN2CLogger::Get().LogWarning(TEXT("Could not parse loadCodeAssist response, but session may still work"));
					if (OnComplete) OnComplete(true);
				}
			}
			else
			{
				FN2CLogger::Get().LogError(FString::Printf(TEXT("loadCodeAssist failed with code %d"), ResponseCode));
				if (OnComplete) OnComplete(false);
			}
		});

	Request->ProcessRequest();
}

bool UN2CGoogleOAuthTokenManager::ParseLoadCodeAssistResponse(const FString& ResponseJson)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseJson);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	// Extract project ID if available
	if (JsonObject->HasField(TEXT("cloudaicompanionProject")))
	{
		CachedProjectId = JsonObject->GetStringField(TEXT("cloudaicompanionProject"));
		FN2CLogger::Get().Log(FString::Printf(TEXT("Code Assist project ID: %s"), *CachedProjectId), EN2CLogSeverity::Debug);
	}

	return true;
}
