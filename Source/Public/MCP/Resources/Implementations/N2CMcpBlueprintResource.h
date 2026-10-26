// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Resources/N2CMcpResourceTypes.h"
#include "Async/Async.h"

class UBlueprint;

/**
 * MCP Resource that provides the currently focused Blueprint in N2CJSON format
 */
class NODETOCODE_API FN2CMcpCurrentBlueprintResource : public IN2CMcpResource
{
public:
	virtual FMcpResourceDefinition GetDefinition() const override;
	virtual FMcpResourceContents Read(const FString& Uri) override;
	virtual bool CanSubscribe() const override { return true; }
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * The contents Read returns for a focused Blueprint's N2C JSON: the JSON marked as untrusted data
	 * (FN2CMcpUntrustedContent::MarkJson, card 0124). Public for the NodeToCode.MCP.UntrustedContent spec.
	 */
	static FMcpResourceContents MakeBlueprintContents(const FString& Uri, const FString& BlueprintJson);

protected:
	/**
	 * Helper to execute code on the game thread
	 */
	template<typename ResultType>
	static ResultType ExecuteOnGameThread(TFunction<ResultType()> Function);
};

/**
 * MCP Resource that provides all open Blueprints
 */
class NODETOCODE_API FN2CMcpAllBlueprintsResource : public IN2CMcpResource
{
public:
	virtual FMcpResourceDefinition GetDefinition() const override;
	virtual FMcpResourceContents Read(const FString& Uri) override;
	virtual bool RequiresGameThread() const override { return true; }

	/** One entry of the list: {name, path, type, resourceUri} (resourceUri from FN2CMcpBlueprintByNameResource::UriFor) */
	static TSharedRef<FJsonObject> DescribeBlueprint(const UBlueprint& Blueprint);

protected:
	/**
	 * Helper to execute code on the game thread
	 */
	template<typename ResultType>
	static ResultType ExecuteOnGameThread(TFunction<ResultType()> Function);
};

/**
 * MCP resource template nodetocode://blueprint/{+path}: any Blueprint as N2C JSON (every graph, the document
 * get-blueprint-n2cjson returns) by its asset path without the leading slash, each segment percent-encoded (card
 * 0208). A single segment is the old form, the name of an open Blueprint. An unknown Blueprint is -32002, a failed
 * capture -32603.
 */
class NODETOCODE_API FN2CMcpBlueprintByNameResource : public IN2CMcpResource
{
public:
	virtual FMcpResourceDefinition GetDefinition() const override;
	virtual FMcpResourceContents Read(const FString& Uri) override;
	virtual bool RequiresGameThread() const override { return true; }
	
	/**
	 * Gets the resource template for this dynamic resource
	 */
	static FMcpResourceTemplate GetResourceTemplate();

	/** nodetocode://blueprint/{+path} */
	static const TCHAR* const UriTemplate;
	/** nodetocode://blueprint/ */
	static const TCHAR* const UriPrefix;

	/**
	 * The URI that reads this Blueprint: UriPrefix + its asset path without the leading '/' (the package name, or the
	 * object path when the Blueprint is not its package's same-named asset), each segment percent-encoded
	 */
	static FString UriFor(const UBlueprint& Blueprint);

	/** The open Blueprint (an asset editor is open on it) with this exact name, or one whose spaces the old URIs wrote as '_' */
	static UBlueprint* FindOpenBlueprintByName(const FString& Name);

protected:
	/**
	 * Helper to execute code on the game thread
	 */
	template<typename ResultType>
	static ResultType ExecuteOnGameThread(TFunction<ResultType()> Function);
};

// Template implementation
template<typename ResultType>
ResultType FN2CMcpCurrentBlueprintResource::ExecuteOnGameThread(TFunction<ResultType()> Function)
{
	if (IsInGameThread())
	{
		return Function();
	}
	
	TPromise<ResultType> Promise;
	TFuture<ResultType> Future = Promise.GetFuture();
	
	AsyncTask(ENamedThreads::GameThread, [Function = MoveTemp(Function), Promise = MoveTemp(Promise)]() mutable
	{
		Promise.SetValue(Function());
	});
	
	return Future.Get();
}

template<typename ResultType>
ResultType FN2CMcpAllBlueprintsResource::ExecuteOnGameThread(TFunction<ResultType()> Function)
{
	if (IsInGameThread())
	{
		return Function();
	}
	
	TPromise<ResultType> Promise;
	TFuture<ResultType> Future = Promise.GetFuture();
	
	AsyncTask(ENamedThreads::GameThread, [Function = MoveTemp(Function), Promise = MoveTemp(Promise)]() mutable
	{
		Promise.SetValue(Function());
	});
	
	return Future.Get();
}

template<typename ResultType>
ResultType FN2CMcpBlueprintByNameResource::ExecuteOnGameThread(TFunction<ResultType()> Function)
{
	if (IsInGameThread())
	{
		return Function();
	}
	
	TPromise<ResultType> Promise;
	TFuture<ResultType> Future = Promise.GetFuture();
	
	AsyncTask(ENamedThreads::GameThread, [Function = MoveTemp(Function), Promise = MoveTemp(Promise)]() mutable
	{
		Promise.SetValue(Function());
	});
	
	return Future.Get();
}