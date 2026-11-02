#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/CGFItemStorageInterface.h"
#include "LocalItemStorage.generated.h"

/**
 * Local file-based storage implementation.
 * Saves/loads inventory data as JSON files in [SaveDir]/SaveGames/Inventories/{OwnerId}.json.
 * Uses atomic writes (write to .tmp then rename) for crash safety.
 */
UCLASS()
class ITEMINVENTORYPLUGIN_API ULocalItemStorage : public UObject, public ICGFItemStorageInterface
{
	GENERATED_BODY()

public:
	// --- ICGFItemStorageInterface ---
	virtual bool SaveInventory_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items) override;
	virtual TArray<FItemInstance> LoadInventory_Implementation(const FString& OwnerId) override;
	virtual bool DeleteInventory_Implementation(const FString& OwnerId) override;
	virtual bool SaveBlob_Implementation(const FString& Key, const FString& Json) override;
	virtual bool LoadBlob_Implementation(const FString& Key, FString& OutJson) override;
	virtual bool DeleteBlob_Implementation(const FString& Key) override;
	virtual bool HasBlob_Implementation(const FString& Key) override;

	/** Synchronous: the save, then the callback, before returning (G93: a file needs no wait). */
	virtual void SaveInventoryAsync_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items,
		const FOnStorageComplete& Callback) override;
	/** Synchronous: the read, then the callback, before returning; bSuccess false only for a file that does not deserialize. */
	virtual void LoadInventoryAsync_Implementation(const FString& OwnerId, const FOnStorageLoadComplete& Callback) override;

	/** [Saved]/SaveGames/Worlds/<Key>.json — where blobs (world saves) live. */
	static FString GetBlobPath(const FString& Key);

private:
	/**
	 * Read one inventory file.
	 * @param OwnerId  The inventory's storage id
	 * @param OutItems The items read; empty when there is no file
	 * @return False when the file exists but does not deserialize
	 */
	static bool ReadInventory(const FString& OwnerId, TArray<FItemInstance>& OutItems);

	/** Get the full file path for a given owner ID */
	static FString GetSavePath(const FString& OwnerId);

	/** Ensure the Inventories directory exists */
	static void EnsureDirectoryExists();
};
