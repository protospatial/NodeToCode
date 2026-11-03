// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CCapture.h"

class UMaterial;
class UMaterialExpression;
class UMaterialExpressionComment;
class UMaterialFunction;
struct FExpressionInput;

/**
 * Material capture (card 0449, epic 0442, decision 0009): turns a saved UMaterial's or UMaterialFunction's expressions
 * into the N2C model, read from the asset itself, never from a Material Editor's UMaterialGraph (a transient copy
 * whose GUIDs change on every rebuild). One context per capture; game thread only.
 *
 * - IDs: expressions are N1, N2, ... in GetExpressions order, then the material's output node; pins P1, P2, ... node
 *   by node, inputs before outputs, unique across the capture. FN2CCapture::NodeGuids maps each ID to the
 *   expression's MaterialExpressionGuid; a duplicate or missing GUID gets one derived from it and the expression's
 *   position (FGuid::NewDeterministicGuid) and a DUPLICATE_EXPRESSION_GUID warning.
 * - Pins: an input is named by its FExpressionInput property ("A", "Coordinates", an array's "Inputs[2]"), a function
 *   call's input by the function input's name; GetInputName and GetCaption text is never an identifier. An output is
 *   named by its OutputName, else by the channels its mask selects ("RGB", "R"), else "Output". The output node has
 *   one input per connected material property, named by FMaterialAttributeDefinitionMap::GetAttributeName.
 * - Flows: each input's source is followed through reroutes, named reroutes and composites with their pin bases (card
 *   0450; none of them gets a node: a composite only organises the nodes inside it, which are in the expression list).
 * - Material attributes (card 0450): Set Material Attributes' inputs and Get Material Attributes' outputs are named by
 *   their attribute (FMaterialAttributeDefinitionMap::GetAttributeName of the attribute GUID), "MaterialAttributes" for
 *   the attributes pin itself; Make and Break Material Attributes already name their pins by attribute.
 * - Liveness: the output node, FunctionOutput and custom outputs are boundaries; an expression none of them reads,
 *   directly or through others, is "unused". Both inputs of a static switch are read.
 * - Function calls: a call to a UMaterialFunction (exact class) is followed when its depth (calls from the captured
 *   asset) is within Options.Depth, as Blueprint function calls are, so /Engine/Functions are described by name at
 *   depth 0 and expanded beyond it (decision 0009 item 4); each called function is captured once.
 *
 * - Text (card 0451): a Custom node's Code, defines and include paths, descriptions, parameter groups and comment text
 *   are captured as they are, as data: the whole document is untrusted (an agent tool marks it, card 0124), and an
 *   include file is never opened. A Custom node's inputs are named by the names its code uses.
 * - Fallbacks (card 0451): an input the class supplies outside its properties is named Input<i> with an UNMAPPED_PIN
 *   warning; a deprecated or material-layer expression is kept as a node, its settings unread (so nothing it references
 *   is followed), with an UNSUPPORTED_EXPRESSION warning. An unknown class never fails a capture.
 * - Object references (textures, collections, virtual textures) in properties are soft object paths.
 * - metadata.material.substrate is the project's r.Substrate.
 *
 * Engine guards (Context/translation-pipeline/material-classes.md): nothing here calls GetMaterialResource, the
 * Material Editor's graph or editing library, or reads EMaterialValueType bits, so no ENGINE_MINOR_VERSION guard is
 * needed on UE 5.6-5.8.
 */
/** How material capture handles an expression class (card 0452; the "Handling" column of material-classes.md) */
enum class EN2CMaterialClassHandling : uint8
{
	/** A hand mapping in the capture (a mapped root or ancestor, or dynamic inputs) */
	Mapped,
	/** The reflection fallback: inputs by property, UNMAPPED_PIN for any other */
	Generic,
	/** Deprecated or part of the material layer system: kept without its settings, UNSUPPORTED_EXPRESSION */
	Refused,
	/** Abstract, never placed in a graph */
	Base,
};

class FN2CMaterialCaptureContext
{
public:
	/** The class's handling under the in-code table (the rules gen_material_classes.py applies to the engine sources) */
	static EN2CMaterialClassHandling ClassHandling(const UClass* Class);

	/** True for an expression the capture follows through instead of capturing: reroutes, composites, comments */
	static bool IsTracedThrough(const UMaterialExpression& Expression);

	explicit FN2CMaterialCaptureContext(const FN2CCaptureOptions& InOptions);

	/** Captures a UMaterial (the caller checked the exact class) */
	void CaptureMaterial(UMaterial& Material);

	/** Captures a UMaterialFunction as the document's asset (the caller checked the exact class) */
	void CaptureFunction(UMaterialFunction& Function);

	FN2CCapture TakeCapture();

private:
	/** The node and output pins of one captured expression */
	struct FCapturedExpression
	{
		int32 NodeIndex = INDEX_NONE;
		TArray<FString> OutputPinIDs;
	};

	/** One graph: Expressions in order, Comments, the material whose properties make the output node (null for a function) */
	void CaptureGraph(const FString& Name, EN2CGraphType GraphType, TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions,
		TConstArrayView<TObjectPtr<UMaterialExpressionComment>> Comments, UMaterial* OutputMaterial, int32 Depth);

	void DescribeExpression(UMaterialExpression& Expression, FN2CNodeDefinition& Node, int32 Depth);
	void CaptureProperties(const UMaterialExpression& Expression, FN2CNodeDefinition& Node) const;
	/** The expression and output index Input reads, followed through reroutes; null when unwired */
	static UMaterialExpression* TraceSource(const FExpressionInput& Input, int32& OutOutputIndex);
	FGuid UniqueGuid(const UMaterialExpression& Expression, int32 Ordinal, const FString& Path);
	void AddWarning(const TCHAR* Code, const FString& Path, const FString& Message);
	FString NewPinID() { return FString::Printf(TEXT("P%d"), NextPinNumber++); }

	FN2CCaptureOptions Options;
	FN2CCapture Capture;
	int32 NextNodeNumber = 1;
	int32 NextPinNumber = 1;
	TSet<FGuid> UsedGuids;
	/** Functions captured or queued, so each is captured once */
	TSet<const UMaterialFunction*> SeenFunctions;
	/** Functions to capture: the function and the depth of its graph */
	TArray<TPair<UMaterialFunction*, int32>> PendingFunctions;
	/** Functions used but beyond the depth limit */
	TArray<const UMaterialFunction*> DepthLimitedFunctions;
};
