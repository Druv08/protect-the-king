#pragma once

#include "CoreMinimal.h"
#include "StrategyTypes.generated.h"

/**
 * Counter-strategies the Enemy AI Director can pick from.
 * Only a subset is implemented at any one time - the selector (Phase 6) scores whichever
 * strategies are registered and ignores the rest, so entries can be added incrementally.
 */
UENUM(BlueprintType)
enum class EEnemyStrategy : uint8
{
	None				UMETA(Hidden),
	/** Baseline: push the King head-on from the strongest available approach. */
	DirectAssault		UMETA(DisplayName = "Direct Assault"),
	/** Concentrate on the side the player defended least. */
	WeakSideFlank		UMETA(DisplayName = "Weak-Side Flank"),
	/** Ignore guards, sprint the King. Punishes guards that roam far from him. */
	KingRush			UMETA(DisplayName = "King Rush"),
	/** Two independent groups on separate sides to stretch the guard rotation. */
	SplitAttack			UMETA(DisplayName = "Split Attack"),
	/** Two groups on opposing sides, arriving together. */
	PincerAttack		UMETA(DisplayName = "Pincer Attack"),
	/** Area and splash pressure aimed at tightly grouped guards. */
	AntiCluster			UMETA(DisplayName = "Anti-Cluster"),
	/** Standoff damage from range rather than committing to melee. */
	RangedSiege			UMETA(DisplayName = "Ranged Siege"),
	/** Single high-value infiltrator targeting the King directly. */
	Assassination		UMETA(DisplayName = "Assassination"),
	/** Armoured frontline soaking damage to open a lane. */
	TankPush			UMETA(DisplayName = "Tank Push"),
	/** Obvious feint on one side, real attack on another. */
	DecoyFlank			UMETA(DisplayName = "Decoy + Flank"),
	/** Commander/Healer backed formation that sustains a slower advance. */
	SupportFormation	UMETA(DisplayName = "Support Formation"),
	Count				UMETA(Hidden)
};

/**
 * One strategy's score for the upcoming round.
 * Reason is a short human-readable justification kept for the debug overlay (Phase 10);
 * it is never used to drive logic.
 */
USTRUCT(BlueprintType)
struct FStrategyScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Strategy")
	EEnemyStrategy Strategy = EEnemyStrategy::None;

	UPROPERTY(BlueprintReadOnly, Category = "Strategy")
	float Score = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Strategy")
	FString Reason;

	FStrategyScore() = default;

	FStrategyScore(EEnemyStrategy InStrategy, float InScore, const FString& InReason = FString())
		: Strategy(InStrategy)
		, Score(InScore)
		, Reason(InReason)
	{
	}
};
