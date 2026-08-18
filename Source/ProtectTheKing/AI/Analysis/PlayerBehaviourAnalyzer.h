#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/Types/CombatDirectionTypes.h"
#include "AI/Types/EnemyTypes.h"
#include "AI/Types/PlayerBehaviourTypes.h"
#include "PlayerBehaviourAnalyzer.generated.h"

class UPlayerPatternTracker;

DECLARE_LOG_CATEGORY_EXTERN(LogPlayerBehaviour, Log, All);

/** Broadcast after a round has been analysed. The Enemy AI Director will subscribe here. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBehaviourProfileReady, const FPlayerBehaviourProfile&, Profile);

/**
 * Turns one round of raw telemetry into a description of how the player played.
 *
 * Phase 2 recorded what happened; this component interprets it. It answers "what kind of
 * gameplay was that?" and stops there - it holds no opinion about how enemies should respond,
 * never picks a strategy, and never spawns anything. That is later work which consumes the
 * FPlayerBehaviourProfile produced here.
 *
 * Every calculation is deterministic and rule-based: the same FPlayerRoundStats always yields
 * the same profile. All scores are bounded, all divisions are guarded, and a completely empty
 * round produces a neutral profile with bIsValid false rather than NaN or a wrong conclusion.
 *
 * Usage is stats-in, profile-out:
 *     const FPlayerBehaviourProfile Profile = Analyzer->AnalyzeRoundStats(Stats);
 *
 * The tracker is optional. BindToTracker wires the Phase 2 completion event straight into
 * AnalyzeRoundStats for convenience, but the analyzer never requires a tracker to exist and
 * the tracker knows nothing about the analyzer, so the dependency runs one way only.
 */
UCLASS(ClassGroup = (ProtectTheKing), meta = (BlueprintSpawnableComponent), DisplayName = "Player Behaviour Analyzer")
class PROTECTTHEKING_API UPlayerBehaviourAnalyzer : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerBehaviourAnalyzer();

	// ==========================================================================================
	// Main entry point
	// ==========================================================================================

	/**
	 * Analyses one completed round and returns the resulting profile.
	 * Also caches the result (GetLastProfile) and broadcasts OnBehaviourProfileReady.
	 *
	 * Safe to call with default-constructed stats: the returned profile is neutral and its
	 * bIsValid is false, so callers can tell "nothing happened" from "the player turtled".
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Behaviour|Analysis")
	FPlayerBehaviourProfile AnalyzeRoundStats(const FPlayerRoundStats& RoundStats);

	/** The most recent result. bIsValid is false until AnalyzeRoundStats has produced one. */
	UFUNCTION(BlueprintPure, Category = "Player Behaviour|Analysis")
	FPlayerBehaviourProfile GetLastProfile() const { return LastProfile; }

	/** Drops the cached profile. */
	UFUNCTION(BlueprintCallable, Category = "Player Behaviour|Analysis")
	void ClearLastProfile();

	/**
	 * Optional convenience wiring: analyse automatically whenever the tracker finishes a round.
	 * Pass null to unbind. Phase 9 owns the real gameplay wiring; this just saves a Blueprint
	 * node during development.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Behaviour|Analysis")
	void BindToTracker(UPlayerPatternTracker* Tracker);

	/** Fires at the end of AnalyzeRoundStats, carrying the finished profile. */
	UPROPERTY(BlueprintAssignable, Category = "Player Behaviour|Events")
	FOnBehaviourProfileReady OnBehaviourProfileReady;

	// ==========================================================================================
	// Debug
	// ==========================================================================================

	/** Multi-line dump of every field in a profile. */
	UFUNCTION(BlueprintPure, Category = "Player Behaviour|Debug")
	FString GetProfileDebugString(const FPlayerBehaviourProfile& Profile) const;

	/** Writes GetProfileDebugString to LogPlayerBehaviour at Log verbosity. */
	UFUNCTION(BlueprintCallable, Category = "Player Behaviour|Debug")
	void LogProfile(const FPlayerBehaviourProfile& Profile) const;

	// ==========================================================================================
	// Tuning - general
	// ==========================================================================================

	/**
	 * Floor applied to round duration before any per-minute rate is computed.
	 * Without it a two-second round would report an enormous attack rate. Rounds shorter than
	 * this still analyse, but their rates are measured as if they lasted this long.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|General", meta = (ClampMin = "0.1"))
	float MinimumAnalysableRoundSeconds = 5.f;

	/**
	 * Guard distance from the King that reads as fully extended, in Unreal units.
	 * Used to normalise guard proximity inside the King exposure score. The profile still
	 * reports AverageGuardDistanceFromKing in raw units, as Phase 1 specified.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|General", meta = (ClampMin = "1.0"))
	float ReferenceGuardDistance = 1200.f;

	/**
	 * Scale the per-side defence values by how consistently the King was escorted.
	 * On (default): a player who defended one side well but abandoned the King often reads
	 * below 1.0 even on their best side, and the relative ordering between sides is preserved.
	 * Off: pure relative distribution, where the best side always reads exactly 1.0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Defence")
	bool bScaleDefenceByKingCoverage = true;

	/**
	 * How far apart the best and worst sides must be before naming a strongest and weakest side.
	 *
	 * Measured on the relative distribution, where the best side is 1.0 by construction, so the
	 * spread is simply 1.0 minus the weakest side. A player who covered all four sides evenly has
	 * no weak flank, and reporting one anyway would hand the Enemy AI Director a direction that
	 * the round data does not actually support. Below this threshold both sides report None.
	 *
	 * The four defence values themselves are never affected - only whether a direction is named.
	 * At the 0.05 default, a side has to sit at least 5% below the best side to be called weakest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Defence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DirectionalDifferenceThreshold = 0.05f;

	// ==========================================================================================
	// Tuning - aggression
	// ==========================================================================================

	/** Attack rate that reads as fully aggressive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Aggression", meta = (ClampMin = "0.01"))
	float ExpectedAttacksPerMinute = 30.f;

	/** Damage output rate that reads as fully aggressive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Aggression", meta = (ClampMin = "0.01"))
	float ExpectedDamagePerMinute = 300.f;

	/** Kill rate that reads as fully aggressive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Aggression", meta = (ClampMin = "0.01"))
	float ExpectedKillsPerMinute = 10.f;

	/** Weights are normalised by their own sum before use, so retuning cannot break the 0..1 range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Aggression", meta = (ClampMin = "0.0"))
	float AttackRateWeight = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Aggression", meta = (ClampMin = "0.0"))
	float DamageRateWeight = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Aggression", meta = (ClampMin = "0.0"))
	float CombatTimeWeight = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Aggression", meta = (ClampMin = "0.0"))
	float KillContributionWeight = 0.20f;

	// ==========================================================================================
	// Tuning - King exposure
	// ==========================================================================================

	/** Total King damage in one round that reads as fully exposed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.01"))
	float ExpectedKingDamagePerRound = 200.f;

	/** Rate of separate King damage events that reads as fully exposed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.01"))
	float ExpectedKingDamageEventsPerMinute = 6.f;

	/** Mean concurrent enemies inside the danger radius that reads as fully exposed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.01"))
	float ExpectedEnemiesNearKing = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.0"))
	float KingDamageWeight = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.0"))
	float DangerTimeWeight = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.0"))
	float ThreatCountWeight = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.0"))
	float NearbyEnemyWeight = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|King Exposure", meta = (ClampMin = "0.0"))
	float GuardProximityWeight = 0.15f;

	// ==========================================================================================
	// Tuning - kill efficiency
	// ==========================================================================================

	/** Time-to-kill that reads as fully efficient. Faster than this still reads 1.0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Kill Efficiency", meta = (ClampMin = "0.01"))
	float ExpectedTimeToKillSeconds = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Kill Efficiency", meta = (ClampMin = "0.0"))
	float ClearRateWeight = 0.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Kill Efficiency", meta = (ClampMin = "0.0"))
	float KillSpeedWeight = 0.40f;

	// ==========================================================================================
	// Tuning - enemy effectiveness
	// ==========================================================================================

	/** Reaching the King matters most, so it carries the heaviest weight by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Enemy Effectiveness", meta = (ClampMin = "0.0"))
	float EnemyKingDamageWeight = 0.40f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Enemy Effectiveness", meta = (ClampMin = "0.0"))
	float EnemyGuardDamageWeight = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Enemy Effectiveness", meta = (ClampMin = "0.0"))
	float EnemyGuardsDownedWeight = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Behaviour|Tuning|Enemy Effectiveness", meta = (ClampMin = "0.0"))
	float EnemySurvivalWeight = 0.20f;

private:
	/** Bound to the tracker completion delegate by BindToTracker. */
	UFUNCTION()
	void HandleRoundTrackingFinished(const FPlayerRoundStats& CompletedRoundStats);

	/** True when the round holds enough to interpret. Drives the profile bIsValid flag. */
	static bool HasAnalysableData(const FPlayerRoundStats& RoundStats);

	/** Round duration with MinimumAnalysableRoundSeconds applied, for rate calculations. */
	float GetRateReferenceSeconds(const FPlayerRoundStats& RoundStats) const;

	/** Fills SideDefenceStrength, StrongestSide and WeakestSide. */
	void CalculateDefenceDistribution(const FPlayerRoundStats& RoundStats, FPlayerBehaviourProfile& OutProfile) const;

	float CalculateAggression(const FPlayerRoundStats& RoundStats) const;
	float CalculateKingExposure(const FPlayerRoundStats& RoundStats) const;
	float CalculateKillEfficiency(const FPlayerRoundStats& RoundStats) const;

	/** Longest-held guard slot, or INDEX_NONE when nobody was controlled. Ties take the lowest slot. */
	static int32 SelectMostControlledGuard(const FPlayerRoundStats& RoundStats);

	/**
	 * Kills credited to the player-controlled guard, summed from the per-enemy-type rows.
	 * FPlayerRoundStats::EnemiesKilled counts every death in the round including AI guard kills,
	 * so it overstates what the human actually did.
	 */
	static int32 SumPlayerAttributedKills(const FPlayerRoundStats& RoundStats);

	/** Highest weighted effectiveness, or None. Ties take the lowest enum value. */
	EEnemyType SelectMostEffectiveEnemyType(const FPlayerRoundStats& RoundStats) const;

	UPROPERTY(VisibleAnywhere, Category = "Player Behaviour|State", meta = (AllowPrivateAccess = "true"))
	FPlayerBehaviourProfile LastProfile;

	/** Tracker this analyzer is currently subscribed to, so BindToTracker can unbind cleanly. */
	UPROPERTY()
	TWeakObjectPtr<UPlayerPatternTracker> BoundTracker;
};
