# ==== EventGraph (EventGraph)
using { /Verse.org/Simulation }

bp_n2c_golden_timeline_multi_exec := class:

    # GoldenTimeline template: length 5 seconds, no tracks, no loop, no autoplay
    TimelineLength:float = 5.0
    var TimelinePosition:float = 0.0
    var TimelineForward:logic = true
    var TimelineRunning:logic = false

    # Custom event OnPlay -> Timeline.Play
    OnPlay():void =
        set TimelineForward = true
        StartTimeline()

    # Custom event OnReverse -> Timeline.Reverse
    OnReverse():void =
        set TimelineForward = false
        StartTimeline()

    # Starts the per-frame timeline loop unless it is already running
    StartTimeline():void =
        if (not TimelineRunning?):
            set TimelineRunning = true
            spawn{RunTimeline()}

    # Timeline playback: advances TimelinePosition every frame, fires Update each frame
    # and Finished when it reaches the end (forward) or the start (reverse)
    RunTimeline()<suspends>:void =
        var LastTime:float = GetSimulationElapsedTime()
        loop:
            Sleep(0.0)
            Now := GetSimulationElapsedTime()
            Delta := Now - LastTime
            set LastTime = Now
            if (TimelineForward?):
                set TimelinePosition = Min(TimelinePosition + Delta, TimelineLength)
            else:
                set TimelinePosition = Max(TimelinePosition - Delta, 0.0)
            OnTimelineUpdate()
            if:
                TimelineForward?
                TimelinePosition >= TimelineLength
            then:
                set TimelineRunning = false
                OnTimelineFinished()
                return
            if:
                not TimelineForward?
                TimelinePosition <= 0.0
            then:
                set TimelineRunning = false
                OnTimelineFinished()
                return

    # Timeline.Update -> CollectGarbage
    OnTimelineUpdate():void =
        CollectGarbage()

    # Timeline.Finished -> ControlScreensaver(bAllowScreenSaver = false)
    OnTimelineFinished():void =
        ControlScreensaver(false)

    # Placeholder: no Verse equivalent for KismetSystemLibrary::CollectGarbage
    CollectGarbage():void =
        # Engine garbage collection cannot be requested from Verse
        return

    # Placeholder: no Verse equivalent for KismetSystemLibrary::ControlScreensaver
    ControlScreensaver(AllowScreenSaver:logic):void =
        # Screensaver control is not available in Verse
        return


# notes: - Timeline GoldenTimeline: Verse has no Timeline node. It is rebuilt as a loop (RunTimeline) that advances TimelinePosition by the elapsed simulation time each frame (Sleep(0.0)), using the template length of 5.0 seconds. The timeline has no tracks, so only the Update and Finished exec outputs are translated. - Update runs every frame while playing and calls CollectGarbage. Finished runs once when the position reaches the end (Play) or 0 (Reverse) and calls ControlScreensaver(false). - Unwired timeline inputs (PlayFromStart, Stop, ReverseFromEnd, SetNewTime/NewTime) and the Direction output are not used by the graph. They are not translated. - Calling OnPlay or OnReverse while the timeline is running only changes its direction. Calling it when the position is already at that end finishes on the first frame. This approximates Blueprint timeline behaviour. - Placeholders (empty bodies, nothing to call in Verse): CollectGarbage(), ControlScreensaver(AllowScreenSaver:logic). - The custom events OnPlay and OnReverse are plain methods. Another class or device must call them. They also declare an OutputDelegate pin that is not used. - The Blueprint has no BeginPlay, so the class is not a creative_device. To place it in a UEFN level, make it a creative_device and call these methods from OnBegin or from subscribed events. - Verse code cannot run in Unreal Engine 5 projects today. UEFN compilation has not been verified.
