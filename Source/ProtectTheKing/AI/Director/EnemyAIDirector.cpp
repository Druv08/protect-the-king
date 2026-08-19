#include "AI/Director/EnemyAIDirector.h"

DEFINE_LOG_CATEGORY(LogEnemyAIDirector);

namespace
{
	const TCHAR* StateToString(EAdaptiveDirectorState State)
	{
		switch (State)
		{
		case EAdaptiveDirectorState::Idle:						return TEXT("Idle");
		case EAdaptiveDirectorState::RoundActive:				return TEXT("RoundActive");
		case EAdaptiveDirectorState::AwaitingStrategySelection:	return TEXT("AwaitingStrategySelection");
		case EAdaptiveDirectorState::StrategyReady:				return TEXT("StrategyReady");
		case EAdaptiveDirectorState::AdaptationUnavailable:		return TEXT("AdaptationUnavailable");
		default:												return TEXT("Unknown");
		}
	}

	const TCHAR* StrategyToString(EEnemyStrategy Strategy)
	{
		switch (Strategy)
		{
		case EEnemyStrategy::DirectAssault:		return TEXT("DirectAssault");
		case EEnemyStrategy::WeakSideFlank:		return TEXT("WeakSideFlank");
		case EEnemyStrategy::KingRush:			return TEXT("KingRush");
		case EEnemyStrategy::SplitAttack:		return TEXT("SplitAttack");
		case EEnemyStrategy::PincerAttack:		return TEXT("PincerAttack");
		case EEnemyStrategy::AntiCluster:		return TEXT("AntiCluster");
		case EEnemyStrategy::RangedSiege:		return TEXT("RangedSiege");
		case EEnemyStrategy::Assassination:		return TEXT("Assassination");
		case EEnemyStrategy::TankPush:			return TEXT("TankPush");
		case EEnemyStrategy::DecoyFlank:		return TEXT("DecoyFlank");
		case EEnemyStrategy::SupportFormation:	return TEXT("SupportFormation");
		default:								return TEXT("None");
		}
	}

	const TCHAR* DirectionToString(ECompassDirection Direction)
	{
		switch (Direction)
		{
		case ECompassDirection::North:	return TEXT("North");
		case ECompassDirection::East:	return TEXT("East");
		case ECompassDirection::South:	return TEXT("South");
		case ECompassDirection::West:	return TEXT("West");
		default:						return TEXT("None");
		}
	}

	const TCHAR* FailureToString(EAdaptiveDecisionFailure Reason)
	{
		switch (Reason)
		{
		case EAdaptiveDecisionFailure::DirectorDisabled:			return TEXT("DirectorDisabled");
		case EAdaptiveDecisionFailure::InvalidPlayerProfile:		return TEXT("InvalidPlayerProfile");
		case EAdaptiveDecisionFailure::UnexpectedStrategyResult:	return TEXT("UnexpectedStrategyResult");
		case EAdaptiveDecisionFailure::InvalidStrategyResult:		return TEXT("InvalidStrategyResult");
		case EAdaptiveDecisionFailure::InvalidRoundNumber:		return TEXT("InvalidRoundNumber");
		default:												return TEXT("None");
		}
	}
}

UEnemyAIDirector::UEnemyAIDirector()
{
	// Purely event driven - the Director acts between rounds, never per frame.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

// ==============================================================================================
// Lifecycle
// ==============================================================================================

void UEnemyAIDirector::InitializeDirector()
{
	ClearAdaptiveHistory();
	UE_LOG(LogEnemyAIDirector, Log, TEXT("Director initialised. Adaptive logic %s."),
		bAdaptiveLogicEnabled ? TEXT("enabled") : TEXT("disabled"));
}

bool UEnemyAIDirector::BeginRound(int32 RoundNumber)
{
	// Rounds are 1-based, and the upper bound exists so CurrentRound + 1 cannot overflow.
	if (RoundNumber < 1 || RoundNumber > FMath::Max(1, MaxSupportedRoundNumber))
	{
		return FailDecision(EAdaptiveDecisionFailure::InvalidRoundNumber,
			FString::Printf(TEXT("Round %d is outside the supported range 1..%d."),
				RoundNumber, FMath::Max(1, MaxSupportedRoundNumber)));
	}

	CurrentRound = RoundNumber;

	// Clear the in-flight decision so last round's strategy cannot be mistaken for this one's.
	// History and learning progress deliberately survive.
	CurrentProfile.Reset();
	SelectedStrategy = FStrategyScore();
	bHasValidProfile = false;

	DirectorState = EAdaptiveDirectorState::RoundActive;

	UE_LOG(LogEnemyAIDirector, Log, TEXT("Round %d started."), CurrentRound);
	return true;
}

bool UEnemyAIDirector::SubmitPlayerBehaviourProfile(const FPlayerBehaviourProfile& Profile)
{
	if (!bAdaptiveLogicEnabled)
	{
		DirectorState = EAdaptiveDirectorState::AdaptationUnavailable;
		return FailDecision(EAdaptiveDecisionFailure::DirectorDisabled,
			TEXT("Adaptive logic is disabled; the profile was not used."));
	}

	// A profile can only describe a round that was actually played.
	if (DirectorState != EAdaptiveDirectorState::RoundActive)
	{
		return FailDecision(EAdaptiveDecisionFailure::UnexpectedStrategyResult,
			FString::Printf(TEXT("Profile submitted while in %s; expected RoundActive."),
				StateToString(DirectorState)));
	}

	if (!Profile.bIsValid)
	{
		// The round taught us nothing. Say so plainly rather than inventing tendencies: no
		// strategy is requested, and the wave generator will fall back to a neutral wave.
		// ValidProfileCount is untouched, so this round does not raise adaptation strength.
		bHasValidProfile = false;
		CurrentProfile.Reset();
		DirectorState = EAdaptiveDirectorState::AdaptationUnavailable;

		FinaliseDecision(FStrategyScore(), /*bFromValidProfile=*/false);

		return FailDecision(EAdaptiveDecisionFailure::InvalidPlayerProfile,
			TEXT("Profile was flagged invalid; adaptation is unavailable for the next round."));
	}

	CurrentProfile = Profile;
	bHasValidProfile = true;
	++ValidProfileCount;

	DirectorState = EAdaptiveDirectorState::AwaitingStrategySelection;

	const int32 TargetRound = GetTargetRound();
	UE_LOG(LogEnemyAIDirector, Log,
		TEXT("Profile for round %d accepted (%d valid so far, adaptation %.2f). Requesting a strategy for round %d."),
		CurrentRound, ValidProfileCount, GetAdaptationStrength(), TargetRound);

	OnPlayerProfileAccepted.Broadcast(CurrentProfile, TargetRound);

	// The Phase 6 hand-off. Everything the selector needs travels in the context; the Director
	// holds no reference to whatever ends up listening.
	OnStrategySelectionRequested.Broadcast(BuildDecisionContext());
	return true;
}

bool UEnemyAIDirector::SubmitStrategySelection(const FStrategyScore& Selection)
{
	// Only accept a result while one is genuinely outstanding. This single check covers both
	// a result arriving before any profile and a second result for the same round, because the
	// first accepted one moves the state on.
	if (DirectorState != EAdaptiveDirectorState::AwaitingStrategySelection)
	{
		return FailDecision(EAdaptiveDecisionFailure::UnexpectedStrategyResult,
			FString::Printf(TEXT("Strategy '%s' submitted while in %s; no selection was outstanding."),
				StrategyToString(Selection.Strategy), StateToString(DirectorState)));
	}

	FString RejectionReason;
	if (!IsSelectionAcceptable(Selection, RejectionReason))
	{
		// The request stays outstanding so the selector can retry with a corrected result.
		return FailDecision(EAdaptiveDecisionFailure::InvalidStrategyResult, RejectionReason);
	}

	// Stored exactly as supplied. The score is the selector's business; re-deriving it here
	// would duplicate Phase 6 and let the two disagree.
	SelectedStrategy = Selection;
	DirectorState = EAdaptiveDirectorState::StrategyReady;

	const int32 TargetRound = GetTargetRound();
	FinaliseDecision(Selection, /*bFromValidProfile=*/true);

	UE_LOG(LogEnemyAIDirector, Log, TEXT("Strategy '%s' (score %.3f) stored for round %d."),
		StrategyToString(Selection.Strategy), Selection.Score, TargetRound);

	OnAdaptiveStrategyReady.Broadcast(SelectedStrategy, TargetRound);
	return true;
}

bool UEnemyAIDirector::IsSelectionAcceptable(const FStrategyScore& Selection, FString& OutReason) const
{
	if (Selection.Strategy == EEnemyStrategy::None || Selection.Strategy == EEnemyStrategy::Count)
	{
		OutReason = TEXT("Strategy is None; a real strategy was expected.");
		return false;
	}

	if (!FMath::IsFinite(Selection.Score))
	{
		OutReason = TEXT("Strategy score is not a finite number.");
		return false;
	}

	if (bValidateStrategyScoreRange)
	{
		// Tolerate a reversed min/max rather than rejecting everything if someone swaps them.
		const float Low = FMath::Min(MinValidStrategyScore, MaxValidStrategyScore);
		const float High = FMath::Max(MinValidStrategyScore, MaxValidStrategyScore);

		if (Selection.Score < Low || Selection.Score > High)
		{
			OutReason = FString::Printf(
				TEXT("Strategy score %.4f is outside the accepted range %.2f..%.2f. ")
				TEXT("If the selector uses a different scale, widen the range or turn off bValidateStrategyScoreRange."),
				Selection.Score, Low, High);
			return false;
		}
	}

	return true;
}

void UEnemyAIDirector::FinaliseDecision(const FStrategyScore& Selection, bool bFromValidProfile)
{
	const int32 TargetRound = GetTargetRound();

	// One record per target round. A repeated round replaces its entry rather than appending,
	// so a retried round cannot appear twice in the history.
	FAdaptiveDecisionRecord Record;
	Record.TargetRound = TargetRound;
	Record.Selection = Selection;
	Record.AdaptationStrength = GetAdaptationStrength();
	Record.bFromValidProfile = bFromValidProfile;

	const int32 ExistingIndex = DecisionHistory.IndexOfByPredicate(
		[TargetRound](const FAdaptiveDecisionRecord& Entry) { return Entry.TargetRound == TargetRound; });

	if (ExistingIndex != INDEX_NONE)
	{
		DecisionHistory[ExistingIndex] = Record;
	}
	else
	{
		DecisionHistory.Add(Record);
	}

	// Newest entries matter most, so trim from the front.
	const int32 MaxEntries = FMath::Max(1, MaxDecisionHistoryEntries);
	if (DecisionHistory.Num() > MaxEntries)
	{
		DecisionHistory.RemoveAt(0, DecisionHistory.Num() - MaxEntries);
	}
}

bool UEnemyAIDirector::FailDecision(EAdaptiveDecisionFailure Reason, const FString& Details)
{
	UE_LOG(LogEnemyAIDirector, Warning, TEXT("Adaptive decision failed (%s): %s"),
		FailureToString(Reason), *Details);

	OnAdaptiveDecisionFailed.Broadcast(Reason, Details);
	return false;
}

// ==============================================================================================
// Reset
// ==============================================================================================

void UEnemyAIDirector::ResetCurrentDecision()
{
	CurrentProfile.Reset();
	SelectedStrategy = FStrategyScore();
	bHasValidProfile = false;

	// Back to the round being playable again; history and learning progress are kept.
	DirectorState = CurrentRound > 0 ? EAdaptiveDirectorState::RoundActive : EAdaptiveDirectorState::Idle;

	UE_LOG(LogEnemyAIDirector, Log, TEXT("Current decision cleared. History and adaptation progress kept."));
}

void UEnemyAIDirector::ClearAdaptiveHistory()
{
	CurrentProfile.Reset();
	SelectedStrategy = FStrategyScore();
	DecisionHistory.Reset();

	bHasValidProfile = false;
	ValidProfileCount = 0;
	CurrentRound = 0;
	DirectorState = EAdaptiveDirectorState::Idle;

	UE_LOG(LogEnemyAIDirector, Log, TEXT("Adaptive history cleared. Director is Idle."));
}

// ==============================================================================================
// Queries
// ==============================================================================================

int32 UEnemyAIDirector::GetTargetRound() const
{
	if (CurrentRound <= 0)
	{
		return 0;
	}
	// Bounded by MaxSupportedRoundNumber in BeginRound, so this cannot overflow.
	return CurrentRound + 1;
}

float UEnemyAIDirector::GetAdaptationStrength() const
{
	// Grows only with rounds that actually taught the system something. A round the analyzer
	// could not interpret leaves this exactly where it was.
	//
	// Each setting is checked for finiteness before clamping. Clamp alone is not enough: every
	// comparison against NaN is false, so a NaN would pass straight through both bounds.
	auto SafeNormalised = [](float Value, float Fallback)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : Fallback;
	};

	const float Ceiling = SafeNormalised(MaximumAdaptationStrength, 1.f);
	const float Start = SafeNormalised(StartingAdaptationStrength, 0.f);
	const float PerRound = SafeNormalised(AdaptationIncreasePerValidRound, 0.f);

	const float Raw = Start + (PerRound * static_cast<float>(FMath::Max(0, ValidProfileCount)));

	// Start is guaranteed finite by now, so the fallback cannot leak a bad value either.
	return FMath::IsFinite(Raw) ? FMath::Clamp(Raw, 0.f, Ceiling) : FMath::Clamp(Start, 0.f, Ceiling);
}

EEnemyStrategy UEnemyAIDirector::GetPreviousStrategy() const
{
	// The most recent stored decision that actually chose something.
	for (int32 i = DecisionHistory.Num() - 1; i >= 0; --i)
	{
		if (DecisionHistory[i].Selection.Strategy != EEnemyStrategy::None)
		{
			return DecisionHistory[i].Selection.Strategy;
		}
	}
	return EEnemyStrategy::None;
}

TArray<EEnemyStrategy> UEnemyAIDirector::GetRecentStrategyHistory() const
{
	TArray<EEnemyStrategy> Recent;
	Recent.Reserve(DecisionHistory.Num());

	// Newest first, which is the order a selector weighing repetition will want.
	for (int32 i = DecisionHistory.Num() - 1; i >= 0; --i)
	{
		if (DecisionHistory[i].Selection.Strategy != EEnemyStrategy::None)
		{
			Recent.Add(DecisionHistory[i].Selection.Strategy);
		}
	}
	return Recent;
}

bool UEnemyAIDirector::IsReadyForWaveGeneration() const
{
	return DirectorState == EAdaptiveDirectorState::StrategyReady
		|| DirectorState == EAdaptiveDirectorState::AdaptationUnavailable;
}

FAdaptiveDecisionContext UEnemyAIDirector::BuildDecisionContext() const
{
	FAdaptiveDecisionContext Context;
	Context.CompletedRound = CurrentRound;
	Context.TargetRound = GetTargetRound();
	Context.AdaptationStrength = GetAdaptationStrength();
	Context.PlayerProfile = CurrentProfile;
	Context.PreviousStrategy = GetPreviousStrategy();
	Context.RecentStrategyHistory = GetRecentStrategyHistory();
	Context.ValidProfileCount = ValidProfileCount;
	return Context;
}

// ==============================================================================================
// Debug
// ==============================================================================================

FString UEnemyAIDirector::GetDirectorDebugSummary() const
{
	FString Out;
	Out.Reserve(1024);

	Out += TEXT("Enemy AI Director\n");
	Out += FString::Printf(TEXT("  round             : %d (next %d)\n"), CurrentRound, GetTargetRound());
	Out += FString::Printf(TEXT("  state             : %s\n"), StateToString(DirectorState));
	Out += FString::Printf(TEXT("  adaptive logic    : %s\n"), bAdaptiveLogicEnabled ? TEXT("enabled") : TEXT("disabled"));
	Out += FString::Printf(TEXT("  profile valid     : %s\n"), bHasValidProfile ? TEXT("yes") : TEXT("no"));
	Out += FString::Printf(TEXT("  adaptation        : %.2f (from %d valid round(s))\n"),
		GetAdaptationStrength(), ValidProfileCount);

	if (bHasValidProfile)
	{
		Out += TEXT("  player:\n");
		Out += FString::Printf(TEXT("    aggression      : %.2f\n"), CurrentProfile.AggressionScore);
		Out += FString::Printf(TEXT("    clustering      : %.2f\n"), CurrentProfile.ClusteringScore);
		Out += FString::Printf(TEXT("    king exposure   : %.2f\n"), CurrentProfile.KingExposureScore);
		Out += FString::Printf(TEXT("    kill efficiency : %.2f\n"), CurrentProfile.KillEfficiency);
		Out += FString::Printf(TEXT("    weakest side    : %s\n"), DirectionToString(CurrentProfile.WeakestSide));
		Out += FString::Printf(TEXT("    strongest side  : %s\n"), DirectionToString(CurrentProfile.StrongestSide));
		Out += FString::Printf(TEXT("    king dmg from   : %s\n"), DirectionToString(CurrentProfile.PrimaryKingDamageDirection));
	}
	else
	{
		Out += TEXT("  player            : no usable data from the last round\n");
	}

	Out += FString::Printf(TEXT("  selected strategy : %s"), StrategyToString(SelectedStrategy.Strategy));
	if (SelectedStrategy.Strategy != EEnemyStrategy::None)
	{
		Out += FString::Printf(TEXT(" (score %.3f)"), SelectedStrategy.Score);
		if (!SelectedStrategy.Reason.IsEmpty())
		{
			Out += FString::Printf(TEXT("\n    reason          : %s"), *SelectedStrategy.Reason);
		}
	}
	Out += TEXT("\n");

	Out += FString::Printf(TEXT("  previous strategy : %s\n"), StrategyToString(GetPreviousStrategy()));

	Out += FString::Printf(TEXT("  history (%d):\n"), DecisionHistory.Num());
	for (const FAdaptiveDecisionRecord& Record : DecisionHistory)
	{
		Out += FString::Printf(TEXT("    round %-4d %-18s adaptation %.2f  %s\n"),
			Record.TargetRound,
			StrategyToString(Record.Selection.Strategy),
			Record.AdaptationStrength,
			Record.bFromValidProfile ? TEXT("(learned)") : TEXT("(fallback)"));
	}

	return Out;
}

void UEnemyAIDirector::LogDirectorState() const
{
	UE_LOG(LogEnemyAIDirector, Log, TEXT("\n%s"), *GetDirectorDebugSummary());
}
