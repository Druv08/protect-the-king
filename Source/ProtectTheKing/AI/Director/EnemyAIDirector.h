#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/Types/PlayerBehaviourTypes.h"
#include "AI/Types/StrategyTypes.h"
#include "EnemyAIDirector.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEnemyAIDirector, Log, All);

/**
 * Where the adaptive decision currently stands.
 *
 * Deliberately small. The only job here is to stop invalid sequences - a wave being built
 * before any profile exists, or a strategy arriving before one was asked for. Anything richer
 * belongs to the round/gameplay flow, not to this component.
 */
UENUM(BlueprintType)
enum class EAdaptiveDirectorState : uint8
{
	/** Not initialised, or the adaptive cycle has been cleared. */
	Idle						UMETA(DisplayName = "Idle"),
	/** A round is being played. The Director is waiting for it to finish. */
	RoundActive					UMETA(DisplayName = "Round Active"),
	/** A valid profile arrived and a strategy has been requested. Waiting on the selector. */
	AwaitingStrategySelection	UMETA(DisplayName = "Awaiting Strategy Selection"),
	/** A strategy has been chosen and stored. The wave generator can proceed. */
	StrategyReady				UMETA(DisplayName = "Strategy Ready"),
	/**
	 * The round produced nothing worth learning from, so no adaptive strategy exists for the
	 * next round. The wave generator should fall back to a neutral wave of its own choosing.
	 */
	AdaptationUnavailable		UMETA(DisplayName = "Adaptation Unavailable")
};

/** Why an adaptive decision could not be completed. Reported with OnAdaptiveDecisionFailed. */
UENUM(BlueprintType)
enum class EAdaptiveDecisionFailure : uint8
{
	None					UMETA(Hidden),
	/** Adaptive logic is switched off, so nothing was attempted. */
	DirectorDisabled		UMETA(DisplayName = "Director Disabled"),
	/** The profile was flagged invalid by the analyzer - the round taught us nothing. */
	InvalidPlayerProfile	UMETA(DisplayName = "Invalid Player Profile"),
	/** A strategy arrived when none had been requested, or one was already stored. */
	UnexpectedStrategyResult UMETA(DisplayName = "Unexpected Strategy Result"),
	/** The strategy itself was unusable - None, non-finite, or out of the accepted range. */
	InvalidStrategyResult	UMETA(DisplayName = "Invalid Strategy Result"),
	/** A round number outside the supported range was supplied. */
	InvalidRoundNumber		UMETA(DisplayName = "Invalid Round Number")
};

/**
 * Everything the strategy selector needs to make its choice, gathered in one place.
 *
 * Carries the player profile by value rather than restating any of its fields, so there is no
 * second copy of the analysis to keep in sync.
 */
USTRUCT(BlueprintType)
struct FAdaptiveDecisionContext
{
	GENERATED_BODY()

	/** Round that just finished and produced the profile. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Context")
	int32 CompletedRound = 0;

	/** Round the chosen strategy will apply to. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Context")
	int32 TargetRound = 0;

	/** How strongly later systems should lean on this decision, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Context")
	float AdaptationStrength = 0.f;

	/** The analysed player behaviour this decision is based on. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Context")
	FPlayerBehaviourProfile PlayerProfile;

	/** Strategy used last time, or None. Supplied so the selector can weigh repetition itself. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Context")
	EEnemyStrategy PreviousStrategy = EEnemyStrategy::None;

	/** Recent strategies, newest first. The Director does not act on this - it only reports it. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Context")
	TArray<EEnemyStrategy> RecentStrategyHistory;

	/** How many rounds have produced usable player data so far. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Context")
	int32 ValidProfileCount = 0;
};

/** One completed adaptive decision, kept for history and debugging. */
USTRUCT(BlueprintType)
struct FAdaptiveDecisionRecord
{
	GENERATED_BODY()

	/** Round the decision applied to. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Decision")
	int32 TargetRound = 0;

	/** The full result as the selector supplied it, including its score and reasoning. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Decision")
	FStrategyScore Selection;

	/** Adaptation strength in force when this decision was made. */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Decision")
	float AdaptationStrength = 0.f;

	/**
	 * Whether this decision came from real analysed player behaviour.
	 * False means the round produced nothing usable and the wave generator fell back.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Adaptive Decision")
	bool bFromValidProfile = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerProfileAccepted, const FPlayerBehaviourProfile&, Profile, int32, TargetRound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStrategySelectionRequested, const FAdaptiveDecisionContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAdaptiveStrategyReady, const FStrategyScore&, Selection, int32, TargetRound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAdaptiveDecisionFailed, EAdaptiveDecisionFailure, Reason, const FString&, Details);

/**
 * Coordinates the adaptive enemy system. It moves information between the analyzer, the
 * strategy selector and the wave generator, and decides when learning may be used at all.
 *
 * What it does NOT do, deliberately: score strategies, rank them, choose enemy composition,
 * decide spawn counts, directions or timing, or control any enemy. It receives a decision that
 * another system made and stores it.
 *
 * Fairness: every input comes from a round that has already finished. The Director never reads
 * live player input and never sees where the player is about to go. The model is "learn from
 * the last round, prepare the next one", not "counter what the player is doing right now".
 *
 * Phase 6 integration is by delegate, not by include. The Director broadcasts
 * OnStrategySelectionRequested with everything needed, and the selector calls
 * SubmitStrategySelection when it has an answer. Nothing here references a selector class, so
 * this compiles and its tests pass before Phase 6 is merged.
 */
UCLASS(ClassGroup = (ProtectTheKing), meta = (BlueprintSpawnableComponent), DisplayName = "Enemy AI Director")
class PROTECTTHEKING_API UEnemyAIDirector : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyAIDirector();

	// ==========================================================================================
	// Lifecycle
	// ==========================================================================================

	/** Clears everything and returns to Idle. Safe to call more than once. */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI Director|Lifecycle")
	void InitializeDirector();

	/**
	 * Marks a round as started. Clears the previous decision so a stale strategy cannot be
	 * mistaken for this round's, while keeping history and learning progress.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI Director|Lifecycle")
	bool BeginRound(int32 RoundNumber);

	/**
	 * Hands the Director the analysis of the round that just finished.
	 *
	 * A valid profile is stored and a strategy request is broadcast. An invalid one is recorded
	 * as such and adaptation is marked unavailable for the next round - the Director will not
	 * invent player tendencies it never observed.
	 *
	 * @return true when the profile was accepted as a basis for adaptation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI Director|Lifecycle")
	bool SubmitPlayerBehaviourProfile(const FPlayerBehaviourProfile& Profile);

	/**
	 * Receives the strategy selector's answer.
	 *
	 * Accepted only while a request is outstanding, so a result cannot arrive before a profile
	 * or be submitted twice for the same round. The score is not recalculated or second-guessed
	 * here; a valid selection is stored exactly as supplied.
	 *
	 * @return true when the selection was accepted and stored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI Director|Lifecycle")
	bool SubmitStrategySelection(const FStrategyScore& Selection);

	// ==========================================================================================
	// Reset
	// ==========================================================================================

	/**
	 * Drops the in-flight decision only: current profile and selected strategy.
	 * History, valid-profile count and adaptation strength survive, so a retried round does not
	 * cost the Director everything it has learned.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI Director|Lifecycle")
	void ResetCurrentDecision();

	/**
	 * Wipes everything the Director has learned: history, valid-profile count and adaptation
	 * strength, back to the configured starting value. For a new match, not a retried round.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI Director|Lifecycle")
	void ClearAdaptiveHistory();

	// ==========================================================================================
	// Queries
	// ==========================================================================================

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	EAdaptiveDirectorState GetDirectorState() const { return DirectorState; }

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	int32 GetCurrentRound() const { return CurrentRound; }

	/** Round the next decision applies to. Zero before the first round begins. */
	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	int32 GetTargetRound() const;

	/** 0..1. How strongly Phase 7 should follow the adaptive decision. */
	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	float GetAdaptationStrength() const;

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	bool HasValidPlayerProfile() const { return bHasValidProfile; }

	/** The profile currently driving adaptation. Check HasValidPlayerProfile first. */
	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	FPlayerBehaviourProfile GetCurrentPlayerProfile() const { return CurrentProfile; }

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	FStrategyScore GetSelectedStrategy() const { return SelectedStrategy; }

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	EEnemyStrategy GetPreviousStrategy() const;

	/** Recent strategies, newest first. Informational - the Director never acts on it. */
	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	TArray<EEnemyStrategy> GetRecentStrategyHistory() const;

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	TArray<FAdaptiveDecisionRecord> GetDecisionHistory() const { return DecisionHistory; }

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	int32 GetValidProfileCount() const { return ValidProfileCount; }

	/**
	 * Whether Phase 7 may build the next wave.
	 * True in StrategyReady (an adaptive strategy exists) and in AdaptationUnavailable (build a
	 * neutral wave instead). The Director does not construct either.
	 */
	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	bool IsReadyForWaveGeneration() const;

	/** The context that was, or would be, handed to the strategy selector. */
	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Query")
	FAdaptiveDecisionContext BuildDecisionContext() const;

	// ==========================================================================================
	// Debug
	// ==========================================================================================

	UFUNCTION(BlueprintPure, Category = "Enemy AI Director|Debug")
	FString GetDirectorDebugSummary() const;

	UFUNCTION(BlueprintCallable, Category = "Enemy AI Director|Debug")
	void LogDirectorState() const;

	// ==========================================================================================
	// Events
	// ==========================================================================================

	/** A profile was accepted as a basis for adaptation. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy AI Director|Events")
	FOnPlayerProfileAccepted OnPlayerProfileAccepted;

	/**
	 * The Phase 6 hook. Carries everything the selector needs; it replies through
	 * SubmitStrategySelection. Bind in Blueprint or with AddDynamic in C++.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Enemy AI Director|Events")
	FOnStrategySelectionRequested OnStrategySelectionRequested;

	/** A strategy has been stored and the wave generator may proceed. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy AI Director|Events")
	FOnAdaptiveStrategyReady OnAdaptiveStrategyReady;

	/** Something prevented an adaptive decision. Includes the neutral-fallback case. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy AI Director|Events")
	FOnAdaptiveDecisionFailed OnAdaptiveDecisionFailed;

	// ==========================================================================================
	// Configuration
	// ==========================================================================================

	/** Master switch. When off, profiles are refused and no requests are broadcast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Config")
	bool bAdaptiveLogicEnabled = true;

	/** Adaptation strength before any round has been analysed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Adaptation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StartingAdaptationStrength = 0.25f;

	/** Added per round that produced a usable profile. Rounds that taught nothing add nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Adaptation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdaptationIncreasePerValidRound = 0.15f;

	/** Ceiling on adaptation strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Adaptation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaximumAdaptationStrength = 1.f;

	/** How many past decisions to keep. Older entries are dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Config", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxDecisionHistoryEntries = 10;

	/**
	 * Whether to range-check the selector's score.
	 *
	 * Phase 1 does not document a range for FStrategyScore::Score, and Phase 6 is being written
	 * separately. The default assumes the project's usual 0..1 convention; turn this off, or
	 * widen the bounds below, if the selector produces unbounded scores. Non-finite scores are
	 * rejected either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Validation")
	bool bValidateStrategyScoreRange = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Validation", meta = (EditCondition = "bValidateStrategyScoreRange"))
	float MinValidStrategyScore = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Validation", meta = (EditCondition = "bValidateStrategyScoreRange"))
	float MaxValidStrategyScore = 1.f;

	/**
	 * Largest round number accepted. Guards the arithmetic rather than capping the game - the
	 * project defines no round total, but CurrentRound + 1 must not overflow.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI Director|Config", meta = (ClampMin = "1"))
	int32 MaxSupportedRoundNumber = 100000;

private:
	/** Records the outcome, appends to history and moves to the terminal state for this round. */
	void FinaliseDecision(const FStrategyScore& Selection, bool bFromValidProfile);

	/** Logs, broadcasts OnAdaptiveDecisionFailed and returns false, so callers can one-line it. */
	bool FailDecision(EAdaptiveDecisionFailure Reason, const FString& Details);

	/** True when the selection is usable. OutReason explains a rejection. */
	bool IsSelectionAcceptable(const FStrategyScore& Selection, FString& OutReason) const;

	UPROPERTY(VisibleAnywhere, Category = "Enemy AI Director|State", meta = (AllowPrivateAccess = "true"))
	EAdaptiveDirectorState DirectorState = EAdaptiveDirectorState::Idle;

	UPROPERTY(VisibleAnywhere, Category = "Enemy AI Director|State", meta = (AllowPrivateAccess = "true"))
	int32 CurrentRound = 0;

	UPROPERTY(VisibleAnywhere, Category = "Enemy AI Director|State", meta = (AllowPrivateAccess = "true"))
	FPlayerBehaviourProfile CurrentProfile;

	UPROPERTY(VisibleAnywhere, Category = "Enemy AI Director|State", meta = (AllowPrivateAccess = "true"))
	FStrategyScore SelectedStrategy;

	UPROPERTY(VisibleAnywhere, Category = "Enemy AI Director|State", meta = (AllowPrivateAccess = "true"))
	TArray<FAdaptiveDecisionRecord> DecisionHistory;

	/** Rounds that produced a usable profile. Drives adaptation strength. */
	UPROPERTY(VisibleAnywhere, Category = "Enemy AI Director|State", meta = (AllowPrivateAccess = "true"))
	int32 ValidProfileCount = 0;

	bool bHasValidProfile = false;
};
