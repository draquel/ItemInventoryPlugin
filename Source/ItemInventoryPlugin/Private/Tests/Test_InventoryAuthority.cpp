#include "Misc/AutomationTest.h"
#include "Components/InventoryComponent.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Tags/CGFGameplayTags.h"
#include "Testing/CGFTestGameInstance.h"
#include "Types/CGFItemTypes.h"

#if WITH_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Server authority over items (G91), the move into a named slot (G94) and the replace that equipping swaps through (G90).
// A standalone game instance holds the definitions (stack sizes, weights, tags come from the item database); the
// components sit on actors in its world. A client is an actor with role AutonomousProxy: its Try* calls take their client
// branch, and the Server RPC it would send is delivered to the authority's component with CGFTesting::FRPCCall, as the net
// driver would on the server.
// ---------------------------------------------------------------------------
namespace InventoryAuthorityTestHelpers
{
	/** A component on the owner with Slots pre-allocated slots (BeginPlay has not run: play has not begun). */
	UInventoryComponent* MakeInventory(AActor* Owner, int32 Slots, float MaxWeight = 0.0f)
	{
		UInventoryComponent* Comp = NewObject<UInventoryComponent>(Owner);
		Comp->MaxSlots = Slots;
		Comp->MaxWeight = MaxWeight;
		Comp->InventorySlots.Items.SetNum(Slots);
		for (int32 i = 0; i < Slots; ++i)
		{
			Comp->InventorySlots.Items[i].SlotIndex = i;
			Comp->InventorySlots.Items[i].bIsOccupied = false;
		}
		Comp->InventorySlots.OwningComponent = Comp;
		return Comp;
	}

	/** Place an item straight into a slot (as replication or a load would; no stacking). */
	void PlaceItemInSlot(UInventoryComponent* Comp, const FItemInstance& Item, int32 Slot)
	{
		FInventorySlot& S = Comp->InventorySlots.Items[Slot];
		S.Item = Item;
		S.bIsOccupied = true;
	}

	/** A registered definition with a stack size, a weight and tags, rooted until Release. */
	FPrimaryAssetId RegisterDefinition(UItemDatabaseSubsystem* Database, const TCHAR* Name, int32 StackSize,
		float Weight = 0.0f, const FGameplayTagContainer& Tags = FGameplayTagContainer())
	{
		UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), Name);
		Definition->AddToRoot();
		Definition->MaxStackSize = StackSize;
		Definition->Weight = Weight;
		Definition->ItemTags = Tags;
		Database->RegisterDefinition(Definition->GetPrimaryAssetId(), Definition);
		return Definition->GetPrimaryAssetId();
	}

	void Release(UItemDatabaseSubsystem* Database, std::initializer_list<FPrimaryAssetId> Ids)
	{
		for (const FPrimaryAssetId& Id : Ids)
		{
			if (UItemDefinition* Definition = Database->GetDefinition(Id))
			{
				Definition->RemoveFromRoot();
			}
		}
	}

	int32 Result(EInventoryOperationResult Value)
	{
		return static_cast<int32>(Value);
	}
}

// ===========================================================================
// G91: a client cannot hand the server an item
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAuthorityClientAddTest,
	"ItemInventory.Authority.ClientAddRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAuthorityClientAddTest::RunTest(const FString& Parameters)
{
	using namespace InventoryAuthorityTestHelpers;
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database))
	{
		return true;
	}
	const FPrimaryAssetId Gem = RegisterDefinition(Database, TEXT("AuthorityTestGem"), 10);

	// The client's own inventory: an add or a replace carries a whole item, so the client is refused outright and no
	// request leaves it (the add RPC is gone). Nothing changes on the client either.
	UInventoryComponent* ClientInventory = MakeInventory(Game.SpawnRemoteOwner(), 5);
	AddExpectedMessage(TEXT("adding items is server-only"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("replacing items is server-only"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	const FItemInstance Forged = Database->CreateItemInstance(Gem, 10);
	TestEqual(TEXT("A client's TryAddItem is NotAuthorized"), Result(ClientInventory->TryAddItem(Forged)), Result(EInventoryOperationResult::NotAuthorized));
	TestEqual(TEXT("Nothing was added on the client"), ClientInventory->GetFilledSlotCount(), 0);

	const FItemInstance Held = Database->CreateItemInstance(Gem, 1);
	PlaceItemInSlot(ClientInventory, Held, 0); // as replicated from the server
	TestEqual(TEXT("A client's TryReplaceItem is NotAuthorized"), Result(ClientInventory->TryReplaceItem(Held.InstanceId, Forged)),
		Result(EInventoryOperationResult::NotAuthorized));
	TestTrue(TEXT("The replicated item is untouched"), ClientInventory->GetItemInSlot(0).InstanceId == Held.InstanceId);

	// The server, and the listen-server host (an owner with authority), add directly.
	UInventoryComponent* ServerInventory = MakeInventory(Game.SpawnOwner(), 5);
	TestEqual(TEXT("The authority's TryAddItem succeeds"), Result(ServerInventory->TryAddItem(Database->CreateItemInstance(Gem, 3))), Result(EInventoryOperationResult::Success));
	TestEqual(TEXT("The authority's inventory holds 3"), ServerInventory->GetItemCount(Gem), 3);

	// No add request exists for a client to send.
	TestNull(TEXT("UInventoryComponent has no ServerRPC_RequestAddItem"), ServerInventory->FindFunction(TEXT("ServerRPC_RequestAddItem")));

	Release(Database, { Gem });
	return true;
}

// ===========================================================================
// G94: a move into a named slot lands there or is refused
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryMoveTargetSlotTest,
	"ItemInventory.Operations.Move.TargetSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryMoveTargetSlotTest::RunTest(const FString& Parameters)
{
	using namespace InventoryAuthorityTestHelpers;
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database))
	{
		return true;
	}
	const FPrimaryAssetId Ore = RegisterDefinition(Database, TEXT("MoveSlotTestOre"), 20, 1.0f);
	const FPrimaryAssetId Sword = RegisterDefinition(Database, TEXT("MoveSlotTestSword"), 1, 5.0f);
	const FPrimaryAssetId Anvil = RegisterDefinition(Database, TEXT("MoveSlotTestAnvil"), 1, 25.0f);
	AActor* Owner = Game.SpawnOwner();
	UInventoryComponent* Bag = MakeInventory(Owner, 6);
	UInventoryComponent* Chest = MakeInventory(Owner, 6, 30.0f);

	// Into an empty named slot: it lands there, not in the first free slot (slot 0).
	const FItemInstance OreA = Database->CreateItemInstance(Ore, 5);
	PlaceItemInSlot(Bag, OreA, 0);
	TestEqual(TEXT("Moving into the chest's empty slot 3 succeeds"), Result(Bag->TryMoveItem(OreA.InstanceId, Chest, 3)), Result(EInventoryOperationResult::Success));
	TestTrue(TEXT("The stack is in slot 3"), Chest->GetItemInSlot(3).InstanceId == OreA.InstanceId);
	TestFalse(TEXT("Slot 0 stayed empty"), Chest->GetItemInSlot(0).IsValid());
	TestFalse(TEXT("The bag no longer holds it"), Bag->GetItemInSlot(0).IsValid());

	// Onto the same item's stack in the named slot: merged whole there, not into another stack.
	const FItemInstance OreB = Database->CreateItemInstance(Ore, 4);
	const FItemInstance OreElsewhere = Database->CreateItemInstance(Ore, 1);
	PlaceItemInSlot(Chest, OreElsewhere, 1);
	PlaceItemInSlot(Bag, OreB, 2);
	TestEqual(TEXT("Moving onto the stack in slot 3 succeeds"), Result(Bag->TryMoveItem(OreB.InstanceId, Chest, 3)), Result(EInventoryOperationResult::Success));
	TestEqual(TEXT("Slot 3 holds 9"), Chest->GetItemInSlot(3).StackCount, 9);
	TestEqual(TEXT("The stack in slot 1 is unchanged"), Chest->GetItemInSlot(1).StackCount, 1);

	// Refusals change neither side.
	const FItemInstance OreC = Database->CreateItemInstance(Ore, 15);
	PlaceItemInSlot(Bag, OreC, 4);
	TestEqual(TEXT("A stack that would overflow the one in the slot (9 + 15 > 20) is StackFull"), Result(Bag->TryMoveItem(OreC.InstanceId, Chest, 3)),
		Result(EInventoryOperationResult::StackFull));
	const FItemInstance SwordItem = Database->CreateItemInstance(Sword, 1);
	PlaceItemInSlot(Bag, SwordItem, 5);
	TestEqual(TEXT("Another item in the slot is SlotOccupied"), Result(Bag->TryMoveItem(SwordItem.InstanceId, Chest, 3)),
		Result(EInventoryOperationResult::SlotOccupied));
	TestEqual(TEXT("A slot past the end is InvalidSlot"), Result(Bag->TryMoveItem(SwordItem.InstanceId, Chest, 6)),
		Result(EInventoryOperationResult::InvalidSlot));
	const FItemInstance AnvilItem = Database->CreateItemInstance(Anvil, 1);
	PlaceItemInSlot(Bag, AnvilItem, 1);
	TestEqual(TEXT("An item past the chest's weight (10 + 25 > 30) is WeightExceeded"), Result(Bag->TryMoveItem(AnvilItem.InstanceId, Chest, 4)),
		Result(EInventoryOperationResult::WeightExceeded));
	TestEqual(TEXT("Slot 3 still holds 9"), Chest->GetItemInSlot(3).StackCount, 9);
	TestTrue(TEXT("The bag kept the refused stacks"), Bag->GetItemInSlot(4).IsValid() && Bag->GetItemInSlot(5).IsValid() && Bag->GetItemInSlot(1).IsValid());

	// Within one inventory: to another slot (the weight is not counted twice), and onto its own slot (nothing to do).
	TestEqual(TEXT("Moving within the bag to slot 0 succeeds"), Result(Bag->TryMoveItem(SwordItem.InstanceId, Bag, 0)), Result(EInventoryOperationResult::Success));
	TestTrue(TEXT("The sword is in slot 0"), Bag->GetItemInSlot(0).InstanceId == SwordItem.InstanceId);
	TestFalse(TEXT("Slot 5 is empty"), Bag->GetItemInSlot(5).IsValid());
	TestEqual(TEXT("Moving onto its own slot succeeds"), Result(Bag->TryMoveItem(SwordItem.InstanceId, Bag, 0)), Result(EInventoryOperationResult::Success));
	TestTrue(TEXT("The sword is still in slot 0"), Bag->GetItemInSlot(0).InstanceId == SwordItem.InstanceId);

	Release(Database, { Ore, Sword, Anvil });
	return true;
}

// ===========================================================================
// The replace equipping swaps through (G90)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryReplaceTest,
	"ItemInventory.Operations.Replace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryReplaceTest::RunTest(const FString& Parameters)
{
	using namespace InventoryAuthorityTestHelpers;
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database))
	{
		return true;
	}
	FGameplayTagContainer WeaponTags;
	WeaponTags.AddTag(CGFGameplayTags::Item_Category_Weapon);
	FGameplayTagContainer QuestTags;
	QuestTags.AddTag(CGFGameplayTags::Item_Category_Quest);
	const FPrimaryAssetId Sword = RegisterDefinition(Database, TEXT("ReplaceTestSword"), 1, 4.0f, WeaponTags);
	const FPrimaryAssetId Axe = RegisterDefinition(Database, TEXT("ReplaceTestAxe"), 1, 6.0f, WeaponTags);
	const FPrimaryAssetId Anvil = RegisterDefinition(Database, TEXT("ReplaceTestAnvil"), 1, 50.0f, WeaponTags);
	const FPrimaryAssetId Relic = RegisterDefinition(Database, TEXT("ReplaceTestRelic"), 1, 0.0f, QuestTags);
	const FPrimaryAssetId Arrow = RegisterDefinition(Database, TEXT("ReplaceTestArrow"), 10, 0.0f, WeaponTags);

	UInventoryComponent* Bag = MakeInventory(Game.SpawnOwner(), 3, 20.0f);
	Bag->DeniedItemTags.AddTag(CGFGameplayTags::Item_Category_Quest);
	const FItemInstance SwordItem = Database->CreateItemInstance(Sword, 1);
	PlaceItemInSlot(Bag, SwordItem, 2);

	// The axe takes the sword's slot in one step.
	const FItemInstance AxeItem = Database->CreateItemInstance(Axe, 1);
	TestEqual(TEXT("Replacing the sword with the axe succeeds"), Result(Bag->TryReplaceItem(SwordItem.InstanceId, AxeItem)), Result(EInventoryOperationResult::Success));
	TestTrue(TEXT("Slot 2 holds the axe"), Bag->GetItemInSlot(2).InstanceId == AxeItem.InstanceId);
	TestEqual(TEXT("The sword is gone"), Bag->FindSlotIndexByInstanceId(SwordItem.InstanceId), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("One slot is filled"), Bag->GetFilledSlotCount(), 1);

	// Refusals leave the axe where it is.
	TestEqual(TEXT("An instance the bag does not hold is ItemNotFound"), Result(Bag->TryReplaceItem(FGuid::NewGuid(), SwordItem)),
		Result(EInventoryOperationResult::ItemNotFound));
	TestEqual(TEXT("No item is InvalidItem"), Result(Bag->TryReplaceItem(AxeItem.InstanceId, FItemInstance())),
		Result(EInventoryOperationResult::InvalidItem));
	TestEqual(TEXT("A denied tag is FilterRejected"), Result(Bag->TryReplaceItem(AxeItem.InstanceId, Database->CreateItemInstance(Relic, 1))),
		Result(EInventoryOperationResult::FilterRejected));
	TestEqual(TEXT("More than one stack is StackFull"), Result(Bag->TryReplaceItem(AxeItem.InstanceId, Database->CreateItemInstance(Arrow, 11))),
		Result(EInventoryOperationResult::StackFull));
	TestEqual(TEXT("Past the weight limit (50 for 6 in 20) is WeightExceeded"), Result(Bag->TryReplaceItem(AxeItem.InstanceId, Database->CreateItemInstance(Anvil, 1))),
		Result(EInventoryOperationResult::WeightExceeded));
	TestTrue(TEXT("Slot 2 still holds the axe"), Bag->GetItemInSlot(2).InstanceId == AxeItem.InstanceId);

	Release(Database, { Sword, Axe, Anvil, Relic, Arrow });
	return true;
}

// ===========================================================================
// Simulated server/client: a move request travels as an instance id and a slot
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAuthorityClientMoveTest,
	"ItemInventory.Authority.ClientMoveRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAuthorityClientMoveTest::RunTest(const FString& Parameters)
{
	using namespace InventoryAuthorityTestHelpers;
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database))
	{
		return true;
	}
	const FPrimaryAssetId Torch = RegisterDefinition(Database, TEXT("ClientMoveTestTorch"), 1);
	const FPrimaryAssetId Rope = RegisterDefinition(Database, TEXT("ClientMoveTestRope"), 1);

	// The server's copy of a player's bag and the client's replicated copy hold the same item in slot 0.
	UInventoryComponent* ServerBag = MakeInventory(Game.SpawnOwner(), 4);
	UInventoryComponent* ClientBag = MakeInventory(Game.SpawnRemoteOwner(), 4);
	const FItemInstance TorchItem = Database->CreateItemInstance(Torch, 1);
	PlaceItemInSlot(ServerBag, TorchItem, 0);
	PlaceItemInSlot(ClientBag, TorchItem, 0);

	// The client checks with the same predicate and sends the request: Pending, its copy unchanged until replication.
	TestEqual(TEXT("The client's move is Pending"), Result(ClientBag->TryMoveItem(TorchItem.InstanceId, ClientBag, 2)), Result(EInventoryOperationResult::Pending));
	TestTrue(TEXT("The client's copy is unchanged"), ClientBag->GetItemInSlot(0).InstanceId == TorchItem.InstanceId);

	// The server receives it: the instance id and the slot, never an item. It lands in the named slot (G94).
	CGFTesting::FRPCCall(ServerBag, TEXT("ServerRPC_RequestMoveItem"))
		.Set(TEXT("InstanceId"), TorchItem.InstanceId).Set(TEXT("Target"), ServerBag).Set(TEXT("TargetSlot"), 2).Send();
	TestTrue(TEXT("On the server the torch is in slot 2"), ServerBag->GetItemInSlot(2).InstanceId == TorchItem.InstanceId);
	TestFalse(TEXT("On the server slot 0 is empty"), ServerBag->GetItemInSlot(0).IsValid());

	// The server checks again: a request into an occupied slot is refused there, whatever the client believed.
	const FItemInstance RopeItem = Database->CreateItemInstance(Rope, 1);
	PlaceItemInSlot(ServerBag, RopeItem, 1);
	const bool bSent = CGFTesting::FRPCCall(ServerBag, TEXT("ServerRPC_RequestMoveItem"))
		.Set(TEXT("InstanceId"), TorchItem.InstanceId).Set(TEXT("Target"), ServerBag).Set(TEXT("TargetSlot"), 1).Send();
	TestTrue(TEXT("The move request reached the server"), bSent);
	TestTrue(TEXT("The torch stays in slot 2"), ServerBag->GetItemInSlot(2).InstanceId == TorchItem.InstanceId);
	TestTrue(TEXT("The rope stays in slot 1"), ServerBag->GetItemInSlot(1).InstanceId == RopeItem.InstanceId);

	Release(Database, { Torch, Rope });
	return true;
}

// ===========================================================================
// G96: a client's move reaches only its own inventories and those opened to it
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryAuthorityMoveReachTest,
	"ItemInventory.Authority.MoveTargetReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FInventoryAuthorityMoveReachTest::RunTest(const FString& Parameters)
{
	using namespace InventoryAuthorityTestHelpers;
	CGFTesting::FTestGameInstance Game;
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The item database"), Database))
	{
		return true;
	}
	const FPrimaryAssetId Ore = RegisterDefinition(Database, TEXT("ReachTestOre"), 20);

	// The server's view of two players: each a controller owning a pawn and a player state, each with an inventory (the
	// player state's as a stash), and an unowned chest. Requests arrive at a player's bag with FRPCCall.
	AActor* ControllerA = Game.SpawnOwner();
	AActor* PawnA = Game.SpawnOwner();
	PawnA->SetOwner(ControllerA);
	AActor* StateA = Game.SpawnOwner();
	StateA->SetOwner(ControllerA);
	AActor* ControllerB = Game.SpawnOwner();
	AActor* PawnB = Game.SpawnOwner();
	PawnB->SetOwner(ControllerB);
	UInventoryComponent* BagA = MakeInventory(PawnA, 4);
	UInventoryComponent* StashA = MakeInventory(StateA, 4);
	UInventoryComponent* BagB = MakeInventory(PawnB, 4);
	UInventoryComponent* Chest = MakeInventory(Game.SpawnOwner(), 4);

	// The rule itself.
	TestTrue(TEXT("A's bag is reachable by A's pawn"), BagA->IsReachableBy(PawnA));
	TestTrue(TEXT("A's stash (its player state) is reachable by A's pawn: the same player"), StashA->IsReachableBy(PawnA));
	TestFalse(TEXT("B's bag is not reachable by A's pawn"), BagB->IsReachableBy(PawnA));
	TestFalse(TEXT("A closed chest is not reachable"), Chest->IsReachableBy(PawnA));
	TestFalse(TEXT("No requester reaches nothing"), BagA->IsReachableBy(nullptr));

	auto Move = [](UInventoryComponent* Source, const FGuid& Id, UInventoryComponent* Target)
	{
		return CGFTesting::FRPCCall(Source, TEXT("ServerRPC_RequestMoveItem"))
			.Set(TEXT("InstanceId"), Id).Set(TEXT("Target"), Target).Set(TEXT("TargetSlot"), -1).Send();
	};
	const FItemInstance OreA = Database->CreateItemInstance(Ore, 5);
	PlaceItemInSlot(BagA, OreA, 0);
	AddExpectedMessage(TEXT("named an inventory it cannot reach"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 4);

	// Into another player's bag: refused, nothing moves.
	TestTrue(TEXT("The move into B's bag reached the server"), Move(BagA, OreA.InstanceId, BagB));
	TestTrue(TEXT("Into B's bag: refused, the ore stays in A's bag"), BagA->GetItemInSlot(0).InstanceId == OreA.InstanceId);
	TestEqual(TEXT("Into B's bag: B's bag stays empty"), BagB->GetFilledSlotCount(), 0);

	// Into a chest nobody opened: refused.
	Move(BagA, OreA.InstanceId, Chest);
	TestEqual(TEXT("Into a closed chest: refused"), Chest->GetFilledSlotCount(), 0);

	// Into the same player's stash: moves.
	Move(BagA, OreA.InstanceId, StashA);
	TestEqual(TEXT("Into A's stash: moved"), StashA->GetItemCount(Ore), 5);
	TestEqual(TEXT("Into A's stash: A's bag is empty"), BagA->GetFilledSlotCount(), 0);

	// The chest opened to A (by A's pawn) takes A's moves, from the stash too (the same player), and not B's.
	TestTrue(TEXT("The server opens the chest to A"), Chest->SetOpenTo(PawnA, true));
	TestTrue(TEXT("The open chest is reachable by A's player state"), Chest->IsReachableBy(StateA));
	Move(StashA, OreA.InstanceId, Chest);
	TestEqual(TEXT("Into the chest opened to A: moved"), Chest->GetItemCount(Ore), 5);
	const FItemInstance OreB = Database->CreateItemInstance(Ore, 2);
	PlaceItemInSlot(BagB, OreB, 0);
	Move(BagB, OreB.InstanceId, Chest);
	TestEqual(TEXT("B into the chest opened to A: refused"), Chest->GetItemCount(Ore), 5);
	TestTrue(TEXT("B into the chest opened to A: B keeps its ore"), BagB->GetItemInSlot(0).InstanceId == OreB.InstanceId);

	// Closed again: A is refused.
	const FItemInstance OreA2 = Database->CreateItemInstance(Ore, 1);
	PlaceItemInSlot(BagA, OreA2, 0);
	TestTrue(TEXT("The server closes the chest to A"), Chest->SetOpenTo(PawnA, false));
	Move(BagA, OreA2.InstanceId, Chest);
	TestEqual(TEXT("Into the chest closed again: refused"), Chest->GetItemCount(Ore), 5);

	// Only the server opens: a client's component refuses.
	UInventoryComponent* ClientChest = MakeInventory(Game.SpawnRemoteOwner(), 2);
	AddExpectedMessage(TEXT("opening an inventory is server-only"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("SetOpenTo on a client is refused"), ClientChest->SetOpenTo(PawnA, true));

	// The authority's own move is game code (a trade, a container's transfer): no reach rule.
	TestEqual(TEXT("The server's TryMoveItem from A's bag into B's bag succeeds"),
		Result(BagA->TryMoveItem(OreA2.InstanceId, BagB)), Result(EInventoryOperationResult::Success));

	Release(Database, { Ore });
	return true;
}

#endif // WITH_AUTOMATION_TESTS
