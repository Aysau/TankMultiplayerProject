#pragma once

#include "CoreMinimal.h"
#include "TankGameplayMath.h"

namespace NetworkTankArena::LocalCombat
{
	inline constexpr double CooldownSeconds = 0.30;
	inline constexpr float ProjectileSpeed = 2000.f;
	inline constexpr float ProjectileRadius = 10.f;
	inline constexpr float ProjectileLifetime = 3.f;
	inline constexpr float Damage = 25.f;
	inline constexpr double MaxAimDistance = 100000.0;

	inline bool TryCalculateFireYaw(const FVector& GameplayPosition, const FVector& AimWorldPoint, float& OutYaw)
	{
		using namespace NetworkTankArena::LocalTank;
		if (!IsFiniteVector(GameplayPosition) || !IsFiniteVector(AimWorldPoint)) { return false; }
		const FVector Delta = AimWorldPoint - GameplayPosition;
		// Bound components before squaring, including finite values near double overflow.
		if (!IsFiniteVector(Delta) || Delta.GetAbsMax() > MaxAimDistance
			|| Delta.SizeSquared() > MaxAimDistance * MaxAimDistance) { return false; }
		return TryCalculateAimYaw(GameplayPosition, AimWorldPoint, OutYaw);
	}

	// Enhanced Input Started requests a shot; trusted release/neutral rearms it.
	struct FFireRequestLatch
	{
		bool Press()
		{
			bAwaitingActuation = false;
			if (!bArmed) { return false; }
			bArmed = false;
			return true;
		}
		void Release() { if (!bAwaitingActuation) { bArmed = true; } }
		void Interrupt() { bArmed = false; }
		// A flush/cancellation can manufacture zero/Completed while physically held.
		// Require a subsequent actuation before trusting that action's neutral state.
		void InvalidateActionValue() { Interrupt(); bAwaitingActuation = true; }
		void ObserveActionValue(bool bHeld)
		{
			if (bHeld) { bAwaitingActuation = false; bArmed = false; }
			else { Release(); }
		}
	private:
		bool bArmed = true;
		bool bAwaitingActuation = false;
	};

	// Caller supplies gameplay time; no device, actor, presentation or network dependency.
	struct FWeaponCooldown
	{
		bool TryConsume(double Now)
		{
			if (!FMath::IsFinite(Now) || Now < NextShotTime) { return false; }
			NextShotTime = Now + CooldownSeconds;
			return true;
		}
	private:
		double NextShotTime = 0.0;
	};

	struct FFiringPose
	{
		FVector TurretOrigin;
		FVector Muzzle;
		FVector Forward;
	};

	inline FFiringPose CalculateFiringPose(const FVector& GameplayPosition, float CanonicalWorldYaw,
		float TurretHeight, float MuzzleDistance)
	{
		const FVector Origin = GameplayPosition + FVector(0.0, 0.0, TurretHeight);
		const FVector Forward = FRotator(0.0, CanonicalWorldYaw, 0.0).Vector();
		return {Origin, Origin + Forward * MuzzleDistance, Forward};
	}
}
