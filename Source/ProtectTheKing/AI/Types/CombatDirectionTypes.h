#pragma once

#include "CoreMinimal.h"
#include "CombatDirectionTypes.generated.h"

/**
 * The four battlefield sides, measured relative to the King.
 * Used for defence strength, spawn direction, attack direction and King damage direction.
 * Assumes Unreal's default axes: +X = North, +Y = East.
 */
UENUM(BlueprintType)
enum class ECompassDirection : uint8
{
	North,
	East,
	South,
	West,
	None	UMETA(Hidden),
	Count	UMETA(Hidden)
};

/** Number of real battlefield sides (excludes None/Count). */
static constexpr uint8 NumCompassSides = 4;

/**
 * A single float value per battlefield side.
 * Reused for every directional metric (kills, damage, guard uptime, ...) so the
 * telemetry and analysis layers only ever deal with one directional container.
 * Counts are stored as floats deliberately - it keeps the struct usable for both
 * accumulated totals and normalised 0..1 scores without duplicating the type.
 */
USTRUCT(BlueprintType)
struct FDirectionalStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction")
	float North = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction")
	float East = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction")
	float South = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Direction")
	float West = 0.f;

	float& operator[](ECompassDirection Direction)
	{
		switch (Direction)
		{
		case ECompassDirection::East:	return East;
		case ECompassDirection::South:	return South;
		case ECompassDirection::West:	return West;
		default:						return North;
		}
	}

	float Get(ECompassDirection Direction) const
	{
		switch (Direction)
		{
		case ECompassDirection::East:	return East;
		case ECompassDirection::South:	return South;
		case ECompassDirection::West:	return West;
		case ECompassDirection::North:	return North;
		default:						return 0.f;
		}
	}

	void Set(ECompassDirection Direction, float Value) { (*this)[Direction] = Value; }
	void Add(ECompassDirection Direction, float Amount) { (*this)[Direction] += Amount; }

	void Reset() { North = East = South = West = 0.f; }

	float GetTotal() const { return North + East + South + West; }

	float GetMax() const { return FMath::Max(FMath::Max(North, East), FMath::Max(South, West)); }

	float GetMin() const { return FMath::Min(FMath::Min(North, East), FMath::Min(South, West)); }

	/** Side holding the largest value. Ties resolve N -> E -> S -> W. */
	ECompassDirection GetStrongestDirection() const
	{
		ECompassDirection Best = ECompassDirection::North;
		float BestValue = North;
		for (uint8 i = 1; i < NumCompassSides; ++i)
		{
			const ECompassDirection Candidate = static_cast<ECompassDirection>(i);
			if (Get(Candidate) > BestValue)
			{
				BestValue = Get(Candidate);
				Best = Candidate;
			}
		}
		return Best;
	}

	/** Side holding the smallest value - the flank the wave generator wants to exploit. */
	ECompassDirection GetWeakestDirection() const
	{
		ECompassDirection Worst = ECompassDirection::North;
		float WorstValue = North;
		for (uint8 i = 1; i < NumCompassSides; ++i)
		{
			const ECompassDirection Candidate = static_cast<ECompassDirection>(i);
			if (Get(Candidate) < WorstValue)
			{
				WorstValue = Get(Candidate);
				Worst = Candidate;
			}
		}
		return Worst;
	}

	/** Rescales so the strongest side reads 1.0. Returns all-zero if nothing was recorded. */
	FDirectionalStats GetNormalised() const
	{
		const float MaxValue = GetMax();
		if (MaxValue <= KINDA_SMALL_NUMBER)
		{
			return FDirectionalStats();
		}

		FDirectionalStats Result;
		Result.North = North / MaxValue;
		Result.East = East / MaxValue;
		Result.South = South / MaxValue;
		Result.West = West / MaxValue;
		return Result;
	}
};

namespace CombatDirection
{
	/** Classifies a King-relative offset into a side. */
	inline ECompassDirection FromOffset(const FVector& KingRelativeOffset)
	{
		if (FMath::Abs(KingRelativeOffset.X) >= FMath::Abs(KingRelativeOffset.Y))
		{
			return KingRelativeOffset.X >= 0.f ? ECompassDirection::North : ECompassDirection::South;
		}
		return KingRelativeOffset.Y >= 0.f ? ECompassDirection::East : ECompassDirection::West;
	}

	/** Convenience wrapper for world-space positions. */
	inline ECompassDirection FromWorldLocation(const FVector& WorldLocation, const FVector& KingLocation)
	{
		return FromOffset(WorldLocation - KingLocation);
	}

	/** The opposite side - used by flank and pincer strategies. */
	inline ECompassDirection GetOpposite(ECompassDirection Direction)
	{
		switch (Direction)
		{
		case ECompassDirection::North:	return ECompassDirection::South;
		case ECompassDirection::South:	return ECompassDirection::North;
		case ECompassDirection::East:	return ECompassDirection::West;
		case ECompassDirection::West:	return ECompassDirection::East;
		default:						return ECompassDirection::None;
		}
	}

	/** Unit vector pointing away from the King along the given side. */
	inline FVector ToUnitVector(ECompassDirection Direction)
	{
		switch (Direction)
		{
		case ECompassDirection::North:	return FVector(1.f, 0.f, 0.f);
		case ECompassDirection::South:	return FVector(-1.f, 0.f, 0.f);
		case ECompassDirection::East:	return FVector(0.f, 1.f, 0.f);
		case ECompassDirection::West:	return FVector(0.f, -1.f, 0.f);
		default:						return FVector::ZeroVector;
		}
	}
}
