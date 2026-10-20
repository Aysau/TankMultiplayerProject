# Thursday Lab 2: Network conditions

Use **Unreal Engine 5.7.4** and the Windows C++ toolchain required by Unreal.
Build the project before opening the Editor after C++ changes. This baseline
covers movement and independent turret aiming. The original Lab 2 version had
no multiplayer combat; the current branch also includes [Lab 3 combat](NETWORK_COMBAT.md).
Use movement/aim only for the observations here. Health, respawn, score and match
state remain out of scope. The earlier [local combat foundation](LOCAL_COMBAT_PARITY.md)
is retained as historical documentation.

The lesson starts by observing latency, then jitter and packet loss, before later
work on interpolation and prediction. Do not fix the observed delay yet.

## Two-player PIE setup

1. Open `NetworkTankArena.uproject` in UE 5.7.4 and load `/Game/Maps/TestMap`.
2. In World Settings, confirm GameMode Override is **None** (uses the project's
   native **TankGameMode**) or explicitly **TankGameMode**. The native mode uses
   **BP_Tank** and **BP_TankPlayerController**. There should be two clear
   PlayerStarts and no manually placed tanks.
3. Open the Play dropdown / Advanced Settings and select:
   - **New Editor Window (PIE)**;
   - **Number of Players = 2** (also labelled Play Number of Clients in advanced
     settings; this listen-server setup has two players total);
   - **Play Net Mode = Play As Listen Server**;
   - **Run Under One Process = enabled**;
   - **Launch Separate Server = disabled**;
   - **Spawn Player At = Default Player Start**, not Current Camera Location.
4. Start with network emulation disabled, then Play. Two windows should open,
   each showing both tanks. Check the window title's NetMode: the **Server**
   window is the listen host and the **Client** window is the remote player.
   Use the Editor's PIE world selector to confirm which world/actors you inspect.

Click a game window to focus it. **W/S** move forward/backward, **A/D** turn the
chassis, and the **mouse** aims the turret independently. **Shift+F1** releases
the mouse from PIE. Release movement keys before switching windows. One physical
keyboard/mouse supplies input to the focused window; it does not control both
players simultaneously. Each player must control only its own tank.

## What this baseline deliberately does

Only a local PlayerController reads device input. The remote owner sends complete
movement/aim intent snapshots to the server through an **unreliable RPC**, nominally
every **0.05 seconds (20 Hz)**, including neutral snapshots. The server validates
the current possession, accepts newer input sequences and simulates movement.
Actor movement replication distributes chassis/root state; canonical turret world
yaw is replicated separately. Hover/banking presentation is calculated locally.

The locally owned **Client** tank deliberately waits for server replication,
just like other client tanks. There is **no client movement prediction,
reconciliation or custom movement interpolation**. Raw/stepped client motion is
intentional for this first observation. Cosmetic banking does not smooth the
gameplay root. The listen host submits input directly on the authoritative
machine, so it has an inherent responsiveness advantage.

Actively control the **Client window** to experience remote-owner latency.
Controlling only the Server window can conceal the main problem being studied.

## Network Emulation

Stop PIE before changing conditions. Open:

**Edit → Editor Preferences → Level Editor → Play → Multiplayer Options**

For impaired runs, enable **Enable Network Emulation**, set **Emulation Target =
Clients Only**, and choose **Network Emulation Profile = Custom**. Expand both
**Incoming traffic** and **Outgoing traffic**. Enter Minimum Latency, Maximum
Latency (milliseconds) and Packet Loss Percentage for each direction.

These names are **course labels**, not existing Unreal presets. Keep Custom
selected and enter the values; Average/Bad are different engine presets.

| Profile | Incoming latency | Outgoing latency | Incoming/outgoing loss |
|---|---:|---:|---:|
| Clean | 0–0 ms | 0–0 ms | 0% / 0% |
| Fixed latency | 100–100 ms | 100–100 ms | 0% / 0% |
| Jitter | 50–150 ms | 50–150 ms | 0% / 0% |
| Lossy | 0–0 ms | 0–0 ms | 10% / 10% |
| Combined | 50–150 ms | 50–150 ms | 5% / 5% |

For Clean, disable emulation or enter all zero values. With Clients Only, the
fixed-latency settings add approximately **200 ms round-trip time (RTT)** before
input sampling, frame scheduling and replication delay. RTT is not complete
input-to-render latency. Selecting Everyone also impairs the server endpoint,
changing the experiment. Random profiles repeat conditions, not identical packet
outcomes. Do not hard-code impairment into gameplay or project configuration.

## Observation order

Run **Clean → Fixed latency → Jitter → Lossy → Combined**. Stop PIE between
profiles and repeat the same actions in the Client window:

1. Start moving, release the key, and compare when the tank starts/stops in each
   window. Then repeat while controlling the listen host.
2. Turn while stationary, then turn while moving. Compare delayed response and
   spacing between visible movement updates.
3. Aim at a fixed point, then move the mouse quickly. Compare turret response
   independently of chassis turning.
4. Release keys and change focus. Check that movement stops rather than retaining
   input indefinitely. Record the profile, focused window and observed behavior.

Neutral heartbeats repair a missed key-release snapshot. If the server receives
no accepted intent for **0.5 seconds**, it clears movement/aim intent and velocity;
the turret retains its last canonical heading. This is a **receipt-silence**
timeout, not a measurement of how old a client sampled input is. Duplicate/stale
or invalid-target submissions do not refresh it. The normal loss profiles may
show missed updates without producing a full half-second silence.

## Sequence behavior

The controller allocates a `uint32` sequence for every submitted complete snapshot,
including neutral input. The server accepts the first value (zero is valid), then
only values ahead by less than half the unsigned range. `UINT32_MAX → 0` is newer;
duplicates, older values and exactly-half-range differences are rejected. This
assumes fewer than `2^31` outstanding increments.

Sequence history lasts for the **PlayerController's lifetime**. Focus loss,
timeout, possession changes, Pawn Restart and controller Reset clear gameplay
intent as appropriate but do not reset numbering/history. A new controller starts
fresh. Old-pawn submissions fail current-possession validation before they can
advance history. This stream does not tag possession generations: a newer snapshot
still in flight for the same currently possessed pawn can be accepted after an
intent clear. There is no timestamp, acknowledgement or reset handshake.

Sequencing is **defensive application hardening**, not a reproduced Unreal
transport-reordering bug. UE 5.7.4 already handles obsolete packets below RPC
dispatch; the explicit check makes the gameplay acceptance rule visible and
unit-testable. It adds no prediction or smoothing.

## Existing inspection tools

- Use PIE window titles and world selection to distinguish server/client actors.
- In a game window, open the console (tilde) and use `stat net` for network
  statistics. These are approximate measurements, not total response latency.
- In a native debugger, inspect ownership, `GetLocalRole()` / `GetRemoteRole()`,
  `IsLocallyControlled()`, and the controller's current Pawn. Client tank copies
  intentionally use simulated-proxy roles, including the locally owned tank;
  ownership and simulation role are separate concerns.
- On the owning controller, inspect `NextMovementIntentSequence`. In the server
  world, inspect `AcceptedMovementIntentSequence.bHasAcceptedSequence` and
  `LastAcceptedSequence`, plus the pawn's `CurrentIntent` and
  `ControlIntentAgeSeconds`. Accepted history is server-local, not replicated
  debug state. The age counter resets on intent clear as well as acceptance.
- Read the selected condition from Editor settings; there is no custom profile
  HUD. Pausing in a debugger affects network timing, so observe motion unpaused.

## Restore a clean baseline

1. Release all input.
2. Stop PIE.
3. Disable Network Emulation or restore all incoming/outgoing values to zero.
4. Restart PIE and repeat the clean movement check in the Client window.

After manual runtime **`NetEmulation.*`** experiments, **restart the Editor before
trusting the clean baseline**. Runtime commands can retain process-wide settings
across PIE sessions. Prefer the Editor settings above for the repeatable lab flow.

## Troubleshooting and completion checklist

- Only one player/window: recheck two players, Listen Server mode and the separate
  window option. This exercise does not use Play As Client/dedicated-server mode.
- Missing or blocked spawn: inspect both PlayerStarts, GameMode assignment and
  the Output Log. Do not add manual tank spawning or Level Blueprint logic.
- No controls: focus the intended window and check the controller's input assets.
- No obvious latency: actively drive the Client tank; verify Custom, Clients
  Only, both traffic directions, and restart PIE after changing settings.
- Unexpected delay during Clean: check settings, restart the Editor after runtime
  emulation commands, and distinguish frame-rate stalls from network effects.
- Before finishing: both tanks move/aim in both windows; ownership is correct;
  focus loss stops input; repeated PIE works; clean conditions are restored.
  Record delayed/stepped behavior before attempting later solutions. Use the
  separate Lab 3 checklist when verifying current network combat.

Engine reference: [UE 5.7 Network Emulation](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-network-emulation-in-unreal-engine?application_version=5.7)
and [PIE multiplayer options](https://dev.epicgames.com/documentation/en-us/unreal-engine/play-in-editor-multiplayer-options-in-unreal-engine?application_version=5.7).
