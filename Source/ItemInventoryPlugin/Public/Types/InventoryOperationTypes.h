#pragma once

#include "CoreMinimal.h"
#include "Types/CGFCommonEnums.h"
#include "InventoryOperationTypes.generated.h"

/**
 * Internal data passed between validation and execution.
 * Filled by Validate* functions, consumed by Internal_* functions.
 */
USTRUCT()
struct FInventoryOperationData
{
	GENERATED_BODY()

	EInventoryOperationResult Result = EInventoryOperationResult::Failed;

	/** Resolved target slot index */
	int32 ResolvedSlot = INDEX_NONE;

	/** For stacking: how much goes into existing stacks vs new slots */
	int32 StackedIntoExisting = 0;
	int32 RemainingQuantity = 0;
};
