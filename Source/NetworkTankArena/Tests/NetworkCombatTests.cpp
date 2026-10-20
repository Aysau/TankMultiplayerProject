#if WITH_DEV_AUTOMATION_TESTS

#include "../TankCombat.h"
#include "../TankDamageTestTarget.h"
#include "../TankPawn.h"
#include "../TankPlayerController.h"
#include "../TankProjectile.h"
#include "../TankWeaponComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/EngineBaseTypes.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
	// Actor authority/logic fixtures only: these tests do not establish RPC transport.
	struct FScopedNetworkCombatWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FScopedNetworkCombatWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			// Destroy routes EndPlay only for initialized actors. DispatchBeginPlay alone
			// does not initialize an actor spawned in an uninitialized transient world.
			World->InitializeActorsForPlay(FURL());
		}
		~FScopedNetworkCombatWorld()
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
	FTankFireRequest Request(uint32 Sequence, const FVector& Aim = FVector(1000.0, 0.0, 100.0))
	{
		FTankFireRequest Result;
		Result.RequestSequence = Sequence;
		Result.AimWorldPoint = Aim;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankNetworkFireAimTest, "NetworkTankArena.NetworkCombat.AimValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankNetworkFireAimTest::RunTest(const FString& Parameters)
{
	using namespace NetworkTankArena::LocalCombat;
	float Yaw = -45.f;
	TestTrue(TEXT("Inclusive 100000 cm bound"), TryCalculateFireYaw(FVector::ZeroVector, FVector(0.0, MaxAimDistance, 0.0), Yaw));
	TestEqual(TEXT("World-space north aim"), Yaw, 90.f);
	TestTrue(TEXT("Z does not tilt the shot"), TryCalculateFireYaw(FVector(20.0, 30.0, 100.0), FVector(1020.0, 30.0, 500.0), Yaw));
	TestEqual(TEXT("Horizontal east aim"), Yaw, 0.f);
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	for (const FVector& Invalid : {FVector(NaN, 0.0, 0.0), FVector(0.0, Inf, 0.0), FVector(1.0, 0.0, -Inf),
		FVector(MaxAimDistance + 1.0, 0.0, 0.0), FVector(80000.0, 80000.0, 0.0),
		FVector(1.e308, 0.0, 0.0), FVector::ZeroVector, FVector(0.0, 0.0, 100.0)})
	{
		TestFalse(TEXT("Invalid/too-far/degenerate aim rejected"), TryCalculateFireYaw(FVector::ZeroVector, Invalid, Yaw));
	}
	TestFalse(TEXT("Subtraction overflow rejected"), TryCalculateFireYaw(FVector(-1.e308, 0.0, 0.0), FVector(1.e308, 0.0, 0.0), Yaw));
	TestFalse(TEXT("Invalid server position rejected"), TryCalculateFireYaw(FVector(0.0, 0.0, NaN), FVector(1000.0, 0.0, 0.0), Yaw));

	NetworkTankArena::FInputSequenceState Movement;
	NetworkTankArena::FInputSequenceState Fire;
	TestTrue(TEXT("Movement stream accepts its first sequence"), Movement.TryAccept(100));
	TestTrue(TEXT("Independent fire stream starts at zero"), Fire.TryAccept(0));
	TestFalse(TEXT("Fire duplicate rejected"), Fire.TryAccept(0));
	TestTrue(TEXT("Fire acceptance does not consume movement sequence"), Movement.TryAccept(101));
	NetworkTankArena::FInputSequenceState WrappedFire;
	TestTrue(TEXT("Fire first sequence can be maximum"), WrappedFire.TryAccept(MAX_uint32));
	TestTrue(TEXT("Fire wraps to zero"), WrappedFire.TryAccept(0));
	TestFalse(TEXT("Pre-wrap fire is stale"), WrappedFire.TryAccept(MAX_uint32));
	TestFalse(TEXT("Half-range fire is ambiguous"), WrappedFire.TryAccept(0x80000000u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankNetworkFireRequestTest, "NetworkTankArena.NetworkCombat.FireRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankNetworkFireRequestTest::RunTest(const FString& Parameters)
{
	FScopedNetworkCombatWorld Scope;
	auto* Tank = Scope.Spawn<ATankPawn>(FVector(0.0, 0.0, 100.0));
	auto* Other = Scope.Spawn<ATankPawn>(FVector(0.0, -3000.0, 100.0));
	auto* Controller = Scope.Spawn<ATankPlayerController>();
	Controller->Possess(Tank);
	const UFunction* RPC = Controller->GetClass()->FindFunctionByName(TEXT("ServerRequestFire"));
	if (TestNotNull(TEXT("Reflected fire RPC"), RPC))
	{
		TestTrue(TEXT("Fire RPC is Server and Reliable"), RPC->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
	}
	int32 Fields = 0;
	for (TFieldIterator<FProperty> It(FTankFireRequest::StaticStruct()); It; ++It) { ++Fields; }
	TestEqual(TEXT("Fire payload has only sequence and aim"), Fields, 2);
	Controller->AcceptFireRequest(nullptr, Request(100));
	Controller->AcceptFireRequest(Other, Request(100));
	Tank->SetOwner(Other);
	Controller->AcceptFireRequest(Tank, Request(100));
	Tank->SetOwner(Controller);
	Tank->SetRole(ROLE_SimulatedProxy);
	Controller->AcceptFireRequest(Tank, Request(100));
	Tank->SetRole(ROLE_Authority);
	Controller->SetRole(ROLE_SimulatedProxy);
	Controller->AcceptFireRequest(Tank, Request(100));
	Controller->SetRole(ROLE_Authority);
	auto* WrongController = Scope.Spawn<ATankPlayerController>();
	WrongController->SetPawn(Tank);
	WrongController->AcceptFireRequest(Tank, Request(100));
	TestFalse(TEXT("Controller mismatch cannot establish its fire history"), WrongController->AcceptedFireRequestSequence.bHasAcceptedSequence);
	TestFalse(TEXT("Invalid identity/authority cannot consume history"), Controller->AcceptedFireRequestSequence.bHasAcceptedSequence);
	TestEqual(TEXT("Invalid requests spawn nothing"), Scope.ProjectileCount(), 0);

	FTankControlIntent ContinuousAim;
	ContinuousAim.bHasAimWorldPoint = true;
	ContinuousAim.AimWorldPoint = FVector(0.0, 1000.0, 100.0);
	Controller->AcceptControlIntent(Tank, 77, ContinuousAim);
	Tank->Tick(0.01f);
	const float TurretBefore = Tank->GetCanonicalTurretWorldYaw();
	Controller->AcceptFireRequest(Tank, Request(0));
	TestEqual(TEXT("First request spawns immediately"), Scope.ProjectileCount(), 1);
	TestEqual(TEXT("Fire leaves canonical turret yaw unchanged"), Tank->GetCanonicalTurretWorldYaw(), TurretBefore);
	for (TActorIterator<ATankProjectile> It(Scope.World); It; ++It)
	{
		TestTrue(TEXT("Explicit shot aim overrides continuous aim for this shot only"), It->GetActorRotation().Vector().Equals(FVector::ForwardVector));
		TestTrue(TEXT("Authoritative snapshot muzzle"), It->GetActorLocation().Equals(FVector(110.0, 0.0, 75.0)));
	}
	Controller->AcceptFireRequest(Tank, Request(1)); // Cooldown rejection must still consume the ID.
	Scope.World->Tick(LEVELTICK_All, 0.31f);
	Controller->AcceptFireRequest(Tank, Request(0));
	Controller->AcceptFireRequest(Tank, Request(1));
	Controller->AcceptFireRequest(Tank, Request(MAX_uint32));
	TestEqual(TEXT("Duplicate, stale and rejected-cooldown IDs cannot replay"), Scope.ProjectileCount(), 1);
	Controller->AcceptFireRequest(Tank, Request(2, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0)));
	Controller->AcceptFireRequest(Tank, Request(2));
	TestEqual(TEXT("Invalid-aim ID cannot be reused"), Scope.ProjectileCount(), 1);
	Controller->AcceptFireRequest(Tank, Request(3));
	TestEqual(TEXT("Invalid aim did not consume cooldown"), Scope.ProjectileCount(), 2);
	TestEqual(TEXT("Independent movement history is unchanged"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 77u);
	Controller->AcceptControlIntent(Tank, 78, FTankControlIntent{});
	TestEqual(TEXT("Movement does not consume fire history"), Controller->AcceptedFireRequestSequence.LastAcceptedSequence, 3u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankNetworkFireLifecycleTest, "NetworkTankArena.NetworkCombat.FireLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankNetworkFireLifecycleTest::RunTest(const FString& Parameters)
{
	FScopedNetworkCombatWorld Scope;
	auto* First = Scope.Spawn<ATankPawn>(FVector(0.0, 0.0, 100.0));
	auto* Second = Scope.Spawn<ATankPawn>(FVector(0.0, -3000.0, 100.0));
	auto* Controller = Scope.Spawn<ATankPlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(First);
	Controller->SubmitLocalFireRequest(First, FVector(1000.0, 0.0, 100.0));
	TestEqual(TEXT("Host allocates and accepts fire zero directly"), Controller->AcceptedFireRequestSequence.LastAcceptedSequence, 0u);
	TestEqual(TEXT("Host allocates exactly once"), Controller->NextFireRequestSequence, 1u);
	First->Restart();
	Controller->FlushPressedKeys();
	Controller->AcknowledgePossession(First);
	Controller->AcceptFireRequest(First, Request(0));
	TestEqual(TEXT("Restart/flush/acknowledgement cannot replay a shot"), Scope.ProjectileCount(), 1);
	Controller->PendingFireTank = First;
	Controller->Possess(Second);
	TestFalse(TEXT("Possession cancels the pending local press"), Controller->PendingFireTank.IsValid());
	Controller->AcceptFireRequest(First, Request(100));
	Controller->SubmitLocalFireRequest(First, FVector(1000.0, 0.0, 100.0));
	TestEqual(TEXT("Former pawn cannot advance either fire counter"), Controller->NextFireRequestSequence, 1u);
	Controller->SubmitLocalFireRequest(Second, FVector(1000.0, -3000.0, 100.0));
	TestEqual(TEXT("New possession continues fire history"), Controller->AcceptedFireRequestSequence.LastAcceptedSequence, 1u);
	Controller->SetPlayerState(Scope.Spawn<APlayerState>());
	Controller->Reset();
	TestEqual(TEXT("Reset preserves allocation"), Controller->NextFireRequestSequence, 2u);
	TestEqual(TEXT("Reset preserves accepted history"), Controller->AcceptedFireRequestSequence.LastAcceptedSequence, 1u);
	Controller->Possess(Second);
	Second->Destroy();
	Controller->AcceptFireRequest(Second, Request(100));
	TestEqual(TEXT("Destroyed shooter cannot advance history"), Controller->AcceptedFireRequestSequence.LastAcceptedSequence, 1u);
	auto* NewController = Scope.Spawn<ATankPlayerController>();
	NewController->SetAsLocalPlayerController();
	NewController->Possess(First);
	Scope.World->Tick(LEVELTICK_All, 0.31f);
	NewController->SubmitLocalFireRequest(First, FVector(1000.0, 0.0, 100.0));
	TestTrue(TEXT("New controller has fresh fire history"), NewController->AcceptedFireRequestSequence.bHasAcceptedSequence);
	TestEqual(TEXT("New controller starts at zero"), NewController->AcceptedFireRequestSequence.LastAcceptedSequence, 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankNetworkProjectileTest, "NetworkTankArena.NetworkCombat.ProjectileAuthorityAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankNetworkProjectileTest::RunTest(const FString& Parameters)
{
	FScopedNetworkCombatWorld Scope;
	auto* Shooter = Scope.Spawn<ATankPawn>(FVector(0.0, 0.0, 100.0));
	auto* Target = Scope.Spawn<ATankDamageTestTarget>(FVector(1000.0, 0.0, 75.0));
	auto* Projectile = Scope.Spawn<ATankProjectile>(FVector(110.0, 0.0, 75.0));
	Projectile->Initialize(Shooter, FVector::ForwardVector);
	TestTrue(TEXT("Projectile is relevant to every course player"), Projectile->bAlwaysRelevant && !Projectile->bOnlyRelevantToOwner && !Projectile->bNetUseOwnerRelevancy);
	TestEqual(TEXT("Projectile baseline frequency"), Projectile->GetNetUpdateFrequency(), 30.f);
	const FVector Before = Projectile->GetActorLocation();
	Projectile->SetRole(ROLE_SimulatedProxy);
	Projectile->Tick(4.f);
	FHitResult Hit(Target, CastChecked<UPrimitiveComponent>(Target->GetRootComponent()), Target->GetActorLocation(), -FVector::ForwardVector);
	Projectile->ResolveHit(Hit);
	ATankProjectile::ApplyAuthoritativeHit(Hit, FVector::ForwardVector, Shooter, Projectile);
	TestTrue(TEXT("Client proxy does not simulate, expire or destroy"), Projectile->GetActorLocation().Equals(Before) && !Projectile->IsActorBeingDestroyed());
	TestEqual(TEXT("Client proxy cannot resolve damage"), Target->HitCount, 0);
	Projectile->SetRole(ROLE_Authority);
	Projectile->ResolveHit(Hit);
	Projectile->ResolveHit(Hit);
	TestEqual(TEXT("Authoritative terminal hit resolves once"), Target->HitCount, 1);
	auto* Active = Scope.Spawn<ATankProjectile>(FVector(110.0, 500.0, 75.0));
	Active->Initialize(Shooter, FVector::ForwardVector);
	auto* AlsoActive = Scope.Spawn<ATankProjectile>(FVector(110.0, -500.0, 75.0));
	AlsoActive->Initialize(Shooter, FVector::ForwardVector);
	Shooter->Destroy();
	TestTrue(TEXT("Shooter EndPlay immediately destroys all its active shots"), Active->IsActorBeingDestroyed() && AlsoActive->IsActorBeingDestroyed());
	Active->Tick(1.f);
	TestEqual(TEXT("Shooter cleanup causes no damage"), Target->HitCount, 1);
#if WITH_EDITOR
	Scope.World->WorldType = EWorldType::PIE;
	Scope.World->SetPlayInEditorInitialNetMode(NM_Client);
	auto* ClientCopy = Scope.Spawn<ATankProjectile>();
	TestFalse(TEXT("Client projectile tick disabled at BeginPlay"), ClientCopy->IsActorTickEnabled());
	ClientCopy->Tick(4.f);
	TestFalse(TEXT("Uninitialized client copy waits for server destruction"), ClientCopy->IsActorBeingDestroyed());
	Scope.World->WorldType = EWorldType::Game;
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankNetworkDamageTargetTest, "NetworkTankArena.NetworkCombat.DamageTargetState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankNetworkDamageTargetTest::RunTest(const FString& Parameters)
{
	FScopedNetworkCombatWorld Scope;
	auto* Target = Scope.Spawn<ATankDamageTestTarget>();
	TestTrue(TEXT("Static target replicates to all players"), Target->GetIsReplicated() && Target->bAlwaysRelevant && !Target->IsReplicatingMovement());
	for (const FName Name : {FName(TEXT("HitCount")), FName(TEXT("TotalDamage"))})
	{
		const FProperty* Property = Target->GetClass()->FindPropertyByName(Name);
		if (TestNotNull(TEXT("Damage result property"), Property))
		{
			TestTrue(TEXT("Result is replicated with notification"), Property->HasAllPropertyFlags(CPF_Net | CPF_RepNotify));
		}
	}
	for (int32 Index = 0; Index < 8; ++Index) { Target->TakeDamage(25.f, FDamageEvent{}, nullptr, nullptr); }
	TestEqual(TEXT("Persistent authoritative hit count"), Target->HitCount, 8);
	TestEqual(TEXT("Persistent authoritative damage"), Target->TotalDamage, 200.f);
	TestEqual(TEXT("Listen host readout updates directly"), Target->ResultText->Text.ToString(), FString(TEXT("Hits: 8\nDamage: 200")));
	Target->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("Client cannot mutate damage state"), Target->TakeDamage(25.f, FDamageEvent{}, nullptr, nullptr), 0.f);
	Target->HitCount = 12;
	Target->TotalDamage = 300.f; // Simulate received current state, not damage on the client.
	Target->OnRep_DamageResult();
	TestEqual(TEXT("RepNotify presents current totals, not an incremental hit event"), Target->ResultText->Text.ToString(), FString(TEXT("Hits: 12\nDamage: 300")));
	Target->SetRole(ROLE_Authority);
	return true;
}

#endif
