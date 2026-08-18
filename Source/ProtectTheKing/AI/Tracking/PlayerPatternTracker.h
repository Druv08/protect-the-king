#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/Types/CombatDirectionTypes.h"
#include "AI/Types/EnemyTypes.h"
#include "AI/Types/PlayerBehaviourTypes.h"
#include "PlayerPatternTracker.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPlayerPattern, Log, All);

/** Broadcast once EndTrackingRound has finalised the raw statistics. Phase 3 subscribes here. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoundTrackingFinished, const FPlayerRoundStats&, CompletedRoundStats);

/**
 * Records what the player did during one round.
 *
 * This component only gathers facts - how far guards stood from the King, which side took
 * damage, how often the player switched guards. It deliberately does NOT decide whether the
 * player was aggressive, defensive, clustered or exposed; that interpretation is Phase 3 work
 * and reads the FPlayerRoundStats this component produces.
 *
 * Ownership: attach to whichever actor owns the round lifecycle (GameMode, GameState or a
 * RoundManager). It is a plain UActorComponent, not a singleton or subsystem, so multiple
 * instances can coexist and tests can create one directly.
 *
 * Data flow:
 *   StartTrackingRound -> gameplay systems call the Record* functions -> EndTrackingRound
 *   -> GetLastCompletedRoundStats / OnRoundTrackingFinished.
 *
 * Compass convention: inherited from Phase 1 CombatDirectionTypes.h, where +X is North and
 * +Y is East. If the battlefield is not built along those axes, set BattlefieldNorthYawDegrees
 * rather than editing the shared enum - see that property for details.
 */
UCLASS(ClassGroup = (ProtectTheKing), meta = (BlueprintSpawnableComponent), DisplayName = "Player Pattern Tracker")
class PROTECTTHEKING_API UPlayerPatternTracker : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerPatternTracker();

	// ==========================================================================================
	// Round lifecycle
	// ==========================================================================================

	/**
	 * Clears the current-round accumulators and begins collecting.
	 * Safe to call while already tracking - the in-progress round is discarded, not finalised.
	 * @param RoundNumber	Round identifier supplied by the round/wave system. Stored verbatim.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Lifecycle")
	void StartTrackingRound(int32 RoundNumber = 0);

	/**
	 * Stops collecting, closes out the open control/aggression timers, stamps the round
	 * duration and copies the result into LastCompletedRoundStats.
	 * Performs no analysis - every value stays exactly as it was recorded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Lifecycle")
	void EndTrackingRound();

	/** Wipes the current round in place and keeps tracking. Does not touch the last completed round. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Lifecycle")
	void ResetRoundTracking();

	/** Discards both the current and the last completed round. Mainly for returning to a menu. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Lifecycle")
	void ClearAllRoundData();

	// ==========================================================================================
	// Integration - King
	// ==========================================================================================

	/**
	 * Supplies the actor to measure everything against. Phase 2 deliberately does not define a
	 * King class; anything with a world location works, so the King team stays free to build it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Integration")
	void SetKingActor(AActor* InKingActor);

	/** Fixed King position, for tests and for maps where the King never moves. Takes priority over the actor. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Integration")
	void SetKingLocationOverride(FVector WorldLocation);

	/** Drops the override and falls back to the King actor. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Integration")
	void ClearKingLocationOverride();

	/** Current King position. bOutIsValid is false when no actor and no override are set. */
	UFUNCTION(BlueprintPure, Category = "Player Pattern|Integration")
	FVector GetKingWorldLocation(bool& bOutIsValid) const;

	// ==========================================================================================
	// Integration - Guards
	// ==========================================================================================

	/**
	 * Associates a guard slot with an actor so periodic sampling can read its position.
	 * Registration is optional: systems that prefer to push positions can call
	 * RecordGuardSpatialSample instead and never register anything.
	 *
	 * @param GuardSlotIndex	0-based slot. Matches FPlayerRoundStats::GuardControlSeconds and
	 *							FPlayerBehaviourProfile::MostControlledGuardIndex from Phase 1.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Integration")
	void RegisterGuard(int32 GuardSlotIndex, AActor* GuardActor);

	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Integration")
	void UnregisterGuard(int32 GuardSlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Integration")
	void ClearRegisteredGuards();

	// ==========================================================================================
	// Recording - guard control
	// ==========================================================================================

	/**
	 * Call when the player takes control of a different guard.
	 * Bills the elapsed time to the outgoing guard, then increments the switch count.
	 * The first call of a round establishes control without counting as a switch.
	 *
	 * @param NewGuardSlotIndex	Slot now under player control, or INDEX_NONE to release control.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordGuardSwitch(int32 NewGuardSlotIndex);

	/**
	 * Marks whether the controlled guard is currently fighting rather than holding position.
	 * While true, elapsed time accrues into TimeSpentAggressiveSeconds on each sample.
	 * This is a raw stopwatch - it does not score aggression.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void SetControlledGuardEngaged(bool bEngaged);

	/** Adds combat time directly, for systems that measure their own engagement windows. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordAggressiveTime(float Seconds);

	// ==========================================================================================
	// Recording - spatial sampling
	// ==========================================================================================

	/**
	 * Submits one snapshot of where the guards are.
	 * Folds into four running aggregates (distance sum, clustering sum, per-side presence
	 * seconds, unguarded seconds) - individual positions are never stored.
	 * Ignored when the King location is unknown, since every derived value is King-relative.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordGuardSpatialSample(const TArray<FVector>& GuardWorldLocations);

	/** Samples the registered guard actors immediately. The periodic timer calls this. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void SampleRegisteredGuardsNow();

	// ==========================================================================================
	// Recording - player combat
	// ==========================================================================================

	/** One attack committed by the player-controlled guard. bDidHit also bumps the landed count. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordPlayerAttack(bool bDidHit = false);

	/**
	 * Damage the player-controlled guard dealt to an enemy.
	 * Buckets into DamageDealtBySide by where the enemy stood relative to the King.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordPlayerDamageDealt(float DamageAmount, FVector TargetWorldLocation);

	// ==========================================================================================
	// Recording - enemies
	// ==========================================================================================

	/** Enemies of one type entering the round. Feeds the per-type spawn count. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordEnemySpawned(EEnemyType EnemyType, int32 Count = 1);

	/**
	 * An enemy died.
	 * @param EnemyWorldLocation	Where it died - buckets into EnemyKillsBySide.
	 * @param TimeToKillSeconds		Lifetime, or how long it took to bring down. Pass 0 if unknown.
	 * @param bKilledByPlayer		True for the player-controlled guard, false for AI guards or
	 *								environment. Only player kills raise KilledByPlayerCount.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordEnemyKill(EEnemyType EnemyType, FVector EnemyWorldLocation, float TimeToKillSeconds = 0.f, bool bKilledByPlayer = true);

	/**
	 * Per-type outcomes reported by the enemy/guard systems: damage it dealt, guards it downed,
	 * how long it survived. Every argument is additive, so partial reports are fine.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordEnemyPerformance(EEnemyType EnemyType, float DamageDealtToGuards = 0.f, int32 GuardsDowned = 0, float LifetimeSeconds = 0.f);

	// ==========================================================================================
	// Recording - King
	// ==========================================================================================

	/**
	 * The King took damage. Resolves the compass side from the attacker position and adds the
	 * amount to KingDamageTakenBySide, plus the per-type DamageDealtToKing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordKingDamage(float DamageAmount, FVector AttackerWorldLocation, EEnemyType SourceEnemyType = EEnemyType::None);

	/** Same as RecordKingDamage but for callers that already know the side. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordKingDamageFromDirection(float DamageAmount, ECompassDirection AttackDirection, EEnemyType SourceEnemyType = EEnemyType::None);

	/**
	 * Reports enemies currently inside the King danger radius.
	 * @param EnemiesInsideDangerRadius	Concurrent count. Updates the peak and the running sum.
	 * @param DurationSeconds			Seconds this state held. Adds to KingInDangerSeconds when
	 *									the count is above zero. Pass 0 for an instantaneous report.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Record")
	void RecordKingThreat(int32 EnemiesInsideDangerRadius, float DurationSeconds = 0.f);

	// ==========================================================================================
	// Read-only access
	// ==========================================================================================

	/** Snapshot of the round in progress. Returned by value - Blueprints cannot write back. */
	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	FPlayerRoundStats GetCurrentRoundStats() const { return CurrentRoundStats; }

	/** The round most recently closed by EndTrackingRound. This is the Phase 3 input. */
	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	FPlayerRoundStats GetLastCompletedRoundStats() const { return LastCompletedRoundStats; }

	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	bool IsTracking() const { return bIsTracking; }

	/** False until at least one round has been finalised. Check before reading the completed stats. */
	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	bool HasCompletedRound() const { return bHasCompletedRound; }

	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	int32 GetCurrentControlledGuardIndex() const { return CurrentControlledGuardIndex; }

	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	int32 GetPreviousControlledGuardIndex() const { return PreviousControlledGuardIndex; }

	/** Seconds since StartTrackingRound, or the final duration once the round has ended. */
	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	float GetElapsedRoundSeconds() const;

	/**
	 * Which side of the King a world position sits on, honouring BattlefieldNorthYawDegrees.
	 * Returns None while the King location is unknown.
	 */
	UFUNCTION(BlueprintPure, Category = "Player Pattern|Query")
	ECompassDirection GetDirectionFromKing(FVector WorldLocation) const;

	// ==========================================================================================
	// Debug
	// ==========================================================================================

	/** Multi-line dump of the current round raw counters. */
	UFUNCTION(BlueprintPure, Category = "Player Pattern|Debug")
	FString GetDebugSummaryString() const;

	/** Writes GetDebugSummaryString to LogPlayerPattern at Log verbosity. */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Debug")
	void LogRoundSummary() const;

	/**
	 * Time source when the component has no world, which is the case in isolated tests.
	 * Ignored during normal play, where world time is used instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Player Pattern|Debug")
	void AdvanceManualClock(float DeltaSeconds);

	/** Fires after EndTrackingRound has finalised the statistics. */
	UPROPERTY(BlueprintAssignable, Category = "Player Pattern|Events")
	FOnRoundTrackingFinished OnRoundTrackingFinished;

	// ==========================================================================================
	// Configuration
	// ==========================================================================================

	/** Sample the registered guards on a timer while a round is running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Pattern|Sampling")
	bool bAutoSampleGuardPositions = true;

	/**
	 * Seconds between automatic samples. 0.25-1.0 is the useful band: faster costs more for
	 * little extra fidelity, slower starts to miss short guard repositions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Pattern|Sampling", meta = (ClampMin = "0.05", ClampMax = "5.0", UIMin = "0.25", UIMax = "1.0"))
	float SpatialSampleIntervalSeconds = 0.5f;

	/** A guard inside this radius counts as escorting the King. Drives KingUnguardedSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Pattern|Sampling", meta = (ClampMin = "0.0"))
	float KingProtectionRadius = 800.f;

	/**
	 * Guard spread that reads as fully dispersed. Spread at or above this stores clustering 0;
	 * guards standing on top of each other store 1. Tune to the map - it is a scale reference,
	 * not a map dimension, so no battlefield size is assumed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Pattern|Sampling", meta = (ClampMin = "1.0"))
	float ClusteringReferenceDistance = 1500.f;

	/**
	 * Yaw in degrees of battlefield North, measured from Unreal +X.
	 * 0 keeps the Phase 1 convention (+X North, +Y East). Set this if the level is built along
	 * different axes - it rotates the King-relative offset before bucketing, so the shared
	 * ECompassDirection enum stays untouched for every other system.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Pattern|Battlefield", meta = (ClampMin = "-360.0", ClampMax = "360.0"))
	float BattlefieldNorthYawDegrees = 0.f;

	/** Upper bound on guard slots, so a bad index cannot balloon GuardControlSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player Pattern|Sampling", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxTrackedGuardSlots = 16;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** World time, falling back to the manual clock when there is no world. */
	float GetNowSeconds() const;

	/** Bills time since the last control change to the current guard and restamps the marker. */
	void AccumulateControlTime(float Now);

	/** Grows GuardControlSeconds to cover Slot. Returns false if the slot is out of range. */
	bool EnsureGuardSlot(int32 Slot);

	/** Shared body of RecordGuardSpatialSample and SampleRegisteredGuardsNow. */
	void ProcessSpatialSample(const TArray<FVector>& GuardWorldLocations, float DeltaSeconds);

	/** Per-type performance row, created on first use. */
	FEnemyTypeRoundPerformance& FindOrAddPerformance(EEnemyType EnemyType);

	void StartSampleTimer();
	void StopSampleTimer();

	UPROPERTY(VisibleAnywhere, Category = "Player Pattern|State", meta = (AllowPrivateAccess = "true"))
	FPlayerRoundStats CurrentRoundStats;

	UPROPERTY(VisibleAnywhere, Category = "Player Pattern|State", meta = (AllowPrivateAccess = "true"))
	FPlayerRoundStats LastCompletedRoundStats;

	UPROPERTY()
	TWeakObjectPtr<AActor> KingActor;

	/** Guard slot -> actor. Weak so a destroyed guard is skipped rather than left dangling. */
	UPROPERTY()
	TMap<int32, TWeakObjectPtr<AActor>> RegisteredGuards;

	FVector KingLocationOverride = FVector::ZeroVector;
	bool bUseKingLocationOverride = false;

	bool bIsTracking = false;
	bool bHasCompletedRound = false;
	bool bControlledGuardEngaged = false;

	int32 CurrentControlledGuardIndex = INDEX_NONE;
	int32 PreviousControlledGuardIndex = INDEX_NONE;

	float RoundStartTimeSeconds = 0.f;
	float RoundEndTimeSeconds = 0.f;
	float LastControlChangeTimeSeconds = 0.f;
	float LastSampleTimeSeconds = 0.f;
	float LastGuardSwitchTimeSeconds = 0.f;
	float ManualClockSeconds = 0.f;

	/**
	 * When the current engagement window opened. Kept separate from LastSampleTimeSeconds so
	 * combat timing and spatial sampling cannot disturb each other.
	 */
	float EngagedSinceSeconds = 0.f;

	/** Throttles the missing-King warning to once per round. */
	bool bWarnedMissingKing = false;

	FTimerHandle SampleTimerHandle;
};
