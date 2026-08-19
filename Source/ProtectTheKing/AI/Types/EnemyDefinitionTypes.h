#pragma once

#include "CoreMinimal.h"
#include "EnemyDefinitionTypes.generated.h"

/**
 * What an enemy tends to go after.
 *
 * Configuration only. Phase 4 stores a preference weight per entry; nothing here selects an
 * actual target, and no distances, traces or actor lookups belong in this file. The target
 * selection pass reads these weights later.
 */
UENUM(BlueprintType)
enum class ETargetPriority : uint8
{
	None			UMETA(Hidden),
	/** The King himself, ignoring guards where possible. */
	King			UMETA(DisplayName = "King"),
	/** Whichever guard happens to be closest. */
	NearestGuard	UMETA(DisplayName = "Nearest Guard"),
	/** The guard with the least health remaining. */
	WeakestGuard	UMETA(DisplayName = "Weakest Guard"),
	/** A guard separated from the rest of the defence. */
	IsolatedGuard	UMETA(DisplayName = "Isolated Guard"),
	/** The middle of a tightly grouped set of guards. */
	GuardCluster	UMETA(DisplayName = "Guard Cluster"),
	Count			UMETA(Hidden)
};

/** Number of real target priorities, excluding None and the Count sentinel. */
static constexpr uint8 NumTargetPriorities = 5;

/**
 * The axes an enemy can be naturally good at.
 *
 * These exist so later systems can ask "which enemy is best at pressuring an exposed King?"
 * and search the definitions, instead of hard-coding "if the King is exposed, spawn Runners".
 * Adding an enemy should never require editing the selection code.
 */
UENUM(BlueprintType)
enum class EEnemyRoleAxis : uint8
{
	None			UMETA(Hidden),
	/** Punishing guards that bunch together, typically through area damage. */
	AntiCluster		UMETA(DisplayName = "Anti-Cluster"),
	/** Reaching and threatening the King directly. */
	KingPressure	UMETA(DisplayName = "King Pressure"),
	/** Holding a durable frontline and absorbing guard attention. */
	Frontline		UMETA(DisplayName = "Frontline"),
	/** Applying damage from outside the guards' reach. */
	RangedPressure	UMETA(DisplayName = "Ranged Pressure"),
	/** Hunting guards that have drifted away from support. */
	IsolatedGuard	UMETA(DisplayName = "Isolated Guard"),
	/** Making the rest of the wave more effective rather than dealing damage. */
	Support			UMETA(DisplayName = "Support"),
	Count			UMETA(Hidden)
};

/** Number of real role axes, excluding None and the Count sentinel. */
static constexpr uint8 NumEnemyRoleAxes = 6;

/**
 * How strongly an enemy prefers each kind of target, each 0..1.
 *
 * Weights rather than a single enum: an Assassin that mostly hunts isolated guards but will
 * take the King if he is unguarded cannot be expressed by one exclusive choice. Values are
 * independent preferences and are not required to sum to anything.
 */
USTRUCT(BlueprintType)
struct FEnemyTargetingProfile
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float King = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NearestGuard = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WeakestGuard = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IsolatedGuard = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GuardCluster = 0.f;

	float Get(ETargetPriority Priority) const
	{
		switch (Priority)
		{
		case ETargetPriority::King:				return King;
		case ETargetPriority::NearestGuard:		return NearestGuard;
		case ETargetPriority::WeakestGuard:		return WeakestGuard;
		case ETargetPriority::IsolatedGuard:	return IsolatedGuard;
		case ETargetPriority::GuardCluster:		return GuardCluster;
		default:								return 0.f;
		}
	}

	void Set(ETargetPriority Priority, float Value)
	{
		switch (Priority)
		{
		case ETargetPriority::King:				King = Value; break;
		case ETargetPriority::NearestGuard:		NearestGuard = Value; break;
		case ETargetPriority::WeakestGuard:		WeakestGuard = Value; break;
		case ETargetPriority::IsolatedGuard:	IsolatedGuard = Value; break;
		case ETargetPriority::GuardCluster:		GuardCluster = Value; break;
		default:								break;
		}
	}

	/**
	 * The single strongest preference, for debug output and coarse reasoning.
	 * Returns None when nothing was configured. Ties keep the lowest enum value, so the result
	 * is deterministic rather than dependent on declaration order.
	 */
	ETargetPriority GetPrimaryPriority() const
	{
		ETargetPriority Best = ETargetPriority::None;
		float BestValue = 0.f;

		for (uint8 i = 1; i <= NumTargetPriorities; ++i)
		{
			const ETargetPriority Candidate = static_cast<ETargetPriority>(i);
			if (Get(Candidate) > BestValue)
			{
				BestValue = Get(Candidate);
				Best = Candidate;
			}
		}
		return Best;
	}

	void Reset() { *this = FEnemyTargetingProfile(); }
};

/**
 * What an enemy is naturally good at, each 0..1.
 *
 * Descriptive, not a decision. A high AntiCluster value says a Bomber punishes grouped guards
 * well; it does not say a Bomber should be spawned. Later phases compare these across the
 * available definitions and decide.
 */
USTRUCT(BlueprintType)
struct FEnemyEffectivenessProfile
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effectiveness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AntiCluster = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effectiveness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KingPressure = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effectiveness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Frontline = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effectiveness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RangedPressure = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effectiveness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IsolatedGuard = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effectiveness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Support = 0.f;

	float Get(EEnemyRoleAxis Axis) const
	{
		switch (Axis)
		{
		case EEnemyRoleAxis::AntiCluster:		return AntiCluster;
		case EEnemyRoleAxis::KingPressure:		return KingPressure;
		case EEnemyRoleAxis::Frontline:			return Frontline;
		case EEnemyRoleAxis::RangedPressure:	return RangedPressure;
		case EEnemyRoleAxis::IsolatedGuard:		return IsolatedGuard;
		case EEnemyRoleAxis::Support:			return Support;
		default:								return 0.f;
		}
	}

	void Set(EEnemyRoleAxis Axis, float Value)
	{
		switch (Axis)
		{
		case EEnemyRoleAxis::AntiCluster:		AntiCluster = Value; break;
		case EEnemyRoleAxis::KingPressure:		KingPressure = Value; break;
		case EEnemyRoleAxis::Frontline:			Frontline = Value; break;
		case EEnemyRoleAxis::RangedPressure:	RangedPressure = Value; break;
		case EEnemyRoleAxis::IsolatedGuard:		IsolatedGuard = Value; break;
		case EEnemyRoleAxis::Support:			Support = Value; break;
		default:								break;
		}
	}

	/** Axis this enemy scores highest on. None when nothing was configured. Ties keep the lowest enum value. */
	EEnemyRoleAxis GetStrongestAxis() const
	{
		EEnemyRoleAxis Best = EEnemyRoleAxis::None;
		float BestValue = 0.f;

		for (uint8 i = 1; i <= NumEnemyRoleAxes; ++i)
		{
			const EEnemyRoleAxis Candidate = static_cast<EEnemyRoleAxis>(i);
			if (Get(Candidate) > BestValue)
			{
				BestValue = Get(Candidate);
				Best = Candidate;
			}
		}
		return Best;
	}

	void Reset() { *this = FEnemyEffectivenessProfile(); }
};

namespace EnemyDefinition
{
	/** Iterates every real target priority, skipping None and the Count sentinel. */
	inline void ForEachTargetPriority(TFunctionRef<void(ETargetPriority)> Predicate)
	{
		for (uint8 i = 1; i <= NumTargetPriorities; ++i)
		{
			Predicate(static_cast<ETargetPriority>(i));
		}
	}

	/** Iterates every real role axis, skipping None and the Count sentinel. */
	inline void ForEachRoleAxis(TFunctionRef<void(EEnemyRoleAxis)> Predicate)
	{
		for (uint8 i = 1; i <= NumEnemyRoleAxes; ++i)
		{
			Predicate(static_cast<EEnemyRoleAxis>(i));
		}
	}
}
