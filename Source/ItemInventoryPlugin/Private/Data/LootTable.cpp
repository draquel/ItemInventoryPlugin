#include "Data/LootTable.h"

#if WITH_EDITOR
void ULootTable::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Validate entries
	auto ValidateEntryArray = [this](TArray<FLootEntry>& EntryArray, const FString& ArrayName)
	{
		for (int32 i = 0; i < EntryArray.Num(); ++i)
		{
			FLootEntry& Entry = EntryArray[i];

			// Ensure MinQuantity <= MaxQuantity
			if (Entry.MaxQuantity < Entry.MinQuantity)
			{
				Entry.MaxQuantity = Entry.MinQuantity;
				UE_LOG(LogTemp, Warning,
					TEXT("LootTable '%s': %s[%d] MaxQuantity was less than MinQuantity, clamped."),
					*GetName(), *ArrayName, i);
			}

			// Warn if neither item nor nested table is set
			if (!Entry.IsValid())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("LootTable '%s': %s[%d] has no ItemDefinitionId or NestedTable set."),
					*GetName(), *ArrayName, i);
			}

			// Warn if both item and nested table are set
			if (Entry.ItemDefinitionId.IsValid() && !Entry.NestedTable.IsNull())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("LootTable '%s': %s[%d] has both ItemDefinitionId and NestedTable — NestedTable takes priority."),
					*GetName(), *ArrayName, i);
			}

			// Check for self-referencing nested tables
			if (!Entry.NestedTable.IsNull())
			{
				ULootTable* Nested = Entry.NestedTable.Get();
				if (Nested == this)
				{
					Entry.NestedTable = nullptr;
					UE_LOG(LogTemp, Error,
						TEXT("LootTable '%s': %s[%d] references itself — cleared to prevent infinite recursion."),
						*GetName(), *ArrayName, i);
				}
			}
		}
	};

	ValidateEntryArray(Entries, TEXT("Entries"));
	ValidateEntryArray(GuaranteedEntries, TEXT("GuaranteedEntries"));
}
#endif
