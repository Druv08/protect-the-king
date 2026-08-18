#include "AI/Analysis/PlayerBehaviourAnalyzer.h"

#include "AI/Tracking/PlayerPatternTracker.h"

DEFINE_LOG_CATEGORY(LogPlayerBehaviour);

namespace
{
	/** Division that returns Fallback instead of NaN or infinity when the divisor is unusable. */
	float SafeDivide(float Numerator, float Denominator, float Fallback = 0.f)
	{
		return FMath::Abs(Denominator) > KINDA_SMALL_NUMBER ? (Numerator / Denominator) : Fallback;
	}

	float Clamp01(float Value)
	{
		// Catches NaN as well: a NaN comparison is always false, so the explicit test is needed.
		if (!FMath::IsFinite(Value))
		{
			return 0.f;
		}
		return FMath::Clamp(Value, 0.f, 1.f);
	}

	/** Value measured against the level that should read 1.0, clamped into range. */
	float NormaliseAgainstReference(float Value, float Reference)
	{
		return Clamp01(SafeDivide(Value, Reference));
	}

	/**
	 * Weighted blend of 0..1 components.
	 * Divides by the summed weight rather than assuming the weights add to 1.0, so the result
	 * stays inside 0..1 no matter how the designer retunes them.
	 */
	struct FWeightedBlend
	{
		float WeightSum = 0.f;
		float ValueSum = 0.f;

		void Add(float Weight, float Component01)
		{
			const float SafeWeight = FMath::Max(0.f, Weight);
			WeightSum += SafeWeight;
			ValueSum += SafeWeight * Clamp01(Component01);
		}

		float GetResult() const
		{
			return WeightSum > KINDA_SMALL_NUMBER ? Clamp01(ValueSum / WeightSum) : 0.f;
		}
	};

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

	const TCHAR* RatingToString(EBehaviourRating Rating)
	{
		switch (Rating)
		{
		case EBehaviourRating::VeryLow:		return TEXT("Very Low");
		case EBehaviourRating::Low:			return TEXT("Low");
		case EBehaviourRating::Medium:		return TEXT("Medium");
		case EBehaviourRating::High:		return TEXT("High");
		case EBehaviourRating::VeryHigh:	return TEXT("Very High");
		default:							return TEXT("Unknown");
		}
	}
}

UPlayerBehaviourAnalyzer::UPlayerBehaviourAnalyzer()
{
	// Analysis is event driven - it runs once per round, never per frame.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

// ==============================================================================================
// Entry point
// ==============================================================================================

bool UPlayerBehaviourAnalyzer::HasAnalysableData(const FPlayerRoundStats& RoundStats)
{
	return RoundStats.RoundDurationSeconds > 0.f
		|| RoundStats.PositionSampleCount > 0
		|| RoundStats.EnemiesSpawned > 0
		|| RoundStats.EnemiesKilled > 0
		|| RoundStats.PlayerAttackCount > 0
		|| RoundStats.KingDamageEventCount > 0
		|| RoundStats.KingThreatReportCount > 0
		|| RoundStats.GuardSwitchCount > 0;
}

float UPlayerBehaviourAnalyzer::GetRateReferenceSeconds(const FPlayerRoundStats& RoundStats) const
{
	return FMath::Max(RoundStats.RoundDurationSeconds, FMath::Max(0.1f, MinimumAnalysableRoundSeconds));
}

FPlayerBehaviourProfile UPlayerBehaviourAnalyzer::AnalyzeRoundStats(const FPlayerRoundStats& RoundStats)
{
	FPlayerBehaviourProfile Profile;
	Profile.RoundNumber = RoundStats.RoundNumber;

	if (!HasAnalysableData(RoundStats))
	{
		// Nothing happened. Everything stays at its neutral default and bIsValid remains false,
		// so a consumer can distinguish "no data" from "the player did nothing aggressive".
		UE_LOG(LogPlayerBehaviour, Verbose,
			TEXT("Round %d held no analysable data. Returning a neutral profile."), RoundStats.RoundNumber);

		LastProfile = Profile;
		OnBehaviourProfileReady.Broadcast(LastProfile);
		return Profile;
	}

	CalculateDefenceDistribution(RoundStats, Profile);

	// Phase 1 keeps this one raw, in Unreal units, so spawn distances can be tuned against it.
	// It is the only score that is not clamped into 0..1, so it needs its own finite check -
	// a corrupt accumulator would otherwise put an infinity straight into the profile.
	const float AverageGuardDistance = RoundStats.GetAverageGuardDistance();
	Profile.AverageGuardDistanceFromKing = FMath::IsFinite(AverageGuardDistance)
		? FMath::Max(0.f, AverageGuardDistance)
		: 0.f;

	// Phase 2 already stores clustering per sample on the 0..1 scale Phase 1 asked for, so the
	// interpretation here is just the mean across the round.
	Profile.ClusteringScore = Clamp01(RoundStats.GetAverageClustering());

	Profile.AggressionScore = CalculateAggression(RoundStats);
	Profile.KingExposureScore = CalculateKingExposure(RoundStats);
	Profile.KillEfficiency = CalculateKillEfficiency(RoundStats);

	Profile.MostControlledGuardIndex = SelectMostControlledGuard(RoundStats);
	Profile.BestPerformingEnemyType = SelectMostEffectiveEnemyType(RoundStats);

	// Switches per minute, as documented on the Phase 1 field. This is a rate, not a 0..1 score.
	Profile.GuardSwitchFrequency = FMath::Max(0.f,
		SafeDivide(static_cast<float>(RoundStats.GuardSwitchCount) * 60.f, GetRateReferenceSeconds(RoundStats)));

	// Where attacks actually landed on the King. Deliberately independent of WeakestSide, which
	// is derived from guard presence - the two answer different questions and can disagree.
	Profile.PrimaryKingDamageDirection = RoundStats.KingDamageTakenBySide.GetTotal() > KINDA_SMALL_NUMBER
		? RoundStats.KingDamageTakenBySide.GetStrongestDirection()
		: ECompassDirection::None;

	Profile.bIsValid = true;

	LastProfile = Profile;
	OnBehaviourProfileReady.Broadcast(LastProfile);
	return Profile;
}

void UPlayerBehaviourAnalyzer::ClearLastProfile()
{
	LastProfile.Reset();
}

// ==============================================================================================
// Defence distribution
// ==============================================================================================

void UPlayerBehaviourAnalyzer::CalculateDefenceDistribution(const FPlayerRoundStats& RoundStats, FPlayerBehaviourProfile& OutProfile) const
{
	const float TotalPresence = RoundStats.GuardPresenceSecondsBySide.GetTotal();

	if (RoundStats.PositionSampleCount <= 0 || TotalPresence <= KINDA_SMALL_NUMBER)
	{
		// No spatial samples means no opinion about any side. Leaving the directions at None is
		// honest; picking a side from four zeroes would invent a conclusion.
		OutProfile.SideDefenceStrength.Reset();
		OutProfile.StrongestSide = ECompassDirection::None;
		OutProfile.WeakestSide = ECompassDirection::None;
		return;
	}

	// Relative distribution first: Phase 1 defines this field as normalised against the best side.
	const FDirectionalStats RelativeDefence = RoundStats.GuardPresenceSecondsBySide.GetNormalised();
	FDirectionalStats Defence = RelativeDefence;

	// Scale the whole distribution by how much of the round the King actually had an escort.
	// Uniform scaling leaves the ordering between sides untouched, so strongest and weakest
	// are unaffected, but a player who wandered off reads lower everywhere.
	// Coverage stays at 1.0 when scaling is disabled or the round has no usable duration.
	float Coverage = 1.f;
	if (bScaleDefenceByKingCoverage && RoundStats.RoundDurationSeconds > KINDA_SMALL_NUMBER)
	{
		const float UnguardedFraction = Clamp01(SafeDivide(RoundStats.KingUnguardedSeconds, RoundStats.RoundDurationSeconds));
		Coverage = Clamp01(1.f - UnguardedFraction);
	}

	// Clamp unconditionally rather than only on the scaling path. GetNormalised divides by the
	// largest side, so a corrupt accumulator can hand back NaN, and a NaN duration skips the
	// scaling branch entirely because every NaN comparison is false - which previously let the
	// NaN through untouched.
	for (uint8 i = 0; i < NumCompassSides; ++i)
	{
		const ECompassDirection Side = static_cast<ECompassDirection>(i);
		Defence.Set(Side, Clamp01(Defence.Get(Side) * Coverage));
	}

	OutProfile.SideDefenceStrength = Defence;

	// Only name sides when the round actually separated them.
	//
	// The spread is measured on the relative distribution, before any coverage scaling. Coverage
	// scales all four sides by the same factor, so measuring afterwards would shrink the spread
	// for a player who wandered off and could hide a genuinely lopsided defence behind the
	// threshold. On the relative scale the best side is 1.0 by construction, so the spread is
	// exactly 1.0 minus the weakest side.
	const float DirectionalSpread = RelativeDefence.GetMax() - RelativeDefence.GetMin();

	// A non-finite spread means the presence data was corrupt. Every comparison against NaN is
	// false, so without this check the threshold test would silently fall through and name a
	// side. No trustworthy data means no directional conclusion.
	if (!FMath::IsFinite(DirectionalSpread) || DirectionalSpread <= FMath::Max(0.f, DirectionalDifferenceThreshold))
	{
		// An evenly covered King has no strong or weak flank. Naming one would hand the later
		// director a direction the data does not support, so report None for both. The four
		// defence values themselves are untouched and still describe the distribution.
		OutProfile.StrongestSide = ECompassDirection::None;
		OutProfile.WeakestSide = ECompassDirection::None;
		return;
	}

	// Read the ordering from the raw presence rather than the scaled values: coverage scaling is
	// uniform so it normally preserves the ordering, but a round where the King was never
	// escorted drives every scaled side to zero, and reading an ordering out of four zeroes
	// would invent a conclusion. Both helpers resolve remaining ties in N -> E -> S -> W order,
	// so the result stays deterministic.
	OutProfile.StrongestSide = RoundStats.GuardPresenceSecondsBySide.GetStrongestDirection();
	OutProfile.WeakestSide = RoundStats.GuardPresenceSecondsBySide.GetWeakestDirection();
}

// ==============================================================================================
// Aggression
// ==============================================================================================

int32 UPlayerBehaviourAnalyzer::SumPlayerAttributedKills(const FPlayerRoundStats& RoundStats)
{
	int32 Total = 0;
	for (const TPair<EEnemyType, FEnemyTypeRoundPerformance>& Entry : RoundStats.PerformanceByEnemyType)
	{
		Total += FMath::Max(0, Entry.Value.KilledByPlayerCount);
	}
	return Total;
}

float UPlayerBehaviourAnalyzer::CalculateAggression(const FPlayerRoundStats& RoundStats) const
{
	const float ReferenceSeconds = GetRateReferenceSeconds(RoundStats);
	const float Minutes = ReferenceSeconds / 60.f;

	// Rates rather than raw totals, so a long round is not automatically "more aggressive".
	const float AttackRate = SafeDivide(static_cast<float>(RoundStats.PlayerAttackCount), Minutes);
	const float DamageRate = SafeDivide(RoundStats.PlayerDamageDealtTotal, Minutes);

	FWeightedBlend Blend;
	Blend.Add(AttackRateWeight, NormaliseAgainstReference(AttackRate, ExpectedAttacksPerMinute));
	Blend.Add(DamageRateWeight, NormaliseAgainstReference(DamageRate, ExpectedDamagePerMinute));
	Blend.Add(CombatTimeWeight, Clamp01(SafeDivide(RoundStats.TimeSpentAggressiveSeconds, ReferenceSeconds)));

	// Aggression describes the human, not the whole defence. EnemiesKilled counts every death in
	// the round, so a passive player standing behind busy AI guards would read as aggressive.
	// Phase 2 already separates the two via RecordEnemyKill's bKilledByPlayer flag, which lands
	// in the per-type KilledByPlayerCount rows - no extra tracking is needed to recover it.
	//
	// With no per-type rows at all, kill attribution was never reported. Omit the component
	// rather than scoring it zero, so the remaining indicators decide the result instead of a
	// missing feed dragging an active player down.
	if (RoundStats.PerformanceByEnemyType.Num() > 0)
	{
		const float PlayerKillRate = SafeDivide(static_cast<float>(SumPlayerAttributedKills(RoundStats)), Minutes);
		Blend.Add(KillContributionWeight, NormaliseAgainstReference(PlayerKillRate, ExpectedKillsPerMinute));
	}

	return Blend.GetResult();
}

// ==============================================================================================
// King exposure
// ==============================================================================================

float UPlayerBehaviourAnalyzer::CalculateKingExposure(const FPlayerRoundStats& RoundStats) const
{
	const float ReferenceSeconds = GetRateReferenceSeconds(RoundStats);
	const float Minutes = ReferenceSeconds / 60.f;

	const float TotalKingDamage = RoundStats.KingDamageTakenBySide.GetTotal();
	const float DamageEventRate = SafeDivide(static_cast<float>(RoundStats.KingDamageEventCount), Minutes);

	FWeightedBlend Blend;
	Blend.Add(KingDamageWeight, NormaliseAgainstReference(TotalKingDamage, ExpectedKingDamagePerRound));
	Blend.Add(DangerTimeWeight, Clamp01(SafeDivide(RoundStats.KingInDangerSeconds, ReferenceSeconds)));
	Blend.Add(ThreatCountWeight, NormaliseAgainstReference(DamageEventRate, ExpectedKingDamageEventsPerMinute));
	Blend.Add(NearbyEnemyWeight, NormaliseAgainstReference(RoundStats.GetAverageEnemiesNearKing(), ExpectedEnemiesNearKing));

	// Guards operating far from the King leave him reachable even before anything lands.
	// Contributes nothing when no spatial samples were taken.
	const float GuardProximityComponent = RoundStats.PositionSampleCount > 0
		? NormaliseAgainstReference(RoundStats.GetAverageGuardDistance(), ReferenceGuardDistance)
		: 0.f;
	Blend.Add(GuardProximityWeight, GuardProximityComponent);

	return Blend.GetResult();
}

// ==============================================================================================
// Kill efficiency
// ==============================================================================================

float UPlayerBehaviourAnalyzer::CalculateKillEfficiency(const FPlayerRoundStats& RoundStats) const
{
	// Both components are optional. A missing feed removes its term from the blend rather than
	// scoring it, because an absent report says nothing about how the player performed - and
	// inventing a value in either direction would be a fabricated conclusion.
	FWeightedBlend Blend;

	// How much of the wave the player cleared. Requires a real spawn count: without one the
	// denominator is unknown, so clearing cannot be judged at all. Previously this substituted
	// 1.0, which reported flawless clearing whenever the spawner simply failed to report.
	if (RoundStats.EnemiesSpawned > 0)
	{
		Blend.Add(ClearRateWeight,
			Clamp01(SafeDivide(static_cast<float>(RoundStats.EnemiesKilled), static_cast<float>(RoundStats.EnemiesSpawned))));
	}

	// How quickly, measured against the expected time-to-kill. Faster than expected saturates
	// at 1.0. Omitted when the combat system reported no timing.
	const float AverageTimeToKill = RoundStats.GetAverageTimeToKillSeconds();
	if (AverageTimeToKill > KINDA_SMALL_NUMBER)
	{
		const float Expected = FMath::Max(0.01f, ExpectedTimeToKillSeconds);
		Blend.Add(KillSpeedWeight, Clamp01(SafeDivide(Expected, AverageTimeToKill)));
	}

	// No usable input at all: FWeightedBlend returns 0 on a zero weight sum, which is the safe
	// neutral answer rather than a claim of success.
	return Blend.GetResult();
}

// ==============================================================================================
// Guard control
// ==============================================================================================

int32 UPlayerBehaviourAnalyzer::SelectMostControlledGuard(const FPlayerRoundStats& RoundStats)
{
	int32 BestIndex = INDEX_NONE;
	float BestSeconds = 0.f;

	for (int32 Index = 0; Index < RoundStats.GuardControlSeconds.Num(); ++Index)
	{
		const float Seconds = RoundStats.GuardControlSeconds[Index];

		// Strictly greater, so a tie keeps the lowest slot index. An all-zero array leaves
		// BestIndex at INDEX_NONE rather than falsely reporting slot 0.
		if (Seconds > BestSeconds)
		{
			BestSeconds = Seconds;
			BestIndex = Index;
		}
	}

	return BestIndex;
}

// ==============================================================================================
// Enemy effectiveness
// ==============================================================================================

EEnemyType UPlayerBehaviourAnalyzer::SelectMostEffectiveEnemyType(const FPlayerRoundStats& RoundStats) const
{
	if (RoundStats.PerformanceByEnemyType.Num() == 0)
	{
		return EEnemyType::None;
	}

	// Pass one: find the largest value of each factor, so every factor can be scored relative to
	// the best performer of the round rather than against invented absolute targets.
	float MaxKingDamage = 0.f;
	float MaxGuardDamage = 0.f;
	int32 MaxGuardsDowned = 0;

	for (const TPair<EEnemyType, FEnemyTypeRoundPerformance>& Entry : RoundStats.PerformanceByEnemyType)
	{
		if (Entry.Key == EEnemyType::None)
		{
			continue;
		}

		MaxKingDamage = FMath::Max(MaxKingDamage, Entry.Value.DamageDealtToKing);
		MaxGuardDamage = FMath::Max(MaxGuardDamage, Entry.Value.DamageDealtToGuards);
		MaxGuardsDowned = FMath::Max(MaxGuardsDowned, Entry.Value.GuardsDowned);
	}

	// Pass two: score each type and keep the best.
	EEnemyType BestType = EEnemyType::None;
	float BestScore = 0.f;

	for (const TPair<EEnemyType, FEnemyTypeRoundPerformance>& Entry : RoundStats.PerformanceByEnemyType)
	{
		if (Entry.Key == EEnemyType::None)
		{
			continue;
		}

		const FEnemyTypeRoundPerformance& Performance = Entry.Value;

		FWeightedBlend Blend;
		Blend.Add(EnemyKingDamageWeight, NormaliseAgainstReference(Performance.DamageDealtToKing, MaxKingDamage));
		Blend.Add(EnemyGuardDamageWeight, NormaliseAgainstReference(Performance.DamageDealtToGuards, MaxGuardDamage));
		Blend.Add(EnemyGuardsDownedWeight,
			NormaliseAgainstReference(static_cast<float>(Performance.GuardsDowned), static_cast<float>(MaxGuardsDowned)));

		// Durability only means something when the type actually spawned; otherwise the survival
		// rate is undefined and contributes nothing.
		const float SurvivalComponent = Performance.SpawnedCount > 0 ? Clamp01(Performance.GetSurvivalRate()) : 0.f;
		Blend.Add(EnemySurvivalWeight, SurvivalComponent);

		const float Score = Blend.GetResult();

		// Strictly greater keeps the lowest enum value on a tie. TMap iteration order is not
		// guaranteed, so compare the enum explicitly rather than relying on visit order.
		if (Score > BestScore || (BestType != EEnemyType::None && FMath::IsNearlyEqual(Score, BestScore) && Entry.Key < BestType))
		{
			BestScore = FMath::Max(Score, BestScore);
			BestType = Entry.Key;
		}
	}

	// Every type scored zero: nothing achieved anything, so name nobody.
	return BestScore > KINDA_SMALL_NUMBER ? BestType : EEnemyType::None;
}

// ==============================================================================================
// Optional tracker integration
// ==============================================================================================

void UPlayerBehaviourAnalyzer::BindToTracker(UPlayerPatternTracker* Tracker)
{
	if (UPlayerPatternTracker* Previous = BoundTracker.Get())
	{
		Previous->OnRoundTrackingFinished.RemoveDynamic(this, &UPlayerBehaviourAnalyzer::HandleRoundTrackingFinished);
	}

	BoundTracker = Tracker;

	if (Tracker)
	{
		Tracker->OnRoundTrackingFinished.AddDynamic(this, &UPlayerBehaviourAnalyzer::HandleRoundTrackingFinished);
		UE_LOG(LogPlayerBehaviour, Log, TEXT("Analyzer bound to tracker on %s."),
			Tracker->GetOwner() ? *Tracker->GetOwner()->GetName() : TEXT("unknown actor"));
	}
}

void UPlayerBehaviourAnalyzer::HandleRoundTrackingFinished(const FPlayerRoundStats& CompletedRoundStats)
{
	AnalyzeRoundStats(CompletedRoundStats);
}

// ==============================================================================================
// Debug
// ==============================================================================================

FString UPlayerBehaviourAnalyzer::GetProfileDebugString(const FPlayerBehaviourProfile& Profile) const
{
	FString Out;
	Out.Reserve(1024);

	Out += FString::Printf(TEXT("PlayerBehaviourProfile - round %d (%s)\n"),
		Profile.RoundNumber, Profile.bIsValid ? TEXT("valid") : TEXT("no analysable data"));

	Out += FString::Printf(TEXT("  defence North         : %.3f\n"), Profile.SideDefenceStrength.North);
	Out += FString::Printf(TEXT("  defence East          : %.3f\n"), Profile.SideDefenceStrength.East);
	Out += FString::Printf(TEXT("  defence South         : %.3f\n"), Profile.SideDefenceStrength.South);
	Out += FString::Printf(TEXT("  defence West          : %.3f\n"), Profile.SideDefenceStrength.West);
	Out += FString::Printf(TEXT("  strongest side        : %s\n"), DirectionToString(Profile.StrongestSide));
	Out += FString::Printf(TEXT("  weakest side          : %s\n"), DirectionToString(Profile.WeakestSide));
	Out += FString::Printf(TEXT("  king damage direction : %s\n"), DirectionToString(Profile.PrimaryKingDamageDirection));

	Out += FString::Printf(TEXT("  avg guard distance    : %.1f uu\n"), Profile.AverageGuardDistanceFromKing);
	Out += FString::Printf(TEXT("  clustering            : %.3f (%s)\n"),
		Profile.ClusteringScore, RatingToString(Profile.GetClusteringRating()));
	Out += FString::Printf(TEXT("  king exposure         : %.3f (%s)\n"),
		Profile.KingExposureScore, RatingToString(Profile.GetKingExposureRating()));
	Out += FString::Printf(TEXT("  aggression            : %.3f (%s)\n"),
		Profile.AggressionScore, RatingToString(Profile.GetAggressionRating()));
	Out += FString::Printf(TEXT("  kill efficiency       : %.3f (%s)\n"),
		Profile.KillEfficiency, RatingToString(Profile.GetKillEfficiencyRating()));

	Out += FString::Printf(TEXT("  most controlled guard : %d\n"), Profile.MostControlledGuardIndex);
	Out += FString::Printf(TEXT("  guard switches/min    : %.2f\n"), Profile.GuardSwitchFrequency);
	Out += FString::Printf(TEXT("  best enemy type       : %d\n"), static_cast<int32>(Profile.BestPerformingEnemyType));

	return Out;
}

void UPlayerBehaviourAnalyzer::LogProfile(const FPlayerBehaviourProfile& Profile) const
{
	UE_LOG(LogPlayerBehaviour, Log, TEXT("\n%s"), *GetProfileDebugString(Profile));
}
