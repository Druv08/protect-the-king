#include "AI/Tracking/PlayerPatternTracker.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogPlayerPattern);

UPlayerPatternTracker::UPlayerPatternTracker()
{
	// Sampling runs on a timer, not per frame - see StartSampleTimer.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPlayerPatternTracker::BeginPlay()
{
	Super::BeginPlay();
}

void UPlayerPatternTracker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSampleTimer();
	Super::EndPlay(EndPlayReason);
}

// ==============================================================================================
// Time
// ==============================================================================================

float UPlayerPatternTracker::GetNowSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetTimeSeconds();
	}
	return ManualClockSeconds;
}

void UPlayerPatternTracker::AdvanceManualClock(float DeltaSeconds)
{
	ManualClockSeconds += FMath::Max(0.f, DeltaSeconds);
}

float UPlayerPatternTracker::GetElapsedRoundSeconds() const
{
	if (bIsTracking)
	{
		return FMath::Max(0.f, GetNowSeconds() - RoundStartTimeSeconds);
	}
	return FMath::Max(0.f, RoundEndTimeSeconds - RoundStartTimeSeconds);
}

// ==============================================================================================
// Round lifecycle
// ==============================================================================================

void UPlayerPatternTracker::StartTrackingRound(int32 RoundNumber)
{
	if (bIsTracking)
	{
		UE_LOG(LogPlayerPattern, Warning,
			TEXT("StartTrackingRound called while round %d was still open. Discarding it and starting round %d."),
			CurrentRoundStats.RoundNumber, RoundNumber);
	}

	const float Now = GetNowSeconds();

	CurrentRoundStats.Reset();
	CurrentRoundStats.RoundNumber = RoundNumber;

	bIsTracking = true;
	bControlledGuardEngaged = false;
	bWarnedMissingKing = false;

	CurrentControlledGuardIndex = INDEX_NONE;
	PreviousControlledGuardIndex = INDEX_NONE;

	RoundStartTimeSeconds = Now;
	RoundEndTimeSeconds = Now;
	LastControlChangeTimeSeconds = Now;
	LastSampleTimeSeconds = Now;
	LastGuardSwitchTimeSeconds = Now;
	EngagedSinceSeconds = Now;

	StartSampleTimer();

	UE_LOG(LogPlayerPattern, Log, TEXT("Round %d tracking started."), RoundNumber);
}

void UPlayerPatternTracker::EndTrackingRound()
{
	if (!bIsTracking)
	{
		UE_LOG(LogPlayerPattern, Warning, TEXT("EndTrackingRound called while no round was being tracked. Ignored."));
		return;
	}

	const float Now = GetNowSeconds();

	StopSampleTimer();

	// Close the open control window so the final guard is credited for its last stretch.
	AccumulateControlTime(Now);

	// Close the open engagement window so the final stretch of combat is counted.
	if (bControlledGuardEngaged)
	{
		CurrentRoundStats.TimeSpentAggressiveSeconds += FMath::Max(0.f, Now - EngagedSinceSeconds);
	}

	RoundEndTimeSeconds = Now;
	CurrentRoundStats.RoundDurationSeconds = FMath::Max(0.f, Now - RoundStartTimeSeconds);

	bIsTracking = false;
	bControlledGuardEngaged = false;

	// Publish. No analysis happens here - Phase 3 owns interpretation.
	LastCompletedRoundStats = CurrentRoundStats;
	bHasCompletedRound = true;

	UE_LOG(LogPlayerPattern, Log, TEXT("Round %d tracking ended after %.2fs."),
		LastCompletedRoundStats.RoundNumber, LastCompletedRoundStats.RoundDurationSeconds);

	OnRoundTrackingFinished.Broadcast(LastCompletedRoundStats);
}

void UPlayerPatternTracker::ResetRoundTracking()
{
	const int32 RoundNumber = CurrentRoundStats.RoundNumber;
	const float Now = GetNowSeconds();

	CurrentRoundStats.Reset();
	CurrentRoundStats.RoundNumber = RoundNumber;

	bControlledGuardEngaged = false;
	CurrentControlledGuardIndex = INDEX_NONE;
	PreviousControlledGuardIndex = INDEX_NONE;

	RoundStartTimeSeconds = Now;
	RoundEndTimeSeconds = Now;
	LastControlChangeTimeSeconds = Now;
	LastSampleTimeSeconds = Now;
	LastGuardSwitchTimeSeconds = Now;
	EngagedSinceSeconds = Now;

	UE_LOG(LogPlayerPattern, Log, TEXT("Round %d tracking reset."), RoundNumber);
}

void UPlayerPatternTracker::ClearAllRoundData()
{
	StopSampleTimer();

	CurrentRoundStats.Reset();
	LastCompletedRoundStats.Reset();

	bIsTracking = false;
	bHasCompletedRound = false;
	bControlledGuardEngaged = false;
	CurrentControlledGuardIndex = INDEX_NONE;
	PreviousControlledGuardIndex = INDEX_NONE;
}

// ==============================================================================================
// Integration - King
// ==============================================================================================

void UPlayerPatternTracker::SetKingActor(AActor* InKingActor)
{
	KingActor = InKingActor;
	bWarnedMissingKing = false;

	UE_LOG(LogPlayerPattern, Log, TEXT("King actor set to %s."),
		InKingActor ? *InKingActor->GetName() : TEXT("None"));
}

void UPlayerPatternTracker::SetKingLocationOverride(FVector WorldLocation)
{
	KingLocationOverride = WorldLocation;
	bUseKingLocationOverride = true;
	bWarnedMissingKing = false;
}

void UPlayerPatternTracker::ClearKingLocationOverride()
{
	bUseKingLocationOverride = false;
}

FVector UPlayerPatternTracker::GetKingWorldLocation(bool& bOutIsValid) const
{
	if (bUseKingLocationOverride)
	{
		bOutIsValid = true;
		return KingLocationOverride;
	}

	if (const AActor* King = KingActor.Get())
	{
		bOutIsValid = true;
		return King->GetActorLocation();
	}

	bOutIsValid = false;
	return FVector::ZeroVector;
}

ECompassDirection UPlayerPatternTracker::GetDirectionFromKing(FVector WorldLocation) const
{
	bool bKingValid = false;
	const FVector KingLocation = GetKingWorldLocation(bKingValid);
	if (!bKingValid)
	{
		return ECompassDirection::None;
	}

	FVector Offset = WorldLocation - KingLocation;
	Offset.Z = 0.f;

	// Rotate the offset into battlefield space so the shared enum keeps its meaning even when
	// the level is not built along +X/+Y.
	if (!FMath::IsNearlyZero(BattlefieldNorthYawDegrees))
	{
		Offset = Offset.RotateAngleAxis(-BattlefieldNorthYawDegrees, FVector::UpVector);
	}

	return CombatDirection::FromOffset(Offset);
}

// ==============================================================================================
// Integration - Guards
// ==============================================================================================

void UPlayerPatternTracker::RegisterGuard(int32 GuardSlotIndex, AActor* GuardActor)
{
	if (GuardSlotIndex < 0 || GuardSlotIndex >= MaxTrackedGuardSlots)
	{
		UE_LOG(LogPlayerPattern, Warning,
			TEXT("RegisterGuard ignored: slot %d is outside 0..%d."), GuardSlotIndex, MaxTrackedGuardSlots - 1);
		return;
	}

	if (!GuardActor)
	{
		UE_LOG(LogPlayerPattern, Warning, TEXT("RegisterGuard ignored: null actor for slot %d."), GuardSlotIndex);
		return;
	}

	RegisteredGuards.Add(GuardSlotIndex, GuardActor);
	EnsureGuardSlot(GuardSlotIndex);
}

void UPlayerPatternTracker::UnregisterGuard(int32 GuardSlotIndex)
{
	RegisteredGuards.Remove(GuardSlotIndex);
}

void UPlayerPatternTracker::ClearRegisteredGuards()
{
	RegisteredGuards.Empty();
}

bool UPlayerPatternTracker::EnsureGuardSlot(int32 Slot)
{
	if (Slot < 0 || Slot >= MaxTrackedGuardSlots)
	{
		return false;
	}

	if (CurrentRoundStats.GuardControlSeconds.Num() <= Slot)
	{
		CurrentRoundStats.GuardControlSeconds.SetNumZeroed(Slot + 1);
	}
	return true;
}

// ==============================================================================================
// Recording - guard control
// ==============================================================================================

void UPlayerPatternTracker::AccumulateControlTime(float Now)
{
	if (CurrentControlledGuardIndex != INDEX_NONE && EnsureGuardSlot(CurrentControlledGuardIndex))
	{
		const float Held = FMath::Max(0.f, Now - LastControlChangeTimeSeconds);
		CurrentRoundStats.GuardControlSeconds[CurrentControlledGuardIndex] += Held;
	}
	LastControlChangeTimeSeconds = Now;
}

void UPlayerPatternTracker::RecordGuardSwitch(int32 NewGuardSlotIndex)
{
	if (!bIsTracking)
	{
		return;
	}

	if (NewGuardSlotIndex != INDEX_NONE && (NewGuardSlotIndex < 0 || NewGuardSlotIndex >= MaxTrackedGuardSlots))
	{
		UE_LOG(LogPlayerPattern, Warning,
			TEXT("RecordGuardSwitch ignored: slot %d is outside 0..%d."), NewGuardSlotIndex, MaxTrackedGuardSlots - 1);
		return;
	}

	const float Now = GetNowSeconds();

	// Credit the outgoing guard before the index moves.
	AccumulateControlTime(Now);

	// Taking control for the first time is not a switch, and neither is re-selecting the same guard.
	const bool bIsRealSwitch = (CurrentControlledGuardIndex != INDEX_NONE) && (CurrentControlledGuardIndex != NewGuardSlotIndex);
	if (bIsRealSwitch)
	{
		++CurrentRoundStats.GuardSwitchCount;
		LastGuardSwitchTimeSeconds = Now;
	}

	PreviousControlledGuardIndex = CurrentControlledGuardIndex;
	CurrentControlledGuardIndex = NewGuardSlotIndex;
	EnsureGuardSlot(NewGuardSlotIndex);

	UE_LOG(LogPlayerPattern, Verbose, TEXT("Guard control %d -> %d at %.2fs (switch=%s, total=%d)."),
		PreviousControlledGuardIndex, CurrentControlledGuardIndex, Now,
		bIsRealSwitch ? TEXT("yes") : TEXT("no"), CurrentRoundStats.GuardSwitchCount);
}

void UPlayerPatternTracker::SetControlledGuardEngaged(bool bEngaged)
{
	if (!bIsTracking || bEngaged == bControlledGuardEngaged)
	{
		return;
	}

	const float Now = GetNowSeconds();

	if (bEngaged)
	{
		// Open the window. Nothing is banked until it closes, so the elapsed time is counted once.
		EngagedSinceSeconds = Now;
	}
	else
	{
		CurrentRoundStats.TimeSpentAggressiveSeconds += FMath::Max(0.f, Now - EngagedSinceSeconds);
	}

	bControlledGuardEngaged = bEngaged;
}

void UPlayerPatternTracker::RecordAggressiveTime(float Seconds)
{
	if (!bIsTracking)
	{
		return;
	}
	CurrentRoundStats.TimeSpentAggressiveSeconds += FMath::Max(0.f, Seconds);
}

// ==============================================================================================
// Recording - spatial sampling
// ==============================================================================================

void UPlayerPatternTracker::StartSampleTimer()
{
	if (!bAutoSampleGuardPositions)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Interval = FMath::Max(0.05f, SpatialSampleIntervalSeconds);
	World->GetTimerManager().SetTimer(
		SampleTimerHandle, this, &UPlayerPatternTracker::SampleRegisteredGuardsNow, Interval, /*bLoop=*/true);
}

void UPlayerPatternTracker::StopSampleTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SampleTimerHandle);
	}
}

void UPlayerPatternTracker::SampleRegisteredGuardsNow()
{
	if (!bIsTracking || RegisteredGuards.Num() == 0)
	{
		return;
	}

	TArray<FVector> Locations;
	Locations.Reserve(RegisteredGuards.Num());

	for (const TPair<int32, TWeakObjectPtr<AActor>>& Entry : RegisteredGuards)
	{
		if (const AActor* Guard = Entry.Value.Get())
		{
			Locations.Add(Guard->GetActorLocation());
		}
	}

	if (Locations.Num() > 0)
	{
		RecordGuardSpatialSample(Locations);
	}
}

void UPlayerPatternTracker::RecordGuardSpatialSample(const TArray<FVector>& GuardWorldLocations)
{
	if (!bIsTracking || GuardWorldLocations.Num() == 0)
	{
		return;
	}

	const float Now = GetNowSeconds();
	const float DeltaSeconds = FMath::Max(0.f, Now - LastSampleTimeSeconds);

	ProcessSpatialSample(GuardWorldLocations, DeltaSeconds);

	LastSampleTimeSeconds = Now;
}

void UPlayerPatternTracker::ProcessSpatialSample(const TArray<FVector>& GuardWorldLocations, float DeltaSeconds)
{
	bool bKingValid = false;
	const FVector KingLocation = GetKingWorldLocation(bKingValid);
	if (!bKingValid)
	{
		// Every value below is King-relative, so a sample without him would corrupt the averages.
		if (!bWarnedMissingKing)
		{
			bWarnedMissingKing = true;
			UE_LOG(LogPlayerPattern, Warning,
				TEXT("Spatial samples are being dropped: no King actor and no location override. ")
				TEXT("Call SetKingActor or SetKingLocationOverride."));
		}
		return;
	}

	const int32 GuardCount = GuardWorldLocations.Num();

	// --- Distance from the King, and per-side presence -------------------------------------
	float DistanceSum = 0.f;
	float NearestDistance = TNumericLimits<float>::Max();
	FVector Centroid = FVector::ZeroVector;

	for (const FVector& GuardLocation : GuardWorldLocations)
	{
		FVector Offset = GuardLocation - KingLocation;
		Offset.Z = 0.f;

		const float Distance = Offset.Size();
		DistanceSum += Distance;
		NearestDistance = FMath::Min(NearestDistance, Distance);

		Centroid += GuardLocation;

		// Guard-seconds, not wall-clock seconds: four guards holding North for one second
		// contribute four. Phase 3 normalises across the sides, so the scale cancels out.
		const ECompassDirection Side = GetDirectionFromKing(GuardLocation);
		if (Side != ECompassDirection::None)
		{
			CurrentRoundStats.GuardPresenceSecondsBySide.Add(Side, DeltaSeconds);
		}
	}

	Centroid /= static_cast<float>(GuardCount);

	// --- Clustering ---------------------------------------------------------------------------
	// Mean distance from the guard centroid, inverted against a configurable reference so the
	// stored value matches the Phase 1 contract: 0 = spread out, 1 = stacked together.
	float SpreadSum = 0.f;
	for (const FVector& GuardLocation : GuardWorldLocations)
	{
		FVector ToCentroid = GuardLocation - Centroid;
		ToCentroid.Z = 0.f;
		SpreadSum += ToCentroid.Size();
	}

	const float MeanSpread = SpreadSum / static_cast<float>(GuardCount);
	const float Reference = FMath::Max(1.f, ClusteringReferenceDistance);
	const float ClusteringSample = 1.f - FMath::Clamp(MeanSpread / Reference, 0.f, 1.f);

	// --- Commit the sample ---------------------------------------------------------------------
	CurrentRoundStats.GuardDistanceSampleSum += DistanceSum / static_cast<float>(GuardCount);
	CurrentRoundStats.ClusteringSampleSum += ClusteringSample;
	++CurrentRoundStats.PositionSampleCount;

	if (NearestDistance > KingProtectionRadius)
	{
		CurrentRoundStats.KingUnguardedSeconds += DeltaSeconds;
	}

	// Engagement time is banked by SetControlledGuardEngaged when the window closes, not here -
	// accumulating in both places would count the same seconds twice.
}

// ==============================================================================================
// Recording - player combat
// ==============================================================================================

void UPlayerPatternTracker::RecordPlayerAttack(bool bDidHit)
{
	if (!bIsTracking)
	{
		return;
	}

	++CurrentRoundStats.PlayerAttackCount;
	if (bDidHit)
	{
		++CurrentRoundStats.PlayerAttacksLandedCount;
	}
}

void UPlayerPatternTracker::RecordPlayerDamageDealt(float DamageAmount, FVector TargetWorldLocation)
{
	if (!bIsTracking || DamageAmount <= 0.f)
	{
		return;
	}

	CurrentRoundStats.PlayerDamageDealtTotal += DamageAmount;

	const ECompassDirection Side = GetDirectionFromKing(TargetWorldLocation);
	if (Side != ECompassDirection::None)
	{
		CurrentRoundStats.DamageDealtBySide.Add(Side, DamageAmount);
	}
}

// ==============================================================================================
// Recording - enemies
// ==============================================================================================

FEnemyTypeRoundPerformance& UPlayerPatternTracker::FindOrAddPerformance(EEnemyType EnemyType)
{
	return CurrentRoundStats.PerformanceByEnemyType.FindOrAdd(EnemyType);
}

void UPlayerPatternTracker::RecordEnemySpawned(EEnemyType EnemyType, int32 Count)
{
	if (!bIsTracking || Count <= 0)
	{
		return;
	}

	CurrentRoundStats.EnemiesSpawned += Count;
	FindOrAddPerformance(EnemyType).SpawnedCount += Count;
}

void UPlayerPatternTracker::RecordEnemyKill(EEnemyType EnemyType, FVector EnemyWorldLocation, float TimeToKillSeconds, bool bKilledByPlayer)
{
	if (!bIsTracking)
	{
		return;
	}

	++CurrentRoundStats.EnemiesKilled;
	CurrentRoundStats.TotalTimeToKillSeconds += FMath::Max(0.f, TimeToKillSeconds);

	const ECompassDirection Side = GetDirectionFromKing(EnemyWorldLocation);
	if (Side != ECompassDirection::None)
	{
		CurrentRoundStats.EnemyKillsBySide.Add(Side, 1.f);
	}

	FEnemyTypeRoundPerformance& Performance = FindOrAddPerformance(EnemyType);
	if (bKilledByPlayer)
	{
		++Performance.KilledByPlayerCount;
	}
	Performance.TotalLifetimeSeconds += FMath::Max(0.f, TimeToKillSeconds);
}

void UPlayerPatternTracker::RecordEnemyPerformance(EEnemyType EnemyType, float DamageDealtToGuards, int32 GuardsDowned, float LifetimeSeconds)
{
	if (!bIsTracking)
	{
		return;
	}

	FEnemyTypeRoundPerformance& Performance = FindOrAddPerformance(EnemyType);
	Performance.DamageDealtToGuards += FMath::Max(0.f, DamageDealtToGuards);
	Performance.GuardsDowned += FMath::Max(0, GuardsDowned);
	Performance.TotalLifetimeSeconds += FMath::Max(0.f, LifetimeSeconds);
}

// ==============================================================================================
// Recording - King
// ==============================================================================================

void UPlayerPatternTracker::RecordKingDamage(float DamageAmount, FVector AttackerWorldLocation, EEnemyType SourceEnemyType)
{
	const ECompassDirection Direction = GetDirectionFromKing(AttackerWorldLocation);
	RecordKingDamageFromDirection(DamageAmount, Direction, SourceEnemyType);
}

void UPlayerPatternTracker::RecordKingDamageFromDirection(float DamageAmount, ECompassDirection AttackDirection, EEnemyType SourceEnemyType)
{
	if (!bIsTracking || DamageAmount <= 0.f)
	{
		return;
	}

	// The event still counts even when the direction could not be resolved, so the exposure
	// total stays honest; only the per-side bucket is skipped.
	++CurrentRoundStats.KingDamageEventCount;

	if (AttackDirection != ECompassDirection::None && AttackDirection != ECompassDirection::Count)
	{
		CurrentRoundStats.KingDamageTakenBySide.Add(AttackDirection, DamageAmount);
	}
	else
	{
		UE_LOG(LogPlayerPattern, Verbose,
			TEXT("King damage of %.1f recorded without a resolvable direction."), DamageAmount);
	}

	if (SourceEnemyType != EEnemyType::None)
	{
		FindOrAddPerformance(SourceEnemyType).DamageDealtToKing += DamageAmount;
	}
}

void UPlayerPatternTracker::RecordKingThreat(int32 EnemiesInsideDangerRadius, float DurationSeconds)
{
	if (!bIsTracking || EnemiesInsideDangerRadius < 0)
	{
		return;
	}

	++CurrentRoundStats.KingThreatReportCount;
	CurrentRoundStats.EnemiesNearKingSum += static_cast<float>(EnemiesInsideDangerRadius);
	CurrentRoundStats.PeakEnemiesNearKing = FMath::Max(CurrentRoundStats.PeakEnemiesNearKing, EnemiesInsideDangerRadius);

	if (EnemiesInsideDangerRadius > 0)
	{
		CurrentRoundStats.KingInDangerSeconds += FMath::Max(0.f, DurationSeconds);
	}
}

// ==============================================================================================
// Debug
// ==============================================================================================

FString UPlayerPatternTracker::GetDebugSummaryString() const
{
	const FPlayerRoundStats& S = CurrentRoundStats;

	FString Out;
	Out.Reserve(2048);

	Out += FString::Printf(TEXT("PlayerPatternTracker - round %d (%s)\n"),
		S.RoundNumber, bIsTracking ? TEXT("tracking") : TEXT("idle"));
	Out += FString::Printf(TEXT("  elapsed               : %.2fs\n"), GetElapsedRoundSeconds());
	Out += FString::Printf(TEXT("  spatial samples       : %d\n"), S.PositionSampleCount);
	Out += FString::Printf(TEXT("  avg guard distance    : %.1f uu\n"), S.GetAverageGuardDistance());
	Out += FString::Printf(TEXT("  avg clustering        : %.3f (0=spread, 1=stacked)\n"), S.GetAverageClustering());
	Out += FString::Printf(TEXT("  king unguarded        : %.2fs\n"), S.KingUnguardedSeconds);
	Out += FString::Printf(TEXT("  guard presence N/E/S/W: %.1f / %.1f / %.1f / %.1f guard-seconds\n"),
		S.GuardPresenceSecondsBySide.North, S.GuardPresenceSecondsBySide.East,
		S.GuardPresenceSecondsBySide.South, S.GuardPresenceSecondsBySide.West);
	Out += FString::Printf(TEXT("  controlled guard      : %d (previous %d)\n"),
		CurrentControlledGuardIndex, PreviousControlledGuardIndex);
	Out += FString::Printf(TEXT("  guard switches        : %d\n"), S.GuardSwitchCount);

	Out += TEXT("  guard control seconds : ");
	for (int32 i = 0; i < S.GuardControlSeconds.Num(); ++i)
	{
		Out += FString::Printf(TEXT("[%d]=%.2f "), i, S.GuardControlSeconds[i]);
	}
	Out += TEXT("\n");

	Out += FString::Printf(TEXT("  player attacks        : %d (%d landed)\n"),
		S.PlayerAttackCount, S.PlayerAttacksLandedCount);
	Out += FString::Printf(TEXT("  player damage dealt   : %.1f\n"), S.PlayerDamageDealtTotal);
	Out += FString::Printf(TEXT("  time engaged          : %.2fs\n"), S.TimeSpentAggressiveSeconds);
	Out += FString::Printf(TEXT("  enemies spawned/killed: %d / %d\n"), S.EnemiesSpawned, S.EnemiesKilled);
	Out += FString::Printf(TEXT("  avg time-to-kill      : %.2fs\n"), S.GetAverageTimeToKillSeconds());
	Out += FString::Printf(TEXT("  kills N/E/S/W         : %.0f / %.0f / %.0f / %.0f\n"),
		S.EnemyKillsBySide.North, S.EnemyKillsBySide.East, S.EnemyKillsBySide.South, S.EnemyKillsBySide.West);
	Out += FString::Printf(TEXT("  king damage events    : %d (total %.1f)\n"),
		S.KingDamageEventCount, S.KingDamageTakenBySide.GetTotal());
	Out += FString::Printf(TEXT("  king damage N/E/S/W   : %.1f / %.1f / %.1f / %.1f\n"),
		S.KingDamageTakenBySide.North, S.KingDamageTakenBySide.East,
		S.KingDamageTakenBySide.South, S.KingDamageTakenBySide.West);
	Out += FString::Printf(TEXT("  king in danger        : %.2fs over %d reports (peak %d, avg %.2f)\n"),
		S.KingInDangerSeconds, S.KingThreatReportCount, S.PeakEnemiesNearKing, S.GetAverageEnemiesNearKing());

	Out += TEXT("  per enemy type        :\n");
	for (const TPair<EEnemyType, FEnemyTypeRoundPerformance>& Entry : S.PerformanceByEnemyType)
	{
		const FEnemyTypeRoundPerformance& P = Entry.Value;
		Out += FString::Printf(TEXT("    type %d: spawned=%d killed=%d dmgGuards=%.1f dmgKing=%.1f downed=%d lifetime=%.2fs\n"),
			static_cast<int32>(Entry.Key), P.SpawnedCount, P.KilledByPlayerCount,
			P.DamageDealtToGuards, P.DamageDealtToKing, P.GuardsDowned, P.TotalLifetimeSeconds);
	}

	return Out;
}

void UPlayerPatternTracker::LogRoundSummary() const
{
	UE_LOG(LogPlayerPattern, Log, TEXT("\n%s"), *GetDebugSummaryString());
}
