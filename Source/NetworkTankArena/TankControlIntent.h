#pragma once

#include "CoreMinimal.h"
#include "TankControlIntent.generated.h"

// Gameplay-facing controls: no device, viewport, or controller dependencies.
USTRUCT()
struct NETWORKTANKARENA_API FTankControlIntent
{
	GENERATED_BODY()

	UPROPERTY()
	float Throttle = 0.f;
	UPROPERTY()
	float Turn = 0.f;
	UPROPERTY()
	FVector AimWorldPoint = FVector::ZeroVector;
	UPROPERTY()
	bool bHasAimWorldPoint = false;
};

// A timestamped, sequenced move executed by the client and sent to the server.
USTRUCT()
struct NETWORKTANKARENA_API FTankMoveRecord
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 Sequence = 0;

	UPROPERTY()
	float DeltaTime = 0.f;

	UPROPERTY()
	FTankControlIntent Intent;

	// The predicted transform on the client after simulating 
	UPROPERTY()
	FVector ResultLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator ResultRotation = FRotator::ZeroRotator;
};

// Authoritative state confirmation sent back from the server to the client
USTRUCT()
struct NETWORKTANKARENA_API FTankServerMoveAck
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 AckSequence = 0;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;
};