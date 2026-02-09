#include "Storage/LocalItemStorage.h"
#include "Storage/ItemSerializationUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"

// ---------------------------------------------------------------------------
// ICGFItemStorageInterface — Sync
// ---------------------------------------------------------------------------

bool ULocalItemStorage::SaveInventory_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items)
{
	EnsureDirectoryExists();

	const FString JsonString = FItemSerializationUtils::SerializeInventory(OwnerId, Items);
	const FString FinalPath = GetSavePath(OwnerId);
	const FString TempPath = FinalPath + TEXT(".tmp");

	// Atomic write: write to temp file, then rename
	if (!FFileHelper::SaveStringToFile(JsonString, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("LocalItemStorage: Failed to write temp file '%s'"), *TempPath);
		return false;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Remove existing final file if present
	if (PlatformFile.FileExists(*FinalPath))
	{
		PlatformFile.DeleteFile(*FinalPath);
	}

	if (!PlatformFile.MoveFile(*FinalPath, *TempPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("LocalItemStorage: Failed to rename '%s' to '%s'"), *TempPath, *FinalPath);
		// Clean up temp file
		PlatformFile.DeleteFile(*TempPath);
		return false;
	}

	UE_LOG(LogTemp, Verbose, TEXT("LocalItemStorage: Saved inventory for '%s' (%d items)"), *OwnerId, Items.Num());
	return true;
}

TArray<FItemInstance> ULocalItemStorage::LoadInventory_Implementation(const FString& OwnerId)
{
	TArray<FItemInstance> Items;
	const FString FilePath = GetSavePath(OwnerId);

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Verbose, TEXT("LocalItemStorage: No save file for '%s' at '%s'"), *OwnerId, *FilePath);
		return Items;
	}

	FString LoadedOwnerId;
	if (!FItemSerializationUtils::DeserializeInventory(JsonString, LoadedOwnerId, Items, GetTransientPackage()))
	{
		UE_LOG(LogTemp, Warning, TEXT("LocalItemStorage: Failed to deserialize inventory for '%s'"), *OwnerId);
		Items.Empty();
	}

	UE_LOG(LogTemp, Verbose, TEXT("LocalItemStorage: Loaded inventory for '%s' (%d items)"), *OwnerId, Items.Num());
	return Items;
}

bool ULocalItemStorage::DeleteInventory_Implementation(const FString& OwnerId)
{
	const FString FilePath = GetSavePath(OwnerId);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*FilePath))
	{
		return PlatformFile.DeleteFile(*FilePath);
	}

	return true; // Nothing to delete
}

// ---------------------------------------------------------------------------
// ICGFItemStorageInterface — Async
// ---------------------------------------------------------------------------

void ULocalItemStorage::SaveInventoryAsync(const FString& OwnerId, const TArray<FItemInstance>& Items,
	const FOnStorageComplete& Callback)
{
	// Serialize to string on the calling thread (UObjects aren't thread-safe)
	const FString JsonString = FItemSerializationUtils::SerializeInventory(OwnerId, Items);
	const FString FinalPath = GetSavePath(OwnerId);

	EnsureDirectoryExists();

	// File I/O on background thread, callback on game thread
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [JsonString, FinalPath, Callback, WeakThis = TWeakObjectPtr<ULocalItemStorage>(this)]()
	{
		const FString TempPath = FinalPath + TEXT(".tmp");
		bool bSuccess = false;

		if (FFileHelper::SaveStringToFile(JsonString, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (PlatformFile.FileExists(*FinalPath))
			{
				PlatformFile.DeleteFile(*FinalPath);
			}
			bSuccess = PlatformFile.MoveFile(*FinalPath, *TempPath);
			if (!bSuccess)
			{
				PlatformFile.DeleteFile(*TempPath);
			}
		}

		AsyncTask(ENamedThreads::GameThread, [Callback, bSuccess]()
		{
			Callback.ExecuteIfBound(bSuccess);
		});
	});
}

void ULocalItemStorage::LoadInventoryAsync(const FString& OwnerId,
	const FOnStorageLoadComplete& Callback)
{
	const FString FilePath = GetSavePath(OwnerId);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [FilePath, OwnerId, Callback]()
	{
		FString JsonString;
		bool bFileLoaded = FFileHelper::LoadFileToString(JsonString, *FilePath);

		// Deserialization must happen on game thread (creates UObjects)
		AsyncTask(ENamedThreads::GameThread, [JsonString = MoveTemp(JsonString), bFileLoaded, OwnerId, Callback]()
		{
			TArray<FItemInstance> Items;
			bool bSuccess = false;

			if (bFileLoaded)
			{
				FString LoadedOwnerId;
				bSuccess = FItemSerializationUtils::DeserializeInventory(JsonString, LoadedOwnerId, Items, GetTransientPackage());
			}
			else
			{
				// No file = valid empty inventory
				bSuccess = true;
			}

			Callback.ExecuteIfBound(bSuccess, Items);
		});
	});
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

FString ULocalItemStorage::GetSavePath(const FString& OwnerId)
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("Inventories") / (OwnerId + TEXT(".json"));
}

void ULocalItemStorage::EnsureDirectoryExists()
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("Inventories");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Dir))
	{
		PlatformFile.CreateDirectoryTree(*Dir);
	}
}
