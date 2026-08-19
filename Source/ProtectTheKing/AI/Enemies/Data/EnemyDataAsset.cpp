#include "AI/Enemies/Data/EnemyDataAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyDefinition, Log, All);

const FPrimaryAssetType UEnemyDataAsset::EnemyDefinitionAssetType = TEXT("EnemyDefinition");

namespace
{
	const TCHAR* EnemyTypeToString(EEnemyType Type)
	{
		switch (Type)
		{
		case EEnemyType::Grunt:			return TEXT("Grunt");
		case EEnemyType::Runner:		return TEXT("Runner");
		case EEnemyType::Tank:			return TEXT("Tank");
		case EEnemyType::Archer:		return TEXT("Archer");
		case EEnemyType::Assassin:		return TEXT("Assassin");
		case EEnemyType::Bomber:		return TEXT("Bomber");
		case EEnemyType::ShieldKnight:	return TEXT("Shield Knight");
		case EEnemyType::Charger:		return TEXT("Charger");
		case EEnemyType::Commander:		return TEXT("Commander");
		case EEnemyType::Healer:		return TEXT("Healer");
		default:						return TEXT("None");
		}
	}

	const TCHAR* FamilyToString(EEnemyBehaviourFamily Family)
	{
		switch (Family)
		{
		case EEnemyBehaviourFamily::Melee:		return TEXT("Melee");
		case EEnemyBehaviourFamily::Rush:		return TEXT("Rush");
		case EEnemyBehaviourFamily::Ranged:		return TEXT("Ranged");
		case EEnemyBehaviourFamily::Special:	return TEXT("Special");
		case EEnemyBehaviourFamily::Support:	return TEXT("Support");
		default:								return TEXT("None");
		}
	}

	const TCHAR* TargetPriorityToString(ETargetPriority Priority)
	{
		switch (Priority)
		{
		case ETargetPriority::King:				return TEXT("King");
		case ETargetPriority::NearestGuard:		return TEXT("Nearest Guard");
		case ETargetPriority::WeakestGuard:		return TEXT("Weakest Guard");
		case ETargetPriority::IsolatedGuard:	return TEXT("Isolated Guard");
		case ETargetPriority::GuardCluster:		return TEXT("Guard Cluster");
		default:								return TEXT("None");
		}
	}

	const TCHAR* RoleAxisToString(EEnemyRoleAxis Axis)
	{
		switch (Axis)
		{
		case EEnemyRoleAxis::AntiCluster:		return TEXT("Anti-Cluster");
		case EEnemyRoleAxis::KingPressure:		return TEXT("King Pressure");
		case EEnemyRoleAxis::Frontline:			return TEXT("Frontline");
		case EEnemyRoleAxis::RangedPressure:	return TEXT("Ranged Pressure");
		case EEnemyRoleAxis::IsolatedGuard:		return TEXT("Isolated Guard");
		case EEnemyRoleAxis::Support:			return TEXT("Support");
		default:								return TEXT("None");
		}
	}

	/** True when Value sits inside 0..1 and is a real number. */
	bool IsNormalised(float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.f && Value <= 1.f;
	}
}

// ==============================================================================================
// Queries
// ==============================================================================================

EEnemyBehaviourFamily UEnemyDataAsset::GetEffectiveBehaviourFamily() const
{
	if (BehaviourFamily != EEnemyBehaviourFamily::None)
	{
		return BehaviourFamily;
	}

	// Phase 1 already holds the canonical type-to-family mapping. Falling back to it keeps one
	// source of truth rather than restating the roster here.
	return EnemyTypeDefaults::GetDefaultFamily(EnemyType);
}

float UEnemyDataAsset::GetAttackRate() const
{
	return AttackCooldownSeconds > KINDA_SMALL_NUMBER ? (1.f / AttackCooldownSeconds) : 0.f;
}

FPrimaryAssetId UEnemyDataAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(EnemyDefinitionAssetType, GetFName());
}

// ==============================================================================================
// Validation
// ==============================================================================================

bool UEnemyDataAsset::ValidateDefinition(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();

	// --- Identity -------------------------------------------------------------------------
	if (EnemyType == EEnemyType::None || EnemyType == EEnemyType::Count)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "NoEnemyType",
			"Enemy Type must be set to one of the ten roster entries."));
	}

	if (DisplayName.IsEmpty())
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "NoDisplayName",
			"Display Name is empty."));
	}

	// --- Combat ---------------------------------------------------------------------------
	// The ClampMin metadata stops a designer typing these in the editor, but nothing stops a
	// value arriving from a copied asset or a future importer, so they are checked properly.
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadHealth",
			"Max Health must be greater than zero."));
	}

	if (!FMath::IsFinite(BaseDamage) || BaseDamage < 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadDamage",
			"Base Damage cannot be negative."));
	}

	if (!FMath::IsFinite(MovementSpeed) || MovementSpeed < 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadSpeed",
			"Movement Speed cannot be negative."));
	}

	if (!FMath::IsFinite(AttackRange) || AttackRange < 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadAttackRange",
			"Attack Range cannot be negative."));
	}

	if (!FMath::IsFinite(AttackCooldownSeconds) || AttackCooldownSeconds <= 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadCooldown",
			"Attack Cooldown must be greater than zero - it is used as a divisor."));
	}

	if (!FMath::IsFinite(DetectionRange) || DetectionRange < 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadDetectionRange",
			"Detection Range cannot be negative."));
	}

	if (!FMath::IsFinite(PreferredEngagementRange) || PreferredEngagementRange < 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadEngagementRange",
			"Preferred Engagement Range cannot be negative."));
	}

	// --- Ability ---------------------------------------------------------------------------
	// Only meaningful when the enemy actually has one; an unused block of zeroes is fine.
	if (bHasSpecialAbility)
	{
		if (!FMath::IsFinite(AbilityCooldownSeconds) || AbilityCooldownSeconds <= 0.f)
		{
			OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadAbilityCooldown",
				"Ability Cooldown must be greater than zero when the enemy has a special ability."));
		}

		if (!FMath::IsFinite(AbilityRadius) || AbilityRadius < 0.f)
		{
			OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadAbilityRadius",
				"Ability Radius cannot be negative."));
		}

		if (!FMath::IsFinite(AbilityStrength) || AbilityStrength < 0.f)
		{
			OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadAbilityStrength",
				"Ability Strength cannot be negative."));
		}

		if (!FMath::IsFinite(AbilityDurationSeconds) || AbilityDurationSeconds < 0.f)
		{
			OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadAbilityDuration",
				"Ability Duration cannot be negative."));
		}
	}

	// --- Wave ------------------------------------------------------------------------------
	if (!FMath::IsFinite(SpawnCost) || SpawnCost <= 0.f)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadSpawnCost",
			"Spawn Cost must be greater than zero - a free enemy would let a wave contain unlimited copies."));
	}

	if (MinimumRecommendedRound < 1)
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadMinimumRound",
			"Minimum Recommended Round must be at least 1."));
	}

	if (!IsNormalised(ThreatRating))
	{
		OutErrors.Add(NSLOCTEXT("EnemyDataAsset", "BadThreatRating",
			"Threat Rating must be between 0 and 1."));
	}

	// --- Normalised profiles ------------------------------------------------------------------
	EnemyDefinition::ForEachTargetPriority([this, &OutErrors](ETargetPriority Priority)
	{
		if (!IsNormalised(Targeting.Get(Priority)))
		{
			OutErrors.Add(FText::Format(
				NSLOCTEXT("EnemyDataAsset", "BadTargetWeight", "Target priority '{0}' must be between 0 and 1."),
				FText::FromString(TargetPriorityToString(Priority))));
		}
	});

	EnemyDefinition::ForEachRoleAxis([this, &OutErrors](EEnemyRoleAxis Axis)
	{
		if (!IsNormalised(Effectiveness.Get(Axis)))
		{
			OutErrors.Add(FText::Format(
				NSLOCTEXT("EnemyDataAsset", "BadEffectiveness", "Effectiveness '{0}' must be between 0 and 1."),
				FText::FromString(RoleAxisToString(Axis))));
		}
	});

	return OutErrors.Num() == ErrorsBefore;
}

bool UEnemyDataAsset::IsDefinitionValid() const
{
	TArray<FText> Ignored;
	return ValidateDefinition(Ignored);
}

// ==============================================================================================
// Debug
// ==============================================================================================

FString UEnemyDataAsset::GetEnemyDebugSummary() const
{
	FString Out;
	Out.Reserve(1024);

	Out += FString::Printf(TEXT("%s (%s)\n"),
		EnemyTypeToString(EnemyType),
		DisplayName.IsEmpty() ? TEXT("no display name") : *DisplayName.ToString());
	Out += FString::Printf(TEXT("  family            : %s%s\n"),
		FamilyToString(GetEffectiveBehaviourFamily()),
		BehaviourFamily == EEnemyBehaviourFamily::None ? TEXT(" (inherited default)") : TEXT(""));

	if (!RoleDescription.IsEmpty())
	{
		Out += FString::Printf(TEXT("  role              : %s\n"), *RoleDescription.ToString());
	}

	Out += FString::Printf(TEXT("  health / damage   : %.0f / %.0f\n"), MaxHealth, BaseDamage);
	Out += FString::Printf(TEXT("  speed             : %.0f uu/s\n"), MovementSpeed);
	Out += FString::Printf(TEXT("  attack range      : %.0f uu every %.2fs (%.2f/s)\n"),
		AttackRange, AttackCooldownSeconds, GetAttackRate());
	Out += FString::Printf(TEXT("  detection / hold  : %.0f / %.0f uu\n"),
		DetectionRange, PreferredEngagementRange);

	if (bHasSpecialAbility)
	{
		Out += FString::Printf(TEXT("  ability           : strength %.1f, radius %.0f uu, every %.1fs, lasts %.1fs\n"),
			AbilityStrength, AbilityRadius, AbilityCooldownSeconds, AbilityDurationSeconds);
	}
	else
	{
		Out += TEXT("  ability           : none\n");
	}

	Out += FString::Printf(TEXT("  spawn cost        : %.1f (from round %d, threat %.2f)\n"),
		SpawnCost, MinimumRecommendedRound, ThreatRating);

	Out += FString::Printf(TEXT("  primary target    : %s\n"),
		TargetPriorityToString(GetPrimaryTargetPriority()));
	Out += TEXT("  target weights    : ");
	EnemyDefinition::ForEachTargetPriority([this, &Out](ETargetPriority Priority)
	{
		Out += FString::Printf(TEXT("%s=%.2f "), TargetPriorityToString(Priority), Targeting.Get(Priority));
	});
	Out += TEXT("\n");

	Out += FString::Printf(TEXT("  strongest axis    : %s\n"), RoleAxisToString(GetStrongestRoleAxis()));
	Out += TEXT("  effectiveness     : ");
	EnemyDefinition::ForEachRoleAxis([this, &Out](EEnemyRoleAxis Axis)
	{
		Out += FString::Printf(TEXT("%s=%.2f "), RoleAxisToString(Axis), Effectiveness.Get(Axis));
	});
	Out += TEXT("\n");

	TArray<FText> Errors;
	if (!ValidateDefinition(Errors))
	{
		Out += FString::Printf(TEXT("  VALIDATION        : %d problem(s)\n"), Errors.Num());
		for (const FText& Error : Errors)
		{
			Out += FString::Printf(TEXT("    - %s\n"), *Error.ToString());
		}
	}
	else
	{
		Out += TEXT("  validation        : ok\n");
	}

	return Out;
}

void UEnemyDataAsset::LogEnemyDefinition() const
{
	UE_LOG(LogEnemyDefinition, Log, TEXT("\n%s"), *GetEnemyDebugSummary());
}
