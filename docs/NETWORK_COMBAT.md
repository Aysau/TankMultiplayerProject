# Friday Lab 3: Authoritative network combat

Use Unreal Engine **5.7.4**. This milestone adds combat to the existing listen-server
movement/aim baseline. Tank health, death, respawn, score and match state remain deferred.
Binary content and the engine version are unchanged. Visual PIE verification is pending.

## Trace the contribution

`IA_Fire -> ATankPlayerController latch -> reliable ServerRequestFire -> server
acceptance -> UTankWeaponComponent::TryFire -> ATankProjectile ->
ATankDamageTestTarget -> replicated current totals`.

The controller consumes a fresh Started event once, after the same input frame
assembles mouse aim. A canceled/invalid local press is discarded without retry.
The existing held/neutral/flush latch and focus/control guards remain. Loss of
local control clears a press that has not been sent; it cannot recall a request
already transmitted. Release the focus-acquiring click before firing until that
capture behavior has been manually verified.

The request struct contains only uint32 RequestSequence and FVector AimWorldPoint.
The separate pawn argument identifies the shooter, not a victim. Neither muzzle,
direction, damage nor a claimed hit is supplied by the client. Fire numbering and
server history are independent of movement and survive controller input clearing,
possession changes and Reset. A new controller starts fresh. Wrap-safe ordering
accepts the first value, then unsigned distances from 1 through 2^31 - 1.

The owned controller's server acceptance verifies current pawn/controller/owner,
authority, same world and lifecycle. Wrong-pawn requests cannot advance history.
A valid owner's new ID is handled once even if aim/cooldown rejects the shot.
Reliable transport retransmission does not create another gameplay request.

The weapon executes immediately with current authoritative state. Finite aim
must be within **100000 cm (1000 m)** of the tank and have a usable horizontal
direction. The server derives shot yaw, then a fixed gameplay origin/muzzle:
root position plus **-25 cm** world height, then **110 cm** along shot forward.
No cosmetic transform or socket participates. Firing leaves CanonicalTurretWorldYaw
unchanged: continuous turret presentation still follows the movement/aim stream.
The immutable shot can therefore differ briefly from a delayed visible turret.

Cooldown is **0.30 seconds of server gameplay time**, including blocked launches.
Rejected requests are never queued until cooldown ends. Several reliably delivered
presses arriving together may produce only one accepted shot; there are no client
timestamps, historical aim, prediction or compensation.

Launch and flight use the same **10 cm** sphere sweep on WorldDynamic, with simple
collision, ignoring the shooter. Cover must block WorldDynamic. After cooldown
acceptance, obstruction receives at most one authoritative 25-damage attempt;
no projectile spawns beyond it. Non-damageable walls still consume the shot.

Projectiles are server-spawned replicated Actors, always relevant in this small
arena, with movement replication targeting **30 Hz**. The server alone travels at
**2000 cm/s**, sweeps, resolves one terminal impact, and expires after **3 seconds**
without damage. No gravity or bouncing. Client copies only display replicated
transforms; they do not run flight or a local expiry timer. Server destruction also
removes remote copies. Shooter EndPlay immediately removes active projectiles without
damage. A flight that ends before initial replication may never render remotely;
no cosmetic events are added to cover that limitation.

GameMode creates one native replicated target at **(0, 500, 100)** when none is
placed. Collision adjustment can change that location; a failed spawn logs a warning.
Placed targets suppress the fallback. Target HitCount and TotalDamage are server-owned
current state, with no health or destruction. A small world-space text readout is
rebuilt from those properties on each machine; late joiners get current totals.
Property updates may coalesce: a RepNotify is not one notification per hit.

## Build and automation

Close the Editor before building the Editor target. Adjust paths for your checkout:

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat' `
  NetworkTankArenaEditor Win64 Development `
  '-Project=C:/dev/tank-arena/network-tank-arena-unreal/NetworkTankArena.uproject' `
  -WaitMutex -NoHotReloadFromIDE

& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'C:/dev/tank-arena/network-tank-arena-unreal/NetworkTankArena.uproject' `
  /Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -nosound -NoLiveCoding `
  '-ExecCmds=Automation RunTests NetworkTankArena' `
  '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=C:/dev/tank-arena/network-tank-arena-unreal/Saved/Automation/NetworkCombat' `
  '-abslog=C:/dev/tank-arena/network-tank-arena-unreal/Saved/Logs/NetworkCombatAutomation.log'
```

Automation writes ignored reports/cache files. Existing math, input latch,
movement and combat geometry tests remain. NetworkCombat adds bounded aim,
independent/wrapped sequencing, RPC reliability metadata, identity/replay/cooldown
rejection, immutable aim, controller lifecycle, authority guards, shooter cleanup
and replicated target current-state/readout coverage. NetworkGuard and TargetFixture
now require server combat and client rejection. Transient worlds and synthetic roles
do **not** verify transport, focused physical input, Blueprint compilation, asset
presentation or packaged gameplay. Those remain manual.

## Automated verification record

On **2026-10-15**, the UE **5.7.4** Editor Win64 Development build succeeded,
including UHT with warnings as errors. The full suite passed **28/28** tests:
13 LocalCombat, 6 LocalTank, 4 Multiplayer and 5 NetworkCombat, with **0 test
warnings/errors** and Editor exit code **0**. Report:
`Saved/Automation/NetworkCombat/index.json`; log:
`Saved/Logs/NetworkCombatAutomation.log`. The same three pre-existing startup
`LogAutomationTest: Error: Condition failed` entries and ToolMenus warning recorded
in the local-combat baseline remain outside test results. The new fixture initializes
actors normally so destruction routes real EndPlay callbacks; the cleanup test
does not manufacture an EndPlay notification.

`git diff --check` passed. Content, configuration, engine association, dependencies
and ignore rules are unchanged. Visual PIE, Blueprint compilation, packaged transport
and physical input have not been verified by this contribution. Wait for maintainer
manual verification before the separate fresh read-only review.

## Blueprint and content check

1. Reopen in UE 5.7.4 after the full build. Compile BP_Tank,
   BP_TankPlayerController and BP_LocalTankGameMode; check Output Log.
2. Keep TestMap on native TankGameMode (World Settings override None or TankGameMode).
   Do not select BP_LocalTankGameMode. Preserve two clear PlayerStarts and no placed tanks.
3. Confirm IA_Fire is Digital (bool), with no action/mapping triggers or modifiers,
   and Trigger When Paused off. IMC_Tank has one Left Mouse Button fire mapping;
   BP_TankPlayerController assigns FireAction, IMC_Tank and the existing movement actions.
   No new Blueprint fire/network events are needed.
4. Preserve tank replication, simulated-proxy movement behavior, camera and visual
   composition. Weapon gameplay offsets stay -25/110 cm; inspect their visual alignment.
5. Verify the automatically spawned cube/readout is clear, readable and reachable
   in both player views. If necessary, place a native TankDamageTestTarget in clear
   space; it replaces the fallback. New cover must have simple collision blocking
   WorldDynamic. No plugin, input config or collision-channel change is required.

## Manual PIE acceptance

Use New Editor Window PIE, 2 players, Play As Listen Server, Run Under One Process,
Launch Separate Server disabled, Default Player Start. Begin with emulation disabled.
Use the PIE world selector and window titles to identify server/client copies.

1. Confirm two possessed tanks and one target in each world. Verify both owners'
   movement, independent aim and cameras, including remote-owner movement updates.
2. In each window, release the focus-acquiring click, then press fire once: one shot.
   Hold several seconds: no repeats. Release/press: one new request. Rapid clicks
   inside cooldown cannot schedule a later shot. Repeat while moving/turning.
3. Shoot through clear space so flight lasts several updates. Both players see the
   projectile. Impact or 3-second expiry removes it in both worlds. Stepped/delayed
   client presentation is expected; extremely short flights may not render remotely.
4. Hit the target once: both readouts reach 1/25. Continue to eight hits: 8/200 and
   the target remains. Check server/client Details counters as well as the readout.
5. Verify thin-wall and grazing-radius hits, shooter filtering, and another tank
   blocking flight. Tanks do not gain health or death behavior.
6. Fire with cover between origin and muzzle, including initial overlap and muzzle
   edge obstruction: no projectile beyond cover, cooldown consumed. A damageable
   obstruction gets one result; non-damageable walls still stop the shot.
7. Temporarily exaggerate hover/bank or visual turret/barrel transforms. Shot pose
   must remain rooted in gameplay state. Restore visual defaults afterward.
8. Repeat focus loss/regain, pause, flush, mapping cancellation, disable/re-enable
   and possession changes while held: no stale fresh request. For DisableInput,
   use a temporary hook/debugger to queue a local press and disable before dispatch;
   it must clear immediately. Test both this/null arguments and remove the hook.
9. Send client-owned shots into open space, then close/disconnect that client.
   Its tank and active projectiles disappear on the server without damage. Check
   a remaining client copy where available (3-player PIE or a separate client).
10. Join after prior target hits; the joining client receives existing totals,
    without replaying damage or restarting the fixture.
11. Repeat remote-owner fast aim-and-click under the Lab 2 Fixed latency, Jitter,
    Lossy and Combined profiles (Clients Only). Shot aim must not depend on arrival
    of the unreliable aim heartbeat; canonical turret presentation may lag it.
    No duplicate shots/damage, local collisions or client expiry are allowed.
12. Stop/restart at least three times, including while holding fire. Check logs for
    new errors/warnings, restore Clean conditions, and repeat movement/aim smoke checks.

Packaged localhost transport remains a separate manual acceptance task. Do not
claim visual PIE, disconnect transport or late-join replication from headless tests.
