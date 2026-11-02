# ==== EventGraph (EventGraph)
bp_n2c_golden_gate_multi_exec := class:

    # State of the Gate macro (bStartClosed = true)
    var IsGateClosed:logic = true

    # Custom event OnEnter -> Gate.Enter: passes through to Exit only while the gate is open
    OnEnter():void =
        if (IsGateClosed = false):
            # Gate.Exit -> CollectGarbage
            CollectGarbage()

    # Custom event OnOpen -> Gate.Open
    OnOpen():void =
        set IsGateClosed = false

    # Custom event OnClose -> Gate.Close
    OnClose():void =
        set IsGateClosed = true

    # Placeholder: Verse has no garbage collection call
    CollectGarbage():void =
        # Intentionally empty: no Verse equivalent of KismetSystemLibrary::CollectGarbage
        return

# notes: Gate macro: inlined as the logic member IsGateClosed, starting true because bStartClosed is true. Enter runs the Exit chain only when the gate is open; Open and Close set the state. The Gate's Toggle input is not wired, so there is no toggle method.  Placeholder: CollectGarbage() is KismetSystemLibrary::CollectGarbage, which has no Verse equivalent. It is declared once with an empty body.  The three custom events (OnEnter, OnOpen, OnClose) are plain methods. Nothing in the graph calls them, so other code or a subscription has to. The Blueprint has no BeginPlay, Tick or overlap events, so the class is not a creative_device and needs no using lines. If you make it a creative_device, add the using lines and call these methods from OnBegin or from device events.  The OutputDelegate pins on the custom events are not wired and are ignored.
