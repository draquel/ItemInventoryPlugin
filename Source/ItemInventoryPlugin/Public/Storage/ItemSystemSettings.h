#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ItemSystemSettings.generated.h"

UENUM(BlueprintType)
enum class EItemStorageBackend : uint8
{
	Local   UMETA(DisplayName = "Local File"),
	Remote  UMETA(DisplayName = "Remote Server")
};

/**
 * Project-wide settings for the item storage/persistence system.
 * Accessible via Project Settings > Plugins > Item System.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Item System"))
class ITEMINVENTORYPLUGIN_API UItemSystemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UItemSystemSettings();

	// --- UDeveloperSettings ---
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Item System"); }

	// --- Storage Backend ---

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Storage")
	EItemStorageBackend StorageBackend = EItemStorageBackend::Local;

	// --- Auto-Save ---

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Storage|Auto-Save",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float AutoSaveIntervalSeconds = 30.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Storage|Auto-Save")
	bool bAutoSaveEnabled = true;

	// --- Remote Backend ---

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Storage|Remote",
		meta = (EditCondition = "StorageBackend == EItemStorageBackend::Remote"))
	FString RemoteEndpointURL;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Storage|Remote",
		meta = (EditCondition = "StorageBackend == EItemStorageBackend::Remote"))
	FString RemoteAuthToken;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Storage|Remote",
		meta = (EditCondition = "StorageBackend == EItemStorageBackend::Remote",
		ClampMin = "0", UIMin = "0", ClampMax = "10", UIMax = "10"))
	int32 RemoteMaxRetries = 3;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Storage|Remote",
		meta = (EditCondition = "StorageBackend == EItemStorageBackend::Remote",
		ClampMin = "1.0", UIMin = "1.0"))
	float RemoteTimeoutSeconds = 10.0f;
};
