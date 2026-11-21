#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "Types/CGFCombatTypes.h"
#include "Data/ItemDefinitionFragment.h"
#include "ItemFragment_Equipment.generated.h"

UCLASS(BlueprintType, DisplayName = "Equipment")
class ITEMINVENTORYPLUGIN_API UItemFragment_Equipment : public UItemDefinitionFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FGameplayTag EquipmentSlotTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	TSoftObjectPtr<UStaticMesh> EquipMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	TSoftObjectPtr<USkeletalMesh> EquipSkeletalMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	TSubclassOf<UAnimInstance> AnimLayerClass;

	/**
	 * The action set the item animates its holder with (Equipment.AnimSet.*): which montages a character plays for it,
	 * beside the layer AnimLayerClass links. Empty = the game's default for a held item. The tags are the game's own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals", meta = (Categories = "Equipment.AnimSet"))
	FGameplayTag ActionSetTag;

	/**
	 * Per-item fit of the equipped mesh, in the slot's corrected frame (applied before the slot's AttachOffset): nudge a
	 * helmet down onto the head, scale a bulky chest piece. Identity for most items.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	FTransform AttachOffset = FTransform::Identity;

	/**
	 * The bone or socket the equipped mesh attaches to instead of its slot's own: a bow equipped in the main hand rides the
	 * left hand. None = the slot's. With an override the slot's skeleton correction (tuned for its own bone) does not apply;
	 * AttachOffset alone fits the mesh to the bone.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Visuals")
	FName AttachBoneOverride;

	/**
	 * Where the item is carried when it is not in the hand (the equipment sheathing plan, D77): a sheath slot
	 * (Equipment.Slot.Sheath.*) or a parent of several, matched as EquipmentSlotTag is. An owner with such a slot equips the
	 * item there and draws it into the sheath's hand (EquipmentSlotTag stays the hand it is used in); an owner without one
	 * equips it into EquipmentSlotTag. Empty = never sheathed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Sheath", meta = (Categories = "Equipment.Slot.Sheath"))
	FGameplayTag SheathSlotTag;

	/** The sheathed mesh's fit on the sheath's bone, in the sheath slot's corrected frame (as AttachOffset is in the hand's). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Sheath")
	FTransform SheathOffset = FTransform::Identity;

	/** The bone the sheathed mesh hangs on instead of the sheath slot's own; with it SheathOffset alone fits the mesh. None = the slot's. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Sheath")
	FName SheathBoneOverride;

	/**
	 * The slots this item empties while it is in the hand: it needs both hands (a bow or a pickaxe blocks
	 * Equipment.Slot.OffHand: no shield, no torch beside it). A sheath drawn into a blocked hand is sheathed when the item
	 * comes into the hand, and an equip or a draw into a blocked hand leaves its item sheathed. Empty = blocks nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (Categories = "Equipment.Slot"))
	FGameplayTagContainer BlockedSlotTags;

	/**
	 * The hand this sheathed item follows (the sheath loadout plan, D96): it comes into its own hand (EquipmentSlotTag)
	 * when that hand holds an item that does not block its own hand (BlockedSlotTags) and its own hand is free, and goes
	 * back to its sheath otherwise. A shield names Equipment.Slot.MainHand: drawn with a one-handed weapon, sheathed with
	 * it. The equipment manager keeps it so; a direct draw or sheathe of the item is refused. Empty = drawn on its own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Sheath", meta = (Categories = "Equipment.Slot"))
	FGameplayTag DrawWithSlotTag;

	/**
	 * The slot whose place on the body this item shares (the sheath loadout plan, D97): a quiver worn on the back shares
	 * the back's sheath, Equipment.Slot.Sheath.Back. Only an item SharesPlaceWithItemTags names can be there with it (a
	 * bow); equipping another there sends this item to the bag, and equipping this item sends another there to the bag.
	 * Empty = it shares no place.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (Categories = "Equipment.Slot"))
	FGameplayTag SharesPlaceWithSlotTag;

	/** The items (their definition's tags, any of these) that can be in SharesPlaceWithSlotTag beside this one: Item.Type.Bow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FGameplayTagContainer SharesPlaceWithItemTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> GrantedAbilities;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Effects")
	TArray<TSubclassOf<UGameplayEffect>> PassiveEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Effects")
	TArray<TSubclassOf<UGameplayEffect>> OnEquipEffects;

	/**
	 * Data-driven lasting stats while equipped ("+3 Defense", "+10 MaxHealth"), no effect asset
	 * needed. Applied through the project's stat-modifier effect class (see
	 * UEquipmentGASSettings::StatModifierEffectClass) and removed on unequip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Stats")
	TArray<FCGFAttributeModifier> StatModifiers;
};
