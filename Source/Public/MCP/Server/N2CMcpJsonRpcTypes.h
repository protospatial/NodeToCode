// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * JSON-RPC 2.0 message types and structures for MCP communication.
 * Based on the JSON-RPC 2.0 specification and MCP protocol requirements.
 */

/**
 * JSON-RPC 2.0 Error Codes
 * Standard error codes as defined in the JSON-RPC 2.0 specification
 */
namespace JsonRpcErrorCodes
{
	constexpr int32 ParseError = -32700;		// Invalid JSON was received by the server
	constexpr int32 InvalidRequest = -32600;	// The JSON sent is not a valid Request object
	constexpr int32 MethodNotFound = -32601;	// The method does not exist / is not available
	constexpr int32 InvalidParams = -32602;		// Invalid method parameter(s)
	constexpr int32 InternalError = -32603;		// Internal JSON-RPC error
	// Server error range: -32000 to -32099 (reserved for implementation-defined server-errors)

	/** MCP 2025-11-25, server/resources, error handling: "Resource not found: -32002", with the uri in data */
	constexpr int32 ResourceNotFound = -32002;
}

/**
 * Represents a JSON-RPC 2.0 request message.
 */
struct NODETOCODE_API FJsonRpcRequest
{
	/** JSON-RPC version (always "2.0") */
	FString JsonRpc = TEXT("2.0");

	/** A String containing the name of the method to be invoked */
	FString Method;

	/** A Structured value that holds the parameter values to be used during the invocation of the method (optional) */
	TSharedPtr<FJsonValue> Params;

	/** An identifier established by the Client (can be String, Number, or null) */
	TSharedPtr<FJsonValue> Id;

	/** Default constructor */
	FJsonRpcRequest() = default;

	/** Constructor from JSON object */
	explicit FJsonRpcRequest(const TSharedPtr<FJsonObject>& JsonObject);

	/** Convert to JSON object */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Check if this is a notification (no Id) */
	bool IsNotification() const { return !Id.IsValid() || Id->IsNull(); }
};

/**
 * Represents a JSON-RPC 2.0 response message (success or error).
 */
struct NODETOCODE_API FJsonRpcResponse
{
	/** JSON-RPC version (always "2.0") */
	FString JsonRpc = TEXT("2.0");

	/** This member is REQUIRED. It MUST be the same as the value of the id member in the Request Object */
	TSharedPtr<FJsonValue> Id;

	/** This member is REQUIRED on success. This member MUST NOT exist if there was an error */
	TSharedPtr<FJsonValue> Result;

	/** This member is REQUIRED on error. This member MUST NOT exist if there was no error */
	TSharedPtr<FJsonObject> Error;

	/** Default constructor */
	FJsonRpcResponse() = default;

	/** Constructor for success response */
	FJsonRpcResponse(const TSharedPtr<FJsonValue>& InId, const TSharedPtr<FJsonValue>& InResult);

	/** Constructor for error response */
	FJsonRpcResponse(const TSharedPtr<FJsonValue>& InId, int32 ErrorCode, const FString& ErrorMessage, const TSharedPtr<FJsonValue>& ErrorData = nullptr);

	/** Constructor from JSON object */
	explicit FJsonRpcResponse(const TSharedPtr<FJsonObject>& JsonObject);

	/** Convert to JSON object */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Check if this is an error response */
	bool IsError() const { return Error.IsValid(); }

	/** Check if this is a success response */
	bool IsSuccess() const { return Result.IsValid(); }
};

/**
 * Represents a JSON-RPC 2.0 notification message.
 * Notifications are requests without an Id - no response is expected.
 */
struct NODETOCODE_API FJsonRpcNotification
{
	/** JSON-RPC version (always "2.0") */
	FString JsonRpc = TEXT("2.0");

	/** A String containing the name of the method to be invoked */
	FString Method;

	/** A Structured value that holds the parameter values to be used during the invocation of the method (optional) */
	TSharedPtr<FJsonValue> Params;

	/** Default constructor */
	FJsonRpcNotification() = default;

	/** Constructor from JSON object */
	explicit FJsonRpcNotification(const TSharedPtr<FJsonObject>& JsonObject);

	/** Convert to JSON object */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Represents a JSON-RPC 2.0 error object.
 */
struct NODETOCODE_API FJsonRpcError
{
	/** A Number that indicates the error type that occurred */
	int32 Code;

	/** A String providing a short description of the error */
	FString Message;

	/** A Primitive or Structured value that contains additional information about the error (optional) */
	TSharedPtr<FJsonValue> Data;

	/** Default constructor */
	FJsonRpcError() = default;

	/** Constructor */
	FJsonRpcError(int32 InCode, const FString& InMessage, const TSharedPtr<FJsonValue>& InData = nullptr);

	/** Constructor from JSON object */
	explicit FJsonRpcError(const TSharedPtr<FJsonObject>& JsonObject);

	/** Convert to JSON object */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Utility class for working with JSON-RPC messages.
 */
class NODETOCODE_API FJsonRpcUtils
{
public:
	/** Parse a JSON string into a JSON-RPC request */
	static bool ParseRequest(const FString& JsonString, FJsonRpcRequest& OutRequest);

	/** Parse a JSON string into a JSON-RPC response */
	static bool ParseResponse(const FString& JsonString, FJsonRpcResponse& OutResponse);

	/** Parse a JSON string into a JSON-RPC notification */
	static bool ParseNotification(const FString& JsonString, FJsonRpcNotification& OutNotification);

	/** Serialize a JSON-RPC request to string */
	static FString SerializeRequest(const FJsonRpcRequest& Request);

	/** Serialize a JSON-RPC response to string */
	static FString SerializeResponse(const FJsonRpcResponse& Response);

	/** Serialize a JSON-RPC notification to string */
	static FString SerializeNotification(const FJsonRpcNotification& Notification);

	/** Create a standard error response */
	static FJsonRpcResponse CreateErrorResponse(const TSharedPtr<FJsonValue>& Id, int32 ErrorCode, const FString& ErrorMessage, const TSharedPtr<FJsonValue>& ErrorData = nullptr);

	/** Create a success response */
	static FJsonRpcResponse CreateSuccessResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonValue>& Result);

	/** Detect if a JSON object is a valid JSON-RPC message */
	static bool IsValidJsonRpcMessage(const TSharedPtr<FJsonObject>& JsonObject);

	/** Get the type of JSON-RPC message (request, response, notification) */
	enum class EJsonRpcMessageType
	{
		Unknown,
		Request,
		Response,
		Notification
	};

	static EJsonRpcMessageType GetMessageType(const TSharedPtr<FJsonObject>& JsonObject);
};