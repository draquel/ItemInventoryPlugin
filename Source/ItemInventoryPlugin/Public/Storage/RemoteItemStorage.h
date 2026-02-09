#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/CGFItemStorageInterface.h"
#include "RemoteItemStorage.generated.h"

/**
 * Remote HTTP-based storage implementation.
 * Sends/receives inventory data as JSON via REST endpoints.
 * Reads configuration (endpoint, auth token, timeout, retries) from UItemSystemSettings.
 */
UCLASS()
class ITEMINVENTORYPLUGIN_API URemoteItemStorage : public UObject, public ICGFItemStorageInterface
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
	/** Build the full URL for a given owner ID */
	static FString GetEndpointURL(const FString& OwnerId);

	/** Get the auth header value */
	static FString GetAuthHeader();

	/** Create a configured HTTP request with auth and content-type headers */
	static TSharedRef<class IHttpRequest> CreateRequest(const FString& Verb, const FString& OwnerId);
};
