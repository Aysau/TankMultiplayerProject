#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TankProjectile.generated.h"

class ATankPawn;
class USphereComponent;

// Replicated straight-line projectile. Only the server sweeps, moves and damages.
UCLASS()
class NETWORKTANKARENA_API ATankProjectile : public AActor
{
	GENERATED_BODY()
public:
	ATankProjectile();
	virtual void Tick(float DeltaSeconds) override;

	// Launch clearance and flight deliberately use the same collision shape/filter.
	static bool SweepPath(UWorld* World, const FVector& Start, const FVector& End,
		const AActor* Shooter, const AActor* Projectile, FHitResult& Hit);
	static void ApplyAuthoritativeHit(const FHitResult& Hit, const FVector& Direction,
		ATankPawn* Shooter, AActor* DamageCauser);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class UTankWeaponComponent;
	friend class FTankLocalProjectileTest;
	friend class FTankLocalWeaponTest;
	friend class FTankNetworkProjectileTest;
	void Initialize(ATankPawn* Shooter, const FVector& Direction);
	void ResolveHit(const FHitResult& Hit);
	UFUNCTION()
	void HandleShooterEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	UPROPERTY(VisibleAnywhere, Category = "Tank|Projectile")
	TObjectPtr<USphereComponent> Sphere;
	TWeakObjectPtr<ATankPawn> FiringTank;
	FVector Forward = FVector::ForwardVector;
	float RemainingLifetime = 0.f;
	bool bTerminal = false;
};
