#include "TankPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Console.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "TankGameplayMath.h"
#include "TankPawn.h"
#include "TankWeaponComponent.h"
#include "UnrealClient.h"

void ATankPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyTankInputContext();
}

void ATankPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	ApplyTankInputContext();
}

void ATankPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!IsLocalController())
	{
		return;
	}
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		return;
	}
	if (ThrottleAction)
	{
		EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ATankPlayerController::UpdateThrottle);
		EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ATankPlayerController::ClearThrottle);
		EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Canceled, this, &ATankPlayerController::ClearThrottle);
	}
	if (TurnAction)
	{
		EnhancedInput->BindAction(TurnAction, ETriggerEvent::Triggered, this, &ATankPlayerController::UpdateTurn);
		EnhancedInput->BindAction(TurnAction, ETriggerEvent::Completed, this, &ATankPlayerController::ClearTurn);
		EnhancedInput->BindAction(TurnAction, ETriggerEvent::Canceled, this, &ATankPlayerController::ClearTurn);
	}
	if (FireAction)
	{
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &ATankPlayerController::HandleFireStarted);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &ATankPlayerController::HandleFireCompleted);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Canceled, this, &ATankPlayerController::HandleFireCanceled);
	}
}

void ATankPlayerController::UpdateThrottle(const FInputActionValue& Value)
{
	CachedThrottle = IsLocalController() && Value.GetValueType() == EInputActionValueType::Axis1D
		? NetworkTankArena::LocalTank::SanitizeAxis(Value.Get<float>()) : 0.f;
}

void ATankPlayerController::UpdateTurn(const FInputActionValue& Value)
{
	CachedTurn = IsLocalController() && Value.GetValueType() == EInputActionValueType::Axis1D
		? NetworkTankArena::LocalTank::SanitizeAxis(Value.Get<float>()) : 0.f;
}

void ATankPlayerController::ClearThrottle()
{
	CachedThrottle = 0.f;
}

void ATankPlayerController::ClearTurn()
{
	CachedTurn = 0.f;
}

void ATankPlayerController::ClearCachedInput()
{
	InterruptLocalFire();
	ClearThrottle();
	ClearTurn();
	SecondsUntilInputSend = 0.f;
	if (ATankPawn* Tank = Cast<ATankPawn>(GetPawn()))
	{
		if (HasAuthority())
		{
			Tank->ClearControlIntent();
		}
		else if (IsLocalController())
		{
			// A lost neutral packet is repaired by the following periodic heartbeat.
			SubmitLocalControlIntent(Tank, FTankControlIntent{});
		}
	}
}

void ATankPlayerController::FlushPressedKeys()
{
	// UEnhancedPlayerInput can emit Completed from the synthetic key release.
	HandleFireCanceled();
	bIgnoreFireInputPass = true;
	Super::FlushPressedKeys();
	ClearCachedInput();
}

void ATankPlayerController::DisableInput(APlayerController* PlayerController)
{
	Super::DisableInput(PlayerController);
	// Disabling controller input does not flush keys or cancel our pending local press.
	if (!InputEnabled()) { InterruptLocalFire(); }
}

void ATankPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime); // Processes this frame's Enhanced Input before assembling intent.
	ATankPawn* Tank = Cast<ATankPawn>(GetPawn());
	if (!IsLocalController() || !IsValid(Tank) || Tank->GetController() != this)
	{
		InterruptLocalFire();
		return;
	}
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const FViewport* Viewport = LocalPlayer && LocalPlayer->ViewportClient ? LocalPlayer->ViewportClient->Viewport : nullptr;
	FTankControlIntent Intent;
	if (Viewport && Viewport->HasFocus())
	{
		Intent.Throttle = CachedThrottle;
		Intent.Turn = CachedTurn;
		FVector RayOrigin;
		FVector RayDirection;
		if (DeprojectMousePositionToWorld(RayOrigin, RayDirection))
		{
			Intent.bHasAimWorldPoint = NetworkTankArena::LocalTank::TryIntersectAimPlane(
				RayOrigin, RayDirection, Tank->GetGameplayPlaneZ(), Intent.AimWorldPoint);
		}
	}
	else
	{
		ClearThrottle();
		ClearTurn();
	}
	if (HasAuthority())
	{
		SubmitLocalControlIntent(Tank, Intent); // Listen host/standalone submits directly, without an RPC.
	}
	else if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
	{
		SecondsUntilInputSend -= DeltaTime;
		if (SecondsUntilInputSend <= UE_SMALL_NUMBER)
		{
			SubmitLocalControlIntent(Tank, Intent);
			// No catch-up burst after a slow frame. Send full current state even when neutral.
			SecondsUntilInputSend = InputSendIntervalSeconds;
		}
	}
	DispatchPendingFire(Intent);
}

void ATankPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// APlayerController calls actor Tick during pause even when it skips PlayerTick.
	if (!HasLocalFireControl()) { InterruptLocalFire(); }
}

bool ATankPlayerController::HasLocalFireControl() const
{
	const ATankPawn* Tank = Cast<ATankPawn>(GetPawn());
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
	const FViewport* Viewport = ViewportClient ? ViewportClient->Viewport : nullptr;
	return InputEnabled() && IsLocalController() && GetNetMode() != NM_DedicatedServer && IsValid(Tank)
		&& Tank->GetController() == this && Viewport && Viewport->HasFocus()
		&& !ViewportClient->IgnoreInput()
		&& (!ViewportClient->ViewportConsole || !ViewportClient->ViewportConsole->ConsoleActive())
		&& !IsPaused() && !IsMoveInputIgnored() && !IsLookInputIgnored();
}

void ATankPlayerController::InterruptLocalFire()
{
	FireLatch.Interrupt();
	PendingFireTank.Reset();
}

void ATankPlayerController::HandleFireStarted()
{
	if (bIgnoreFireInputPass || !HasLocalFireControl())
	{
		InterruptLocalFire();
		return;
	}
	if (FireLatch.Press())
	{
		PendingFireTank = CastChecked<ATankPawn>(GetPawn());
	}
}

void ATankPlayerController::HandleFireCompleted()
{
	if (!bIgnoreFireInputPass && HasLocalFireControl())
	{
		FireLatch.Release();
	}
	else
	{
		InterruptLocalFire();
	}
}

void ATankPlayerController::HandleFireCanceled()
{
	InterruptLocalFire();
	FireLatch.InvalidateActionValue();
}

void ATankPlayerController::PostProcessInput(float DeltaTime, bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);
	if (bGamePaused || !HasLocalFireControl())
	{
		InterruptLocalFire();
	}
	else if (!bIgnoreFireInputPass && FireAction && FireAction->ValueType == EInputActionValueType::Boolean)
	{
		if (const UEnhancedPlayerInput* EnhancedInput = Cast<UEnhancedPlayerInput>(PlayerInput))
		{
			if (const FInputActionInstance* Action = EnhancedInput->FindActionInstanceData(FireAction))
			{
				// An absent/canceled action is not evidence of a physical release.
				if (Action->GetTriggerEvent() != ETriggerEvent::Canceled)
				{
					FireLatch.ObserveActionValue(Action->GetValue().Get<bool>());
				}
			}
		}
	}
	bIgnoreFireInputPass = false;
}

void ATankPlayerController::SubmitLocalControlIntent(ATankPawn* Tank, const FTankControlIntent& Intent)
{
	if (IsLocalController() && IsValid(Tank) && GetPawn() == Tank && Tank->GetController() == this)
	{
		const uint32 Sequence = NextMovementIntentSequence++;
		if (HasAuthority())
		{
			AcceptControlIntent(Tank, Sequence, Intent);
		}
		else
		{
			ServerSubmitControlIntent(Tank, Sequence, NetworkTankArena::LocalTank::SanitizeIntent(Intent));
		}
	}
}

void ATankPlayerController::DispatchPendingFire(const FTankControlIntent& Intent)
{
	ATankPawn* Tank = PendingFireTank.Get();
	PendingFireTank.Reset(); // Invalid aim/control discards this press, without retry.
	if (Tank && HasLocalFireControl() && Intent.bHasAimWorldPoint)
	{
		SubmitLocalFireRequest(Tank, Intent.AimWorldPoint);
	}
}

void ATankPlayerController::SubmitLocalFireRequest(ATankPawn* Tank, const FVector& AimWorldPoint)
{
	if (!IsLocalController() || !IsValid(Tank) || GetPawn() != Tank || Tank->GetController() != this) { return; }
	FTankFireRequest Request;
	Request.RequestSequence = NextFireRequestSequence++;
	Request.AimWorldPoint = AimWorldPoint;
	if (HasAuthority()) { AcceptFireRequest(Tank, Request); }
	else { ServerRequestFire(Tank, Request); }
}

void ATankPlayerController::ServerRequestFire_Implementation(ATankPawn* FiringTank, FTankFireRequest Request)
{
	AcceptFireRequest(FiringTank, Request);
}

void ATankPlayerController::AcceptFireRequest(ATankPawn* FiringTank, const FTankFireRequest& Request)
{
	// The owning connection authenticates this controller. The pawn parameter names
	// the shooter, not a claimed victim, and cannot redirect a request after possession.
	if (!HasAuthority() || GetNetMode() == NM_Client || IsActorBeingDestroyed() || !HasActorBegunPlay()
		|| !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(FiringTank)
		|| FiringTank->GetWorld() != GetWorld() || !FiringTank->HasAuthority()
		|| FiringTank->IsActorBeingDestroyed() || !FiringTank->HasActorBegunPlay()
		|| GetPawn() != FiringTank || FiringTank->GetController() != this || FiringTank->GetOwner() != this
		|| !AcceptedFireRequestSequence.TryAccept(Request.RequestSequence)) { return; }
	// A new owner's request is handled once, even when aim/cooldown rejects its shot.
	FiringTank->GetWeaponComponent()->TryFire(Request.AimWorldPoint);
}

void ATankPlayerController::ServerSubmitControlIntent_Implementation(ATankPawn* TargetTank, uint32 Sequence, FTankControlIntent Intent)
{
	AcceptControlIntent(TargetTank, Sequence, Intent);
}

void ATankPlayerController::AcceptControlIntent(ATankPawn* TargetTank, uint32 Sequence, const FTankControlIntent& Intent)
{
	// The RPC is routed through the owned controller. Never let its caller name another
	// player's tank, or let an in-flight update for an old pawn affect a new possession.
	if (HasAuthority() && IsValid(TargetTank) && TargetTank->HasAuthority()
		&& GetPawn() == TargetTank && TargetTank->GetController() == this
		&& AcceptedMovementIntentSequence.TryAccept(Sequence))
	{
		TargetTank->SubmitControlIntent(Intent); // Pawn sanitizes again at the authority boundary.
	}
}

void ATankPlayerController::AcknowledgePossession(APawn* P)
{
	ClearCachedInput();
	Super::AcknowledgePossession(P);
	ApplyTankInputContext();
}

void ATankPlayerController::Reset()
{
	ClearCachedInput();
	RemoveTankInputContext();
	Super::Reset();
}

void ATankPlayerController::HostLocalhost()
{
	if (IsLocalController() && GetWorld() && GetNetMode() == NM_Standalone)
	{
		ClearCachedInput();
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/TestMap")), true, TEXT("listen"));
	}
}

void ATankPlayerController::JoinLocalhost()
{
	if (IsLocalController() && GetWorld() && GetNetMode() == NM_Standalone)
	{
		ClearCachedInput();
		ClientTravel(TEXT("127.0.0.1:7777"), TRAVEL_Absolute);
	}
}

void ATankPlayerController::SetPawn(APawn* InPawn)
{
	ClearCachedInput();
	RemoveTankInputContext();
	Super::SetPawn(InPawn);
	ApplyTankInputContext();
}

void ATankPlayerController::ApplyTankInputContext()
{
	if (!IsLocalController() || !Cast<ATankPawn>(GetPawn()))
	{
		return;
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return;
	}
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem || !TankMappingContext)
	{
		return; // Asset references are intentionally supplied by a later Blueprint child.
	}
	if (MappingSubsystem.Get() == Subsystem && AppliedMappingContext == TankMappingContext)
	{
		return;
	}
	RemoveTankInputContext();
	Subsystem->AddMappingContext(TankMappingContext, 0);
	MappingSubsystem = Subsystem;
	AppliedMappingContext = TankMappingContext;
}

void ATankPlayerController::RemoveTankInputContext()
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = MappingSubsystem.Get())
	{
		if (AppliedMappingContext)
		{
			Subsystem->RemoveMappingContext(AppliedMappingContext);
		}
	}
	MappingSubsystem.Reset();
	AppliedMappingContext = nullptr;
}

void ATankPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearCachedInput();
	RemoveTankInputContext();
	Super::EndPlay(EndPlayReason);
}
