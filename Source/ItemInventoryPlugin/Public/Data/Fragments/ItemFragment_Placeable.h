#pragma once

#include "CoreMinimal.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Placeable.generated.h"

/**
 * Marks an item as placeable in the world (feature 8: the campfire kit): using it spawns ActorClass
 * on the ground in front of the user. The character's use path owns the trace and the spawn
 * (authority); this fragment only says what and where.
 */
UCLASS(BlueprintType, DisplayName = "Placeable")
class ITEMINVENTORYPLUGIN_API UItemFragment_Placeable : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	/** The actor spawned where the item is placed (replicated actors place for everyone). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placeable")
	TSoftClassPtr<AActor> ActorClass;

	/** How far in front of the user the item may be placed (world units). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placeable", meta = (ClampMin = "50.0"))
	float PlaceDistance = 300.0f;

	/** Steepest ground the item accepts (degrees from flat). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placeable", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxSlopeDegrees = 35.0f;

	/** Lift above the hit point so the actor's origin sits on the ground. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placeable")
	float PlacementOffsetZ = 0.0f;

	/** Remove one from the stack once placed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Placeable")
	bool bConsumeOnPlace = true;

	/** Pure: does a ground normal pass the slope limit. */
	bool AcceptsGround(const FVector& Normal) const
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0))) <= MaxSlopeDegrees;
	}
};
