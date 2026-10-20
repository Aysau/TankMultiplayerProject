#pragma once

#include "CoreMinimal.h"
#include "TankFireRequest.generated.h"

// Discrete intent only. The server supplies pose, direction, collision and damage.
USTRUCT()
struct NETWORKTANKARENA_API FTankFireRequest
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 RequestSequence = 0;
	UPROPERTY()
	FVector AimWorldPoint = FVector::ZeroVector;
};
