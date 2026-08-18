#pragma once

#include "CoreMinimal.h"
#include "AI/Types/CombatDirectionTypes.h"
#include "AI/Types/EnemyTypes.h"
#include "PlayerBehaviourTypes.generated.h"

/**
 * Coarse bucket for a normalised 0..1 score.
 * The director rules read in these terms ("North Defence = High") rather than raw floats.
 */
UENUM(BlueprintType)
enum class EBehaviourRating : uint8
{
	VeryLow		UMETA(DisplayName = "Very Low"),
	Low			UMETA(DisplayName = "Low"),
	Medium		UMETA(DisplayName = "Medium"),
	High		UMETA(DisplayName = "High"),
	VeryHigh	UMETA(DisplayName = "Very High")
};

namespace BehaviourRating
{
	/** Buckets a 0..1 score at 0.2 / 0.4 / 0.6 / 0.8. Values outside the range are clamped. */
	inline EBehaviourRating FromScore(float NormalisedScore)
	{
		const float Score = FMath::Clamp(NormalisedScore, 0.f, 1.f);
		if (Score < 0.2f) { return EBehaviourRating::VeryLow; }
		if (Score < 0.4f) { return EBehaviourRating::Low; }
		if (Score < 0.6f) { return EBehaviourRating::Medium; }
		if (Score < 0.8f) { return EBehaviourRating::High; }
		return EBehaviourRating::VeryHigh;
	}
}

/**
 * How one enemy type performed during a single round.
 * Feeds the "enemy type that performed best against the player" statistic.
 */
USTRUCT(BlueprintType)
struct FEnemyTypeRoundPerformance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Enemy Performance")
	int32 SpawnedCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Enemy Performance")
	int32 KilledByPlayerCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Enemy Performance")
	float DamageDealtToGuards = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Enemy Performance")
	float DamageDealtToKing = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Enemy Performance")
	int32 GuardsDowned = 0;

	/** Summed lifetime of every instance of this type that spawned. */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy Performance")
	float TotalLifetimeSeconds = 0.f;

	float GetAverageLifetimeSeconds() const
	{
		return SpawnedCount > 0 ? TotalLifetimeSeconds / static_cast<float>(SpawnedCount) : 0.f;
	}

	/** Fraction that survived the round, 0..1. */
	float GetSurvivalRate() const
	{
		if (SpawnedCount <= 0)
		{
			return 0.f;
		}
		return 1.f - (static_cast<float>(KilledByPlayerCount) / static_cast<float>(SpawnedCount));
	}

	void Reset() { *this = FEnemyTypeRoundPerformance(); }
};

/**
 * Raw telemetry accumulated by the PlayerPatternTracker during one round (Phase 2).
 * These are unprocessed running totals - all interpretation happens in Phase 3.
 */
USTRUCT(BlueprintType)
struct FPlayerRoundStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Round")
	int32 RoundNumber = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Round")
	float RoundDurationSeconds = 0.f;

	// --- Directional accumulators -------------------------------------------------

	/** Seconds of guard presence on each side. Primary input to defence strength. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Direction")
	FDirectionalStats GuardPresenceSecondsBySide;

	/** Enemies killed on each side. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Direction")
	FDirectionalStats EnemyKillsBySide;

	/** Damage the player and guards dealt on each side. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Direction")
	FDirectionalStats DamageDealtBySide;

	/** Damage the King took, bucketed by the direction the attacker came from. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Direction")
	FDirectionalStats KingDamageTakenBySide;

	// --- Guard positioning --------------------------------------------------------

	/** Running sum of mean-guard-distance samples, in Unreal units. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Guards")
	float GuardDistanceSampleSum = 0.f;

	/** Running sum of clustering samples, each 0..1. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Guards")
	float ClusteringSampleSum = 0.f;

	/** Number of samples taken - divides both sums above. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Guards")
	int32 PositionSampleCount = 0;

	/** Seconds the King spent with no guard inside the protection radius. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Guards")
	float KingUnguardedSeconds = 0.f;

	// --- Control -------------------------------------------------------------------

	/** Seconds the player directly controlled each guard, indexed by guard slot. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Control")
	TArray<float> GuardControlSeconds;

	UPROPERTY(BlueprintReadWrite, Category = "Round|Control")
	int32 GuardSwitchCount = 0;

	// --- Combat --------------------------------------------------------------------

	/** Seconds the controlled guard spent attacking or advancing on enemies. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	float TimeSpentAggressiveSeconds = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	int32 EnemiesSpawned = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	int32 EnemiesKilled = 0;

	/** Summed time-to-kill across every enemy killed. Divided by EnemiesKilled for efficiency. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	float TotalTimeToKillSeconds = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	TMap<EEnemyType, FEnemyTypeRoundPerformance> PerformanceByEnemyType;

	// --- Player combat activity ------------------------------------------------------
	// Added in Phase 2. DamageDealtBySide above covers player + AI guards combined;
	// these three isolate what the player-controlled guard did.

	/** Attack inputs the player-controlled guard committed, whether or not they connected. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	int32 PlayerAttackCount = 0;

	/** Subset of PlayerAttackCount that actually damaged an enemy. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	int32 PlayerAttacksLandedCount = 0;

	/** Total damage dealt by the player-controlled guard only. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|Combat")
	float PlayerDamageDealtTotal = 0.f;

	// --- King exposure ----------------------------------------------------------------
	// Added in Phase 2. KingDamageTakenBySide above holds the amounts; these describe the
	// shape of the exposure (how often, how long, how crowded) for the Phase 3 score.

	/** How many separate times the King was damaged, independent of amount. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|King")
	int32 KingDamageEventCount = 0;

	/** Seconds at least one enemy was inside the King danger radius. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|King")
	float KingInDangerSeconds = 0.f;

	/** Number of threat reports received. Divides EnemiesNearKingSum. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|King")
	int32 KingThreatReportCount = 0;

	/** Highest simultaneous enemy count seen inside the danger radius. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|King")
	int32 PeakEnemiesNearKing = 0;

	/** Running sum of per-report enemy counts near the King. */
	UPROPERTY(BlueprintReadWrite, Category = "Round|King")
	float EnemiesNearKingSum = 0.f;

	/** Clears every accumulator. Call at the start of each round. */
	void Reset()
	{
		*this = FPlayerRoundStats();
	}

	float GetAverageGuardDistance() const
	{
		return PositionSampleCount > 0 ? GuardDistanceSampleSum / static_cast<float>(PositionSampleCount) : 0.f;
	}

	float GetAverageClustering() const
	{
		return PositionSampleCount > 0 ? ClusteringSampleSum / static_cast<float>(PositionSampleCount) : 0.f;
	}

	/** Mean number of enemies inside the King danger radius per threat report. */
	float GetAverageEnemiesNearKing() const
	{
		return KingThreatReportCount > 0 ? EnemiesNearKingSum / static_cast<float>(KingThreatReportCount) : 0.f;
	}

	/** Mean seconds the player needed per enemy killed. 0 when nothing died. */
	float GetAverageTimeToKillSeconds() const
	{
		return EnemiesKilled > 0 ? TotalTimeToKillSeconds / static_cast<float>(EnemiesKilled) : 0.f;
	}
};

/**
 * The analysed result of one round, produced from FPlayerRoundStats (Phase 3).
 * Every Score field is normalised 0..1 so the strategy selector can compare them directly.
 * Blueprint read-only: this is the output of the analysis step, not designer-authored data.
 */
USTRUCT(BlueprintType)
struct FPlayerBehaviourProfile
{
	GENERATED_BODY()

	/** False until a round has actually been analysed. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile")
	bool bIsValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Profile")
	int32 RoundNumber = 0;

	// --- Defence ---------------------------------------------------------------------

	/** Per-side defence strength, normalised so the best-defended side reads 1.0. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Defence")
	FDirectionalStats SideDefenceStrength;

	UPROPERTY(BlueprintReadOnly, Category = "Profile|Defence")
	ECompassDirection StrongestSide = ECompassDirection::None;

	UPROPERTY(BlueprintReadOnly, Category = "Profile|Defence")
	ECompassDirection WeakestSide = ECompassDirection::None;

	/** Side the King took the most damage from last round. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Defence")
	ECompassDirection PrimaryKingDamageDirection = ECompassDirection::None;

	// --- Positioning -------------------------------------------------------------------

	/** Mean guard distance from the King, in Unreal units. Kept raw for spawn-distance tuning. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Positioning")
	float AverageGuardDistanceFromKing = 0.f;

	/** 0 = guards spread out, 1 = guards stacked together. Drives Anti-Cluster scoring. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Positioning")
	float ClusteringScore = 0.f;

	/** 0 = King always escorted, 1 = King routinely left alone. Drives King Rush scoring. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Positioning")
	float KingExposureScore = 0.f;

	// --- Playstyle -----------------------------------------------------------------------

	/** 0 = turtling near the King, 1 = constantly pushing out at enemies. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Playstyle")
	float AggressionScore = 0.f;

	/** Guard slot the player controlled the longest. INDEX_NONE if unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Playstyle")
	int32 MostControlledGuardIndex = INDEX_NONE;

	/** Guard switches per minute. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Playstyle")
	float GuardSwitchFrequency = 0.f;

	// --- Combat performance ----------------------------------------------------------------

	/** 0 = enemies survived or took a long time to kill, 1 = the player cleared them fast. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Combat")
	float KillEfficiency = 0.f;

	/** Enemy type that did the most damage relative to how easily it died. */
	UPROPERTY(BlueprintReadOnly, Category = "Profile|Combat")
	EEnemyType BestPerformingEnemyType = EEnemyType::None;

	void Reset() { *this = FPlayerBehaviourProfile(); }

	// Rating helpers - let the director rules read qualitatively.
	EBehaviourRating GetAggressionRating() const { return BehaviourRating::FromScore(AggressionScore); }
	EBehaviourRating GetClusteringRating() const { return BehaviourRating::FromScore(ClusteringScore); }
	EBehaviourRating GetKingExposureRating() const { return BehaviourRating::FromScore(KingExposureScore); }
	EBehaviourRating GetKillEfficiencyRating() const { return BehaviourRating::FromScore(KillEfficiency); }
	EBehaviourRating GetSideDefenceRating(ECompassDirection Side) const
	{
		return BehaviourRating::FromScore(SideDefenceStrength.Get(Side));
	}
};
