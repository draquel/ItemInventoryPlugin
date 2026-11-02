#include "Storage/LocalItemStorage.h"
#include "ItemInventoryPlugin.h"
#include "Storage/ItemSerializationUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

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
		UE_LOG(LogIIP, Warning, TEXT("LocalItemStorage: Failed to write temp file '%s'"), *TempPath);
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
		UE_LOG(LogIIP, Warning, TEXT("LocalItemStorage: Failed to rename '%s' to '%s'"), *TempPath, *FinalPath);
		// Clean up temp file
		PlatformFile.DeleteFile(*TempPath);
		return false;
	}

	UE_LOG(LogIIP, Verbose, TEXT("LocalItemStorage: Saved inventory for '%s' (%d items)"), *OwnerId, Items.Num());
	return true;
}

TArray<FItemInstance> ULocalItemStorage::LoadInventory_Implementation(const FString& OwnerId)
{
	TArray<FItemInstance> Items;
	ReadInventory(OwnerId, Items);
	return Items;
}

bool ULocalItemStorage::ReadInventory(const FString& OwnerId, TArray<FItemInstance>& OutItems)
{
	OutItems.Reset();
	const FString FilePath = GetSavePath(OwnerId);

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogIIP, Verbose, TEXT("LocalItemStorage: No save file for '%s' at '%s'"), *OwnerId, *FilePath);
		return true;
	}

	FString LoadedOwnerId;
	if (!FItemSerializationUtils::DeserializeInventory(JsonString, LoadedOwnerId, OutItems, GetTransientPackage()))
	{
		UE_LOG(LogIIP, Warning, TEXT("LocalItemStorage: Failed to deserialize inventory for '%s'"), *OwnerId);
		OutItems.Empty();
		return false;
	}

	UE_LOG(LogIIP, Verbose, TEXT("LocalItemStorage: Loaded inventory for '%s' (%d items)"), *OwnerId, OutItems.Num());
	return true;
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

// A file answers at once: the asynchronous calls do the synchronous work on the calling thread and run the callback
// before returning, so the auto-save writes exactly as it always has and two saves of one inventory cannot reorder (G93).

void ULocalItemStorage::SaveInventoryAsync_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items,
	const FOnStorageComplete& Callback)
{
	const bool bSuccess = SaveInventory_Implementation(OwnerId, Items);
	Callback.ExecuteIfBound(OwnerId, bSuccess);
}

void ULocalItemStorage::LoadInventoryAsync_Implementation(const FString& OwnerId, const FOnStorageLoadComplete& Callback)
{
	TArray<FItemInstance> Items;
	const bool bSuccess = ReadInventory(OwnerId, Items);
	Callback.ExecuteIfBound(OwnerId, bSuccess, Items);
}

// ---------------------------------------------------------------------------
// ICGFItemStorageInterface — Blobs (feature 9)
// ---------------------------------------------------------------------------

namespace
{
	bool WriteFileAtomically(const FString& FinalPath, const FString& Contents)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const FString Dir = FPaths::GetPath(FinalPath);
		if (!PlatformFile.DirectoryExists(*Dir))
		{
			PlatformFile.CreateDirectoryTree(*Dir);
		}
		const FString TempPath = FinalPath + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(Contents, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return false;
		}
		if (PlatformFile.FileExists(*FinalPath))
		{
			PlatformFile.DeleteFile(*FinalPath);
		}
		if (!PlatformFile.MoveFile(*FinalPath, *TempPath))
		{
			PlatformFile.DeleteFile(*TempPath);
			return false;
		}
		return true;
	}
}

FString ULocalItemStorage::GetBlobPath(const FString& Key)
{
	// Keys are file names: keep them to a safe character set.
	FString Safe = Key;
	for (TCHAR& C : Safe)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-') && C != TEXT('.'))
		{
			C = TEXT('_');
		}
	}
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("Worlds") / (Safe + TEXT(".json"));
}

bool ULocalItemStorage::SaveBlob_Implementation(const FString& Key, const FString& Json)
{
	const bool bOk = WriteFileAtomically(GetBlobPath(Key), Json);
	UE_LOG(LogIIP, Log, TEXT("LocalItemStorage: %s blob '%s' (%d chars) -> %s"), bOk ? TEXT("saved") : TEXT("FAILED to save"), *Key, Json.Len(), *GetBlobPath(Key));
	return bOk;
}

bool ULocalItemStorage::LoadBlob_Implementation(const FString& Key, FString& OutJson)
{
	OutJson.Reset();
	return FFileHelper::LoadFileToString(OutJson, *GetBlobPath(Key));
}

bool ULocalItemStorage::DeleteBlob_Implementation(const FString& Key)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString Path = GetBlobPath(Key);
	return PlatformFile.FileExists(*Path) && PlatformFile.DeleteFile(*Path);
}

bool ULocalItemStorage::HasBlob_Implementation(const FString& Key)
{
	return FPlatformFileManager::Get().GetPlatformFile().FileExists(*GetBlobPath(Key));
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
