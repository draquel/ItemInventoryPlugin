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
	virtual void SaveInventoryAsync(const FString& OwnerId, const TArray<FItemInstance>& Items,
		const FOnStorageComplete& Callback) override;
	virtual void LoadInventoryAsync(const FString& OwnerId,
		const FOnStorageLoadComplete& Callback) override;

private:
	/** Get the full file path for a given owner ID */
	static FString GetSavePath(const FString& OwnerId);

	/** Ensure the Inventories directory exists */
	static void EnsureDirectoryExists();
};
