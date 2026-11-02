# ==== Spawned (Function)
using { /Fortnite.com/Devices }
using { /Verse.org/Simulation }
using { /UnrealEngine.com/Temporary/Diagnostics }

bp_n2c_golden_spawned_variable_nodes := class:

    var Score:int = 0

    Spawned():void =
        # Spawned nodes
        var Counter:int = 0
        set Score = Counter
        set Counter = Score

# notes: The Blueprint has no BeginPlay, Tick or overlap events, so the class is a plain class and not a creative_device.  Score is a member variable because it is set, so it is declared as `var Score:int = 0`.  Counter is a function local. The JSON lists no "local_variables", so its default of 0 is taken from the pin default value.  The final `set Counter = Score` is never read afterwards. This matches the Blueprint, but the compiler may warn about it.  The Spawned function has no return value, so it returns void.  Its default effect is <transacts>, which allows setting the class variable.  No nodes were unused, unreachable or disabled.
