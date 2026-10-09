#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Key.generated.h"

/**
 * Marks an item as a key. A lock names the Item.Key.* tag it accepts; an interactor carrying an
 * item whose Key fragment has that tag can open it. Whether the key is spent is the fragment's
 * call, so single-use and permanent keys share one definition type.
 *
 * Consumers (doors, chests) find the key by walking the interactor's inventory for an item whose
 * definition carries this fragment with a matching KeyTag; see VoxelWorldPOI's APOIDungeonDoor.
 */
UCLASS(BlueprintType, DisplayName = "Key")
class ITEMINVENTORYPLUGIN_API UItemFragment_Key : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	/** The lock identity this key opens (Item.Key.*). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Key", meta = (Categories = "Item.Key"))
	FGameplayTag KeyTag;

	/** Remove one from the stack when the key unlocks something. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Key")
	bool bConsumeOnUnlock = true;

	/** True if this key opens a lock that accepts LockTag (exact match or a child of it). */
	bool Opens(const FGameplayTag& LockTag) const { return KeyTag.IsValid() && LockTag.IsValid() && KeyTag.MatchesTag(LockTag); }
};
