// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Resources/N2CMcpResourceTypes.h"
#include "Async/Async.h"

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

protected:
	/**
	 * Helper to execute code on the game thread
	 */
	template<typename ResultType>
	static ResultType ExecuteOnGameThread(TFunction<ResultType()> Function);
};

/**
 * MCP Resource template for accessing specific Blueprints by name
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