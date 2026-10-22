#include "TankPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"
#include "TankGameplayMath.h"
#include "TankMovementComponent.h"
#include "TankWeaponComponent.h"

ATankPawn::ATankPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(30.f);
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CollisionRoot = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionRoot"));
	SetRootComponent(CollisionRoot);
	CollisionRoot->InitCapsuleSize(90.f, 100.f);
	CollisionRoot->SetCollisionProfileName(TEXT("Pawn"));
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionRoot->SetSimulatePhysics(false);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(CollisionRoot);
	VisualRoot->SetRelativeLocation(FVector(0.0, 0.0, -65.0));
	ChassisVisualPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ChassisVisualPivot"));
	ChassisVisualPivot->SetupAttachment(VisualRoot);
	ChassisMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChassisMesh"));
	ChassisMesh->SetupAttachment(ChassisVisualPivot);
	TurretYawPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretYawPivot"));
	TurretYawPivot->SetupAttachment(VisualRoot);
	TurretYawPivot->SetRelativeLocation(FVector(0.0, 0.0, 40.0));
	TurretMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TurretMesh"));
	TurretMesh->SetupAttachment(TurretYawPivot);
	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(TurretYawPivot);
	BarrelMesh->SetRelativeLocation(FVector(70.0, 0.0, 0.0));
	for (UStaticMeshComponent* Mesh : {ChassisMesh.Get(), TurretMesh.Get(), BarrelMesh.Get()})
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCanEverAffectNavigation(false);
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionRoot);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-60.0, 45.0, 0.0));
	CameraBoom->TargetArmLength = 1500.f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;
	CameraBoom->AddTickPrerequisiteActor(this);
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->ProjectionMode = ECameraProjectionMode::Orthographic;
	FollowCamera->OrthoWidth = 2400.f;
	FollowCamera->bUsePawnControlRotation = false;

	TankMovement = CreateDefaultSubobject<UTankMovementComponent>(TEXT("TankMovement"));
	TankMovement->SetUpdatedComponent(CollisionRoot);
	TankWeapon = CreateDefaultSubobject<UTankWeaponComponent>(TEXT("TankWeapon"));
}

UPawnMovementComponent* ATankPawn::GetMovementComponent() const
{
	return TankMovement;
}

void ATankPawn::BeginPlay()
{
	Super::BeginPlay();
	using namespace NetworkTankArena::LocalTank;
	if (HasAuthority())
	{
		CollisionRoot->SetWorldScale3D(FVector::OneVector);
		CollisionRoot->SetWorldRotation(FRotator(0.0, NormalizeYaw(GetActorRotation().Yaw), 0.0));
		CanonicalTurretWorldYaw = ResolveTurretWorldYaw(GetActorRotation().Yaw, GetActorLocation(), FTankControlIntent{});
	}
	CollisionRoot->SetSimulatePhysics(false);
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	for (UStaticMeshComponent* Mesh : {ChassisMesh.Get(), TurretMesh.Get(), BarrelMesh.Get()})
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	// Import-axis corrections belong on meshes, not the shared yaw/bob pivots.
	VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
	TurretYawPivot->SetRelativeRotation(FRotator::ZeroRotator);
	BaseVisualTransform = VisualRoot->GetRelativeTransform();
	BaseChassisTransform = ChassisVisualPivot->GetRelativeTransform();
	PredictedTurretWorldYaw = CanonicalTurretWorldYaw;
	RemoteSmoothedTurretYaw = CanonicalTurretWorldYaw;
	RemoteSmoothedLocation = GetActorLocation();
	RemoteSmoothedChassisYaw = GetActorRotation().Yaw;
	TankMovement->SetUpdatedComponent(CollisionRoot);
	TankMovement->InitializeGameplayPlane();
	PreviousPresentationYaw = NormalizeYaw(GetActorRotation().Yaw);
	UpdateControllerTickPrerequisite();
	ApplyPresentation(0.f);
}

void ATankPawn::SubmitControlIntent(const FTankControlIntent& Intent)
{
	if (!HasAuthority() && !IsLocallyControlled())
	{
		return;
	}
	CurrentIntent = NetworkTankArena::LocalTank::SanitizeIntent(Intent);
	ControlIntentAgeSeconds = 0.f;
}

void ATankPawn::ClearControlIntent()
{
	CurrentIntent = FTankControlIntent{};
	ControlIntentAgeSeconds = 0.f;
	TankMovement->Velocity = FVector::ZeroVector;
}

double ATankPawn::GetGameplayPlaneZ() const
{
	return TankMovement->GetGameplayPlaneZ();
}

void ATankPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (HasAuthority())
	{
		// Enforce authoritative heartbeat timeout
		if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
		{
			ControlIntentAgeSeconds += DeltaTime;
			if (ControlIntentAgeSeconds >= ControlIntentTimeoutSeconds)
			{
				ClearControlIntent();
			}
		}

		// Only simulate in Tick if this tank is locally controlled on the server
		if (!bIsRemoteControlled)
		{
			TankMovement->SimulateMovement(CurrentIntent, DeltaTime);
			if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
			{
				CanonicalTurretWorldYaw = NetworkTankArena::LocalTank::ResolveTurretWorldYaw(
					CanonicalTurretWorldYaw, GetActorLocation(), CurrentIntent);
			}
		}
	}

	ApplyPresentation(DeltaTime);
}

void ATankPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATankPawn, CanonicalTurretWorldYaw);
}


void ATankPawn::OnRep_CanonicalTurretWorldYaw()
{
	// Initial replication can notify before BeginPlay captures Blueprint visual offsets.
	if (HasActorBegunPlay())
	{
		ApplyPresentation(0.f);
	}
}

void ATankPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// Allow remote owning clients to be Autonomous Proxies so they can predict movement locally.
	// Remote observers will still remain Simulated Proxies.
	SetAutonomousProxy(true);
	ClearControlIntent();
}

void ATankPawn::Restart()
{
	Super::Restart();
	ClearControlIntent();
	UpdateControllerTickPrerequisite();
}

void ATankPawn::ApplyPresentation(float DeltaTime)
{
	using namespace NetworkTankArena::LocalTank;
	const bool bValidDelta = IsValidDeltaTime(DeltaTime);

	if (bValidDelta)
	{
		HoverPhase = FMath::Fmod(HoverPhase + 2.0 * UE_PI * NonNegativeFinite(HoverFrequencyHz) * DeltaTime, 2.0 * UE_PI);
		const float TurnRate = NonNegativeFinite(TankMovement->GetTurnRateDegreesPerSecond());
		const float ObservedTurn = TurnRate > UE_SMALL_NUMBER
			? SanitizeAxis(FMath::FindDeltaAngleDegrees(PreviousPresentationYaw, GetActorRotation().Yaw) / (DeltaTime * TurnRate)) : 0.f;
		const float TargetBank = -ObservedTurn * FMath::Min(NonNegativeFinite(MaxBankDegrees), 15.f);
		CurrentBankDegrees = FMath::FInterpTo(CurrentBankDegrees, TargetBank, DeltaTime, NonNegativeFinite(BankInterpSpeed));
		PreviousPresentationYaw = NormalizeYaw(GetActorRotation().Yaw);

		VisualCorrectionOffset = FMath::VInterpTo(VisualCorrectionOffset, FVector::ZeroVector, DeltaTime, VisualSmoothingInterpSpeed);

		if (!IsLocallyControlled())
		{
			const float DeltaAngle = FMath::FindDeltaAngleDegrees(RemoteSmoothedTurretYaw, CanonicalTurretWorldYaw);
			RemoteSmoothedTurretYaw = FRotator::NormalizeAxis(FMath::FInterpTo(RemoteSmoothedTurretYaw, RemoteSmoothedTurretYaw + DeltaAngle, DeltaTime, 20.f));
		}
	}
	else
	{
		// DeltaTime <= 0.f (called directly from OnRep_CanonicalTurretWorldYaw or BeginPlay):
		// Immediately snap presentation to the authoritative replicated state
		if (!IsLocallyControlled())
		{
			RemoteSmoothedTurretYaw = CanonicalTurretWorldYaw;
			RemoteSmoothedLocation = GetActorLocation();
			RemoteSmoothedChassisYaw = GetActorRotation().Yaw;
		}
	}

	const double Bob = FMath::Sin(HoverPhase) * NonNegativeFinite(HoverAmplitude);

	// Render-time entity interpolation for any tank not controlled on this machine
	if (!IsLocallyControlled())
	{
		// Guard against huge teleports on spawn/respawn
		if (FVector::DistSquared(RemoteSmoothedLocation, GetActorLocation()) > 40000.f)
		{
			RemoteSmoothedLocation = GetActorLocation();
			RemoteSmoothedChassisYaw = GetActorRotation().Yaw;
		}
		else if (bValidDelta)
		{
			RemoteSmoothedLocation = FMath::VInterpTo(RemoteSmoothedLocation, GetActorLocation(), DeltaTime, 15.f);

			const float DeltaChassis = FMath::FindDeltaAngleDegrees(RemoteSmoothedChassisYaw, GetActorRotation().Yaw);
			RemoteSmoothedChassisYaw = FRotator::NormalizeAxis(FMath::FInterpTo(RemoteSmoothedChassisYaw, RemoteSmoothedChassisYaw + DeltaChassis, DeltaTime, 15.f));
		}

		VisualRoot->SetWorldLocation(RemoteSmoothedLocation + FVector(0.0, 0.0, -65.0 + Bob));
		VisualRoot->SetWorldRotation(FRotator(0.0, RemoteSmoothedChassisYaw, 0.0));
	}
	else
	{
		VisualRoot->SetRelativeLocation(BaseVisualTransform.GetLocation() + FVector(0.0, 0.0, Bob) + VisualCorrectionOffset);
		VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
	}

	ChassisVisualPivot->SetRelativeRotation(BaseChassisTransform.Rotator() + FRotator(0.0, 0.0, CurrentBankDegrees));

	// Turret aim selection
	float ActiveTurretYaw;
	if (IsLocallyControlled())
	{
		ActiveTurretYaw = HasAuthority() ? CanonicalTurretWorldYaw : PredictedTurretWorldYaw;
	}
	else
	{
		ActiveTurretYaw = RemoteSmoothedTurretYaw;
	}

	const float CurrentChassisYaw = !IsLocallyControlled() ? RemoteSmoothedChassisYaw : GetActorRotation().Yaw;
	TurretYawPivot->SetRelativeRotation(FRotator(0.0,
		CalculateRelativeTurretYaw(CurrentChassisYaw, ActiveTurretYaw), 0.0));
}

void ATankPawn::UpdateControllerTickPrerequisite()
{
	if (AController* Previous = TickController.Get())
	{
		RemoveTickPrerequisiteActor(Previous);
	}
	TickController = GetController();
	if (AController* Current = TickController.Get())
	{
		// Only controller -> pawn -> camera. Movement has no independent tick.
		AddTickPrerequisiteActor(Current);
	}
}

void ATankPawn::NotifyControllerChanged()
{
	ClearControlIntent();
	UpdateControllerTickPrerequisite();
	Super::NotifyControllerChanged();
}

void ATankPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearControlIntent();
	if (AController* Previous = TickController.Get())
	{
		RemoveTickPrerequisiteActor(Previous);
	}
	TickController.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATankPawn::ExecuteAuthoritativeMove(float DeltaTime, const FTankControlIntent& Intent)
{
	if (!HasAuthority())
	{
		return;
	}

	bIsRemoteControlled = true;

	SubmitControlIntent(Intent);
	TankMovement->SimulateMovement(CurrentIntent, DeltaTime);

	if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
	{
		CanonicalTurretWorldYaw = NetworkTankArena::LocalTank::ResolveTurretWorldYaw(
			CanonicalTurretWorldYaw, GetActorLocation(), CurrentIntent);
	}
}

void ATankPawn::PredictLocalMove(uint32 Sequence, float DeltaTime, const FTankControlIntent& Intent)
{
	if (!IsLocallyControlled() || HasAuthority())
	{
		return;
	}

	CurrentIntent = NetworkTankArena::LocalTank::SanitizeIntent(Intent);

	// Simulate the movement locally
	TankMovement->SimulateMovement(CurrentIntent, DeltaTime);

	// Predict local turret yaw
	if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
	{
		PredictedTurretWorldYaw = NetworkTankArena::LocalTank::ResolveTurretWorldYaw(
			PredictedTurretWorldYaw, GetActorLocation(), CurrentIntent);
	}

	// Record move in the prediction buffer
	FTankMoveRecord Record;
	Record.Sequence = Sequence;
	Record.DeltaTime = DeltaTime;
	Record.Intent = CurrentIntent;
	Record.ResultLocation = GetActorLocation();
	Record.ResultRotation = GetActorRotation();

	SavedMoves.Add(Record);

	// Safety cap to prevent memory costs if connection drops
	if (SavedMoves.Num() > MaxSavedMovesCapacity)
	{
		SavedMoves.RemoveAt(0, SavedMoves.Num() - MaxSavedMovesCapacity);
	}
}

void ATankPawn::GetRecentMoves(int32 MaxCount, TArray<FTankMoveRecord>& OutMoves) const
{
	OutMoves.Reset();
	const int32 Count = FMath::Clamp(MaxCount, 1, SavedMoves.Num());
	const int32 StartIndex = SavedMoves.Num() - Count;
	for (int32 i = StartIndex; i < SavedMoves.Num(); ++i)
	{
		OutMoves.Add(SavedMoves[i]);
	}
}

void ATankPawn::ReconcileServerMove(const FTankServerMoveAck& Ack)
{
	if (!IsLocallyControlled() || HasAuthority())
	{
		return;
	}

	// Discard all moves acknowledged up to AckSequence
	int32 AckIndex = INDEX_NONE;
	for (int32 i = 0; i < SavedMoves.Num(); ++i)
	{
		if (SavedMoves[i].Sequence == Ack.AckSequence)
		{
			AckIndex = i;
			break;
		}
		if (SavedMoves[i].Sequence > Ack.AckSequence)
		{
			// The acked move was dropped or already discarded
			break;
		}
	}

	if (AckIndex == INDEX_NONE)
	{
		// Move already cleared or out of sequence
		return;
	}

	const FTankMoveRecord& AckedMove = SavedMoves[AckIndex];

	// Calculate error between client predicted result and server authoritative truth
	const float PositionError = FVector::Dist(AckedMove.ResultLocation, Ack.Location);
	const float YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(AckedMove.ResultRotation.Yaw, Ack.Rotation.Yaw));

	if (PositionError > MaxReconciliationError)
	{
		MaxReconciliationError = PositionError;
	}

	const bool bRequiresReconciliation = (PositionError > PositionCorrectionThreshold) || (YawError > YawCorrectionThreshold);

	if (bRequiresReconciliation)
	{
		ReconciliationCount++;
		UE_LOG(LogTemp, Verbose, TEXT("[Prediction] Reconciliation #%d! PosError: %.2f cm, YawError: %.2f deg"),
			ReconciliationCount, PositionError, YawError);

		// Record the client's position at the current time before correction
		const FVector PreCorrectionLocation = GetActorLocation();

		// Rewind physics state to server truth
		SetActorLocation(Ack.Location, false, nullptr, ETeleportType::TeleportPhysics);
		SetActorRotation(Ack.Rotation, ETeleportType::TeleportPhysics);
		TankMovement->Velocity = Ack.Velocity;

		// Replay all remaining unacknowledged moves forward to current time
		for (int32 i = AckIndex + 1; i < SavedMoves.Num(); ++i)
		{
			FTankMoveRecord& PendingMove = SavedMoves[i];
			TankMovement->SimulateMovement(PendingMove.Intent, PendingMove.DeltaTime);
			PendingMove.ResultLocation = GetActorLocation();
			PendingMove.ResultRotation = GetActorRotation();
		}

		// The true visual offset is the difference between old prediction and new replayed position!
		const FVector PostReplayLocation = GetActorLocation();
		VisualCorrectionOffset = (PreCorrectionLocation - PostReplayLocation);
		VisualCorrectionOffset = VisualCorrectionOffset.GetClampedToMaxSize(30.f);
	}

	// Remove all moves through AckIndex from the buffer
	SavedMoves.RemoveAt(0, AckIndex + 1);
}