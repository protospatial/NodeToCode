// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"

/**
 * When a pin connection made for an agent (the connect-pins MCP tool, the Python bridge's ConnectPins) may go ahead.
 * Both callers ask the schema with CanCreateConnection and then call TryCreateConnection, which breaks exactly what
 * the response says. Any connection that would cut an existing link is refused unless the caller passed
 * breakExistingLinks, and the error names the link that would be lost.
 */
namespace N2CPinConnectPolicy
{
	/** Responses where the schema itself breaks links (replace) before connecting */
	inline bool BreaksOthers(ECanCreateConnectionResponse R)
	{
		return R == CONNECT_RESPONSE_BREAK_OTHERS_A || R == CONNECT_RESPONSE_BREAK_OTHERS_B || R == CONNECT_RESPONSE_BREAK_OTHERS_AB;
	}

	/**
	 * The pin whose existing links the schema would cut to make this connection, or nullptr when nothing is cut.
	 * Exec inputs take many incoming links, so fan-in never cuts anything. A conversion-node response wires the new
	 * conversion node into the input pin, which replaces whatever already feeds a data input (card 0415).
	 */
	inline const UEdGraphPin* PinLosingLinks(const FPinConnectionResponse& Response, const UEdGraphPin* FromPin, const UEdGraphPin* ToPin)
	{
		switch (Response.Response)
		{
		case CONNECT_RESPONSE_BREAK_OTHERS_A:
			return FromPin;
		case CONNECT_RESPONSE_BREAK_OTHERS_B:
			return ToPin;
		case CONNECT_RESPONSE_BREAK_OTHERS_AB:
			return (ToPin && ToPin->LinkedTo.Num() > 0) || !FromPin ? ToPin : FromPin;
		case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
		{
			const UEdGraphPin* InputPin = (FromPin && FromPin->Direction == EGPD_Input) ? FromPin : ToPin;
			const bool bDataInput = InputPin && InputPin->Direction == EGPD_Input && InputPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec;
			return (bDataInput && InputPin->LinkedTo.Num() > 0) ? InputPin : nullptr;
		}
		default:
			return nullptr;
		}
	}

	/** Whether making this connection would cut an existing link */
	inline bool WouldReplaceExistingLink(const FPinConnectionResponse& Response, const UEdGraphPin* FromPin, const UEdGraphPin* ToPin)
	{
		return BreaksOthers(Response.Response) || PinLosingLinks(Response, FromPin, ToPin) != nullptr;
	}

	/**
	 * Whether to go ahead with a connection, given the schema's answer. A connection that would cut an existing link
	 * (a replace, or a conversion node into an already linked data input) only goes ahead with bBreakExistingLinks.
	 */
	inline bool Allow(const FPinConnectionResponse& Response, const UEdGraphPin* FromPin, const UEdGraphPin* ToPin, bool bBreakExistingLinks, FString& OutError)
	{
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			OutError = Response.Message.ToString();
			return false;
		}
		if (WouldReplaceExistingLink(Response, FromPin, ToPin) && !bBreakExistingLinks)
		{
			const UEdGraphPin* Busy = PinLosingLinks(Response, FromPin, ToPin);
			FString Existing;
			if (Busy && Busy->LinkedTo.Num() > 0 && Busy->LinkedTo[0] && Busy->LinkedTo[0]->GetOwningNode())
			{
				Existing = FString::Printf(TEXT(" (currently linked to %s.%s)"),
					*Busy->LinkedTo[0]->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					*Busy->LinkedTo[0]->PinName.ToString());
			}
			const TCHAR* Via = Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE ? TEXT(" through a conversion node") : TEXT("");
			OutError = FString::Printf(TEXT("Connecting%s would replace the existing link on %s%s. Pass breakExistingLinks=true to replace it."),
				Via, Busy ? *Busy->PinName.ToString() : TEXT("the pin"), *Existing);
			return false;
		}
		return true;
	}
}
