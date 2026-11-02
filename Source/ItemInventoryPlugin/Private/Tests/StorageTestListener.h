// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/CGFItemTypes.h"
#include "StorageTestListener.generated.h"

/**
 * Test-only: records the storage callbacks (ICGFItemStorageInterface's asynchronous calls, UItemStorageSubsystem's
 * OnInventorySaved / OnInventoryLoaded), which are dynamic delegates and need a UFUNCTION to bind to. Used by
 * Test_RemoteStorage.cpp (G93).
 */
UCLASS(Transient)
class UStorageTestListener : public UObject
{
	GENERATED_BODY()

public:
	/** One reported result. */
	struct FResult
	{
		FString OwnerId;
		bool bSuccess = false;
		int32 ItemCount = 0;
		/** FPlatformTime::Seconds() when it arrived. */
		double Time = 0.0;
	};

	TArray<FResult> Saves;
	TArray<FResult> Loads;

	UFUNCTION()
	void OnSaved(const FString& OwnerId, bool bSuccess)
	{
		Saves.Add({ OwnerId, bSuccess, 0, FPlatformTime::Seconds() });
	}

	UFUNCTION()
	void OnLoaded(const FString& OwnerId, bool bSuccess, const TArray<FItemInstance>& Items)
	{
		Loads.Add({ OwnerId, bSuccess, Items.Num(), FPlatformTime::Seconds() });
	}

	UFUNCTION()
	void OnSubsystemLoaded(const FString& OwnerId, bool bSuccess)
	{
		Loads.Add({ OwnerId, bSuccess, 0, FPlatformTime::Seconds() });
	}

	/** @return A save callback bound to OnSaved */
	FOnStorageComplete SaveCallback()
	{
		FOnStorageComplete Callback;
		Callback.BindDynamic(this, &UStorageTestListener::OnSaved);
		return Callback;
	}

	/** @return A load callback bound to OnLoaded */
	FOnStorageLoadComplete LoadCallback()
	{
		FOnStorageLoadComplete Callback;
		Callback.BindDynamic(this, &UStorageTestListener::OnLoaded);
		return Callback;
	}
};
