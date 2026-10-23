# Network Tank Arena — Client-Side Movement Prediction, Server Reconciliation & Multi-View Smoothing

## 1. Project / Feature Summary
This project investigates and resolves the fundamental input responsiveness and network synchronization limitations of the Network Tank Arena starter project. 

In the starter baseline, remote owning clients were explicitly demoted to Simulated Proxy (`SetAutonomousProxy(false)`). This forced clients to drive with round-trip input delay, resulting in sluggish steering and noticeable turret rotation lag.

This implementation restores the Autonomous Proxy model and implements a deterministic Client-Side Prediction, Server Reconciliation, Redundant Move Packing, and Multi-View Render-Time Entity Interpolation system. 
The local client simulates 2D planar chassis movement and turret aim with closer to zero input lag, buffers unacknowledged moves, transmits redundant history windows to survive packet loss, and reconciles discrepancies when server-authoritative collisions occur.

## 2. How to Run and Test the Project

### Running the Multiplayer Session:
1. Open `NetworkTankArena.uproject` in Unreal Engine 5.
2. In the Editor Viewport toolbar, set **Number of Players** to `2` and mode to **Play as Listen Server**.

### Testing Under Network Impairment:
Open the Unreal Networking editor settings and adjust latency and packet loss.

### Running Automated Unit Tests:
1. In the Editor, open Tools -> Session Frontend -> Automation tab.
2. Check `NetworkTankArena` and click Start Tests. All tests will execute 

## 3. What Was Implemented or Changed

1. Autonomous Proxy Restoration (`TankPawn.cpp`):
   - Removed `SetAutonomousProxy(false)` demotion on possession, enabling clients to simulate movement locally.
2. Move & Acknowledgment Serialization (`TankControlIntent.h`):
   - Created `FTankMoveRecord` storing `(Sequence, DeltaTime, Intent, ResultLocation, ResultRotation)`.
   - Created `FTankServerMoveAck` returning `(AckSequence, Location, Rotation, Velocity)`.
3. Sequenced Redundant Move Pipeline (`TankPlayerController.cpp`):
   - Implemented `ServerSubmitMoves` (Unreliable Server RPC), transmitting a redundant window of the last 6 unacknowledged moves per packet to survive packet drops.
   - Implemented `ClientAckMove` (Unreliable Client RPC) returning authoritative server confirmation.
4. Client-Side Simulation & Move Buffer (`TankPawn.cpp`):
   - Implemented `PredictLocalMove`: executes moves immediately on the local frame delta time and appends to a capped circular buffer `SavedMoves`.
5. Server Reconciliation & Replay Loop (`TankPawn.cpp`):
   - Implemented `ReconcileServerMove`: compares client predicted historical transforms against server truth.
   - If error is above a set limit, rewinds the client physics capsule to server state and sequentially replays all pending unacknowledged moves.
6. Visual Correction Smoothing (`TankPawn.cpp`):
   - Eliminates visual camera snapping on corrections by computing `VisualCorrectionOffset = (PreCorrectionLocation - PostReplayLocation)` and dampening it back to zero over around 100ms via `FMath::VInterpTo`.
7. Render-Time Entity Interpolation for Remote Proxies (`TankPawn.cpp`):
   - Decouples remote visual presentation from network packet arrival jitter by continuously interpolating `RemoteSmoothedLocation` and `RemoteSmoothedChassisYaw` at render framerates.
8. Shortest-Path Angular Interpolation (`TankPawn.cpp`):
   - Fixed the angular boundary snap by using shortest-path delta calculations (`FindDeltaAngleDegrees`), preventing unnatural full-circle spins.
9. Turret Aim Prediction (`TankPawn.cpp`):
   - Autonomous clients predict aim in real time (`PredictedTurretWorldYaw`), while remote proxies receive smoothed authoritative `CanonicalTurretWorldYaw`.
10. Automated Unit Testing (`MultiplayerTankTests.cpp`):
    - Added `FTankPredictionReconciliationTest`, injecting a server desync and verifying deterministic rewind, replay, and spatial recovery.

## 4. Important Client/Server, Authority, and Replication Decisions

Strict Server Authority: The client never dictates its final world transform. The server independently simulates moves using its authoritative `UTankMovementComponent`, performs sweep collision checks, resolves damage, and decides real world locations.
Unreliable RPCs for Continuous Movement:
  - `ServerSubmitMoves` and `ClientAckMove` use `Unreliable` transmission. Using reliable RPCs could cause head-of-line blocking under packet loss. Dropped packets are naturally resolved by redundant packing and the next arriving acknowledgment.
Separation of Simulation Roles:
  - Local Owning Client (`AutonomousProxy`): Runs local prediction and buffers moves, reconciles only when a discrepancy is detected.
  - Remote Proxies (`SimulatedProxy`): Do not simulate physics. They receive replicated transforms and are smoothed via continuous render-time entity interpolation.
  - Listen Server Host (`Authority`): Simulates directly on authority, exempt from prediction lag.
DeltaTime Lockstep: The server simulates client moves using the client's sanitized move `DeltaTime` rather than arbitrary server frame times.

## 5. Network Conditions Used for Testing

Testing was conducted across four distinct reproducible network emulation environments:
1. Ideal Baseline: 0 latency, 0% packet loss. 
2. High Latency Profile: 100 latency both ways
3. Adverse Network Profile: 100 latency, 10% packet loss
4. Stress / Catastrophic Profile: 150-200 latency, 20-40% packet loss

## 6. Networking/Synchronization Problems Encountered & Diagnosed

### Problem 1: Server Double-Simulation & Continuous Desync Under Latency
Symptom: Initial prediction tests under 100 latency resulted in continuous, violent reconciliations every 3–4 frames during straight-line driving, even with 0% packet loss.
Diagnosis: Investigation revealed that when network packets arrived with minor jitter, the server experienced frames where 0 client RPCs arrived. In those frames, `ATankPawn::Tick` fell back to simulating the pawn's held `CurrentIntent` using server frame time. When the delayed client RPC arrived on the following frame, the server executed the move again. The server was advancing faster than the client predicted, constantly reporting positions 10–20 cm ahead.
Solution: Introduced `bIsRemoteControlled` on `ATankPawn`. Pawns controlled by remote network connections are strictly simulated when their sequenced move RPC arrives, preventing tick moves on authority.

### Problem 2: Visual Teleporting Snaps During Corrections
Symptom: When a legitimate reconciliation occurred, the tank mesh visibly jumped 50–100 cm forward before sliding back.
Diagnosis:The visual offset formula was calculating `VisualCorrectionOffset = OldClientLocation - Ack.Location`. `OldClientLocation` was the present time, while `Ack.Location` was from 200ms in the past. This offset the mesh by 200ms of forward travel rather than the true misprediction error.
Solution: Rewrote the formula to capture `PreCorrectionLocation` before rewind, replay all pending moves to `PostReplayLocation`, and compute `VisualCorrectionOffset = PreCorrectionLocation - PostReplayLocation`. This dampened corrections to only the true post-replay error (a few cm).

### Problem 3: 350-Degree Turret Flip Across Angular Wrap
Symptom: When aiming towards the bottom-right corner, the turret took a violent full-circle spin instead of rotating directly.
Diagnosis: Bottom-right is the -180 / 180 wrap-around boundary. Using linear interpolation (`FMath::FInterpTo`) across +179 -179 interpolated across the 358 degree span.
Solution: Used shortest-path angular unwrapping (`FindDeltaAngleDegrees` and `NormalizeAxis`) to ensure interpolation always takes the shortest angular path.


## 7. Networking Improvement & Optimization (Why It Was Chosen)

Chosen Improvement: Client-Side Prediction, Server Reconciliation, Redundant Move Packing, and Render-Time Interpolation.
Why Chosen: Tank combat requires precise spatial maneuvering and aim. Round-trip input latency made driving feel sluggish and floaty. Client-side prediction gives the player instant responsiveness while preserving server authority against cheating.
Measured Evidence & Results:
  Input-to-Motion Latency: Reduced from having delay to instant response.
  Reconciliation Count: 0 corrections during unimpeded straight-line driving, small, corrections only on dynamic obstruction.
  Packet Loss Resilience: Upgraded from single-move RPCs to a 6-move redundant history window, reducing dropped-move mispredictions under 10% packet loss to virtually zero.
  Remote Proxy Smoothness: Increased net update frequency from 20 Hz to 60 Hz and implemented render-time entity interpolation, eliminating visual stepping.

## 8. Known Limitations, Trade-offs & Stress Testing

Stress Testing at 20%–40% Packet Loss:
  Under extreme packet loss combined with high latency, the 6-move redundancy window can occasionally be exhausted if 4+ packets drop consecutively. This causes packet starvation on the server, resulting in temporary stepping when catch-up bursts arrive. In commercial production, this might be handled by introducing an intentional Interpolation Buffer Delay, which trades 100ms of visual delay for guaranteed continuity.
Replay CPU Overhead:
  Replaying unacknowledged moves requires executing physics sweeps. While our 2D capsule movement is lightweight, complex physics with thousands of dynamic rigidbodies would require limiting replay depth or sub-stepping.