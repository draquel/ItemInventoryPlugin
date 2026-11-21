#include "Misc/AutomationTest.h"
#include "Data/Fragments/ItemFragment_RangedWeapon.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
// UItemFragment_RangedWeapon::GetDrawPower: no shot below the shortest draw, MinDrawPower at it, full power at the
// full draw and after it, linear between.
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRangedWeapon_DrawPower,
	"ItemInventory.Fragments.RangedWeapon.DrawPower",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRangedWeapon_DrawPower::RunTest(const FString& Parameters)
{
	UItemFragment_RangedWeapon* Bow = NewObject<UItemFragment_RangedWeapon>();
	Bow->MinDrawSeconds = 0.25f;
	Bow->FullDrawSeconds = 1.25f;
	Bow->MinDrawPower = 0.3f;

	TestEqual("No draw: no shot", Bow->GetDrawPower(0.f), 0.f);
	TestEqual("Just short of the shortest draw: no shot", Bow->GetDrawPower(0.24f), 0.f);
	TestEqual("The shortest draw: its power", Bow->GetDrawPower(0.25f), 0.3f);
	TestEqual("Half way: half way between", Bow->GetDrawPower(0.75f), 0.65f);
	TestEqual("The full draw: full power", Bow->GetDrawPower(1.25f), 1.f);
	TestEqual("Held past it: still full", Bow->GetDrawPower(5.f), 1.f);

	// A weapon whose shortest draw is its full draw shoots at full power or not at all.
	Bow->FullDrawSeconds = 0.25f;
	TestEqual("No span: full power at the draw", Bow->GetDrawPower(0.25f), 1.f);
	TestEqual("No span: nothing below it", Bow->GetDrawPower(0.1f), 0.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
