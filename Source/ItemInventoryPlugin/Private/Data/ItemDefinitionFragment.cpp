#include "Data/ItemDefinitionFragment.h"

FText UItemDefinitionFragment::GetFragmentDisplayName_Implementation() const
{
	return FText::FromString(GetClass()->GetName());
}

FText UItemDefinitionFragment::GetFragmentDescription_Implementation() const
{
	return FText::GetEmpty();
}
