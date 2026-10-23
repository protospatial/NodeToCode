// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Utils/N2CLogger.h"

// FJsonRpcRequest Implementation

FJsonRpcRequest::FJsonRpcRequest(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("jsonrpc"), JsonRpc);
		JsonObject->TryGetStringField(TEXT("method"), Method);
		Params = JsonObject->TryGetField(TEXT("params"));
		Id = JsonObject->TryGetField(TEXT("id"));
	}
}

TSharedPtr<FJsonObject> FJsonRpcRequest::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("jsonrpc"), JsonRpc);
	JsonObject->SetStringField(TEXT("method"), Method);
	
	if (Params.IsValid())
	{
		JsonObject->SetField(TEXT("params"), Params);
	}
	
	if (Id.IsValid())
	{
		JsonObject->SetField(TEXT("id"), Id);
	}
	
	return JsonObject;
}

// FJsonRpcResponse Implementation

FJsonRpcResponse::FJsonRpcResponse(const TSharedPtr<FJsonValue>& InId, const TSharedPtr<FJsonValue>& InResult)
	: Id(InId), Result(InResult)
{
}

FJsonRpcResponse::FJsonRpcResponse(const TSharedPtr<FJsonValue>& InId, int32 ErrorCode, const FString& ErrorMessage, const TSharedPtr<FJsonValue>& ErrorData)
	: Id(InId)
{
	Error = FJsonRpcError(ErrorCode, ErrorMessage, ErrorData).ToJson();
}

FJsonRpcResponse::FJsonRpcResponse(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("jsonrpc"), JsonRpc);
		Id = JsonObject->TryGetField(TEXT("id"));
		Result = JsonObject->TryGetField(TEXT("result"));
		
		const TSharedPtr<FJsonObject>* ErrorObject;
		if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject))
		{
			Error = *ErrorObject;
		}
	}
}

TSharedPtr<FJsonObject> FJsonRpcResponse::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("jsonrpc"), JsonRpc);
	
	if (Id.IsValid())
	{
		JsonObject->SetField(TEXT("id"), Id);
	}
	else
	{
		JsonObject->SetField(TEXT("id"), MakeShareable(new FJsonValueNull));
	}
	
	if (Result.IsValid())
	{
		JsonObject->SetField(TEXT("result"), Result);
	}
	
	if (Error.IsValid())
	{
		JsonObject->SetObjectField(TEXT("error"), Error);
	}
	
	return JsonObject;
}

// FJsonRpcNotification Implementation

FJsonRpcNotification::FJsonRpcNotification(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("jsonrpc"), JsonRpc);
		JsonObject->TryGetStringField(TEXT("method"), Method);
		Params = JsonObject->TryGetField(TEXT("params"));
	}
}

TSharedPtr<FJsonObject> FJsonRpcNotification::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("jsonrpc"), JsonRpc);
	JsonObject->SetStringField(TEXT("method"), Method);
	
	if (Params.IsValid())
	{
		JsonObject->SetField(TEXT("params"), Params);
	}
	
	return JsonObject;
}

// FJsonRpcError Implementation

FJsonRpcError::FJsonRpcError(int32 InCode, const FString& InMessage, const TSharedPtr<FJsonValue>& InData)
	: Code(InCode), Message(InMessage), Data(InData)
{
}

FJsonRpcError::FJsonRpcError(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (JsonObject.IsValid())
	{
		double CodeValue;
		if (JsonObject->TryGetNumberField(TEXT("code"), CodeValue))
		{
			Code = static_cast<int32>(CodeValue);
		}
		
		JsonObject->TryGetStringField(TEXT("message"), Message);
		Data = JsonObject->TryGetField(TEXT("data"));
	}
}

TSharedPtr<FJsonObject> FJsonRpcError::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetNumberField(TEXT("code"), Code);
	JsonObject->SetStringField(TEXT("message"), Message);
	
	if (Data.IsValid())
	{
		JsonObject->SetField(TEXT("data"), Data);
	}
	
	return JsonObject;
}

// FJsonRpcUtils Implementation

bool FJsonRpcUtils::ParseRequest(const FString& JsonString, FJsonRpcRequest& OutRequest)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonValue> JsonValue;

	if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid() && JsonValue->Type == EJson::Object)
	{
		OutRequest = FJsonRpcRequest(JsonValue->AsObject());
		return true;
	}

	return false;
}

bool FJsonRpcUtils::ParseResponse(const FString& JsonString, FJsonRpcResponse& OutResponse)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonValue> JsonValue;

	if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid() && JsonValue->Type == EJson::Object)
	{
		OutResponse = FJsonRpcResponse(JsonValue->AsObject());
		return true;
	}

	return false;
}

bool FJsonRpcUtils::ParseNotification(const FString& JsonString, FJsonRpcNotification& OutNotification)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonValue> JsonValue;

	if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid() && JsonValue->Type == EJson::Object)
	{
		OutNotification = FJsonRpcNotification(JsonValue->AsObject());
		return true;
	}

	return false;
}

FString FJsonRpcUtils::SerializeRequest(const FJsonRpcRequest& Request)
{
	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Request.ToJson().ToSharedRef(), Writer);
	return OutputString;
}

FString FJsonRpcUtils::SerializeResponse(const FJsonRpcResponse& Response)
{
	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToJson().ToSharedRef(), Writer);
	return OutputString;
}

FString FJsonRpcUtils::SerializeNotification(const FJsonRpcNotification& Notification)
{
	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Notification.ToJson().ToSharedRef(), Writer);
	return OutputString;
}

FJsonRpcResponse FJsonRpcUtils::CreateErrorResponse(const TSharedPtr<FJsonValue>& Id, int32 ErrorCode, const FString& ErrorMessage, const TSharedPtr<FJsonValue>& ErrorData)
{
	return FJsonRpcResponse(Id, ErrorCode, ErrorMessage, ErrorData);
}

FJsonRpcResponse FJsonRpcUtils::CreateSuccessResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonValue>& Result)
{
	return FJsonRpcResponse(Id, Result);
}

bool FJsonRpcUtils::IsValidJsonRpcMessage(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	// Check for required "jsonrpc" field with value "2.0"
	FString JsonRpcVersion;
	if (!JsonObject->TryGetStringField(TEXT("jsonrpc"), JsonRpcVersion) || JsonRpcVersion != TEXT("2.0"))
	{
		return false;
	}

	return true;
}

FJsonRpcUtils::EJsonRpcMessageType FJsonRpcUtils::GetMessageType(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!IsValidJsonRpcMessage(JsonObject))
	{
		return EJsonRpcMessageType::Unknown;
	}

	// Check if it has a method field (request or notification)
	FString Method;
	if (JsonObject->TryGetStringField(TEXT("method"), Method))
	{
		// Check if it has an id field (request vs notification)
		if (JsonObject->HasField(TEXT("id")))
		{
			return EJsonRpcMessageType::Request;
		}
		else
		{
			return EJsonRpcMessageType::Notification;
		}
	}
	
	// Check if it has result or error field (response)
	if (JsonObject->HasField(TEXT("result")) || JsonObject->HasField(TEXT("error")))
	{
		return EJsonRpcMessageType::Response;
	}

	return EJsonRpcMessageType::Unknown;
}