#if WITH_DEV_AUTOMATION_TESTS

#include "../TankGameMode.h"
#include "../TankMovementComponent.h"
#include "../TankPawn.h"
#include "../TankPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include <limits>

namespace
{
	// Real Unreal actors/components in a transient world. No simulated transport layer.
	struct FScopedTankTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		~FScopedTankTestWorld() { World->DestroyWorld(false); }

		ATankPawn* SpawnTank(const FVector& Location)
		{
			ATankPawn* Tank = World->SpawnActor<ATankPawn>(Location, FRotator::ZeroRotator);
			Tank->DispatchBeginPlay();
			return Tank;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerAuthorityTest, "NetworkTankArena.Multiplayer.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerAuthorityTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	ATankPawn* Tank = Scope.SpawnTank(FVector(0.0, 0.0, 100.0));
	ATankPawn* Other = Scope.SpawnTank(FVector(1000.0, 0.0, 100.0));
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	Tank->SetAutonomousProxy(true); // Possession must deliberately undo autonomous promotion.
	Controller->Possess(Tank);
	TestTrue(TEXT("Actor and movement replication enabled"), Tank->GetIsReplicated() && Tank->IsReplicatingMovement());
	TestEqual(TEXT("Client pawn role receives ordinary FRepMovement, including owner"), Tank->GetRemoteRole(), ROLE_SimulatedProxy);

	FTankControlIntent Intent;
	Intent.Throttle = 4.f;
	Intent.Turn = std::numeric_limits<float>::infinity();
	Intent.bHasAimWorldPoint = true;
	Intent.AimWorldPoint = FVector(0.0, 1000.0, 100.0);
	Controller->AcceptControlIntent(Other, 100u, Intent);
	TestEqual(TEXT("Cannot submit for another tank"), Other->CurrentIntent.Throttle, 0.f);
	Controller->AcceptControlIntent(nullptr, 100u, Intent);
	Tank->SetRole(ROLE_SimulatedProxy);
	Controller->AcceptControlIntent(Tank, 100u, Intent);
	Tank->SetRole(ROLE_Authority);
	// SetPawn alone does not possess Other: exercise the target's controller check.
	Controller->SetPawn(Other);
	Controller->AcceptControlIntent(Other, 100u, Intent);
	Controller->SetPawn(Tank);
	TestFalse(TEXT("Invalid target/authority/controller cannot establish sequence history"),
		Controller->AcceptedMovementIntentSequence.bHasAcceptedSequence);
	Controller->AcceptControlIntent(Tank, 0u, Intent);
	TestEqual(TEXT("Authority clamps incoming throttle"), Tank->CurrentIntent.Throttle, 1.f);
	TestEqual(TEXT("Authority neutralizes non-finite turn"), Tank->CurrentIntent.Turn, 0.f);
	Tank->Tick(0.05f);
	TestTrue(TEXT("Server moves using existing movement component"), Tank->GetActorLocation().X > 0.0);
	TestTrue(TEXT("Server resolves canonical turret aim"), Tank->GetCanonicalTurretWorldYaw() > 90.f);
	const float AgeBeforeRejection = Tank->ControlIntentAgeSeconds;
	const FVector VelocityBeforeRejection = Tank->TankMovement->Velocity;
	ATankPlayerController* WrongController = Scope.World->SpawnActor<ATankPlayerController>();
	WrongController->SetPawn(Tank); // Target matches GetPawn(), but Tank still identifies Controller.
	WrongController->AcceptControlIntent(Tank, 100u, FTankControlIntent{});
	TestFalse(TEXT("Mismatched controller cannot consume sequence history"),
		WrongController->AcceptedMovementIntentSequence.bHasAcceptedSequence);
	Controller->AcceptControlIntent(Other, 100u, FTankControlIntent{});
	Controller->AcceptControlIntent(nullptr, 100u, FTankControlIntent{});
	Controller->AcceptControlIntent(Tank, 0u, FTankControlIntent{});
	Controller->AcceptControlIntent(Tank, std::numeric_limits<uint32>::max(), FTankControlIntent{});
	TestEqual(TEXT("Rejected submissions preserve accepted intent"), Tank->CurrentIntent.Throttle, 1.f);
	TestEqual(TEXT("Rejected submissions do not refresh timeout"), Tank->ControlIntentAgeSeconds, AgeBeforeRejection);
	TestTrue(TEXT("Rejected submissions do not change velocity"), Tank->TankMovement->Velocity.Equals(VelocityBeforeRejection));
	TestEqual(TEXT("Wrong target and stale submissions do not advance sequence history"),
		Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 0u);
	Intent.AimWorldPoint.X = std::numeric_limits<double>::quiet_NaN();
	Controller->AcceptControlIntent(Tank, 1u, Intent);
	TestFalse(TEXT("Authority rejects non-finite aim"), Tank->CurrentIntent.bHasAimWorldPoint);

	Tank->ClearControlIntent();
	const FTransform Before = Tank->GetActorTransform();
	const float YawBefore = Tank->GetCanonicalTurretWorldYaw();
	Tank->SetRole(ROLE_SimulatedProxy);
	Tank->SubmitControlIntent(Intent);
	TestEqual(TEXT("Client cannot install gameplay intent"), Tank->CurrentIntent.Throttle, 0.f);
	Tank->TankMovement->SimulateMovement(Intent, 0.1f);
	Tank->Tick(0.1f);
	TestTrue(TEXT("Client tick/component cannot author gameplay transform"), Tank->GetActorTransform().Equals(Before));
	TestEqual(TEXT("Client cannot author canonical turret yaw"), Tank->GetCanonicalTurretWorldYaw(), YawBefore);
	Tank->CanonicalTurretWorldYaw = -75.f; // Exercise presentation notification, not a transport test.
	Tank->OnRep_CanonicalTurretWorldYaw();
	TestTrue(TEXT("Replicated yaw is applied in world space"), FMath::IsNearlyEqual(
		static_cast<float>(Tank->TurretYawPivot->GetComponentRotation().Yaw), -75.f, 0.001f));
	TestTrue(TEXT("Turret/hover presentation leaves gameplay transform unchanged"), Tank->GetActorTransform().Equals(Before));
	Tank->SetRole(ROLE_Authority);
	Controller->SetRole(ROLE_SimulatedProxy);
	Controller->AcceptControlIntent(Tank, 100u, Intent);
	TestEqual(TEXT("Non-authority controller cannot accept intent"), Tank->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Non-authority controller cannot advance sequence history"),
		Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 1u);
	Controller->SetRole(ROLE_Authority);
	Tank->SubmitControlIntent(Intent);
	TestEqual(TEXT("Direct authoritative producers remain independent of controller sequencing"), Tank->CurrentIntent.Throttle, 1.f);
	TestEqual(TEXT("Direct producer does not consume controller sequence history"),
		Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 1u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerLifecycleTest, "NetworkTankArena.Multiplayer.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerLifecycleTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	ATankPawn* First = Scope.SpawnTank(FVector(0.0, 0.0, 100.0));
	ATankPawn* Second = Scope.SpawnTank(FVector(1000.0, 0.0, 100.0));
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	// Designate a local host as GameMode normally would; no device/viewport is needed.
	Controller->SetAsLocalPlayerController();
	FTankControlIntent Intent;
	Intent.Throttle = 1.f;
	Intent.Turn = 1.f;
	Controller->Possess(First);
	Controller->SubmitLocalControlIntent(First, Intent); // Exercise the host's real allocation/direct path.
	TestTrue(TEXT("First local submission establishes history"), Controller->AcceptedMovementIntentSequence.bHasAcceptedSequence);
	TestEqual(TEXT("First local sequence is zero"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 0u);
	TestEqual(TEXT("Local submission allocates one sequence"), Controller->NextMovementIntentSequence, 1u);
	First->Tick(0.25f);
	TestTrue(TEXT("Most recent intent is held between updates"), First->CurrentIntent.Throttle == 1.f);
	TestFalse(TEXT("Active intent produces velocity"), First->TankMovement->Velocity.IsZero());
	const FVector MovingPosition = First->GetActorLocation();
	Controller->AcceptControlIntent(First, 0u, Intent);
	First->Tick(0.25f); // The duplicate must not postpone the original 0.5-second timeout.
	TestEqual(TEXT("Missing heartbeat clears throttle"), First->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Missing heartbeat clears turn"), First->CurrentIntent.Turn, 0.f);
	TestTrue(TEXT("Expired intent cannot keep moving the tank"), First->GetActorLocation().Equals(MovingPosition));
	TestTrue(TEXT("Timeout clears velocity"), First->TankMovement->Velocity.IsZero());
	Controller->AcceptControlIntent(First, 0u, Intent);
	TestEqual(TEXT("Timeout does not make an old sequence valid"), First->CurrentIntent.Throttle, 0.f);
	Controller->SubmitLocalControlIntent(First, FTankControlIntent{});
	TestEqual(TEXT("Neutral snapshot advances sequence normally"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 1u);
	TestEqual(TEXT("Neutral snapshot allocates a sequence"), Controller->NextMovementIntentSequence, 2u);
	TestEqual(TEXT("Neutral snapshot is retained"), First->CurrentIntent.Throttle, 0.f);
	Controller->SubmitLocalControlIntent(First, Intent);
	First->Restart();
	TestEqual(TEXT("Restart clears authoritative intent"), First->CurrentIntent.Throttle, 0.f);
	Controller->AcceptControlIntent(First, 1u, Intent);
	Controller->AcceptControlIntent(First, 2u, Intent);
	TestEqual(TEXT("Same-pawn restart rejects older and duplicate input"), First->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Restart preserves sequence history"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 2u);
	Controller->CachedThrottle = 1.f;
	Controller->CachedTurn = -1.f;
	Controller->AcknowledgePossession(First);
	TestEqual(TEXT("Possession acknowledgement clears cached throttle"), Controller->CachedThrottle, 0.f);
	TestEqual(TEXT("Possession acknowledgement clears cached turn"), Controller->CachedTurn, 0.f);
	TestEqual(TEXT("Acknowledgement preserves next sequence"), Controller->NextMovementIntentSequence, 3u);
	TestEqual(TEXT("Acknowledgement preserves accepted sequence"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 2u);
	Controller->CachedThrottle = 1.f;
	Controller->CachedTurn = -1.f;
	Controller->SecondsUntilInputSend = 0.04f;
	Controller->SubmitLocalControlIntent(First, Intent);
	Controller->FlushPressedKeys();
	TestEqual(TEXT("Flush clears cached throttle"), Controller->CachedThrottle, 0.f);
	TestEqual(TEXT("Flush clears cached turn"), Controller->CachedTurn, 0.f);
	TestEqual(TEXT("Flush resets send cadence"), Controller->SecondsUntilInputSend, 0.f);
	TestEqual(TEXT("Flush clears server intent"), First->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Flush preserves next sequence"), Controller->NextMovementIntentSequence, 4u);
	TestEqual(TEXT("Flush preserves accepted sequence"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 3u);
	Controller->SubmitLocalControlIntent(First, Intent);
	Controller->Possess(Second);
	TestEqual(TEXT("Unpossessed tank has neutral intent"), First->CurrentIntent.Throttle, 0.f);
	TestFalse(TEXT("Unpossessed tank drops tick controller"), First->TickController.IsValid());
	TestTrue(TEXT("New tank uses current controller as prerequisite"), Second->TickController.Get() == Controller);
	Controller->AcceptControlIntent(First, 100u, Intent);
	Controller->AcceptControlIntent(Second, 3u, Intent);
	Controller->SubmitLocalControlIntent(First, Intent);
	TestEqual(TEXT("Late submission for previous pawn is ignored"), First->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Previous pawn and older sequence cannot move new pawn"), Second->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Possession change preserves sequence history"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 4u);
	TestEqual(TEXT("Possession change preserves next sequence"), Controller->NextMovementIntentSequence, 5u);
	Controller->SubmitLocalControlIntent(Second, Intent);
	TestEqual(TEXT("Next valid snapshot continues on new pawn"), Second->CurrentIntent.Throttle, 1.f);
	TestEqual(TEXT("Wrong former target did not consume its high sequence"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 5u);
	Controller->UnPossess();
	TestEqual(TEXT("Unpossession clears intent"), Second->CurrentIntent.Throttle, 0.f);
	TestFalse(TEXT("Unpossession removes new tick prerequisite"), Second->TickController.IsValid());
	TestFalse(TEXT("Unpossession removes input context bookkeeping"), Controller->MappingSubsystem.IsValid());
	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		Controller->Possess(First);
		Controller->AcceptControlIntent(First, 4u, Intent);
		TestEqual(TEXT("Reusing pawn cannot revive an older sequence"), First->CurrentIntent.Throttle, 0.f);
		Controller->SubmitLocalControlIntent(First, Intent);
		TestEqual(TEXT("Repeated possession continues sequence allocation"),
			Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 6u + static_cast<uint32>(Iteration));
		Controller->UnPossess();
		TestEqual(TEXT("Repeated possession clears intent"), First->CurrentIntent.Throttle, 0.f);
		TestFalse(TEXT("Repeated possession drops old prerequisite"), First->TickController.IsValid());
	}
	// Normal login supplies this; the transient world has no GameMode/login flow.
	Controller->SetPlayerState(Scope.World->SpawnActor<APlayerState>());
	Controller->Reset();
	TestEqual(TEXT("Controller reset preserves next sequence"), Controller->NextMovementIntentSequence, 9u);
	TestEqual(TEXT("Controller reset preserves sequence history"), Controller->AcceptedMovementIntentSequence.LastAcceptedSequence, 8u);
	ATankPlayerController* NewController = Scope.World->SpawnActor<ATankPlayerController>();
	NewController->SetAsLocalPlayerController();
	NewController->Possess(Second);
	NewController->SubmitLocalControlIntent(Second, FTankControlIntent{});
	TestTrue(TEXT("New controller establishes fresh history"), NewController->AcceptedMovementIntentSequence.bHasAcceptedSequence);
	TestEqual(TEXT("New controller may start at zero on a reused pawn"), NewController->AcceptedMovementIntentSequence.LastAcceptedSequence, 0u);
	TestEqual(TEXT("New controller allocates independently"), NewController->NextMovementIntentSequence, 1u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerContentTest, "NetworkTankArena.Multiplayer.ContentDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerContentTest::RunTest(const FString& Parameters)
{
	const ATankGameMode* Mode = GetDefault<ATankGameMode>();
	TestEqual(TEXT("GameMode reuses visual Blueprint"), Mode->DefaultPawnClass->GetPathName(), FString(TEXT("/Game/Blueprints/BP_Tank.BP_Tank_C")));
	TestEqual(TEXT("GameMode reuses input Blueprint"), Mode->PlayerControllerClass->GetPathName(), FString(TEXT("/Game/Blueprints/BP_TankPlayerController.BP_TankPlayerController_C")));
	const ATankPawn* Tank = Cast<ATankPawn>(Mode->DefaultPawnClass->GetDefaultObject());
	TestTrue(TEXT("Blueprint inherits replication defaults"), Tank && Tank->GetIsReplicated() && Tank->IsReplicatingMovement());
	TestTrue(TEXT("Blueprint defers spawning/possession to GameMode"), Tank && Tank->AutoPossessPlayer == EAutoReceiveInput::Disabled && Tank->AutoPossessAI == EAutoPossessAI::Disabled);
	TestTrue(TEXT("Visual hierarchy remains unreplicated"), Tank && !Tank->VisualRoot->GetIsReplicated() && !Tank->ChassisVisualPivot->GetIsReplicated() && !Tank->TurretYawPivot->GetIsReplicated());
	TestTrue(TEXT("Camera remains orthographic beneath the gameplay root"), Tank && Tank->FollowCamera->ProjectionMode == ECameraProjectionMode::Orthographic && Tank->CameraBoom->GetAttachParent() == Tank->GetRootComponent() && Tank->CameraBoom->IsUsingAbsoluteRotation());
	const ATankPlayerController* Controller = Cast<ATankPlayerController>(Mode->PlayerControllerClass->GetDefaultObject());
	if (TestNotNull(TEXT("Controller Blueprint derives from native tank controller"), Controller))
	{
		TestTrue(TEXT("Throttle action is assigned as Axis1D"), Controller->ThrottleAction && Controller->ThrottleAction->ValueType == EInputActionValueType::Axis1D);
		TestTrue(TEXT("Turn action is assigned as Axis1D"), Controller->TurnAction && Controller->TurnAction->ValueType == EInputActionValueType::Axis1D);
		TestNotNull(TEXT("Tank mapping context is assigned"), Controller->TankMappingContext.Get());
		TestTrue(TEXT("Local possession manages the view target"), Controller->bAutoManageActiveCameraTarget);
	}
	FString DefaultMode;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), DefaultMode, GEngineIni);
	TestEqual(TEXT("Project selects native GameMode"), DefaultMode, FString(TEXT("/Script/NetworkTankArena.TankGameMode")));
	UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/TestMap.TestMap"));
	if (!TestNotNull(TEXT("TestMap loads"), Map))
	{
		return false;
	}
	int32 Starts = 0;
	int32 Tanks = 0;
	for (AActor* Actor : Map->PersistentLevel->Actors)
	{
		Starts += Actor && Actor->IsA<APlayerStart>() ? 1 : 0;
		Tanks += Actor && Actor->IsA<ATankPawn>() ? 1 : 0;
	}
	TestEqual(TEXT("TestMap contains no preplaced duplicate tanks"), Tanks, 0);
	const TSubclassOf<AGameModeBase> MapMode = Map->GetWorldSettings()->DefaultGameMode;
	TestTrue(TEXT("TestMap allows native project GameMode"), !MapMode || MapMode->IsChildOf(ATankGameMode::StaticClass()));
	TestTrue(TEXT("TestMap contains at least two PlayerStarts"), Starts >= 2);
	return true;
}

#endif
