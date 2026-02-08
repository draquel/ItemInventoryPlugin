#include "Data/ItemDefinition.h"

UItemDefinitionFragment* UItemDefinition::FindFragmentByClass(TSubclassOf<UItemDefinitionFragment> FragmentClass) const
{
	if (!FragmentClass)
	{
		return nullptr;
	}

	for (UItemDefinitionFragment* Fragment : Fragments)
	{
		if (Fragment && Fragment->IsA(FragmentClass))
		{
			return Fragment;
		}
	}
	return nullptr;
}

FPrimaryAssetId UItemDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("ItemDefinition"), GetFName());
}
