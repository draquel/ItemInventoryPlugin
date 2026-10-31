#include "Storage/RemoteItemStorage.h"
#include "ItemInventoryPlugin.h"
#include "Storage/ItemSystemSettings.h"
#include "Storage/ItemSerializationUtils.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// ---------------------------------------------------------------------------
// ICGFItemStorageInterface — Sync
// ---------------------------------------------------------------------------

bool URemoteItemStorage::SaveInventory_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items)
{
	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();
	const FString JsonString = FItemSerializationUtils::SerializeInventory(OwnerId, Items);

	for (int32 Attempt = 0; Attempt <= Settings->RemoteMaxRetries; ++Attempt)
	{
		TSharedRef<IHttpRequest> Request = CreateRequest(TEXT("POST"), OwnerId);
		Request->SetContentAsString(JsonString);

		// Blocking wait
		bool bRequestComplete = false;
		bool bSuccess = false;
		Request->OnProcessRequestComplete().BindLambda(
			[&bRequestComplete, &bSuccess](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				bSuccess = bConnectedSuccessfully && Response.IsValid() &&
					EHttpResponseCodes::IsOk(Response->GetResponseCode());
				bRequestComplete = true;
			});

		Request->ProcessRequest();

		// Spin-wait for completion (suitable for shutdown saves)
		const double TimeoutEnd = FPlatformTime::Seconds() + Settings->RemoteTimeoutSeconds;
		while (!bRequestComplete && FPlatformTime::Seconds() < TimeoutEnd)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.016f);
			FPlatformProcess::Sleep(0.016f);
		}

		if (bSuccess)
		{
			return true;
		}

		if (Attempt < Settings->RemoteMaxRetries)
		{
			// Exponential backoff: 1s, 2s, 4s...
			const float Delay = FMath::Pow(2.0f, static_cast<float>(Attempt));
			UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: Save attempt %d failed for '%s', retrying in %.0fs"),
				Attempt + 1, *OwnerId, Delay);
			FPlatformProcess::Sleep(Delay);
		}
	}

	UE_LOG(LogIIP, Error, TEXT("RemoteItemStorage: Failed to save inventory for '%s' after %d attempts"),
		*OwnerId, Settings->RemoteMaxRetries + 1);
	return false;
}

TArray<FItemInstance> URemoteItemStorage::LoadInventory_Implementation(const FString& OwnerId)
{
	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();
	TArray<FItemInstance> Items;

	for (int32 Attempt = 0; Attempt <= Settings->RemoteMaxRetries; ++Attempt)
	{
		TSharedRef<IHttpRequest> Request = CreateRequest(TEXT("GET"), OwnerId);

		bool bRequestComplete = false;
		bool bSuccess = false;
		FString ResponseBody;
		Request->OnProcessRequestComplete().BindLambda(
			[&bRequestComplete, &bSuccess, &ResponseBody](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				if (bConnectedSuccessfully && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
				{
					bSuccess = true;
					ResponseBody = Response->GetContentAsString();
				}
				bRequestComplete = true;
			});

		Request->ProcessRequest();

		const double TimeoutEnd = FPlatformTime::Seconds() + Settings->RemoteTimeoutSeconds;
		while (!bRequestComplete && FPlatformTime::Seconds() < TimeoutEnd)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.016f);
			FPlatformProcess::Sleep(0.016f);
		}

		if (bSuccess)
		{
			FString LoadedOwnerId;
			if (FItemSerializationUtils::DeserializeInventory(ResponseBody, LoadedOwnerId, Items, GetTransientPackage()))
			{
				return Items;
			}
			UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: Failed to deserialize response for '%s'"), *OwnerId);
			return Items;
		}

		if (Attempt < Settings->RemoteMaxRetries)
		{
			const float Delay = FMath::Pow(2.0f, static_cast<float>(Attempt));
			UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: Load attempt %d failed for '%s', retrying in %.0fs"),
				Attempt + 1, *OwnerId, Delay);
			FPlatformProcess::Sleep(Delay);
		}
	}

	UE_LOG(LogIIP, Error, TEXT("RemoteItemStorage: Failed to load inventory for '%s' after %d attempts"),
		*OwnerId, Settings->RemoteMaxRetries + 1);
	return Items;
}

bool URemoteItemStorage::DeleteInventory_Implementation(const FString& OwnerId)
{
	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();

	for (int32 Attempt = 0; Attempt <= Settings->RemoteMaxRetries; ++Attempt)
	{
		TSharedRef<IHttpRequest> Request = CreateRequest(TEXT("DELETE"), OwnerId);

		bool bRequestComplete = false;
		bool bSuccess = false;
		Request->OnProcessRequestComplete().BindLambda(
			[&bRequestComplete, &bSuccess](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				bSuccess = bConnectedSuccessfully && Response.IsValid() &&
					EHttpResponseCodes::IsOk(Response->GetResponseCode());
				bRequestComplete = true;
			});

		Request->ProcessRequest();

		const double TimeoutEnd = FPlatformTime::Seconds() + Settings->RemoteTimeoutSeconds;
		while (!bRequestComplete && FPlatformTime::Seconds() < TimeoutEnd)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.016f);
			FPlatformProcess::Sleep(0.016f);
		}

		if (bSuccess)
		{
			return true;
		}

		if (Attempt < Settings->RemoteMaxRetries)
		{
			const float Delay = FMath::Pow(2.0f, static_cast<float>(Attempt));
			FPlatformProcess::Sleep(Delay);
		}
	}

	UE_LOG(LogIIP, Error, TEXT("RemoteItemStorage: Failed to delete inventory for '%s'"), *OwnerId);
	return false;
}

// ---------------------------------------------------------------------------
// ICGFItemStorageInterface — Async
// ---------------------------------------------------------------------------

void URemoteItemStorage::SaveInventoryAsync(const FString& OwnerId, const TArray<FItemInstance>& Items,
	const FOnStorageComplete& Callback)
{
	const FString JsonString = FItemSerializationUtils::SerializeInventory(OwnerId, Items);
	TSharedRef<IHttpRequest> Request = CreateRequest(TEXT("POST"), OwnerId);
	Request->SetContentAsString(JsonString);

	Request->OnProcessRequestComplete().BindLambda(
		[Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			const bool bSuccess = bConnectedSuccessfully && Response.IsValid() &&
				EHttpResponseCodes::IsOk(Response->GetResponseCode());
			Callback.ExecuteIfBound(bSuccess);
		});

	Request->ProcessRequest();
}

void URemoteItemStorage::LoadInventoryAsync(const FString& OwnerId,
	const FOnStorageLoadComplete& Callback)
{
	TSharedRef<IHttpRequest> Request = CreateRequest(TEXT("GET"), OwnerId);

	Request->OnProcessRequestComplete().BindLambda(
		[Callback](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			TArray<FItemInstance> Items;
			bool bSuccess = false;

			if (bConnectedSuccessfully && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				FString LoadedOwnerId;
				bSuccess = FItemSerializationUtils::DeserializeInventory(
					Response->GetContentAsString(), LoadedOwnerId, Items, GetTransientPackage());
			}

			Callback.ExecuteIfBound(bSuccess, Items);
		});

	Request->ProcessRequest();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Blobs (feature 9)
// ---------------------------------------------------------------------------

bool URemoteItemStorage::SaveBlob_Implementation(const FString& Key, const FString& Json)
{
	TSharedRef<IHttpRequest> Request = CreateRequest(TEXT("POST"), TEXT("blobs/") + Key);
	Request->SetContentAsString(Json);
	Request->OnProcessRequestComplete().BindLambda([Key](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		UE_LOG(LogIIP, Log, TEXT("RemoteItemStorage: blob '%s' save %s (%d)"), *Key, bOk ? TEXT("ok") : TEXT("FAILED"), Response.IsValid() ? Response->GetResponseCode() : 0);
	});
	Request->ProcessRequest();
	return true;
}

bool URemoteItemStorage::LoadBlob_Implementation(const FString& Key, FString& OutJson)
{
	OutJson.Reset();
	UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: synchronous LoadBlob('%s') is not supported; use a local backend for world saves until the async path exists."), *Key);
	return false;
}

bool URemoteItemStorage::DeleteBlob_Implementation(const FString& Key)
{
	TSharedRef<IHttpRequest> Request = CreateRequest(TEXT("DELETE"), TEXT("blobs/") + Key);
	Request->ProcessRequest();
	return true;
}

bool URemoteItemStorage::HasBlob_Implementation(const FString& Key)
{
	return false;
}

FString URemoteItemStorage::GetEndpointURL(const FString& OwnerId)
{
	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();
	FString URL = Settings->RemoteEndpointURL;
	if (!URL.EndsWith(TEXT("/")))
	{
		URL += TEXT("/");
	}
	return URL + OwnerId;
}

FString URemoteItemStorage::GetAuthHeader()
{
	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();
	return FString::Printf(TEXT("Bearer %s"), *Settings->RemoteAuthToken);
}

TSharedRef<IHttpRequest> URemoteItemStorage::CreateRequest(const FString& Verb, const FString& OwnerId)
{
	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();

	TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(GetEndpointURL(OwnerId));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), GetAuthHeader());
	Request->SetTimeout(Settings->RemoteTimeoutSeconds);

	return Request;
}
