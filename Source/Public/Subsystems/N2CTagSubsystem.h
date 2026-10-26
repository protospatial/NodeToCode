// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "N2CTagSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnN2CTagAdded, const FN2CTagInfo&, TagInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnN2CTagRemoved, const FString&, GraphGuid, const FString&, Tag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnN2CTagsLoaded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnN2CTagsSaved);

/**
 * @class UN2CTagSubsystem
 * @brief Editor subsystem for Blueprint tag management events
 * 
 * This subsystem provides Blueprint-accessible delegates for tag events,
 * allowing UI elements to react to tag changes in real-time.
 */
UCLASS(BlueprintType)
class NODETOCODE_API UN2CTagSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/** Initialize the subsystem */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	/** Deinitialize the subsystem */
	virtual void Deinitialize() override;

	/** Static getter for easy Blueprint access */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get Tag Subsystem"))
	static UN2CTagSubsystem* Get();

	/** Called when a tag is added to a Blueprint graph */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Tags")
	FOnN2CTagAdded OnBlueprintTagAdded;

	/** Called when a tag is removed from a Blueprint graph */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Tags")
	FOnN2CTagRemoved OnBlueprintTagRemoved;

	/** Called when tags are loaded from disk */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Tags")
	FOnN2CTagsLoaded OnTagsLoaded;

	/** Called when tags are saved to disk */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Tags")
	FOnN2CTagsSaved OnTagsSaved;

	/**
	 * Get the currently focused Blueprint graph info
	 * @param bIsValid Whether a valid focused graph was found
	 * @param GraphGuid The GUID of the focused graph
	 * @param GraphName The name of the focused graph
	 * @param BlueprintPath The path to the owning Blueprint
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get Focused Graph Info"))
	void GetFocusedGraphInfo(
		bool& bIsValid,
		FString& GraphGuid,
		FString& GraphName,
		FString& BlueprintPath
	);

	/**
	 * Refresh tag data from the tag manager
	 * Useful for UI updates after external changes
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Refresh Tags"))
	void RefreshTags();

private:
	/** Handle tag added from C++ */
	void HandleTagAdded(const FN2CTaggedBlueprintGraph& TaggedGraph);
	
	/** Handle tag removed from C++ */
	void HandleTagRemoved(const struct FN2CGraphKey& Graph, const FString& Tag);
};