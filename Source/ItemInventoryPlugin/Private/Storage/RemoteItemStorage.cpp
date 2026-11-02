#include "Storage/RemoteItemStorage.h"
#include "ItemInventoryPlugin.h"
#include "Storage/ItemSystemSettings.h"
#include "Storage/ItemSerializationUtils.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// ---------------------------------------------------------------------------
// The request queue (G93)
// ---------------------------------------------------------------------------

/**
 * One queue per resource path (an OwnerId, or blobs/<Key>). The front operation is the one running: in flight, or
 * waiting out a retry's back-off on the core ticker. Everything runs on the game thread (the HTTP module completes
 * requests there), so nothing here locks. Requests in flight and pending retries hold the queue by a shared reference.
 */
class FRemoteItemStorageQueue : public TSharedFromThis<FRemoteItemStorageQueue>
{
public:
	/** One request and the callbacks waiting on it. */
	struct FOperation
	{
		FString Verb;
		/** The resource path under the endpoint, and the queue's key. */
		FString Path;
		/** What the callbacks report as their OwnerId. */
		FString OwnerId;
		FString Body;
		TArray<FOnStorageComplete> DoneCallbacks;
		TArray<FOnStorageLoadComplete> LoadCallbacks;
		/** The configuration taken when the operation was queued. */
		FRemoteItemStorageConfig Config;
		/** Attempts that have failed so far. */
		int32 FailedAttempts = 0;

		bool IsSave() const { return Verb == TEXT("POST"); }
		bool IsLoad() const { return Verb == TEXT("GET"); }
	};

	FRemoteItemStorageConfig Config;

	/** Queue an operation behind its resource's others; start it when the resource is idle. */
	void Enqueue(FOperation&& InOp)
	{
		InOp.Config = Config;
		if (InOp.Config.EndpointURL.IsEmpty())
		{
			UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: %s %s refused: no endpoint URL (Project Settings > Plugins > Item System)"),
				*InOp.Verb, *InOp.Path);
			RunCallbacks(InOp, false, TArray<FItemInstance>());
			return;
		}

		const FString Path = InOp.Path;
		TArray<TSharedRef<FOperation>>& Ops = Queues.FindOrAdd(Path);

		// A save waiting behind the running operation takes the newer contents: the last save requested is the one that
		// lands, and the replaced save's callbacks report it. The running operation is never touched here.
		if (InOp.IsSave() && Ops.Num() > 1 && Ops.Last()->IsSave())
		{
			FOperation& Waiting = Ops.Last().Get();
			Waiting.Body = MoveTemp(InOp.Body);
			Waiting.Config = InOp.Config;
			Waiting.DoneCallbacks.Append(InOp.DoneCallbacks);
			UE_LOG(LogIIP, Verbose, TEXT("RemoteItemStorage: save of '%s' merged into the one waiting"), *Path);
			return;
		}

		Ops.Add(MakeShared<FOperation>(MoveTemp(InOp)));
		if (Ops.Num() == 1)
		{
			Send(Ops[0]);
		}
	}

	/** @return The operations queued or running, across every resource */
	int32 NumPending() const
	{
		int32 Count = 0;
		for (const TPair<FString, TArray<TSharedRef<FOperation>>>& Pair : Queues)
		{
			Count += Pair.Value.Num();
		}
		return Count;
	}

private:
	/** Per resource path: the running operation first, then the waiting ones in request order. */
	TMap<FString, TArray<TSharedRef<FOperation>>> Queues;

	void Send(TSharedRef<FOperation> Op)
	{
		FString URL = Op->Config.EndpointURL;
		if (!URL.EndsWith(TEXT("/")))
		{
			URL += TEXT("/");
		}

		TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(Op->Verb);
		Request->SetURL(URL + Op->Path);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Op->Config.AuthToken));
		Request->SetTimeout(Op->Config.TimeoutSeconds);
		if (!Op->Body.IsEmpty())
		{
			Request->SetContentAsString(Op->Body);
		}

		// The completion holds the queue, so a request sent as the game instance shuts down still lands.
		Request->OnProcessRequestComplete().BindLambda(
			[Self = AsShared(), Op](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				Self->OnResponse(Op, Response, bConnectedSuccessfully);
			});
		Request->ProcessRequest();
	}

	void OnResponse(const TSharedRef<FOperation>& Op, const FHttpResponsePtr& Response, bool bConnectedSuccessfully)
	{
		const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
		const bool bAnswered = bConnectedSuccessfully && Response.IsValid();

		if (bAnswered && EHttpResponseCodes::IsOk(Code))
		{
			TArray<FItemInstance> Items;
			bool bSuccess = true;
			if (Op->IsLoad())
			{
				// Deserialization creates instance fragments (UObjects): the game thread, where this runs.
				FString LoadedOwnerId;
				bSuccess = FItemSerializationUtils::DeserializeInventory(Response->GetContentAsString(), LoadedOwnerId,
					Items, GetTransientPackage());
				if (!bSuccess)
				{
					UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: GET %s answered with a document that does not deserialize"),
						*Op->Path);
					Items.Reset();
				}
			}
			UE_LOG(LogIIP, Verbose, TEXT("RemoteItemStorage: %s %s done (%d)"), *Op->Verb, *Op->Path, Code);
			Finish(Op, bSuccess, Items);
			return;
		}

		// Nothing stored is an empty inventory, as the local backend reads a missing file.
		if (bAnswered && Op->IsLoad() && Code == EHttpResponseCodes::NotFound)
		{
			Finish(Op, true, TArray<FItemInstance>());
			return;
		}

		const FString Reason = bAnswered ? FString::Printf(TEXT("HTTP %d"), Code) : FString(TEXT("no response"));
		const bool bRetryable = !bAnswered || Code >= 500 || Code == EHttpResponseCodes::RequestTimeout
			|| Code == EHttpResponseCodes::TooManyRequests;
		++Op->FailedAttempts;

		if (SupersedeFront(Op))
		{
			UE_LOG(LogIIP, Log, TEXT("RemoteItemStorage: save of '%s' failed (%s); the newer save waiting replaces it"),
				*Op->Path, *Reason);
			return;
		}

		if (bRetryable && Op->FailedAttempts <= Op->Config.MaxRetries && !IsEngineExitRequested())
		{
			const float Delay = Op->Config.RetryDelaySeconds * FMath::Pow(2.0f, static_cast<float>(Op->FailedAttempts - 1));
			UE_LOG(LogIIP, Log, TEXT("RemoteItemStorage: %s %s attempt %d failed (%s), retrying in %.2fs"),
				*Op->Verb, *Op->Path, Op->FailedAttempts, *Reason, Delay);

			// The back-off waits on the core ticker, never on the calling thread. A newer save that arrives meanwhile
			// replaces this one when the timer fires.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self = AsShared(), Op](float)
			{
				if (!Self->SupersedeFront(Op))
				{
					Self->Send(Op);
				}
				return false;
			}), Delay);
			return;
		}

		UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: %s %s failed after %d attempt(s) (%s)"),
			*Op->Verb, *Op->Path, Op->FailedAttempts, *Reason);
		Finish(Op, false, TArray<FItemInstance>());
	}

	/**
	 * A failed save with a newer save right behind it gives way: the newer one starts now and inherits the callbacks.
	 * @return True when Op was superseded (it is off the queue)
	 */
	bool SupersedeFront(const TSharedRef<FOperation>& Op)
	{
		TArray<TSharedRef<FOperation>>* Ops = Queues.Find(Op->Path);
		if (!Ops || Ops->Num() < 2 || (*Ops)[0] != Op || !Op->IsSave() || !(*Ops)[1]->IsSave())
		{
			return false;
		}

		TSharedRef<FOperation> Next = (*Ops)[1];
		Next->DoneCallbacks.Insert(Op->DoneCallbacks, 0);
		Ops->RemoveAt(0);
		Send(Next);
		return true;
	}

	/** Take the running operation off its queue, start the next one, then report. */
	void Finish(const TSharedRef<FOperation>& Op, bool bSuccess, const TArray<FItemInstance>& Items)
	{
		if (TArray<TSharedRef<FOperation>>* Ops = Queues.Find(Op->Path))
		{
			Ops->Remove(Op);
			if (Ops->Num() > 0)
			{
				Send((*Ops)[0]);
			}
			else
			{
				Queues.Remove(Op->Path);
			}
		}

		// Last: a callback may queue another operation.
		RunCallbacks(*Op, bSuccess, Items);
	}

	static void RunCallbacks(const FOperation& Op, bool bSuccess, const TArray<FItemInstance>& Items)
	{
		for (const FOnStorageComplete& Callback : Op.DoneCallbacks)
		{
			Callback.ExecuteIfBound(Op.OwnerId, bSuccess);
		}
		for (const FOnStorageLoadComplete& Callback : Op.LoadCallbacks)
		{
			Callback.ExecuteIfBound(Op.OwnerId, bSuccess, Items);
		}
	}
};

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

FRemoteItemStorageConfig FRemoteItemStorageConfig::FromSettings()
{
	const UItemSystemSettings* Settings = GetDefault<UItemSystemSettings>();
	FRemoteItemStorageConfig Config;
	Config.EndpointURL = Settings->RemoteEndpointURL;
	Config.AuthToken = Settings->RemoteAuthToken;
	Config.MaxRetries = Settings->RemoteMaxRetries;
	Config.TimeoutSeconds = Settings->RemoteTimeoutSeconds;
	Config.RetryDelaySeconds = Settings->RemoteRetryDelaySeconds;
	return Config;
}

URemoteItemStorage::URemoteItemStorage()
	: Queue(MakeShared<FRemoteItemStorageQueue>())
{
}

void URemoteItemStorage::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Queue->Config = FRemoteItemStorageConfig::FromSettings();
	}
}

void URemoteItemStorage::Configure(const FRemoteItemStorageConfig& InConfig)
{
	Queue->Config = InConfig;
}

FRemoteItemStorageConfig URemoteItemStorage::GetConfig() const
{
	return Queue->Config;
}

int32 URemoteItemStorage::GetPendingRequestCount() const
{
	return Queue->NumPending();
}

// ---------------------------------------------------------------------------
// ICGFItemStorageInterface — Inventories
// ---------------------------------------------------------------------------

void URemoteItemStorage::SaveInventoryAsync_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items,
	const FOnStorageComplete& Callback)
{
	FRemoteItemStorageQueue::FOperation Op;
	Op.Verb = TEXT("POST");
	Op.Path = OwnerId;
	Op.OwnerId = OwnerId;
	Op.Body = FItemSerializationUtils::SerializeInventory(OwnerId, Items);
	Op.DoneCallbacks.Add(Callback);
	Queue->Enqueue(MoveTemp(Op));
}

void URemoteItemStorage::LoadInventoryAsync_Implementation(const FString& OwnerId, const FOnStorageLoadComplete& Callback)
{
	FRemoteItemStorageQueue::FOperation Op;
	Op.Verb = TEXT("GET");
	Op.Path = OwnerId;
	Op.OwnerId = OwnerId;
	Op.LoadCallbacks.Add(Callback);
	Queue->Enqueue(MoveTemp(Op));
}

void URemoteItemStorage::DeleteInventoryAsync_Implementation(const FString& OwnerId, const FOnStorageComplete& Callback)
{
	FRemoteItemStorageQueue::FOperation Op;
	Op.Verb = TEXT("DELETE");
	Op.Path = OwnerId;
	Op.OwnerId = OwnerId;
	Op.DoneCallbacks.Add(Callback);
	Queue->Enqueue(MoveTemp(Op));
}

bool URemoteItemStorage::SaveInventory_Implementation(const FString& OwnerId, const TArray<FItemInstance>& Items)
{
	SaveInventoryAsync_Implementation(OwnerId, Items, FOnStorageComplete());
	return true;
}

TArray<FItemInstance> URemoteItemStorage::LoadInventory_Implementation(const FString& OwnerId)
{
	UE_LOG(LogIIP, Warning, TEXT("RemoteItemStorage: synchronous LoadInventory('%s') is not supported (it would wait on the network); use LoadInventoryAsync."),
		*OwnerId);
	return TArray<FItemInstance>();
}

bool URemoteItemStorage::DeleteInventory_Implementation(const FString& OwnerId)
{
	DeleteInventoryAsync_Implementation(OwnerId, FOnStorageComplete());
	return true;
}

// ---------------------------------------------------------------------------
// ICGFItemStorageInterface — Blobs (feature 9)
// ---------------------------------------------------------------------------

bool URemoteItemStorage::SaveBlob_Implementation(const FString& Key, const FString& Json)
{
	FRemoteItemStorageQueue::FOperation Op;
	Op.Verb = TEXT("POST");
	Op.Path = TEXT("blobs/") + Key;
	Op.OwnerId = Key;
	Op.Body = Json;
	Queue->Enqueue(MoveTemp(Op));
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
	FRemoteItemStorageQueue::FOperation Op;
	Op.Verb = TEXT("DELETE");
	Op.Path = TEXT("blobs/") + Key;
	Op.OwnerId = Key;
	Queue->Enqueue(MoveTemp(Op));
	return true;
}

bool URemoteItemStorage::HasBlob_Implementation(const FString& Key)
{
	return false;
}
