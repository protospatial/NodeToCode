// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/N2CModelDiscovery.h"

#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "LLM/N2CHttpTransport.h"
#include "LLM/Providers/N2COpenAICompatibleService.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace N2CModelDiscoveryPrivate
{
	static const TCHAR* const LogContext = TEXT("ModelDiscovery");

	/** Page size asked for where the endpoint takes one (Anthropic allows 1000, Gemini 1000) */
	static const TCHAR* const PageSize = TEXT("1000");

	static TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
	}

	static int32 ReadInt(const FJsonObject& Object, std::initializer_list<const TCHAR*> Fields)
	{
		for (const TCHAR* Field : Fields)
		{
			double Value = 0.0;
			if (Object.TryGetNumberField(Field, Value) && Value > 0.0)
			{
				return static_cast<int32>(FMath::Min(Value, static_cast<double>(MAX_int32)));
			}
		}
		return 0;
	}

	static FString ReadString(const FJsonObject& Object, const TCHAR* Field)
	{
		FString Value;
		return Object.TryGetStringField(Field, Value) ? Value.TrimStartAndEnd() : FString();
	}

	/** A date field as YYYY-MM-DD: an ISO 8601 string ("2026-11-30T17:00:00Z") or Unix seconds; empty when absent or null */
	static FString ReadDate(const FJsonObject& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Field);
		if (!Value.IsValid() || Value->IsNull())
		{
			return FString();
		}
		double Seconds = 0.0;
		if (Value->Type == EJson::Number && Value->TryGetNumber(Seconds) && Seconds > 0.0)
		{
			return FDateTime::FromUnixTimestamp(static_cast<int64>(Seconds)).ToString(TEXT("%Y-%m-%d"));
		}
		FString Text;
		if (Value->TryGetString(Text))
		{
			Text.TrimStartAndEndInline();
			return Text.Len() >= 10 ? Text.Left(10) : Text;
		}
		return FString();
	}

	static void Notify(const FString& Message, bool bSuccess)
	{
		if (FApp::IsUnattended() || !FSlateApplication::IsInitialized())
		{
			return;
		}
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 8.0f;
		Info.bUseSuccessFailIcons = true;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** The provider's own message from an error body ({"error":{"message"}}, {"error":"..."}, {"message"}), else the start of the body */
	static FString DescribeErrorBody(const FString& Body)
	{
		if (const TSharedPtr<FJsonObject> Root = ParseObject(Body))
		{
			const TSharedPtr<FJsonObject>* Error = nullptr;
			FString Message;
			if (Root->TryGetObjectField(TEXT("error"), Error) && Error && (*Error)->TryGetStringField(TEXT("message"), Message))
			{
				return Message;
			}
			if (Root->TryGetStringField(TEXT("error"), Message) || Root->TryGetStringField(TEXT("message"), Message))
			{
				return Message;
			}
		}
		return Body.Left(200);
	}

	static FString ProviderLabel(FName ProviderId)
	{
		const FN2CProviderProfile* Row = FN2CModelCatalog::Get().FindProvider(ProviderId);
		return Row && !Row->DisplayName.IsEmpty() ? Row->DisplayName : ProviderId.ToString();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FN2CModelList

const FN2CDiscoveredModel* FN2CModelList::Find(const FString& ModelId) const
{
	const FString Wanted = ModelId.TrimStartAndEnd();
	return Models.FindByPredicate([&Wanted](const FN2CDiscoveredModel& Model) { return Model.Id.Equals(Wanted, ESearchCase::CaseSensitive); });
}

FString FN2CModelList::ToJson() const
{
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("schema_version"), 1);
	Writer->WriteValue(TEXT("provider"), ProviderId.ToString());
	Writer->WriteValue(TEXT("endpoint"), Endpoint);
	Writer->WriteValue(TEXT("fetched_at"), FetchedAt.ToIso8601());
	Writer->WriteArrayStart(TEXT("models"));
	for (const FN2CDiscoveredModel& Model : Models)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Model.Id);
		if (!Model.DisplayName.IsEmpty())
		{
			Writer->WriteValue(TEXT("display_name"), Model.DisplayName);
		}
		if (Model.ContextWindow > 0)
		{
			Writer->WriteValue(TEXT("context_window"), Model.ContextWindow);
		}
		if (Model.MaxOutput > 0)
		{
			Writer->WriteValue(TEXT("max_output"), Model.MaxOutput);
		}
		if (Model.bDeprecated)
		{
			Writer->WriteValue(TEXT("deprecated"), true);
		}
		if (!Model.ShutdownDate.IsEmpty())
		{
			Writer->WriteValue(TEXT("shutdown_date"), Model.ShutdownDate);
		}
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}

bool FN2CModelList::FromJson(const FString& Json, FN2CModelList& Out, FString& OutError)
{
	using namespace N2CModelDiscoveryPrivate;
	const TSharedPtr<FJsonObject> Root = ParseObject(Json);
	if (!Root.IsValid())
	{
		OutError = TEXT("not JSON");
		return false;
	}
	int32 Version = 0;
	if (!Root->TryGetNumberField(TEXT("schema_version"), Version) || Version != 1)
	{
		OutError = TEXT("schema_version is not 1");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
	if (!Root->TryGetArrayField(TEXT("models"), Models))
	{
		OutError = TEXT("no models array");
		return false;
	}
	Out = FN2CModelList();
	Out.ProviderId = FName(*ReadString(*Root, TEXT("provider")));
	Out.Endpoint = ReadString(*Root, TEXT("endpoint"));
	FDateTime::ParseIso8601(*ReadString(*Root, TEXT("fetched_at")), Out.FetchedAt);
	for (const TSharedPtr<FJsonValue>& Value : *Models)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object)
		{
			continue;
		}
		FN2CDiscoveredModel Model;
		Model.Id = ReadString(**Object, TEXT("id"));
		if (Model.Id.IsEmpty())
		{
			continue;
		}
		Model.DisplayName = ReadString(**Object, TEXT("display_name"));
		Model.ContextWindow = ReadInt(**Object, { TEXT("context_window") });
		Model.MaxOutput = ReadInt(**Object, { TEXT("max_output") });
		(*Object)->TryGetBoolField(TEXT("deprecated"), Model.bDeprecated);
		Model.ShutdownDate = ReadString(**Object, TEXT("shutdown_date"));
		Out.Models.Add(MoveTemp(Model));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Parsing

EN2CModelListFormat FN2CModelDiscovery::FormatFor(const FN2CProviderProfile& Profile)
{
	if (Profile.Dialect == TEXT("anthropic_messages"))
	{
		return EN2CModelListFormat::Anthropic;
	}
	if (Profile.Dialect == TEXT("gemini"))
	{
		return EN2CModelListFormat::Gemini;
	}
	if (Profile.Dialect == TEXT("ollama"))
	{
		return EN2CModelListFormat::Ollama;
	}
	return EN2CModelListFormat::OpenAI;
}

bool FN2CModelDiscovery::ParsePage(EN2CModelListFormat Format, const FString& Body, FN2CModelListPage& OutPage, FString& OutError)
{
	using namespace N2CModelDiscoveryPrivate;
	OutPage = FN2CModelListPage();
	const TSharedPtr<FJsonObject> Root = ParseObject(Body);
	if (!Root.IsValid())
	{
		OutError = TEXT("the model list is not a JSON object");
		return false;
	}

	const TCHAR* const ArrayField = (Format == EN2CModelListFormat::Gemini || Format == EN2CModelListFormat::Ollama) ? TEXT("models") : TEXT("data");
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Root->TryGetArrayField(ArrayField, Items) || !Items)
	{
		OutError = FString::Printf(TEXT("the model list has no \"%s\" array"), ArrayField);
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Items)
	{
		const TSharedPtr<FJsonObject>* ItemPtr = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(ItemPtr) || !ItemPtr)
		{
			continue;
		}
		const FJsonObject& Item = **ItemPtr;
		FN2CDiscoveredModel Model;
		switch (Format)
		{
		case EN2CModelListFormat::Anthropic:
		{
			Model.Id = ReadString(Item, TEXT("id"));
			Model.DisplayName = ReadString(Item, TEXT("display_name"));
			Model.ContextWindow = ReadInt(Item, { TEXT("max_input_tokens") });
			Model.MaxOutput = ReadInt(Item, { TEXT("max_tokens") });
			const FString Lifecycle = ReadString(Item, TEXT("lifecycle"));
			Model.ShutdownDate = ReadDate(Item, TEXT("retires_at"));
			Model.bDeprecated = (!Lifecycle.IsEmpty() && Lifecycle != TEXT("active"))
				|| !ReadDate(Item, TEXT("deprecated_at")).IsEmpty() || !Model.ShutdownDate.IsEmpty();
			break;
		}
		case EN2CModelListFormat::Gemini:
		{
			// Only models that answer generateContent (the list also has embedding, live and answer-only models)
			const TArray<TSharedPtr<FJsonValue>>* Methods = nullptr;
			if (Item.TryGetArrayField(TEXT("supportedGenerationMethods"), Methods) && Methods
				&& !Methods->ContainsByPredicate([](const TSharedPtr<FJsonValue>& M) { return M.IsValid() && M->AsString() == TEXT("generateContent"); }))
			{
				continue;
			}
			Model.Id = ReadString(Item, TEXT("name"));
			Model.Id.RemoveFromStart(TEXT("models/"));
			Model.DisplayName = ReadString(Item, TEXT("displayName"));
			Model.ContextWindow = ReadInt(Item, { TEXT("inputTokenLimit") });
			Model.MaxOutput = ReadInt(Item, { TEXT("outputTokenLimit") });
			break;
		}
		case EN2CModelListFormat::Ollama:
		{
			Model.Id = ReadString(Item, TEXT("name"));
			if (Model.Id.IsEmpty())
			{
				Model.Id = ReadString(Item, TEXT("model"));
			}
			break;
		}
		case EN2CModelListFormat::OpenAI:
		default:
		{
			Model.Id = ReadString(Item, TEXT("id"));
			Model.DisplayName = ReadString(Item, TEXT("name"));
			// DeepSeek and Groq context_window, OpenRouter context_length, vLLM max_model_len, LM Studio max_context_length
			Model.ContextWindow = ReadInt(Item, { TEXT("context_window"), TEXT("context_length"), TEXT("max_model_len"), TEXT("max_context_length") });
			Model.MaxOutput = ReadInt(Item, { TEXT("max_output_tokens"), TEXT("max_completion_tokens") });
			Model.ShutdownDate = ReadDate(Item, TEXT("shutdown_date"));
			bool bDeprecated = false;
			Item.TryGetBoolField(TEXT("deprecated"), bDeprecated);
			Model.bDeprecated = bDeprecated || !Model.ShutdownDate.IsEmpty();
			break;
		}
		}
		if (!Model.Id.IsEmpty())
		{
			OutPage.Models.Add(MoveTemp(Model));
		}
	}

	// Where the next page starts
	if (Format == EN2CModelListFormat::Gemini)
	{
		OutPage.NextCursor = ReadString(*Root, TEXT("nextPageToken"));
	}
	else if (Format != EN2CModelListFormat::Ollama)
	{
		bool bHasMore = false;
		if (Root->TryGetBoolField(TEXT("has_more"), bHasMore) && bHasMore)
		{
			OutPage.NextCursor = ReadString(*Root, TEXT("last_id"));
			if (OutPage.NextCursor.IsEmpty() && OutPage.Models.Num() > 0)
			{
				OutPage.NextCursor = OutPage.Models.Last().Id;
			}
		}
	}
	return true;
}

FString FN2CModelDiscovery::MakePageUrl(const FN2CProviderProfile& Profile, const FString& Cursor)
{
	FString Url = Profile.BaseUrl.TrimStartAndEnd();
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	FString Path = Profile.ModelsPath.TrimStartAndEnd();
	if (!Path.IsEmpty() && !Path.StartsWith(TEXT("/")))
	{
		Path = TEXT("/") + Path;
	}
	Url += Path;

	TArray<FString> Query;
	const FString EncodedCursor = FGenericPlatformHttp::UrlEncode(Cursor);
	switch (FormatFor(Profile))
	{
	case EN2CModelListFormat::Anthropic:
		Query.Add(FString(TEXT("limit=")) + N2CModelDiscoveryPrivate::PageSize);
		if (!Cursor.IsEmpty())
		{
			Query.Add(TEXT("after_id=") + EncodedCursor);
		}
		break;
	case EN2CModelListFormat::Gemini:
		Query.Add(FString(TEXT("pageSize=")) + N2CModelDiscoveryPrivate::PageSize);
		if (!Cursor.IsEmpty())
		{
			Query.Add(TEXT("pageToken=") + EncodedCursor);
		}
		break;
	case EN2CModelListFormat::OpenAI:
		if (!Cursor.IsEmpty())
		{
			Query.Add(TEXT("after=") + EncodedCursor);
		}
		break;
	default:
		break;
	}
	if (Query.Num() > 0)
	{
		Url += (Url.Contains(TEXT("?")) ? TEXT("&") : TEXT("?")) + FString::Join(Query, TEXT("&"));
	}
	return Url;
}

// ---------------------------------------------------------------------------------------------------------------------
// Where and whether

FN2CModelDiscovery& FN2CModelDiscovery::Get()
{
	static FN2CModelDiscovery Instance;
	return Instance;
}

EN2CModelDiscoverySkip FN2CModelDiscovery::GetEnvironmentSkip(bool bIsCommandlet, bool bIsUnattended)
{
	if (bIsCommandlet)
	{
		return EN2CModelDiscoverySkip::Commandlet;
	}
	if (bIsUnattended)
	{
		return EN2CModelDiscoverySkip::Unattended;
	}
	return EN2CModelDiscoverySkip::None;
}

EN2CModelDiscoverySkip FN2CModelDiscovery::GetEnvironmentSkip()
{
	return GetEnvironmentSkip(IsRunningCommandlet(), FApp::IsUnattended());
}

FName FN2CModelDiscovery::ProviderIdFor(EN2CLLMProvider Provider, const FString& ProfileId)
{
	if (Provider != EN2CLLMProvider::OpenAICompatible)
	{
		return FN2CModelCatalog::ProviderIdFor(Provider);
	}
	FString Profile = ProfileId.TrimStartAndEnd();
	if (Profile.IsEmpty())
	{
		Profile = GetDefault<UN2CUserSettings>()->ProviderProfile.TrimStartAndEnd();
	}
	return Profile.IsEmpty() ? NAME_None : FName(*Profile);
}

FName FN2CModelDiscovery::GetActiveProviderId()
{
	return ProviderIdFor(GetDefault<UN2CUserSettings>()->Provider);
}

bool FN2CModelDiscovery::IsOptedIn(FName ProviderId)
{
	if (ProviderId.IsNone())
	{
		return false;
	}
	const FString Wanted = ProviderId.ToString();
	return GetDefault<UN2CSettings>()->LiveModelListProviders.ContainsByPredicate(
		[&Wanted](const FString& Entry) { return Entry.TrimStartAndEnd().Equals(Wanted, ESearchCase::IgnoreCase); });
}

bool FN2CModelDiscovery::MakeTarget(EN2CLLMProvider Provider, const FString& ProfileId, FTarget& OutTarget, FString& OutError)
{
	OutTarget = FTarget();
	const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
	switch (Provider)
	{
	case EN2CLLMProvider::OpenAI:
	case EN2CLLMProvider::Anthropic:
	case EN2CLLMProvider::Gemini:
	case EN2CLLMProvider::Ollama:
	{
		const FN2CProviderProfile* Row = Catalog.FindProvider(FN2CModelCatalog::ProviderIdFor(Provider));
		if (!Row)
		{
			OutError = TEXT("the model catalog has no row for this provider");
			return false;
		}
		OutTarget.Profile = *Row;
		if (Provider == EN2CLLMProvider::Ollama)
		{
			const FString Endpoint = GetDefault<UN2CSettings>()->OllamaConfig.OllamaEndpoint.TrimStartAndEnd();
			if (!Endpoint.IsEmpty())
			{
				OutTarget.Profile.BaseUrl = Endpoint;
			}
		}
		break;
	}
	default:
		// DeepSeek, LM Studio and OpenAI-Compatible: the profile their requests use (endpoint, Base URL Override)
		if (!UN2COpenAICompatibleService::ResolveProfile(Provider, ProfileId, OutTarget.Profile, &OutError))
		{
			return false;
		}
		break;
	}
	if (OutTarget.Profile.ModelsPath.TrimStartAndEnd().IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s has no model list endpoint (models_path)"), *OutTarget.Profile.DisplayName);
		return false;
	}
	OutTarget.ProviderId = ProviderIdFor(Provider, ProfileId);
	return true;
}

FString FN2CModelDiscovery::GetDefaultCacheDir()
{
	return FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("ModelCache");
}

FString FN2CModelDiscovery::DescribeSkip(EN2CModelDiscoverySkip Skip, FName ProviderId)
{
	const FString Id = ProviderId.ToString();
	switch (Skip)
	{
	case EN2CModelDiscoverySkip::Commandlet:
		return TEXT("model lists are never fetched in a commandlet");
	case EN2CModelDiscoverySkip::Unattended:
		return TEXT("model lists are never fetched in an -unattended session");
	case EN2CModelDiscoverySkip::NotOptedIn:
		return FString::Printf(TEXT("%s is not in Project Settings > Plugins > Node to Code > LLM Provider > Live Model Lists"), *Id);
	case EN2CModelDiscoverySkip::NoKey:
		return FString::Printf(TEXT("%s has no API key (saved or in the environment)"), *Id);
	case EN2CModelDiscoverySkip::NoProfile:
		return FString::Printf(TEXT("no provider profile '%s'"), *Id);
	case EN2CModelDiscoverySkip::Busy:
		return FString::Printf(TEXT("%s's model list is being fetched"), *Id);
	default:
		return FString();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Fetching

struct FN2CModelDiscovery::FFetchState
{
	FTarget Target;
	EN2CModelListFormat Format = EN2CModelListFormat::OpenAI;
	FOnFetched OnDone;
	FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
	FN2CModelList List;
	TSet<FString> Seen;
	FString Cursor;
	int32 Pages = 0;

	void Finish(bool bSucceeded, const FString& Error)
	{
		if (Handle->IsFinished())
		{
			return;
		}
		Handle->Complete(FOnLLMResponseReceived(), FString());
		if (OnDone)
		{
			OnDone(bSucceeded, List, Error);
		}
	}
};

TSharedRef<IN2CHttpTransport> FN2CModelDiscovery::GetTransport() const
{
	return Transport.IsValid() ? Transport.ToSharedRef() : FN2CHttpModuleTransport::Get();
}

void FN2CModelDiscovery::SetTransport(TSharedPtr<IN2CHttpTransport> InTransport)
{
	Transport = InTransport;
}

FN2CLLMRequestHandleRef FN2CModelDiscovery::Fetch(const FTarget& Target, FOnFetched OnDone)
{
	const TSharedRef<FFetchState> State = MakeShared<FFetchState>();
	State->Target = Target;
	State->Format = FormatFor(Target.Profile);
	State->OnDone = MoveTemp(OnDone);
	State->List.ProviderId = Target.ProviderId;
	State->List.Endpoint = MakePageUrl(Target.Profile, FString());
	FetchPage(State);
	return State->Handle;
}

void FN2CModelDiscovery::FetchPage(const TSharedRef<FFetchState>& State)
{
	if (State->Handle->IsFinished())
	{
		return;
	}
	FN2CHttpRequestSpec Request;
	Request.Verb = TEXT("GET");
	Request.Url = MakePageUrl(State->Target.Profile, State->Cursor);
	Request.TimeoutSeconds = 30.0f;
	Request.ActivityTimeoutSeconds = 20.0f;
	Request.Headers.Emplace(TEXT("Accept"), TEXT("application/json"));
	FString KeyHeader, KeyValue;
	if (State->Target.Credential.GetHeader(KeyHeader, KeyValue))
	{
		Request.Headers.Emplace(KeyHeader, KeyValue);
	}
	if (State->Format == EN2CModelListFormat::Anthropic)
	{
		Request.Headers.Emplace(TEXT("anthropic-version"), TEXT("2023-06-01"));
	}
	for (const TPair<FString, FString>& Header : State->Target.Profile.ExtraHeaders)
	{
		if (!FN2CCredential::IsCredentialHeader(Header.Key))
		{
			Request.Headers.Add(Header);
		}
	}
	++State->Pages;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Fetching the model list of %s: GET %s"), *State->Target.ProviderId.ToString(), *Request.Url),
		EN2CLogSeverity::Info, N2CModelDiscoveryPrivate::LogContext);

	GetTransport()->Send(Request, State->Handle, [this, State](const FN2CHttpResponseData& Response)
	{
		if (State->Handle->IsFinished())
		{
			return;
		}
		if (!Response.bReceived)
		{
			State->Finish(false, Response.FailureMessage.IsEmpty() ? FString(TEXT("no response")) : Response.FailureMessage);
			return;
		}
		if (Response.Status < 200 || Response.Status >= 300)
		{
			State->Finish(false, FString::Printf(TEXT("HTTP %d: %s"), Response.Status, *N2CModelDiscoveryPrivate::DescribeErrorBody(Response.Body)));
			return;
		}
		FN2CModelListPage Page;
		FString Error;
		if (!ParsePage(State->Format, Response.Body, Page, Error))
		{
			State->Finish(false, Error);
			return;
		}
		for (FN2CDiscoveredModel& Model : Page.Models)
		{
			if (!State->Seen.Contains(Model.Id))
			{
				State->Seen.Add(Model.Id);
				State->List.Models.Add(MoveTemp(Model));
			}
		}
		// The next page, unless the server repeats a cursor or the guard is reached
		if (!Page.NextCursor.IsEmpty() && Page.NextCursor != State->Cursor && State->Pages < MaxPages)
		{
			State->Cursor = Page.NextCursor;
			FetchPage(State);
			return;
		}
		State->List.FetchedAt = FDateTime::UtcNow();
		State->Finish(true, FString());
	});
}

EN2CModelDiscoverySkip FN2CModelDiscovery::RequestActiveRefresh(const TCHAR* Reason, bool bNotify)
{
	return RequestRefresh(GetDefault<UN2CUserSettings>()->Provider, FString(), Reason, bNotify);
}

EN2CModelDiscoverySkip FN2CModelDiscovery::RequestRefresh(EN2CLLMProvider Provider, const FString& ProfileId, const TCHAR* Reason, bool bNotify)
{
	using namespace N2CModelDiscoveryPrivate;
	const FName Id = ProviderIdFor(Provider, ProfileId);
	auto Skip = [Id, bNotify](EN2CModelDiscoverySkip Why)
	{
		const FString Message = FString::Printf(TEXT("Model list not fetched: %s."), *DescribeSkip(Why, Id));
		FN2CLogger::Get().Log(Message, Why == EN2CModelDiscoverySkip::NotOptedIn || Why == EN2CModelDiscoverySkip::Unattended
			|| Why == EN2CModelDiscoverySkip::Commandlet ? EN2CLogSeverity::Debug : EN2CLogSeverity::Info, LogContext);
		if (bNotify)
		{
			Notify(Message, false);
		}
		return Why;
	};

	const EN2CModelDiscoverySkip Environment = GetEnvironmentSkip();
	if (Environment != EN2CModelDiscoverySkip::None)
	{
		return Skip(Environment);
	}
	if (!IsOptedIn(Id))
	{
		return Skip(EN2CModelDiscoverySkip::NotOptedIn);
	}
	if (InFlight.Contains(Id))
	{
		return Skip(EN2CModelDiscoverySkip::Busy);
	}
	FTarget Target;
	FString Error;
	if (!MakeTarget(Provider, ProfileId, Target, Error))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Model list of %s not fetched: %s"), *Id.ToString(), *Error), LogContext);
		if (bNotify)
		{
			Notify(FString::Printf(TEXT("Model list not fetched: %s"), *Error), false);
		}
		return EN2CModelDiscoverySkip::NoProfile;
	}

	// The key: an OpenAI-Compatible profile's own entry or variables, Ollama none, else the provider's credential source
	if (Provider == EN2CLLMProvider::OpenAICompatible)
	{
		if (!UN2COpenAICompatibleService::ResolveProfileCredential(Target.Profile, Target.Credential))
		{
			return Skip(EN2CModelDiscoverySkip::NoKey);
		}
	}
	else if (Provider != EN2CLLMProvider::Ollama)
	{
		const TSharedRef<EN2CModelDiscoverySkip> Outcome = MakeShared<EN2CModelDiscoverySkip>(EN2CModelDiscoverySkip::None);
		const FString ReasonText = Reason ? Reason : TEXT("");
		IN2CCredentialSource::Get()->Resolve(Provider, [this, Target, ReasonText, bNotify, Provider, Outcome, Skip](FN2CCredentialResult Result) mutable
		{
			if (Result.HasError())
			{
				*Outcome = Skip(EN2CModelDiscoverySkip::NoKey);
				return;
			}
			Target.Credential = Result.StealValue();
			SendFetch(Target, *ReasonText, bNotify, Provider);
		});
		return *Outcome;
	}
	SendFetch(Target, Reason, bNotify, Provider);
	return EN2CModelDiscoverySkip::None;
}

void FN2CModelDiscovery::SendFetch(const FTarget& Target, const TCHAR* Reason, bool bNotify, EN2CLLMProvider Provider)
{
	using namespace N2CModelDiscoveryPrivate;
	const FName Id = Target.ProviderId;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Refreshing the model list of %s (%s)"), *Id.ToString(), Reason ? Reason : TEXT("")),
		EN2CLogSeverity::Info, LogContext);
	const TSharedRef<bool> bFinished = MakeShared<bool>(false);
	const FN2CLLMRequestHandleRef Handle = Fetch(Target, [this, Id, bNotify, Provider, bFinished](bool bSucceeded, const FN2CModelList& List, const FString& Error)
	{
		*bFinished = true;
		InFlight.Remove(Id);
		const FString Label = ProviderLabel(Id);
		if (!bSucceeded)
		{
			const FString Message = FString::Printf(TEXT("Could not fetch the model list of %s: %s. The last list fetched (if any) and the catalog stay in use."),
				*Label, *Error);
			FN2CLogger::Get().LogWarning(Message, LogContext);
			if (bNotify)
			{
				Notify(Message, false);
			}
			return;
		}
		SetList(List);
		SaveCache(List, GetDefaultCacheDir());
		const int32 Deprecated = List.Models.FilterByPredicate([](const FN2CDiscoveredModel& M) { return M.bDeprecated; }).Num();
		const FString Message = FString::Printf(TEXT("Model list of %s: %d models (%d marked deprecated)"), *Label, List.Models.Num(), Deprecated);
		FN2CLogger::Get().Log(Message, EN2CLogSeverity::Info, LogContext);
		if (bNotify)
		{
			Notify(Message, true);
		}
		if (Id == GetActiveProviderId())
		{
			const FString Warning = GetSelectedModelWarning(Provider);
			if (!Warning.IsEmpty())
			{
				FN2CLogger::Get().LogWarning(Warning, LogContext);
				Notify(Warning, false);
			}
		}
	});
	if (!*bFinished)
	{
		InFlight.Add(Id, Handle);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Lists

const FN2CModelList* FN2CModelDiscovery::GetList(FName ProviderId) const
{
	return Lists.Find(ProviderId);
}

const FN2CModelList* FN2CModelDiscovery::GetListIfOptedIn(FName ProviderId) const
{
	return IsOptedIn(ProviderId) ? GetList(ProviderId) : nullptr;
}

void FN2CModelDiscovery::SetList(const FN2CModelList& List)
{
	Lists.Add(List.ProviderId, List);
	OnListsChanged.Broadcast();
	// The cost and context displays read the list for a model the catalog lacks
	if (UN2CSettings* Settings = GetMutableDefault<UN2CSettings>())
	{
		Settings->NotifyModelSettingsChanged();
	}
}

void FN2CModelDiscovery::RemoveList(FName ProviderId)
{
	if (Lists.Remove(ProviderId) > 0)
	{
		OnListsChanged.Broadcast();
		if (UN2CSettings* Settings = GetMutableDefault<UN2CSettings>())
		{
			Settings->NotifyModelSettingsChanged();
		}
	}
}

bool FN2CModelDiscovery::SaveCache(const FN2CModelList& List, const FString& Dir)
{
	const FString Path = Dir / FPaths::MakeValidFileName(List.ProviderId.ToString()) + TEXT(".json");
	IFileManager::Get().MakeDirectory(*Dir, true);
	if (!FFileHelper::SaveStringToFile(List.ToJson(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Could not write the model list cache %s"), *Path), N2CModelDiscoveryPrivate::LogContext);
		return false;
	}
	return true;
}

bool FN2CModelDiscovery::LoadCache(FName ProviderId, const FString& Dir, FN2CModelList& OutList)
{
	const FString Path = Dir / FPaths::MakeValidFileName(ProviderId.ToString()) + TEXT(".json");
	FString Text;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return false;
	}
	FString Error;
	if (!FN2CModelList::FromJson(Text, OutList, Error))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Ignoring the model list cache %s: %s"), *Path, *Error), N2CModelDiscoveryPrivate::LogContext);
		return false;
	}
	OutList.ProviderId = ProviderId;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// What Project Settings shows

FString FN2CModelDiscovery::GetSelectedModelId(EN2CLLMProvider Provider)
{
	FString ModelId = GetDefault<UN2CSettings>()->GetActiveModelForProvider(Provider).TrimStartAndEnd();
	if (ModelId.IsEmpty() && Provider == EN2CLLMProvider::OpenAICompatible)
	{
		FN2CProviderProfile Profile;
		if (UN2COpenAICompatibleService::ResolveProfile(Provider, FString(), Profile))
		{
			ModelId = Profile.DefaultModel;
		}
	}
	return ModelId;
}

FString FN2CModelDiscovery::GetSelectedModelWarning(EN2CLLMProvider Provider) const
{
	using namespace N2CModelDiscoveryPrivate;
	const FString ModelId = GetSelectedModelId(Provider);
	if (ModelId.IsEmpty())
	{
		return FString();
	}
	const FName Id = ProviderIdFor(Provider);
	const FString Label = ProviderLabel(Id);

	if (const FN2CModelMetadata* Row = GetDefault<UN2CSettings>()->GetActiveModelMetadataForProvider(Provider))
	{
		if (Row->ModelId == ModelId && Row->bIsDeprecated && !Row->bIsRetired)
		{
			return FString::Printf(TEXT("%s is deprecated: %s shuts it down on %s. Pick another model in the %s Model setting."),
				*ModelId, *Label, Row->ShutdownDate.IsEmpty() ? TEXT("a date not yet announced") : *Row->ShutdownDate, *Label);
		}
	}

	const FN2CModelList* List = GetListIfOptedIn(Id);
	if (!List || List->Models.Num() == 0)
	{
		return FString();
	}
	const FN2CDiscoveredModel* Listed = List->Find(ModelId);
	if (!Listed)
	{
		return FString::Printf(TEXT("%s is not in the model list %s returned for your key (fetched %s): requests to it may fail. Pick a listed model, or press Refresh Model List if the list is old."),
			*ModelId, *Label, *List->FetchedAt.ToString(TEXT("%Y-%m-%d")));
	}
	if (Listed->bDeprecated)
	{
		return Listed->ShutdownDate.IsEmpty()
			? FString::Printf(TEXT("%s marks %s as deprecated. Pick another model in the %s Model setting."), *Label, *ModelId, *Label)
			: FString::Printf(TEXT("%s marks %s as deprecated: it shuts down on %s. Pick another model in the %s Model setting."),
				*Label, *ModelId, *Listed->ShutdownDate, *Label);
	}
	return FString();
}

FString FN2CModelDiscovery::DescribeActiveStatus() const
{
	const FName Id = GetActiveProviderId();
	const FString IdText = Id.ToString();
	const EN2CModelDiscoverySkip Environment = GetEnvironmentSkip();
	if (Environment != EN2CModelDiscoverySkip::None)
	{
		return FString::Printf(TEXT("Off: %s."), *DescribeSkip(Environment, Id));
	}
	if (!IsOptedIn(Id))
	{
		return FString::Printf(TEXT("%s: off. Add %s to Live Model Lists to fetch the models your key can use."), *IdText, *IdText);
	}
	if (IsFetching(Id))
	{
		return FString::Printf(TEXT("%s: fetching..."), *IdText);
	}
	if (const FN2CModelList* List = GetList(Id))
	{
		const int32 Deprecated = List->Models.FilterByPredicate([](const FN2CDiscoveredModel& M) { return M.bDeprecated; }).Num();
		return FString::Printf(TEXT("%s: %d models (%d deprecated), fetched %s"), *IdText, List->Models.Num(), Deprecated,
			*List->FetchedAt.ToString(TEXT("%Y-%m-%d %H:%M UTC")));
	}
	return FString::Printf(TEXT("%s: no list yet. Press Refresh Model List."), *IdText);
}

// ---------------------------------------------------------------------------------------------------------------------
// Editor lifetime

void FN2CModelDiscovery::Startup()
{
	const EN2CModelDiscoverySkip Environment = GetEnvironmentSkip();
	if (Environment != EN2CModelDiscoverySkip::None)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Live model lists off: %s"), *DescribeSkip(Environment, NAME_None)),
			EN2CLogSeverity::Debug, N2CModelDiscoveryPrivate::LogContext);
		return;
	}
	// The last lists fetched, so an offline start still shows them
	for (const FString& Entry : GetDefault<UN2CSettings>()->LiveModelListProviders)
	{
		const FString Trimmed = Entry.TrimStartAndEnd();
		FN2CModelList Cached;
		const FName Id(*Trimmed);
		if (!Trimmed.IsEmpty() && LoadCache(Id, GetDefaultCacheDir(), Cached))
		{
			Lists.Add(Id, MoveTemp(Cached));
		}
	}
	if (Lists.Num() > 0)
	{
		OnListsChanged.Broadcast();
		GetMutableDefault<UN2CSettings>()->NotifyModelSettingsChanged();
	}
	// The active provider's list, once the editor has finished starting
	StartupTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		StartupTick.Reset();
		if (IsOptedIn(GetActiveProviderId()))
		{
			RequestActiveRefresh(TEXT("editor start"));
		}
		return false;
	}), 3.0f);
}

void FN2CModelDiscovery::Shutdown()
{
	if (StartupTick.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StartupTick);
		StartupTick.Reset();
	}
	TArray<FN2CLLMRequestHandleRef> Running;
	InFlight.GenerateValueArray(Running);
	InFlight.Reset();
	for (const FN2CLLMRequestHandleRef& Handle : Running)
	{
		Handle->Cancel();
	}
}
