#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TankCombat.h"
#include "TankWeaponComponent.generated.h"

UCLASS(ClassGroup = (Tank))
class NETWORKTANKARENA_API UTankWeaponComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTankWeaponComponent();
	// Synchronous server processing. No cooldown retry or queued gameplay request.
	bool TryFire(const FVector& AimWorldPoint);
	NetworkTankArena::LocalCombat::FFiringPose GetFiringPose() const;
	NetworkTankArena::LocalCombat::FFiringPose GetFiringPose(float ShotWorldYaw) const;

private:
	friend class FTankLocalWeaponTest;
	friend class FTankLocalCombatContentTest;
	// Fixed gameplay offsets, independent of the visual hierarchy and actor scale.
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Weapon", meta = (Units = "cm"))
	float GameplayTurretHeight = -25.f;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Weapon", meta = (ClampMin = "0", Units = "cm"))
	float GameplayMuzzleDistance = 110.f;
	NetworkTankArena::LocalCombat::FWeaponCooldown Cooldown;
};
