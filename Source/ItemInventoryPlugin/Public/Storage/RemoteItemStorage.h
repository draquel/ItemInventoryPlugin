#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/CGFItemStorageInterface.h"
#include "RemoteItemStorage.generated.h"

class FRemoteItemStorageQueue;

/** Where and how a URemoteItemStorage sends its requests; taken from UItemSystemSettings when the backend is created. */
USTRUCT(BlueprintType)
struct ITEMINVENTORYPLUGIN_API FRemoteItemStorageConfig
{
	GENERATED_BODY()

	/** The base URL; an inventory lives at <EndpointURL>/<OwnerId>, a blob at <EndpointURL>/blobs/<Key>. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage|Remote")
	FString EndpointURL;

	/** Sent as "Authorization: Bearer <AuthToken>". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage|Remote")
	FString AuthToken;

	/** Retries after a failed attempt (no connection, a timeout, 408, 429 or 5xx); other refusals are not retried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage|Remote", meta = (ClampMin = "0"))
	int32 MaxRetries = 3;

	/** One attempt's timeout, enforced by the HTTP module. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage|Remote", meta = (ClampMin = "0.0"))
	float TimeoutSeconds = 10.0f;

	/** The first retry's back-off; each further retry doubles it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage|Remote", meta = (ClampMin = "0.0"))
	float RetryDelaySeconds = 1.0f;

	/** @return The project's settings (UItemSystemSettings' Remote* properties) */
	static FRemoteItemStorageConfig FromSettings();
};

/**
 * Remote HTTP storage: inventories and blobs as JSON over REST, configured from UItemSystemSettings.
 *
 * Nothing waits on the calling thread (G93). Each resource (an inventory, a blob) has one request in flight at a time
 * and a queue behind it, so its operations land in the order requested; a save queued behind another replaces a save
 * already waiting there, and a failed save with a newer one waiting gives way to it, so of overlapping saves the last one
 * requested lands and the superseded ones report its result. Retries wait on the core ticker with exponential back-off.
 * The queue outlives this object while requests are in flight, so a save started at shutdown is still sent.
 * Callbacks run on the game thread.
 */
UCLASS()
class ITEMINVENTORYPLUGIN_API URemoteItemStorage : public UObject, public ICGFItemStorageInterface
{
	GENERATED_BODY()

public:
	URemoteItemStorage();

	// --- UObject ---
	/** Takes the configuration from UItemSystemSettings (not on the class default object). */
	virtual void PostInitProperties() override;

	/**
	 * Replace the configuration; requests already queued keep the one they started with.
	 * @param InConfig The endpoint, token, retries and timeouts to use from now on
	 */
	UFUNCTION(BlueprintCallable, Category = "Storage|Remote")
	void Configure(const FRemoteItemStorageConfig& InConfig);

	/** @return The configuration new requests use */
	UFUNCTION(BlueprintPure, Category = "Storage|Remote")
	FRemoteItemStorageConfig GetConfig() const;

	/** @return The operations queued or in flight, across every resource */
	UFUNCTION(BlueprintPure, Category = "Storage|Remote")
	int32 GetPendingRequestCount() const;

	// --- ICGFItemStorageInterface ---

	/** Queues the save and returns true at once (accepted); the outcome is logged. SaveInventoryAsync reports it. */
	virtual bool SaveInventory_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items) override;
	/** Cannot answer without waiting: returns nothing, with a warning. Use LoadInventoryAsync. */
	virtual TArray<FItemInstance> LoadInventory_Implementation(const FString& OwnerId) override;
	/** Queues the delete and returns true at once (accepted); the outcome is logged. DeleteInventoryAsync reports it. */
	virtual bool DeleteInventory_Implementation(const FString& OwnerId) override;
	virtual void SaveInventoryAsync_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items,
		const FOnStorageComplete& Callback) override;
	virtual void LoadInventoryAsync_Implementation(const FString& OwnerId, const FOnStorageLoadComplete& Callback) override;
	virtual void DeleteInventoryAsync_Implementation(const FString& OwnerId, const FOnStorageComplete& Callback) override;
	/** Blobs: Save / Delete queue a request on the blob's resource and return true at once (the outcome is logged); Load / Has need an async path (not implemented) and return false. */
	virtual bool SaveBlob_Implementation(const FString& Key, const FString& Json) override;
	virtual bool LoadBlob_Implementation(const FString& Key, FString& OutJson) override;
	virtual bool DeleteBlob_Implementation(const FString& Key) override;
	virtual bool HasBlob_Implementation(const FString& Key) override;

private:
	/** The per-resource request queues; shared with the requests in flight, which keep it alive. */
	TSharedPtr<FRemoteItemStorageQueue> Queue;
};
