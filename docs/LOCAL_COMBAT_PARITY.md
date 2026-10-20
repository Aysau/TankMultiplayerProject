# Local projectile-combat foundation (historical, before Friday Lab 3)

This document records the standalone foundation and its earlier verification.
The current implementation evolves it into [authoritative network combat](NETWORK_COMBAT.md).
Standalone-only guards, the queued weapon API and non-replication assertions below
describe that earlier foundation, not the current branch. Use NETWORK_COMBAT.md
for current behavior, automation and acceptance checks.

Use **Unreal Engine 5.7.4**. This contribution adds local combat to the completed
Lab 2 movement/aim baseline. It does not implement authoritative network combat.
The earlier game specification describes later milestones as well; this milestone
has no tank health, multiplayer damage, death, respawn, score or match state.

## Run the local baseline

1. Close the Editor, build `NetworkTankArenaEditor Win64 Development`, then open
   `NetworkTankArena.uproject` in UE 5.7.4 and load `/Game/Maps/TestMap`.
2. Confirm Maps & Modes uses native **TankGameMode**, or World Settings has no
   override. Preserve BP_Tank and BP_TankPlayerController. The Enhanced Input
   assets below are already configured; compile these Blueprints after C++ changes.
   No socket or Blueprint gameplay/input graph is needed.
3. Set **Number of Players = 1**, **Play Net Mode = Play Standalone**, and use
   **New Editor Window (PIE)** / **Default Player Start**. Disable network emulation.
4. Native TankGameMode creates one **TankDamageTestTarget**, a visible 100 cm cube
   at `(0, 500, 100)` when no target is placed. It exists for this play session,
   survives repeated hits, and is recreated with zero counters on the next session.
   A placed native TankDamageTestTarget replaces the automatic fixture. Check the
   default fixture has clear space in TestMap; its spawn adjusts for collision and
   can fail if no clear location exists. It is not created during multiplayer play.
5. Click into the window and release the left mouse button. **W/S** move, **A/D**
   turn, mouse aims, **left mouse** fires. Initial control acquisition, focus loss,
   input flush, controller input disabling, unpossession and controller reset block
   stale fire requests.
   A held action cannot fire merely because control returns; an observed release
   or trustworthy neutral action value permits the next fresh press. Release the
   focus-acquiring click before firing; that specific capture behavior has not been
   verified.
6. Aim at the cube. Output Log reports `Local damage target ...: hits=N total=D`.
   In the active PIE world, select the target to inspect **Hit Count** and
   **Total Damage** under Local Damage. Each hit adds one and 25 respectively.

Combat is deliberately disabled in listen-server, client and dedicated-server
worlds. Use the two-player Lab 2 setup to verify movement/aim regression only.
There is no Server fire RPC, replicated projectile or replicated damage.

## Configured Enhanced Input assets (UE 5.7.4)

This contribution includes `/Game/Input/IA_Fire`, adds its Left Mouse Button
mapping to `/Game/Input/IMC_Tank`, and assigns Fire Action on
`/Game/Blueprints/BP_TankPlayerController`. The maintainer has completed the asset
wiring and manually verified local firing in UE 5.7.4. Enhanced Input is the final
fire-input route. The existing throttle/turn mappings are preserved.
The following settings describe the supplied content for inspection:

1. Open **IA_Fire** (`/Game/Input/IA_Fire`). **Value Type = Digital (bool)**,
   **Triggers** and **Modifiers** are empty, and **Trigger When Paused** is off.
   The ordinary Boolean down/neutral action is intentional; do not add Pressed,
   Hold, Pulse, Released or repeat triggers.
2. **IMC_Tank** (`/Game/Input/IMC_Tank`) contains one mapping for **IA_Fire**
   to **Left Mouse Button**, with empty mapping triggers/modifiers.
   Preserve all IA_Throttle / IA_Turn mappings and their existing modifiers.
3. Open **BP_TankPlayerController** (`/Game/Blueprints/BP_TankPlayerController`).
   **Class Defaults > Tank > Input** has **Fire Action = IA_Fire** assigned.
   Preserve **Throttle Action = IA_Throttle**, **Turn Action = IA_Turn**, and
   **Tank Mapping Context = IMC_Tank**. Add no Blueprint fire/input events.
4. After C++ changes, compile BP_TankPlayerController, BP_Tank and
   BP_LocalTankGameMode as part of the usual content check. Keep the
   native TankGameMode selected for TestMap. No new mapping context, plugin,
   project input settings or Level Blueprint changes are required.

## Gameplay contract and ownership

- Fire follows `IA_Fire -> IMC_Tank -> ATankPlayerController` Enhanced Input
  bindings. **Started** attempts one discrete request through `FFireRequestLatch`
  to `UTankWeaponComponent`; **Completed** observes release; **Canceled** blocks
  fire and cancels pending requests without treating cancellation as release.
  No raw InputKey override, hard-coded mouse key or raw mouse-event fire path remains.
- `PostProcessInput` observes the processed Boolean FireAction value after
  Enhanced Input delegates run. A held value never rearms or fires. A valid
  neutral value can rearm after control returns, even when there was no Completed
  callback in the focused window. Missing/canceled action instances do not count
  as release. The existing actor Tick guard still covers pause/control loss when
  Unreal skips PlayerTick; focus, possession/reset and input flush clear pending fire.
- `HasLocalFireControl` also requires the controller's `InputEnabled()` state.
  `DisableInput(this)` and `DisableInput(nullptr)` retain Unreal's normal input
  behavior and immediately interrupt local fire, canceling a queued request before
  the pawn can consume it. Re-enabling input does not rearm held fire; an observed
  genuine release/neutral permits the next fresh press. This does not clear movement
  caches or change the movement intent/RPC path.
- UE 5.7.4 can generate synthetic completion/zero values during a key flush.
  The controller ignores fire events and polling during that input-processing
  pass. The latch distrusts neutral values after a flush/cancellation until a
  subsequent actuation has been observed. This suppresses a resumed held Started;
  its eventual release rearms the next fresh press. If Enhanced Input has lost
  the held state entirely, a suppressed press/release cycle may be needed to
  establish genuine release. Zero values created by a flush alone never rearm.
  Input observation never queues or retries a rejected shot.
- `FTankControlIntent` continues to contain movement/aim only. A separate local
  weapon request is consumed after that frame's pawn movement and canonical aim.
  An early cooldown request is discarded, never buffered or retried while held.
- `UTankWeaponComponent` owns a **0.30 s** cooldown. The pure cooldown accepts an
  explicit gameplay time and is ready for a later authority boundary; only local
  standalone invokes it here. Cooldown uses world gameplay time (pauses/time
  dilation follow Unreal gameplay time). Blocked launches consume the cooldown.
- The firing pose takes only gameplay/root **world position**, canonical turret
  **world yaw**, a fixed world-height offset and a fixed forward distance. Native
  defaults are **-25 cm** turret height relative to the capsule center and
  **110 cm** muzzle distance. The height matches native VisualRoot `-65` plus
  turret pivot `40`; these are fixed gameplay numbers, never runtime reads from
  those components. The muzzle distance is a placeholder safe tuning choice.
  Blueprint may tune the weapon offsets to agree with the composed tank, without
  making gameplay read presentation transforms. No teacher-specified cross-engine
  offset distances were supplied; exact visual alignment remains an Editor check.
- Bob, bank, visual turret/barrel transforms, cosmetic sockets, mesh scale and
  chassis rotation are absent from the firing-pose calculation. Canonical yaw
  already includes the gameplay aim decision.
- `ATankProjectile` travels straight at **2000 cm/s**, with **10 cm** radius,
  **3 s** lifetime, **25** point damage, no gravity and no bounce. Each step sweeps
  its complete traveled segment and clamps its final step to remaining lifetime.
  It resolves at most one terminal collision before destruction. Expiry causes
  no damage. Visuals are an engine sphere and cube with collision disabled.
- Launch and flight share the same radius sweep. The launch sweep covers turret
  origin to muzzle, including initial overlap and muzzle clearance. A blocker
  receives the shot's one damage attempt and no projectile appears beyond it.
  A non-damageable wall still consumes the shot.
- Sweeps query simple collision using the **WorldDynamic** collision response,
  ignoring the firing tank and projectile. Arena blockers and the tank capsule
  block that channel. Unlike Visibility, it detects the existing Pawn profile.
  Overlap-only triggers are not terminal blockers. Projectile visuals/root do not
  obstruct tank movement. No collision channel or project configuration changes
  are required. New cover must block WorldDynamic and have usable simple collision.
- `ATankDamageTestTarget::TakeDamage` records accepted positive finite local
  damage with persistent counters. It has no health/death system. Tank health and
  multiplayer damage are deferred to the authoritative-combat lab.

## Automation

After building, run the focused local-combat and multiplayer regression suite
(writes only ignored reports/cache files):

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'C:/dev/tank-arena/network-tank-arena-unreal/NetworkTankArena.uproject' `
  /Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -nosound -NoLiveCoding `
  '-ExecCmds=Automation RunTests NetworkTankArena.LocalCombat+NetworkTankArena.Multiplayer' `
  '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=C:/dev/tank-arena/network-tank-arena-unreal/Saved/Automation/DisableInputLifecycle' `
  '-abslog=C:/dev/tank-arena/network-tank-arena-unreal/Saved/Logs/DisableInputLifecycleAutomation.log'
```

Use your own checkout path. To run all project tests, use `Automation RunTests
NetworkTankArena` instead. LocalCombat automation covers pure latch/cooldown/pose
logic and real transient Unreal actors: presentation independence, lifecycle
cancellation (including the pause actor-tick path and immediate DisableInput
cancellation), swept thin-wall and grazing hits, self filtering, initial overlap,
blocked-launch cooldown, same-frame aim, single terminal hit, travel/expiry and target
accounting. ContentDefaults loads the existing tank/controller Blueprints
and checks inherited native weapon defaults. TargetFixture verifies default and
placed target setup; NetworkGuard checks listen/client/dedicated-world rejection
without simulating transport. The existing LocalTank/Multiplayer
tests remain part of the run. These are not visual PIE, physical input, Blueprint
compile, transport or packaged-build acceptance tests.

The input refactor also adds pure held/neutral/flush/cancellation latch coverage
and a transient Enhanced Input binding test. The latter injects Boolean action
values through real `UEnhancedPlayerInput`, checks Started/Completed/Canceled
routing, and does not require IA_Fire or Blueprint wiring. Headless tests have
no focused viewport and do not establish physical mouse/capture behavior.
FireDisableInput uses the existing transient actor fixture to queue a valid request,
disable controller input before consumption, and verify no projectile appears.
It tests both the self and null controller arguments, held suppression after re-enable,
neutral/release recovery, exactly one subsequent shot, and preservation of movement
input caches. It exercises the latch/weapon boundary directly because the headless
fixture has no focused viewport.

Baseline verification before the input refactor, **2026-10-14**: UE **5.7.4**, CL **51494982**,
`NetworkTankArenaEditor Win64 Development` build succeeded, including UHT with
warnings as errors. All **20** NetworkTankArena automation tests passed (**10**
existing and **10** LocalCombat), with **0 test warnings/errors**; Editor exit
code **0**. Report: `Saved/Automation/LocalCombat/index.json`; log:
`Saved/Logs/LocalCombatAutomation.log`. The log retains the same three startup
`LogAutomationTest: Error: Condition failed` entries and one ToolMenus warning
observed in the earlier Lab 2 baseline log, outside the automation test results.
That headless run did not verify Blueprint compilation, visual PIE or packaged
behavior.
The fresh read-only review found a pause interruption gap; the targeted actor-tick
fix and test-fixture cleanup were rebuilt and verified in the final full run.

Enhanced Input refactor verification, **2026-10-14**: the UE **5.7.4** Editor
Win64 Development build succeeded, including UHT with warnings as errors and no
new compiler warnings. The focused suite at that time passed all **16** tests
(**12** LocalCombat and **4** Multiplayer), with **0 test warnings/errors** and
Editor exit code **0**. Report: `Saved/Automation/EnhancedFire/index.json`; log:
`Saved/Logs/EnhancedFireAutomation.log`. The same pre-existing startup diagnostics
remain outside test results. The fire assets were subsequently configured and
local gameplay, including the Enhanced Input fire route, was manually verified
by the maintainer. That report predates the DisableInput lifecycle follow-up;
focus-acquiring click behavior was not specifically checked. Packaged behavior
is not established by these checks.

DisableInput lifecycle verification, **2026-10-15**: UE **5.7.4**, CL **51494982**,
`NetworkTankArenaEditor Win64 Development` build succeeded, including UHT with
warnings as errors. All **17** focused tests passed (**13** LocalCombat and **4**
Multiplayer), including FireDisableInput; Editor exit code **0**. Report:
`Saved/Automation/DisableInputLifecycle/index.json`; log:
`Saved/Logs/DisableInputLifecycleAutomation.log`. The same three pre-existing
startup condition errors and ToolMenus warning remain outside the test results.
Physical input and the new disable/re-enable path still require the targeted
manual repeat below.

## Manual UE 5.7.4 acceptance checklist

The maintainer reports successful local manual verification before the DisableInput
fix. After this fix, repeat Blueprint compilation, standalone press/hold/release,
input interruption/recovery and the two-player movement/no-combat smoke check below.
Using a temporary test hook or debugger, queue fire and call controller
`DisableInput(this)` before the pawn processes it: no shot may occur. Re-enable
while still held: no shot. Release, then press again: exactly one shot. Repeat with
`DisableInput(nullptr)`, then remove any temporary test hook.

1. Open the Editor after a full build. Compile BP_Tank, BP_TankPlayerController
   and BP_LocalTankGameMode; check Output Log for new errors/warnings. Confirm
   TestMap and the existing input/visual assignments load cleanly.
2. Run the standalone setup above. Verify movement, independent aim and camera
   still work. Confirm the automatic cube is visible and reachable by projectiles.
3. Confirm the supplied IA_Fire/IMC_Tank/BP_TankPlayerController settings above.
   Release the focus-acquiring click, then click once: one shot. Hold for several
   seconds: no repeat. Release/click: one more shot. Rapid clicks within 0.30 s
   produce no extra shots; a later fresh press is accepted without a queued shot.
4. While holding fire, Shift+F1 / switch focus, then regain control while still
   holding: no shot. Release, then press again: one shot. Repeat while moving,
   while paused, and after possession/control changes. No stale request should
   survive. Also release while unfocused, return with the button neutral, and
   verify the next fresh press after neutral is observed. Test input flush and
   canceled/rebuilt mappings while held: synthetic completion must not rearm or
   create a shot. After a flushed action loses its held state, check the documented
   suppressed press/release recovery. Repeat first-window focus acquisition and
   double clicks to determine whether the brief focus-click precaution can be removed.
5. Move/turn while aiming. Verify muzzle position/direction follows the gameplay
   pose. Exaggerate hover/bank or offset/rotate visual turret/barrel components in
   a temporary Blueprint edit: shot pose must stay unchanged. Restore visual
   defaults afterward. Gameplay offsets remain fixed during these checks.
6. Drive close to a wall and aim across it with the muzzle beyond its near face.
   Click: no projectile beyond the wall; the cooldown is consumed. Repeat with
   cover intersecting the turret origin and with cover just at the muzzle edge.
   A thin wall must also stop a fast projectile between frames.
7. Hit the target once: Hit Count increases by 1, Total Damage by 25. Fire at least
   eight accepted hits: totals 8/200 and the target remains. A blocked launch into
   the target increments once. Projectiles do not hit their shooter.
8. Fire into clear space: straight horizontal travel, no bounce, disappearance
   after 3 s and no damage on expiry. Stop/restart PIE at least three times,
   including stopping while holding fire. Target counters reset each session.
9. Restore Blueprint tuning and clean network conditions. Run two-player listen
   PIE using LAB_2_NETWORK_CONDITIONS.md: both tanks move/aim in both windows,
   focus/timeout still stop stale movement, and no player can fire. No automatic
   damage target is created and no combat actors/state replicate.

Placeholder art may not visually coincide exactly with the fixed gameplay muzzle.
Do not correct that by deriving gameplay pose from a mesh/socket. No visual PIE,
Blueprint compilation or packaged behavior is established by automation alone.
