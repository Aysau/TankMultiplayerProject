# Network Tank Arena

Student starter for the Network Programming lab. Requires **Unreal Engine 5.7.4**
and the Windows C++ toolchain required by Unreal. Open `NetworkTankArena.uproject`
with UE 5.7.4 and build the project if prompted. The startup level is `TestMap`.

W/S move forward/backward, A/D turn the chassis, and the mouse aims the turret
independently. Left mouse sends one discrete fire request per fresh press;
the server fires replicated projectiles at a persistent damage-test cube.
Enhanced Input is configured through **IA_Fire**,
the fire mapping in **IMC_Tank**, and **BP_TankPlayerController's Fire Action**.
The existing asset setup and earlier local firing behavior were manually verified in UE 5.7.4.
Release the focus-acquiring click before firing; that specific capture behavior
has not been verified. See [Authoritative network combat](docs/NETWORK_COMBAT.md)
for the current contract, automation and manual checks. Network combat and the
target readout still require manual UE 5.7.4 verification. Tank health is not implemented.

To run two players locally:

1. Open `TestMap` and select **Play → Advanced Settings**.
2. Set **Number of Players = 2** and **Net Mode = Play As Listen Server**.
3. Use **New Editor Window (PIE)** and enable **Run Under One Process**.
4. Select **Default Player Start**, then Play. Focus each player window in turn
   to control that player's tank. Shift+F1 releases the mouse from PIE.
5. Each window should see both tanks move and aim. Stop PIE and repeat.

Movement is server-authoritative without prediction; the client waits for server
updates, so some input delay and stepped motion are expected.

Thursday Lab 2 starts with [Network conditions](docs/LAB_2_NETWORK_CONDITIONS.md):
use its PIE setup and impairment profiles to observe this movement baseline.
Friday Lab 3 adds reliable fire requests, server collision/damage, replicated
projectiles and target totals. Projectile presentation deliberately has no prediction
or custom interpolation.

For a packaged **Development** build, start two copies. In the first copy, open
the console with tilde and enter `HostLocalhost`. In the second, enter
`JoinLocalhost`. This uses `127.0.0.1:7777`; start the host before joining.
These commands start from standalone play and require `TestMap` in the package.
No account or online service is required.
