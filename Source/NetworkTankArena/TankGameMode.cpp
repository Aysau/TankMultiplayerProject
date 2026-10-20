#include "TankGameMode.h"

#include "TankPawn.h"
#include "TankDamageTestTarget.h"
#include "TankPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ATankGameMode::ATankGameMode()
{
	// Reuse the established Blueprint visual composition and input asset assignments.
	static ConstructorHelpers::FClassFinder<ATankPawn> TankClass(TEXT("/Game/Blueprints/BP_Tank"));
	static ConstructorHelpers::FClassFinder<ATankPlayerController> ControllerClass(TEXT("/Game/Blueprints/BP_TankPlayerController"));
	DefaultPawnClass = TankClass.Succeeded() ? TankClass.Class.Get() : ATankPawn::StaticClass();
	PlayerControllerClass = ControllerClass.Succeeded() ? ControllerClass.Class.Get() : ATankPlayerController::StaticClass();
}

void ATankGameMode::StartPlay()
{
	// GameMode exists on the server. A placed target replaces this replicated fixture.
	if (HasAuthority() && GetNetMode() != NM_Client && !TActorIterator<ATankDamageTestTarget>(GetWorld()))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		if (!GetWorld()->SpawnActor<ATankDamageTestTarget>(DamageTargetLocation, FRotator::ZeroRotator, Params))
		{
			UE_LOG(LogTemp, Warning, TEXT("Damage test target fixture could not spawn; check map clearance."));
		}
	}
	Super::StartPlay();
}
