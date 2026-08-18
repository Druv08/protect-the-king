#pragma once

#include "CoreMinimal.h"
#include "EnemyTypes.generated.h"

/**
 * The complete enemy roster. One entry per authored EnemyDataAsset (Phase 4).
 * Do not reorder existing entries - saved assets and Blueprints store the numeric value.
 */
UENUM(BlueprintType)
enum class EEnemyType : uint8
{
	None			UMETA(DisplayName = "None"),
	Grunt			UMETA(DisplayName = "Grunt"),
	Runner			UMETA(DisplayName = "Runner"),
	Tank			UMETA(DisplayName = "Tank"),
	Archer			UMETA(DisplayName = "Archer"),
	Assassin		UMETA(DisplayName = "Assassin"),
	Bomber			UMETA(DisplayName = "Bomber"),
	ShieldKnight	UMETA(DisplayName = "Shield Knight"),
	Charger			UMETA(DisplayName = "Charger"),
	Commander		UMETA(DisplayName = "Commander"),
	Healer			UMETA(DisplayName = "Healer"),
	Count			UMETA(Hidden)
};

/**
 * Shared behaviour archetypes. Enemies of the same family reuse one AI implementation
 * and differ only through their data asset, rather than shipping ten separate AI stacks.
 */
UENUM(BlueprintType)
enum class EEnemyBehaviourFamily : uint8
{
	None		UMETA(Hidden),
	/** Walk to the nearest guard and trade blows. Grunt, Tank, Shield Knight. */
	Melee		UMETA(DisplayName = "Melee"),
	/** Ignore guards where possible and close distance fast. Runner, Charger. */
	Rush		UMETA(DisplayName = "Rush"),
	/** Hold distance and fire. Archer. */
	Ranged		UMETA(DisplayName = "Ranged"),
	/** One-shot / high-impact behaviour. Assassin, Bomber. */
	Special		UMETA(DisplayName = "Special"),
	/** Buff or repair other enemies. Commander, Healer. */
	Support		UMETA(DisplayName = "Support"),
	Count		UMETA(Hidden)
};

namespace EnemyTypeDefaults
{
	/**
	 * Fallback family classification.
	 * Authored EnemyDataAssets are the source of truth from Phase 4 onward - this exists so the
	 * director and wave generator can reason about families before any assets are created.
	 */
	inline EEnemyBehaviourFamily GetDefaultFamily(EEnemyType EnemyType)
	{
		switch (EnemyType)
		{
		case EEnemyType::Grunt:
		case EEnemyType::Tank:
		case EEnemyType::ShieldKnight:
			return EEnemyBehaviourFamily::Melee;

		case EEnemyType::Runner:
		case EEnemyType::Charger:
			return EEnemyBehaviourFamily::Rush;

		case EEnemyType::Archer:
			return EEnemyBehaviourFamily::Ranged;

		case EEnemyType::Assassin:
		case EEnemyType::Bomber:
			return EEnemyBehaviourFamily::Special;

		case EEnemyType::Commander:
		case EEnemyType::Healer:
			return EEnemyBehaviourFamily::Support;

		default:
			return EEnemyBehaviourFamily::None;
		}
	}

	/** Iterates every real enemy type, skipping None and the Count sentinel. */
	inline void ForEachEnemyType(TFunctionRef<void(EEnemyType)> Predicate)
	{
		for (uint8 i = static_cast<uint8>(EEnemyType::Grunt); i < static_cast<uint8>(EEnemyType::Count); ++i)
		{
			Predicate(static_cast<EEnemyType>(i));
		}
	}
}
