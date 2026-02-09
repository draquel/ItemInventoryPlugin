#pragma once

#include "CoreMinimal.h"
#include "Types/CGFItemTypes.h"

class UItemInstanceFragment;

/**
 * Static utility class for serializing/deserializing inventory data to/from JSON.
 * Shared by both LocalItemStorage and RemoteItemStorage.
 */
class ITEMINVENTORYPLUGIN_API FItemSerializationUtils
{
public:
	/**
	 * Serialize an entire inventory to a JSON string.
	 * @param OwnerId  Owner identifier embedded in the JSON
	 * @param Items    Items to serialize
	 * @return JSON string ready for storage
	 */
	static FString SerializeInventory(const FString& OwnerId, const TArray<FItemInstance>& Items);

	/**
	 * Deserialize a JSON string back into an inventory.
	 * @param JsonString  The JSON string to parse
	 * @param OutOwnerId  Extracted owner ID
	 * @param OutItems    Reconstructed item instances
	 * @param Outer       UObject outer for creating fragment objects (typically GetTransientPackage())
	 * @return true on success
	 */
	static bool DeserializeInventory(const FString& JsonString, FString& OutOwnerId,
		TArray<FItemInstance>& OutItems, UObject* Outer);

	/** Serialize a single FItemInstance to a JSON object */
	static TSharedPtr<FJsonObject> SerializeItemInstance(const FItemInstance& Item);

	/** Deserialize a single FItemInstance from a JSON object */
	static FItemInstance DeserializeItemInstance(const TSharedPtr<FJsonObject>& JsonObj, UObject* Outer);

	/** Serialize a single UItemInstanceFragment to a JSON object */
	static TSharedPtr<FJsonObject> SerializeFragment(const UItemInstanceFragment* Fragment);

	/** Deserialize a single UItemInstanceFragment from a JSON object */
	static UItemInstanceFragment* DeserializeFragment(const TSharedPtr<FJsonObject>& JsonObj, UObject* Outer);
};
