#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TankControlIntent.h"
#include "TankInputSequence.h"
#include "TankCombat.h"
#include "TankFireRequest.h"
#include "TankPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;
class ATankPawn;

UCLASS()
class NETWORKTANKARENA_API ATankPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void PlayerTick(float DeltaTime) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetPawn(APawn* InPawn) override;
	virtual void FlushPressedKeys() override;
	virtual void DisableInput(APlayerController* PlayerController) override;
	virtual void AcknowledgePossession(APawn* P) override;
	virtual void Reset() override;

	// Small localhost lab flow, also callable from a Blueprint widget or the console.
	UFUNCTION(Exec, BlueprintCallable, Category = "Tank|Localhost")
	void HostLocalhost();
	UFUNCTION(Exec, BlueprintCallable, Category = "Tank|Localhost")
	void JoinLocalhost();

protected:
	virtual void BeginPlay() override;
	virtual void ReceivedPlayer() override;
	virtual void SetupInputComponent() override;
	virtual void PostProcessInput(float DeltaTime, bool bGamePaused) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FTankMultiplayerAuthorityTest;
	friend class FTankMultiplayerLifecycleTest;
	friend class FTankMultiplayerContentTest;
	friend class FTankLocalFireLifecycleTest;
	friend class FTankLocalFireDisableInputTest;
	friend class FTankEnhancedFireBindingTest;
	friend class FTankLocalCombatContentTest;
	friend class FTankNetworkFireRequestTest;
	friend class FTankNetworkFireLifecycleTest;

	UFUNCTION(Server, Unreliable)
	void ServerSubmitControlIntent(ATankPawn* TargetTank, uint32 Sequence, FTankControlIntent Intent);
	void SubmitLocalControlIntent(ATankPawn* Tank, const FTankControlIntent& Intent);
	void AcceptControlIntent(ATankPawn* TargetTank, uint32 Sequence, const FTankControlIntent& Intent);

	UFUNCTION(Server, Reliable)
	void ServerRequestFire(ATankPawn* FiringTank, FTankFireRequest Request);
	void SubmitLocalFireRequest(ATankPawn* Tank, const FVector& AimWorldPoint);
	void AcceptFireRequest(ATankPawn* FiringTank, const FTankFireRequest& Request);
	void DispatchPendingFire(const FTankControlIntent& Intent);

	void UpdateThrottle(const FInputActionValue& Value);
	void UpdateTurn(const FInputActionValue& Value);
	void ClearThrottle();
	void ClearTurn();
	void HandleFireStarted();
	void HandleFireCompleted();
	void HandleFireCanceled();
	void ClearCachedInput();
	void ApplyTankInputContext();
	void RemoveTankInputContext();
	bool HasLocalFireControl() const;
	void InterruptLocalFire();
	NetworkTankArena::LocalCombat::FFireRequestLatch FireLatch;
	// Exists only until this input frame has assembled its mouse aim; never retried.
	TWeakObjectPtr<ATankPawn> PendingFireTank;
	// Covers the next Enhanced Input processing pass, including synthetic flush events.
	bool bIgnoreFireInputPass = false;

	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> TurnAction;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> FireAction;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputMappingContext> TankMappingContext;

	float CachedThrottle = 0.f;
	float CachedTurn = 0.f;
	static constexpr float InputSendIntervalSeconds = 1.f / 20.f;
	float SecondsUntilInputSend = 0.f;
	// Never reset on intent clearing, timeout or possession changes. A new controller
	// creates a fresh stream; uint32 overflow is handled by the acceptance state.
	uint32 NextMovementIntentSequence = 0;
	NetworkTankArena::FMovementIntentSequenceState AcceptedMovementIntentSequence;
	uint32 NextFireRequestSequence = 0;
	NetworkTankArena::FInputSequenceState AcceptedFireRequestSequence;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSubsystem;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AppliedMappingContext;
};
