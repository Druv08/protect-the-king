#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AI/Types/EnemyTypes.h"
#include "AI/Types/EnemyDefinitionTypes.h"
#include "EnemyDataAsset.generated.h"

/**
 * The definition of one enemy: what it is, how tough it is, what it goes after, and what it is
 * naturally good at.
 *
 * This is data, not behaviour. Nothing here decides when an enemy should spawn, which enemy
 * counters the player, or how one fights - those are later phases that read these assets.
 *
 * One asset per enemy type, authored in the editor as DA_Enemy_Grunt, DA_Enemy_Runner and so on.
 * The numbers in an asset are designer-owned; C++ only defines the schema and safe defaults.
 *
 * Derives from UPrimaryDataAsset so the Asset Manager can enumerate every enemy definition
 * without a hard reference to each one. That matters for the intended architecture: later
 * systems ask "which definition scores highest on this axis?" rather than naming enemies.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Enemy Data Asset"))
class PROTECTTHEKING_API UEnemyDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ==========================================================================================
	// Identity
	// ==========================================================================================

	/** Which roster entry this asset defines. Must be set, and must be unique across assets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EEnemyType EnemyType = EEnemyType::None;

	/** Name shown to players and in debug output. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/**
	 * Which shared AI implementation this enemy uses.
	 * Leave at None to inherit the Phase 1 default for this type - see GetEffectiveBehaviourFamily.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EEnemyBehaviourFamily BehaviourFamily = EEnemyBehaviourFamily::None;

	/**
	 * One line on what this enemy is for, in design terms.
	 * Documentation for the team - never parsed, and never a substitute for the numeric fields.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = "true"))
	FText RoleDescription;

	// ==========================================================================================
	// Combat stats
	//
	// Schema defaults below are deliberately neutral placeholders - a generic melee body. They
	// exist so a freshly created asset validates rather than to suggest balance. Per-enemy
	// values belong in the assets.
	// ==========================================================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BaseDamage = 10.f;

	/** Unreal units per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MovementSpeed = 400.f;

	/** How close this enemy must be to attack, in Unreal units. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AttackRange = 150.f;

	/** Seconds between attacks. Must stay above zero - it is used as a divisor for attack rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.01", UIMin = "0.1"))
	float AttackCooldownSeconds = 1.5f;

	/** How far this enemy notices targets, in Unreal units. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DetectionRange = 1500.f;

	/**
	 * Distance this enemy tries to hold while fighting, in Unreal units.
	 * Matches AttackRange for melee; an Archer holds well beyond it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PreferredEngagementRange = 150.f;

	// ==========================================================================================
	// Special ability
	//
	// One generic set of knobs rather than ten ability classes. Each enemy reads them
	// differently: Bomber as explosion radius and damage, Healer as heal radius and amount,
	// Commander as buff radius and strength, Charger as charge range and impact. The abilities
	// themselves are not implemented in this phase.
	// ==========================================================================================

	/** Whether this enemy has a special ability at all. The four values below are ignored when false. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	bool bHasSpecialAbility = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.01", EditCondition = "bHasSpecialAbility"))
	float AbilityCooldownSeconds = 10.f;

	/** Effect radius in Unreal units. Zero means the ability is not area based. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.0", EditCondition = "bHasSpecialAbility"))
	float AbilityRadius = 0.f;

	/** Magnitude, interpreted per enemy: damage, heal amount or buff multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.0", EditCondition = "bHasSpecialAbility"))
	float AbilityStrength = 0.f;

	/** How long the effect lasts. Zero means instant. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0.0", EditCondition = "bHasSpecialAbility"))
	float AbilityDurationSeconds = 0.f;

	// ==========================================================================================
	// Wave metadata
	// ==========================================================================================

	/** Budget this enemy consumes in a wave. Higher means fewer of them per wave. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.01", UIMin = "1.0"))
	float SpawnCost = 1.f;

	/** Earliest round this enemy should be considered for. Gates complexity early on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "1", UIMin = "1"))
	int32 MinimumRecommendedRound = 1;

	/** Overall danger, 0..1. A coarse summary used for pacing, not for counter-selection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThreatRating = 0.5f;

	// ==========================================================================================
	// Targeting and effectiveness
	// ==========================================================================================

	/** How strongly this enemy prefers each kind of target. Configuration only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting")
	FEnemyTargetingProfile Targeting;

	/** What this enemy is naturally good at. Read by later phases to pick counters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effectiveness")
	FEnemyEffectivenessProfile Effectiveness;

	// ==========================================================================================
	// Queries
	// ==========================================================================================

	/**
	 * Behaviour family to actually use.
	 * Falls back to the Phase 1 default mapping when the asset leaves BehaviourFamily at None,
	 * so a half-filled asset still resolves to something sensible instead of nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	EEnemyBehaviourFamily GetEffectiveBehaviourFamily() const;

	/** Effectiveness on one axis, 0..1. The entry point for metadata-driven selection later. */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	float GetEffectiveness(EEnemyRoleAxis Axis) const { return Effectiveness.Get(Axis); }

	/** Preference weight for one kind of target, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	float GetTargetPriority(ETargetPriority Priority) const { return Targeting.Get(Priority); }

	/** Strongest configured target preference, or None. */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	ETargetPriority GetPrimaryTargetPriority() const { return Targeting.GetPrimaryPriority(); }

	/** Axis this enemy scores highest on, or None. */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	EEnemyRoleAxis GetStrongestRoleAxis() const { return Effectiveness.GetStrongestAxis(); }

	/** Attacks per second, derived from the cooldown. Guarded against a zero divisor. */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	float GetAttackRate() const;

	// ==========================================================================================
	// Validation and debug
	// ==========================================================================================

	/**
	 * Checks the asset for values that would break later systems.
	 * Returns true when the definition is usable; OutErrors lists every problem found, so one
	 * pass reports everything rather than stopping at the first fault.
	 *
	 * Callable from Blueprint and from C++ so a designer can check an asset without an
	 * editor-only code path.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy Definition")
	bool ValidateDefinition(TArray<FText>& OutErrors) const;

	/** Convenience wrapper for callers that only need a yes or no. */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	bool IsDefinitionValid() const;

	/** Multi-line dump of this definition. Testing and logging only - no UI. */
	UFUNCTION(BlueprintPure, Category = "Enemy Definition")
	FString GetEnemyDebugSummary() const;

	/** Writes GetEnemyDebugSummary to the log. */
	UFUNCTION(BlueprintCallable, Category = "Enemy Definition")
	void LogEnemyDefinition() const;

	/** Groups every enemy definition under one Asset Manager type so they can be enumerated. */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** Asset Manager category for all enemy definitions. Register this in Project Settings. */
	static const FPrimaryAssetType EnemyDefinitionAssetType;
};
