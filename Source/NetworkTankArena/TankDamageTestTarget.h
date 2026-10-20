#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TankDamageTestTarget.generated.h"

class UTextRenderComponent;

// Persistent server-owned measuring target. Replicates totals, with no health/death.
UCLASS()
class NETWORKTANKARENA_API ATankDamageTestTarget : public AActor
{
	GENERATED_BODY()
public:
	ATankDamageTestTarget();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(ReplicatedUsing = OnRep_DamageResult, VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Damage Test")
	int32 HitCount = 0;
	UPROPERTY(ReplicatedUsing = OnRep_DamageResult, VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Damage Test")
	float TotalDamage = 0.f;

protected:
	virtual void BeginPlay() override;

private:
	friend class FTankNetworkDamageTargetTest;
	UFUNCTION()
	void OnRep_DamageResult();
	UPROPERTY(VisibleAnywhere, Category = "Tank|Damage Test")
	TObjectPtr<UTextRenderComponent> ResultText;
};
