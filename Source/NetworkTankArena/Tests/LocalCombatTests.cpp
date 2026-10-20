#if WITH_DEV_AUTOMATION_TESTS

#include "../TankCombat.h"
#include "../TankDamageTestTarget.h"
#include "../TankGameMode.h"
#include "../TankPawn.h"
#include "../TankPlayerController.h"
#include "../TankProjectile.h"
#include "../TankWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Misc/AutomationTest.h"
#include <limits>

using namespace NetworkTankArena::LocalCombat;

namespace
{
	struct FScopedCombatWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FScopedCombatWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
		~FScopedCombatWorld()
		{
			if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		template<typename T> T* Spawn(const FVector& Location = FVector::ZeroVector)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			T* Actor = World->SpawnActor<T>(Location, FRotator::ZeroRotator, Params);
			Actor->DispatchBeginPlay();
			return Actor;
		}
		int32 ProjectileCount() const
		{
			int32 Count = 0;
			for (TActorIterator<ATankProjectile> It(World); It; ++It) { ++Count; }
			return Count;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankFireLatchTest, "NetworkTankArena.LocalCombat.FireLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankFireLatchTest::RunTest(const FString& Parameters)
{
	FFireRequestLatch Latch;
	TestTrue(TEXT("One press requests fire"), Latch.Press());
	for (int32 Index = 0; Index < 100; ++Index)
	{
		TestFalse(TEXT("Held/repeated press never repeats"), Latch.Press());
	}
	Latch.Release();
	TestTrue(TEXT("Observed release rearms"), Latch.Press());
	Latch.Release();
	Latch.Interrupt();
	TestFalse(TEXT("Even an armed latch is blocked after interruption"), Latch.Press());
	Latch.Interrupt();
	TestFalse(TEXT("Repeated interruption cannot rearm"), Latch.Press());
	Latch.Release();
	TestTrue(TEXT("Release after interruption permits a new press"), Latch.Press());
	Latch.Interrupt();
	TestFalse(TEXT("Interrupted held button stays blocked"), Latch.Press());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankWeaponCooldownTest, "NetworkTankArena.LocalCombat.Cooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankWeaponCooldownTest::RunTest(const FString& Parameters)
{
	FWeaponCooldown Cooldown;
	TestTrue(TEXT("First shot at time zero is accepted"), Cooldown.TryConsume(0.0));
	TestFalse(TEXT("Duplicate rejected"), Cooldown.TryConsume(0.0));
	TestFalse(TEXT("Early request rejected"), Cooldown.TryConsume(0.299));
	TestTrue(TEXT("Exact 0.30 boundary accepted"), Cooldown.TryConsume(0.30));
	TestFalse(TEXT("Rejected request did not shift cooldown"), Cooldown.TryConsume(0.599));
	TestTrue(TEXT("Later request accepted"), Cooldown.TryConsume(1.0));
	TestFalse(TEXT("Time reversal rejected"), Cooldown.TryConsume(0.5));
	TestFalse(TEXT("NaN rejected"), Cooldown.TryConsume(std::numeric_limits<double>::quiet_NaN()));
	TestFalse(TEXT("Infinity rejected"), Cooldown.TryConsume(std::numeric_limits<double>::infinity()));
	TestTrue(TEXT("Invalid requests leave cooldown usable"), Cooldown.TryConsume(2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankEnhancedFireLatchTest, "NetworkTankArena.LocalCombat.EnhancedFireLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankEnhancedFireLatchTest::RunTest(const FString& Parameters)
{
	FFireRequestLatch Latch;
	Latch.ObserveActionValue(true); // An action value can be visible while a Started delegate was blocked.
	TestFalse(TEXT("Held observation without a Started callback blocks a later stale Started"), Latch.Press());
	Latch.ObserveActionValue(false);
	TestTrue(TEXT("Release after a skipped Started permits a fresh press"), Latch.Press());
	Latch.Interrupt();
	Latch.ObserveActionValue(true); // Held when control returns; no fresh Started required.
	TestFalse(TEXT("Held across interruption cannot request a shot on regain"), Latch.Press());
	for (int32 Index = 0; Index < 100; ++Index) { Latch.ObserveActionValue(true); }
	TestFalse(TEXT("Polling held never rearms"), Latch.Press());
	Latch.ObserveActionValue(false);
	TestTrue(TEXT("Observed neutral permits the next fresh Started"), Latch.Press());
	TestFalse(TEXT("Repeated Started cannot repeat a request"), Latch.Press());
	Latch.Release();
	TestTrue(TEXT("Completed rearms normal press/release"), Latch.Press());
	Latch.Interrupt();
	Latch.ObserveActionValue(false);
	TestTrue(TEXT("Neutral after ordinary control loss permits a fresh press"), Latch.Press());

	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		Latch.InvalidateActionValue(); // Flush or Canceled, rather than genuine release.
		for (int32 Index = 0; Index < 100; ++Index)
		{
			Latch.Release();
			Latch.ObserveActionValue(false);
		}
		TestFalse(TEXT("Synthetic Completed/zero after flush/cancel cannot rearm"), Latch.Press());
		Latch.ObserveActionValue(true);
		TestFalse(TEXT("Resumed held action remains blocked"), Latch.Press());
		Latch.ObserveActionValue(false);
		TestTrue(TEXT("Subsequent genuine neutral permits next press"), Latch.Press());
	}
	Latch.InvalidateActionValue();
	Latch.Interrupt();
	Latch.ObserveActionValue(false);
	TestFalse(TEXT("Further control loss preserves the invalidated-value guard"), Latch.Press());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankEnhancedFireBindingTest, "NetworkTankArena.LocalCombat.EnhancedFireBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankEnhancedFireBindingTest::RunTest(const FString& Parameters)
{
	FScopedCombatWorld Scope;
	auto* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Scope.Spawn<ATankPawn>());
	// Transient Enhanced Input objects: no binary action/context/Blueprint wiring required.
	Controller->FireAction = NewObject<UInputAction>(Controller);
	Controller->FireAction->ValueType = EInputActionValueType::Boolean;
	Controller->InputComponent = NewObject<UEnhancedInputComponent>(Controller);
	Controller->SetupInputComponent();
	auto* Component = CastChecked<UEnhancedInputComponent>(Controller->InputComponent);
	const auto& Bindings = Component->GetActionEventBindings();
	TestEqual(TEXT("Only three native fire bindings on the unconfigured native controller"), Bindings.Num(), 3);
	for (const ETriggerEvent Event : {ETriggerEvent::Started, ETriggerEvent::Completed, ETriggerEvent::Canceled})
	{
		int32 Count = 0;
		for (const auto& Binding : Bindings)
		{
			if (Binding->GetAction() == Controller->FireAction && Binding->GetTriggerEvent() == Event) { ++Count; }
		}
		TestEqual(TEXT("One binding per required event"), Count, 1);
	}
	auto* Input = NewObject<UEnhancedPlayerInput>(Controller);
	Controller->PlayerInput = Input;
	int32 Starts = 0;
	int32 Completions = 0;
	int32 Cancellations = 0;
	Component->BindActionInstanceLambda(Controller->FireAction, ETriggerEvent::Started,
		[&](const FInputActionInstance&) { ++Starts; });
	Component->BindActionInstanceLambda(Controller->FireAction, ETriggerEvent::Completed,
		[&](const FInputActionInstance&) { ++Completions; });
	Component->BindActionInstanceLambda(Controller->FireAction, ETriggerEvent::Canceled,
		[&](const FInputActionInstance&) { ++Cancellations; });
	const TArray<UInputComponent*> Stack{Component};
	auto Process = [&](bool bHeld)
	{
		Input->InjectInputForAction(Controller->FireAction, FInputActionValue(bHeld));
		Input->ProcessInputStack(Stack, 0.016f, false);
	};
	Process(true);
	for (int32 Index = 0; Index < 5; ++Index) { Process(true); }
	TestEqual(TEXT("Real Enhanced Input emits one Started while held"), Starts, 1);
	Process(false);
	TestEqual(TEXT("Real Enhanced Input emits Completed on neutral"), Completions, 1);
	Process(true);
	TestEqual(TEXT("New press emits next Started"), Starts, 2);
	Controller->FlushPressedKeys();
	Process(false);
	TestFalse(TEXT("Flush processing pass has ended"), Controller->bIgnoreFireInputPass);
	TestFalse(TEXT("Synthetic completion without control cannot rearm"), Controller->FireLatch.Press());
	TestEqual(TEXT("No viewport/control means no gameplay request"), Scope.ProjectileCount(), 0);

	// Exercise Canceled using a transient ongoing trigger, not the recommended IA_Fire defaults.
	auto* Hold = NewObject<UInputTriggerHold>(Controller->FireAction);
	Hold->HoldTimeThreshold = 10.f;
	Controller->FireAction->Triggers.Add(Hold);
	Input = NewObject<UEnhancedPlayerInput>(Controller); // New action instance copies the changed trigger defaults.
	Controller->PlayerInput = Input;
	Process(true);
	Process(false);
	TestEqual(TEXT("Enhanced Input routes Canceled for an unfinished action"), Cancellations, 1);
	TestFalse(TEXT("Canceled cannot rearm"), Controller->FireLatch.Press());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalFireLifecycleTest, "NetworkTankArena.LocalCombat.FireLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalFireLifecycleTest::RunTest(const FString& Parameters)
{
	FScopedCombatWorld Scope;
	ATankPawn* Tank = Scope.Spawn<ATankPawn>();
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Tank);
	Controller->FireLatch.Release();
	Controller->PendingFireTank = Tank;
	Controller->FlushPressedKeys();
	TestFalse(TEXT("Key flush cannot synthesize a fire release"), Controller->FireLatch.Press());
	TestFalse(TEXT("Key flush clears pending local press"), Controller->PendingFireTank.IsValid());
	TestEqual(TEXT("Key flush cancels pending shot"), Scope.ProjectileCount(), 0);
	Controller->HandleFireCompleted(); // No viewport/focus in this transient world.
	TestFalse(TEXT("Release without local focus/control cannot rearm"), Controller->FireLatch.Press());
	Controller->FireLatch.Release();
	Controller->AcknowledgePossession(Tank);
	TestFalse(TEXT("Possession acknowledgement requires release"), Controller->FireLatch.Press());
	Controller->FireLatch.Release();
	Controller->UnPossess();
	TestFalse(TEXT("Unpossession requires release"), Controller->FireLatch.Press());
	Controller->Possess(Tank);
	Controller->FireLatch.Release();
	Controller->SetPlayerState(Scope.World->SpawnActor<APlayerState>());
	Controller->Reset();
	TestFalse(TEXT("Controller reset requires release"), Controller->FireLatch.Press());
	Controller->Possess(Tank); // Reset can unpossess; pause must exercise the currently controlled tank.
	TestTrue(TEXT("Pause fixture has current possession"), Controller->GetPawn() == Tank);
	Controller->FireLatch.Release();
	Controller->PendingFireTank = Tank;
	Controller->TickActor(0.01f, LEVELTICK_PauseTick, Controller->PrimaryActorTick);
	TestFalse(TEXT("Pause actor-tick path interrupts even without PlayerTick"), Controller->FireLatch.Press());
	TestFalse(TEXT("Pause tick clears pending local press"), Controller->PendingFireTank.IsValid());
	TestEqual(TEXT("Pause tick cancels pending shot"), Scope.ProjectileCount(), 0);
	Controller->FireLatch.Release();
	Controller->HandleFireCanceled();
	Controller->HandleFireCompleted();
	TestFalse(TEXT("Canceled/Completed without control cannot restore a stale press"), Controller->FireLatch.Press());
	Controller->FireLatch.Release();
	TestTrue(TEXT("A real subsequent release rearms the latch"), Controller->FireLatch.Press());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalFireDisableInputTest, "NetworkTankArena.LocalCombat.FireDisableInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalFireDisableInputTest::RunTest(const FString& Parameters)
{
	for (const bool bNullController : {false, true})
	{
		FScopedCombatWorld Scope;
		ATankPawn* Tank = Scope.Spawn<ATankPawn>(FVector(0.0, 0.0, 100.0));
		ATankPlayerController* Controller = Scope.Spawn<ATankPlayerController>();
		Controller->SetAsLocalPlayerController();
		Controller->Possess(Tank);
		// No viewport: exercise the local pending-press boundary of a valid Started.
		Controller->FireLatch.ObserveActionValue(false);
		if (!TestTrue(TEXT("Fresh press accepted before disable"), Controller->FireLatch.Press())) { return false; }
		Controller->PendingFireTank = Tank;
		Controller->CachedThrottle = 0.75f;
		Controller->CachedTurn = -0.25f;
		Controller->DisableInput(bNullController ? nullptr : Controller);
		TestFalse(TEXT("Engine controller input is disabled"), Controller->InputEnabled());
		TestFalse(TEXT("Disabled controller has no local fire control"), Controller->HasLocalFireControl());
		TestFalse(TEXT("DisableInput clears pending press before dispatch"), Controller->PendingFireTank.IsValid());
		TestEqual(TEXT("DisableInput immediately cancels pending shot"), Scope.ProjectileCount(), 0);
		TestFalse(TEXT("DisableInput suppresses held fire"), Controller->FireLatch.Press());
		TestEqual(TEXT("Fire interruption preserves cached throttle"), Controller->CachedThrottle, 0.75f);
		TestEqual(TEXT("Fire interruption preserves cached turn"), Controller->CachedTurn, -0.25f);

		Controller->EnableInput(bNullController ? nullptr : Controller);
		TestTrue(TEXT("Engine controller input is enabled again"), Controller->InputEnabled());
		Controller->FireLatch.ObserveActionValue(true);
		if (Controller->FireLatch.Press()) { Controller->SubmitLocalFireRequest(Tank, FVector(1000.0, 0.0, 100.0)); }
		TestEqual(TEXT("Re-enable while held produces no shot"), Scope.ProjectileCount(), 0);
		Controller->FireLatch.ObserveActionValue(false);
		if (!TestTrue(TEXT("Genuine neutral rearms next fresh press"), Controller->FireLatch.Press())) { return false; }
		Controller->SubmitLocalFireRequest(Tank, FVector(1000.0, 0.0, 100.0));
		TestEqual(TEXT("Fresh press after release produces exactly one shot"), Scope.ProjectileCount(), 1);
		TestFalse(TEXT("Held press cannot request another shot"), Controller->FireLatch.Press());
		TestEqual(TEXT("Consumed request cannot fire twice"), Scope.ProjectileCount(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankFiringPoseTest, "NetworkTankArena.LocalCombat.FiringPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankFiringPoseTest::RunTest(const FString& Parameters)
{
	const auto Pose = CalculateFiringPose(FVector(100.0, 200.0, 300.0), 90.f, -25.f, 110.f);
	TestTrue(TEXT("Origin is root position plus fixed world height"), Pose.TurretOrigin.Equals(FVector(100.0, 200.0, 275.0)));
	TestTrue(TEXT("Canonical world yaw defines forward"), Pose.Forward.Equals(FVector::RightVector));
	TestTrue(TEXT("Muzzle is fixed distance along canonical forward"), Pose.Muzzle.Equals(FVector(100.0, 310.0, 275.0)));
	for (const float Yaw : {0.f, 45.f, -179.f, 181.f})
	{
		const auto Result = CalculateFiringPose(FVector::ZeroVector, Yaw, 20.f, 100.f);
		TestTrue(TEXT("Horizontal unit forward"), FMath::IsNearlyEqual(Result.Forward.Size(), 1.0) && FMath::IsNearlyZero(Result.Forward.Z));
		TestTrue(TEXT("Fixed gameplay reach"), FMath::IsNearlyEqual(FVector::Distance(Result.Muzzle, Result.TurretOrigin), 100.0));
	}
	FScopedCombatWorld Scope;
	ATankPawn* Tank = Scope.Spawn<ATankPawn>(FVector(0.0, 0.0, 100.0));
	auto* Weapon = Tank->GetWeaponComponent();
	const auto Before = Weapon->GetFiringPose();
	// Deliberately distort every presentation scene component (including mesh/socket parents).
	TInlineComponentArray<USceneComponent*> Components(Tank);
	for (USceneComponent* Component : Components)
	{
		if (Component != Tank->GetRootComponent())
		{
			Component->SetRelativeLocation(FVector(1000.0, -500.0, 2000.0));
			Component->SetRelativeRotation(FRotator(50.0, 75.0, 60.0));
			Component->SetRelativeScale3D(FVector(2.0));
		}
	}
	const auto After = Weapon->GetFiringPose();
	TestTrue(TEXT("All cosmetic transforms leave pose unchanged"), Before.TurretOrigin.Equals(After.TurretOrigin)
		&& Before.Muzzle.Equals(After.Muzzle) && Before.Forward.Equals(After.Forward));
	Tank->SetActorRotation(FRotator(0.0, 135.0, 0.0));
	TestTrue(TEXT("Chassis yaw is not turret yaw"), Weapon->GetFiringPose().Forward.Equals(Before.Forward));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalProjectileTest, "NetworkTankArena.LocalCombat.Projectile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalProjectileTest::RunTest(const FString& Parameters)
{
	FScopedCombatWorld Scope;
	ATankPawn* Shooter = Scope.Spawn<ATankPawn>();
	auto Launch = [&](const FVector& Position, const FVector& Direction)
	{
		ATankProjectile* Projectile = Scope.Spawn<ATankProjectile>(Position);
		Projectile->Initialize(Shooter, Direction);
		return Projectile;
	};
	ATankProjectile* Travel = Launch(FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("Projectile and movement replicate"), Travel->GetIsReplicated() && Travel->IsReplicatingMovement());
	TestEqual(TEXT("Collision radius is 10 cm"), Travel->Sphere->GetUnscaledSphereRadius(), 10.f);
	Travel->Tick(0.1f);
	TestTrue(TEXT("2000 cm/s, no gravity; shooter at origin ignored"), Travel->GetActorLocation().Equals(FVector(200.0, 0.0, 0.0), 0.001));
	TestFalse(TEXT("Leaving shooter does not terminate projectile"), Travel->bTerminal);
	ATankDamageTestTarget* Thin = Scope.Spawn<ATankDamageTestTarget>(FVector(500.0, 0.0, 0.0));
	CastChecked<UBoxComponent>(Thin->GetRootComponent())->SetBoxExtent(FVector(1.0, 50.0, 50.0));
	Travel->Tick(0.5f); // Endpoint is beyond a 2 cm wall: a point-only check would miss it.
	TestEqual(TEXT("Swept hit records one result"), Thin->HitCount, 1);
	TestEqual(TEXT("Hit damage is 25"), Thin->TotalDamage, 25.f);
	TestTrue(TEXT("Stops at radius contact with thin wall"), FMath::IsNearlyEqual(Travel->GetActorLocation().X, 489.0, 0.1));
	TestTrue(TEXT("Collision is terminal"), Travel->bTerminal && Travel->IsActorBeingDestroyed());
	FHitResult Duplicate(Thin, CastChecked<UPrimitiveComponent>(Thin->GetRootComponent()), Thin->GetActorLocation(), -FVector::ForwardVector);
	Travel->ResolveHit(Duplicate);
	Travel->Tick(1.f);
	TestEqual(TEXT("Repeated terminal notification/tick cannot damage again"), Thin->HitCount, 1);
	ATankProjectile* Expiring = Launch(FVector(0.0, 1000.0, 100.0), FVector::ForwardVector);
	Expiring->Tick(2.5f);
	TestFalse(TEXT("Alive before lifetime"), Expiring->bTerminal);
	Expiring->Tick(1.f);
	TestTrue(TEXT("Clamps final travel to exactly 3 seconds, 6000 cm"), Expiring->GetActorLocation().Equals(FVector(6000.0, 1000.0, 100.0), 0.01));
	TestTrue(TEXT("Lifetime expires cleanly"), Expiring->bTerminal && Expiring->IsActorBeingDestroyed());
	Expiring->Tick(1.f);
	TestTrue(TEXT("Expired projectile stays stopped"), Expiring->GetActorLocation().Equals(FVector(6000.0, 1000.0, 100.0), 0.01));
	ATankProjectile* Partitioned = Launch(FVector(0.0, 1500.0, 100.0), FVector::ForwardVector);
	for (int32 Index = 0; Index < 4; ++Index) { Partitioned->Tick(0.25f); }
	ATankProjectile* SingleStep = Launch(FVector(0.0, 2000.0, 100.0), FVector::ForwardVector);
	SingleStep->Tick(1.f);
	TestTrue(TEXT("Travel is independent of time partition"), FMath::IsNearlyEqual(Partitioned->GetActorLocation().X, SingleStep->GetActorLocation().X));
	// Sphere radius matters even if the centerline would miss the target edge.
	ATankProjectile* Edge = Launch(FVector(200.0, 59.0, 0.0), FVector::ForwardVector);
	Edge->Tick(0.3f);
	TestEqual(TEXT("Radius sweep catches grazing collision"), Thin->HitCount, 2);
	TestTrue(TEXT("Grazing hit is terminal without bouncing"), Edge->bTerminal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalWeaponTest, "NetworkTankArena.LocalCombat.Weapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalWeaponTest::RunTest(const FString& Parameters)
{
	FScopedCombatWorld Scope;
	ATankPawn* Tank = Scope.Spawn<ATankPawn>(FVector(0.0, 0.0, 100.0));
	UTankWeaponComponent* Weapon = Tank->GetWeaponComponent();
	ATankDamageTestTarget* Cover = Scope.Spawn<ATankDamageTestTarget>(FVector(60.0, 0.0, 75.0));
	CastChecked<UBoxComponent>(Cover->GetRootComponent())->SetBoxExtent(FVector(1.0, 50.0, 50.0));
	const FVector ForwardAim(1000.0, 0.0, 100.0);
	Weapon->TryFire(ForwardAim);
	TestEqual(TEXT("Blocked launch resolves obstruction"), Cover->HitCount, 1);
	TestEqual(TEXT("Blocked launch damage is 25"), Cover->TotalDamage, 25.f);
	TestEqual(TEXT("Blocked launch never spawns beyond cover"), Scope.ProjectileCount(), 0);
	Weapon->TryFire(ForwardAim);
	TestEqual(TEXT("Blocked shot consumed cooldown"), Cover->HitCount, 1);
	Scope.World->Tick(LEVELTICK_All, 0.31f);
	TestEqual(TEXT("Early rejected request is not buffered"), Cover->HitCount, 1);
	// Start overlapping the sphere at turret origin; must not escape through cover.
	Cover->SetActorLocation(Weapon->GetFiringPose().TurretOrigin);
	Weapon->TryFire(ForwardAim);
	TestEqual(TEXT("Initial overlap also resolves obstruction"), Cover->HitCount, 2);
	TestEqual(TEXT("Initial overlap spawns no projectile"), Scope.ProjectileCount(), 0);
	Cover->Destroy();
	Scope.World->Tick(LEVELTICK_All, 0.31f);
	FTankControlIntent Aim;
	Aim.bHasAimWorldPoint = true;
	Aim.AimWorldPoint = FVector(0.0, 1000.0, 100.0);
	Tank->SubmitControlIntent(Aim);
	Tank->Tick(0.01f);
	const float CanonicalBefore = Tank->GetCanonicalTurretWorldYaw();
	Weapon->TryFire(ForwardAim); // Intentionally differs from the continuous canonical aim.
	TestEqual(TEXT("Clear launch spawns exactly one projectile"), Scope.ProjectileCount(), 1);
	for (TActorIterator<ATankProjectile> It(Scope.World); It; ++It)
	{
		TestTrue(TEXT("Shot uses explicit authoritative aim snapshot"), It->Forward.Equals(FVector::ForwardVector));
		TestTrue(TEXT("Spawned at snapshot muzzle"), It->GetActorLocation().Equals(Weapon->GetFiringPose(0.f).Muzzle));
	}
	TestEqual(TEXT("Shot leaves continuous canonical turret yaw unchanged"), Tank->GetCanonicalTurretWorldYaw(), CanonicalBefore);
	Tank->Tick(0.1f);
	TestEqual(TEXT("Request is consumed once"), Scope.ProjectileCount(), 1);
	Scope.World->Tick(LEVELTICK_All, 0.31f);
	Tank->ClearControlIntent();
	TestEqual(TEXT("Control clear has no queued fire to replay"), Scope.ProjectileCount(), 1);
	Tank->SetRole(ROLE_SimulatedProxy);
	Weapon->TryFire(ForwardAim);
	TestEqual(TEXT("Non-authority cannot fire even in standalone world"), Scope.ProjectileCount(), 1);
	Tank->SetRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalTargetTest, "NetworkTankArena.LocalCombat.DamageTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalTargetTest::RunTest(const FString& Parameters)
{
	FScopedCombatWorld Scope;
	ATankDamageTestTarget* Target = Scope.Spawn<ATankDamageTestTarget>();
	const FDamageEvent Event;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		TestEqual(TEXT("Local damage accepted"), Target->TakeDamage(25.f, Event, nullptr, nullptr), 25.f);
	}
	TestEqual(TEXT("Persistent count across more than 100 damage"), Target->HitCount, 8);
	TestEqual(TEXT("Persistent accumulated damage"), Target->TotalDamage, 200.f);
	TestFalse(TEXT("Target has no death/destruction"), Target->IsActorBeingDestroyed());
	for (const float Invalid : {0.f, -25.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		TestEqual(TEXT("Invalid damage rejected"), Target->TakeDamage(Invalid, Event, nullptr, nullptr), 0.f);
	}
	Target->SetCanBeDamaged(false);
	TestEqual(TEXT("Respects damage-disabled flag"), Target->TakeDamage(25.f, Event, nullptr, nullptr), 0.f);
	TestEqual(TEXT("Rejected damage leaves accounting unchanged"), Target->HitCount, 8);
	TestTrue(TEXT("Target totals replicate, but movement does not"), Target->GetIsReplicated() && !Target->IsReplicatingMovement());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalCombatNetworkGuardTest, "NetworkTankArena.LocalCombat.NetworkGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalCombatNetworkGuardTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	for (const ENetMode Mode : {NM_ListenServer, NM_Client, NM_DedicatedServer})
	{
		FScopedCombatWorld Scope;
		ATankPawn* Tank = Scope.Spawn<ATankPawn>();
		ATankDamageTestTarget* Target = Scope.Spawn<ATankDamageTestTarget>(FVector(500.0, 0.0, 0.0));
		Tank->GetWeaponComponent()->TryFire(FVector(1000.0, 0.0, 0.0));
		ATankProjectile* Stray = *TActorIterator<ATankProjectile>(Scope.World);
		if (!TestNotNull(TEXT("Live initialized standalone projectile before transition"), Stray)) { return false; }
		Scope.World->WorldType = EWorldType::PIE;
		Scope.World->SetPlayInEditorInitialNetMode(Mode);
		TestEqual(TEXT("Exercise real world net-mode guard, without transport"), Scope.World->GetNetMode(), Mode);
		const bool bServer = Mode != NM_Client;
		TestEqual(TEXT("Server modes accept target damage; client world rejects it"),
			Target->TakeDamage(25.f, FDamageEvent{}, nullptr, Tank), bServer ? 25.f : 0.f);
		FHitResult Hit(Target, CastChecked<UPrimitiveComponent>(Target->GetRootComponent()), Target->GetActorLocation(), -FVector::ForwardVector);
		ATankProjectile::ApplyAuthoritativeHit(Hit, FVector::ForwardVector, Tank, Stray);
		TestEqual(TEXT("Only server modes dispatch point damage"), Target->HitCount, bServer ? 2 : 0);
		const FVector Before = Stray->GetActorLocation();
		Stray->Tick(0.1f);
		TestEqual(TEXT("Client guard does not locally destroy a replicated projectile"), Stray->IsActorBeingDestroyed(), false);
		TestEqual(TEXT("Only server modes simulate projectile movement"), Stray->GetActorLocation().Equals(Before), !bServer);
		Scope.World->Tick(LEVELTICK_All, 0.31f);
		TestEqual(TEXT("Only server modes can spawn a new shot"),
			Tank->GetWeaponComponent()->TryFire(FVector(0.0, 1000.0, 0.0)), bServer);
		Scope.World->WorldType = EWorldType::Game;
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalCombatContentTest, "NetworkTankArena.LocalCombat.ContentDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalCombatContentTest::RunTest(const FString& Parameters)
{
	const auto* Mode = GetDefault<ATankGameMode>();
	const auto* Tank = Cast<ATankPawn>(Mode->DefaultPawnClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Existing tank Blueprint loads"), Tank)) { return false; }
	const auto* Weapon = Tank->GetWeaponComponent();
	if (!TestNotNull(TEXT("Blueprint inherits native weapon"), Weapon)) { return false; }
	TestFalse(TEXT("Weapon remains non-replicated"), Weapon->GetIsReplicated());
	TestEqual(TEXT("Fixed gameplay turret height"), Weapon->GameplayTurretHeight, -25.f);
	TestEqual(TEXT("Fixed gameplay muzzle distance"), Weapon->GameplayMuzzleDistance, 110.f);
	const auto* Controller = Cast<ATankPlayerController>(Mode->PlayerControllerClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Existing input Blueprint loads"), Controller)) { return false; }
	if (TestNotNull(TEXT("Fire action asset assigned"), Controller->FireAction.Get()))
	{
		TestEqual(TEXT("Fire action is Boolean"), Controller->FireAction->ValueType, EInputActionValueType::Boolean);
		TestTrue(TEXT("Ordinary fire action has no triggers/modifiers"), Controller->FireAction->Triggers.IsEmpty() && Controller->FireAction->Modifiers.IsEmpty());
		TestFalse(TEXT("Fire does not trigger while paused"), Controller->FireAction->bTriggerWhenPaused);
	}
	if (TestNotNull(TEXT("Input mapping context assigned"), Controller->TankMappingContext.Get()))
	{
		int32 FireMappings = 0;
		for (const FEnhancedActionKeyMapping& Mapping : Controller->TankMappingContext->GetMappings())
		{
			if (Mapping.Action == Controller->FireAction)
			{
				++FireMappings;
				TestTrue(TEXT("Fire maps Left Mouse Button"), Mapping.Key == EKeys::LeftMouseButton);
				TestTrue(TEXT("Fire mapping has no triggers/modifiers"), Mapping.Triggers.IsEmpty() && Mapping.Modifiers.IsEmpty());
			}
		}
		TestEqual(TEXT("Exactly one fire mapping"), FireMappings, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankLocalCombatFixtureTest, "NetworkTankArena.LocalCombat.TargetFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankLocalCombatFixtureTest::RunTest(const FString& Parameters)
{
	for (const bool bPlacedTarget : {false, true})
	{
		FScopedCombatWorld Scope;
		ATankGameMode* Mode = Scope.World->SpawnActor<ATankGameMode>();
		Mode->GameState = Scope.World->SpawnActor<AGameStateBase>();
		Scope.World->SetGameState(Mode->GameState);
		ATankDamageTestTarget* Placed = bPlacedTarget
			? Scope.Spawn<ATankDamageTestTarget>(FVector(1000.0, 0.0, 100.0)) : nullptr;
		Mode->StartPlay();
		int32 Count = 0;
		for (TActorIterator<ATankDamageTestTarget> It(Scope.World); It; ++It)
		{
			++Count;
			TestTrue(TEXT("Standalone keeps placed target or creates default fixture"), bPlacedTarget
				? *It == Placed : It->GetActorLocation().Equals(FVector(0.0, 500.0, 100.0)));
		}
		TestEqual(TEXT("Exactly one local target"), Count, 1);
	}
#if WITH_EDITOR
	for (const ENetMode NetMode : {NM_ListenServer, NM_Client, NM_DedicatedServer})
	{
		FScopedCombatWorld Scope;
		ATankGameMode* Mode = Scope.World->SpawnActor<ATankGameMode>();
		Mode->GameState = Scope.World->SpawnActor<AGameStateBase>();
		Scope.World->SetGameState(Mode->GameState);
		Scope.World->WorldType = EWorldType::PIE;
		Scope.World->SetPlayInEditorInitialNetMode(NetMode);
		Mode->StartPlay();
		TestEqual(TEXT("Only server worlds create the automatic target"),
			!!TActorIterator<ATankDamageTestTarget>(Scope.World), NetMode != NM_Client);
		Scope.World->WorldType = EWorldType::Game;
	}
#endif
	return true;
}

#endif
