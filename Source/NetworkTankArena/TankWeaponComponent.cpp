#include "TankWeaponComponent.h"

#include "Engine/World.h"
#include "TankGameplayMath.h"
#include "TankPawn.h"
#include "TankProjectile.h"

UTankWeaponComponent::UTankWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

NetworkTankArena::LocalCombat::FFiringPose UTankWeaponComponent::GetFiringPose() const
{
	const ATankPawn* Tank = CastChecked<ATankPawn>(GetOwner());
	return GetFiringPose(Tank->GetCanonicalTurretWorldYaw());
}

NetworkTankArena::LocalCombat::FFiringPose UTankWeaponComponent::GetFiringPose(float ShotWorldYaw) const
{
	const ATankPawn* Tank = CastChecked<ATankPawn>(GetOwner());
	return NetworkTankArena::LocalCombat::CalculateFiringPose(Tank->GetActorLocation(),
		ShotWorldYaw, GameplayTurretHeight, GameplayMuzzleDistance);
}

bool UTankWeaponComponent::TryFire(const FVector& AimWorldPoint)
{
	ATankPawn* Tank = Cast<ATankPawn>(GetOwner());
	if (!IsValid(Tank) || !Tank->HasAuthority() || GetNetMode() == NM_Client
		|| Tank->IsActorBeingDestroyed() || !Tank->HasActorBegunPlay()
		|| !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return false;
	}
	float ShotYaw;
	if (!NetworkTankArena::LocalCombat::TryCalculateFireYaw(Tank->GetActorLocation(), AimWorldPoint, ShotYaw)) { return false; }
	if (!FMath::IsFinite(GameplayTurretHeight) || !FMath::IsFinite(GameplayMuzzleDistance)
		|| GameplayMuzzleDistance < 0.f) { return false; }
	// Immutable gameplay snapshot: firing never writes CanonicalTurretWorldYaw.
	const auto Pose = GetFiringPose(ShotYaw);
	if (!NetworkTankArena::LocalTank::IsFiniteVector(Pose.Muzzle)
		|| !NetworkTankArena::LocalTank::IsFiniteVector(Pose.TurretOrigin)
		|| !NetworkTankArena::LocalTank::IsFiniteVector(Pose.Forward)
		|| !Cooldown.TryConsume(GetWorld()->GetTimeSeconds())) { return false; }

	FHitResult Hit;
	if (ATankProjectile::SweepPath(GetWorld(), Pose.TurretOrigin, Pose.Muzzle, Tank, nullptr, Hit))
	{
		// Obstruction consumes the shot and receives its one damage result. Never spawn beyond cover.
		ATankProjectile::ApplyAuthoritativeHit(Hit, Pose.Forward, Tank, Tank);
		return true;
	}
	const FTransform Transform(Pose.Forward.Rotation(), Pose.Muzzle);
	ATankProjectile* Projectile = GetWorld()->SpawnActorDeferred<ATankProjectile>(
		ATankProjectile::StaticClass(), Transform, Tank, Tank, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		Projectile->Initialize(Tank, Pose.Forward);
		Projectile->FinishSpawning(Transform);
	}
	return true; // The shot/cooldown is consumed even if spawning failed.
}
