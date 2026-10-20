#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TankControlIntent.h"
#include "TankPawn.generated.h"

class UCapsuleComponent;
class USceneComponent;
class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UTankMovementComponent;
class UTankWeaponComponent;

UCLASS()
class NETWORKTANKARENA_API ATankPawn : public APawn
{
	GENERATED_BODY()

public:
	ATankPawn();
	virtual void Tick(float DeltaTime) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void NotifyControllerChanged() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Restart() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SubmitControlIntent(const FTankControlIntent& Intent);
	void ClearControlIntent();
	double GetGameplayPlaneZ() const;
	float GetCanonicalTurretWorldYaw() const { return CanonicalTurretWorldYaw; }
	UTankWeaponComponent* GetWeaponComponent() const { return TankWeapon; }

	// Executes a predicted move on the client and stores it in the prediction buffer
	void PredictLocalMove(uint32 Sequence, float DeltaTime, const FTankControlIntent& Intent);

	// Receives authoritative confirmation from the server and reconciles if necessary
	void ReconcileServerMove(const FTankServerMoveAck& Ack);

	// Profiling
	int32 GetReconciliationCount() const { return ReconciliationCount; }
	float GetMaxReconciliationError() const { return MaxReconciliationError; }

	// Independent gameplay state: cosmetic component transforms are never replicated.
	UPROPERTY(ReplicatedUsing = OnRep_CanonicalTurretWorldYaw)
	float CanonicalTurretWorldYaw = 0.f;

	void ExecuteAuthoritativeMove(float DeltaTime, const FTankControlIntent& Intent);

	UTankMovementComponent* GetTankMovement() const { return TankMovement; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FTankMultiplayerAuthorityTest;
	friend class FTankMultiplayerLifecycleTest;
	friend class FTankMultiplayerContentTest;

	UFUNCTION()
	void OnRep_CanonicalTurretWorldYaw();

	void UpdateControllerTickPrerequisite();
	void ApplyPresentation(float DeltaTime);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCapsuleComponent> CollisionRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ChassisVisualPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ChassisMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> TurretYawPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> TurretMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BarrelMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTankMovementComponent> TankMovement;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTankWeaponComponent> TankWeapon;

	UPROPERTY(EditDefaultsOnly, Category = "Tank|Presentation", meta = (ClampMin = "0", Units = "cm"))
	float HoverAmplitude = 3.f;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Presentation", meta = (ClampMin = "0", Units = "Hz"))
	float HoverFrequencyHz = 1.f;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Presentation", meta = (ClampMin = "0", ClampMax = "15", Units = "deg"))
	float MaxBankDegrees = 4.f;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Presentation", meta = (ClampMin = "0"))
	float BankInterpSpeed = 8.f;

	FTankControlIntent CurrentIntent;
	bool bIsRemoteControlled = false;
	float RemoteSmoothedTurretYaw = 0.f;

	// Intent is refreshed by producers, not replicated. Stop if unreliable updates cease.
	static constexpr float ControlIntentTimeoutSeconds = 0.5f;
	float ControlIntentAgeSeconds = 0.f;
	FTransform BaseVisualTransform = FTransform::Identity;
	FTransform BaseChassisTransform = FTransform::Identity;
	double HoverPhase = 0.0;
	float CurrentBankDegrees = 0.f;
	float PreviousPresentationYaw = 0.f;
	float PredictedTurretWorldYaw = 0.f;
	TWeakObjectPtr<AController> TickController;

	// Visual reconciliation smoothing offset
	FVector VisualCorrectionOffset = FVector::ZeroVector;
	static constexpr float VisualSmoothingInterpSpeed = 20.f;

	// Move prediction buffer (circular/capped list of unacknowledged moves)
	TArray<FTankMoveRecord> SavedMoves;

	// Tuning thresholds for server corrections
	static constexpr float PositionCorrectionThreshold = 2.0f; // 2 cm tolerance
	static constexpr float YawCorrectionThreshold = 1.0f;      // 1 degree tolerance
	static constexpr int32 MaxSavedMovesCapacity = 120;       // Max ~2 seconds of moves

	// Reconciliation diagnostics
	int32 ReconciliationCount = 0;
	float MaxReconciliationError = 0.f;
};
