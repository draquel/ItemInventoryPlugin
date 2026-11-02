// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/StorageTestListener.h"
#include "Components/InventoryComponent.h"
#include "Data/ItemDefinition.h"
#include "Storage/ItemSerializationUtils.h"
#include "Storage/ItemStorageSubsystem.h"
#include "Storage/LocalItemStorage.h"
#include "Storage/RemoteItemStorage.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Testing/CGFTestGameInstance.h"

#if WITH_AUTOMATION_TESTS

#include "Containers/Ticker.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

// ---------------------------------------------------------------------------
// G93: storage never waits on the calling thread. The remote backend returns at once and reports later, retries on
// timers, and of overlapping saves of one inventory lands the last one requested. The tests talk to a minimal HTTP
// server in this process (a socket on its own port, polled on the core ticker; the engine's HTTPServer module was not
// used because its listeners start together, which would start the editor's Claudius router too) or to a closed
// localhost port, and wait with latent commands, so the engine ticks (the HTTP module, the core ticker) between steps.
// ---------------------------------------------------------------------------
namespace RemoteStorageTestHelpers
{
	/** The in-process server's port. */
	constexpr uint32 ServerPort = 18093;

	/** Nothing listens on port 1 on a development machine: every request fails to connect. */
	const TCHAR* UnreachableEndpoint = TEXT("http://127.0.0.1:1/g93");

	/** The longest a latent wait lasts before the test gives up. */
	constexpr double WaitLimitSeconds = 30.0;

	/** An item that serializes (a definition id and an instance id); the stack count tells payloads apart. */
	FItemInstance MakeItem(int32 Stack)
	{
		FItemInstance Item;
		Item.InstanceId = FGuid(0x93, 0, 0, static_cast<uint32>(Stack));
		Item.ItemDefinitionId = FPrimaryAssetId(TEXT("ItemDefinition"), FName(TEXT("G93_TestItem")));
		Item.StackCount = Stack;
		return Item;
	}

	/** A remote backend on Endpoint, rooted (release with RemoveFromRoot). */
	URemoteItemStorage* MakeRemote(const FString& Endpoint, int32 MaxRetries, float RetryDelaySeconds, UObject* Outer = nullptr)
	{
		URemoteItemStorage* Storage = NewObject<URemoteItemStorage>(Outer ? Outer : GetTransientPackage());
		Storage->AddToRoot();
		FRemoteItemStorageConfig Config;
		Config.EndpointURL = Endpoint;
		Config.MaxRetries = MaxRetries;
		Config.TimeoutSeconds = 5.0f;
		Config.RetryDelaySeconds = RetryDelaySeconds;
		Storage->Configure(Config);
		return Storage;
	}

	UStorageTestListener* MakeListener()
	{
		UStorageTestListener* Listener = NewObject<UStorageTestListener>();
		Listener->AddToRoot();
		return Listener;
	}

	/**
	 * A minimal HTTP/1.1 server for one inventory resource, /g93/<OwnerId> on ServerPort: records every POST body in
	 * arrival order, answers the next POSTs with FailCodes (in order) before storing, and answers GET with what is stored
	 * (404 when nothing is). One request per connection (it answers "Connection: close"). Stop it before release.
	 */
	struct FTestInventoryServer
	{
		TArray<FString> Posts;
		TArray<int32> FailCodes;
		FString Stored;

		~FTestInventoryServer()
		{
			Stop();
		}

		/** @return False when the port could not be bound */
		bool Start(const FString& OwnerId)
		{
			ResourcePath = TEXT("/g93/") + OwnerId;
			ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
			bool bValidIp = false;
			Address->SetIp(TEXT("127.0.0.1"), bValidIp);
			Address->SetPort(ServerPort);

			ListenSocket = Sockets->CreateSocket(NAME_Stream, TEXT("G93TestServer"), false);
			if (!ListenSocket || !ListenSocket->SetReuseAddr() || !ListenSocket->Bind(*Address) || !ListenSocket->Listen(8)
				|| !ListenSocket->SetNonBlocking())
			{
				Stop();
				return false;
			}
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTestInventoryServer::Tick));
			return true;
		}

		void Stop()
		{
			if (TickHandle.IsValid())
			{
				FTSTicker::RemoveTicker(TickHandle);
				TickHandle.Reset();
			}
			ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			for (FConnection& Connection : Connections)
			{
				Connection.Socket->Close();
				Sockets->DestroySocket(Connection.Socket);
			}
			Connections.Reset();
			if (ListenSocket)
			{
				ListenSocket->Close();
				Sockets->DestroySocket(ListenSocket);
				ListenSocket = nullptr;
			}
		}

	private:
		struct FConnection
		{
			FSocket* Socket = nullptr;
			TArray<uint8> Data;
			bool bSentContinue = false;
		};

		FString ResourcePath;
		FSocket* ListenSocket = nullptr;
		TArray<FConnection> Connections;
		FTSTicker::FDelegateHandle TickHandle;

		bool Tick(float)
		{
			bool bPending = false;
			while (ListenSocket->HasPendingConnection(bPending) && bPending)
			{
				FSocket* Socket = ListenSocket->Accept(TEXT("G93TestConnection"));
				if (!Socket)
				{
					break;
				}
				Socket->SetNonBlocking();
				Connections.Add({ Socket });
			}

			for (int32 i = Connections.Num() - 1; i >= 0; --i)
			{
				FConnection& Connection = Connections[i];
				uint8 Buffer[4096];
				int32 Read = 0;
				while (Connection.Socket->Recv(Buffer, sizeof(Buffer), Read) && Read > 0)
				{
					Connection.Data.Append(Buffer, Read);
				}
				if (TryAnswer(Connection))
				{
					Connection.Socket->Close();
					ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Connection.Socket);
					Connections.RemoveAt(i);
				}
			}
			return true;
		}

		/** @return True when a whole request arrived and was answered */
		bool TryAnswer(FConnection& Connection)
		{
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Connection.Data.GetData()), Connection.Data.Num());
			const FString Received(Text.Length(), Text.Get());
			const int32 HeaderEnd = Received.Find(TEXT("\r\n\r\n"));
			if (HeaderEnd == INDEX_NONE)
			{
				return false;
			}

			TArray<FString> Lines;
			Received.Left(HeaderEnd).ParseIntoArray(Lines, TEXT("\r\n"));
			TArray<FString> RequestLine;
			if (Lines.Num() == 0 || Lines[0].ParseIntoArrayWS(RequestLine) < 2)
			{
				Respond(Connection, 400, FString());
				return true;
			}
			int32 ContentLength = 0;
			bool bExpectContinue = false;
			for (const FString& Line : Lines)
			{
				if (Line.StartsWith(TEXT("Content-Length:"), ESearchCase::IgnoreCase))
				{
					ContentLength = FCString::Atoi(*Line.Mid(15).TrimStartAndEnd());
				}
				bExpectContinue |= Line.StartsWith(TEXT("Expect: 100-continue"), ESearchCase::IgnoreCase);
			}

			const int32 BodyStart = HeaderEnd + 4;
			if (Connection.Data.Num() < BodyStart + ContentLength)
			{
				if (bExpectContinue && !Connection.bSentContinue)
				{
					SendText(Connection, TEXT("HTTP/1.1 100 Continue\r\n\r\n"));
					Connection.bSentContinue = true;
				}
				return false;
			}

			const FString& Verb = RequestLine[0];
			if (RequestLine[1] != ResourcePath)
			{
				Respond(Connection, 404, FString());
			}
			else if (Verb == TEXT("GET"))
			{
				Stored.IsEmpty() ? Respond(Connection, 404, FString()) : Respond(Connection, 200, Stored);
			}
			else
			{
				Posts.Add(Received.Mid(BodyStart, ContentLength));
				if (FailCodes.Num() > 0)
				{
					const int32 Code = FailCodes[0];
					FailCodes.RemoveAt(0);
					Respond(Connection, Code, FString());
				}
				else
				{
					Stored = Posts.Last();
					Respond(Connection, 200, FString());
				}
			}
			return true;
		}

		static void Respond(FConnection& Connection, int32 Code, const FString& Body)
		{
			const FTCHARToUTF8 BodyUtf8(*Body);
			SendText(Connection, FString::Printf(
				TEXT("HTTP/1.1 %d Test\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"),
				Code, BodyUtf8.Length(), *Body));
		}

		static void SendText(FConnection& Connection, const FString& Text)
		{
			const FTCHARToUTF8 Utf8(*Text);
			Connection.Socket->SetNonBlocking(false);
			int32 Offset = 0;
			int32 Sent = 0;
			while (Offset < Utf8.Length()
				&& Connection.Socket->Send(reinterpret_cast<const uint8*>(Utf8.Get()) + Offset, Utf8.Length() - Offset, Sent) && Sent > 0)
			{
				Offset += Sent;
			}
			Connection.Socket->SetNonBlocking(true);
		}
	};

	/** A component with Slots pre-allocated slots (BeginPlay has not run: play has not begun). */
	UInventoryComponent* MakeInventory(AActor* Owner, int32 Slots)
	{
		UInventoryComponent* Comp = NewObject<UInventoryComponent>(Owner);
		Comp->MaxSlots = Slots;
		Comp->InventorySlots.Items.SetNum(Slots);
		for (int32 i = 0; i < Slots; ++i)
		{
			Comp->InventorySlots.Items[i].SlotIndex = i;
			Comp->InventorySlots.Items[i].bIsOccupied = false;
		}
		Comp->InventorySlots.OwningComponent = Comp;
		return Comp;
	}

	void PlaceItemInSlot(UInventoryComponent* Comp, const FItemInstance& Item, int32 Slot)
	{
		FInventorySlot& S = Comp->InventorySlots.Items[Slot];
		S.Item = Item;
		S.bIsOccupied = true;
	}
}

// ===========================================================================
// An unreachable server: the save returns at once, the failure arrives later, after the retry's back-off
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteStorage_UnreachableReturnsAtOnce,
	"ItemInventory.Storage.Remote.UnreachableReturnsAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRemoteStorage_UnreachableReturnsAtOnce::RunTest(const FString& Parameters)
{
	using namespace RemoteStorageTestHelpers;

	// One retry: two attempts, then one warning naming them.
	AddExpectedMessagePlain(TEXT("POST G93_Unreachable failed after 2 attempt(s)"), ELogVerbosity::Warning);

	URemoteItemStorage* Storage = MakeRemote(UnreachableEndpoint, /*MaxRetries*/ 1, /*RetryDelaySeconds*/ 0.25f);
	UStorageTestListener* Listener = MakeListener();

	const double Start = FPlatformTime::Seconds();
	ICGFItemStorageInterface::Execute_SaveInventoryAsync(Storage, TEXT("G93_Unreachable"), { MakeItem(1) },
		Listener->SaveCallback());
	const double CallSeconds = FPlatformTime::Seconds() - Start;

	TestTrue(FString::Printf(TEXT("The save returns at once (%.3f s)"), CallSeconds), CallSeconds < 0.25);
	TestEqual(TEXT("Nothing is reported before the request has run"), Listener->Saves.Num(), 0);
	TestEqual(TEXT("The save is queued"), Storage->GetPendingRequestCount(), 1);

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Storage, Listener, Start]()
	{
		if (Listener->Saves.Num() == 0 && FPlatformTime::Seconds() - Start < WaitLimitSeconds)
		{
			return false;
		}

		if (TestEqual(TEXT("One result"), Listener->Saves.Num(), 1))
		{
			const UStorageTestListener::FResult& Result = Listener->Saves[0];
			TestFalse(TEXT("The callback reports the failure"), Result.bSuccess);
			TestEqual(TEXT("For its inventory"), Result.OwnerId, FString(TEXT("G93_Unreachable")));
			TestTrue(TEXT("After the retry's back-off"), Result.Time - Start >= 0.25);
		}
		TestEqual(TEXT("Nothing left queued"), Storage->GetPendingRequestCount(), 0);

		Storage->RemoveFromRoot();
		Listener->RemoveFromRoot();
		return true;
	}));
	return true;
}

// ===========================================================================
// Retries: a server that answers 503 twice gets a third attempt, which lands
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteStorage_Retries,
	"ItemInventory.Storage.Remote.Retries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRemoteStorage_Retries::RunTest(const FString& Parameters)
{
	using namespace RemoteStorageTestHelpers;

	TSharedRef<FTestInventoryServer> Server = MakeShared<FTestInventoryServer>();
	if (!TestTrue(TEXT("The test server binds its port"), Server->Start(TEXT("G93_Retries"))))
	{
		return false;
	}
	Server->FailCodes = { 503, 503 };

	URemoteItemStorage* Storage = MakeRemote(FString::Printf(TEXT("http://127.0.0.1:%u/g93"), ServerPort),
		/*MaxRetries*/ 3, /*RetryDelaySeconds*/ 0.05f);
	UStorageTestListener* Listener = MakeListener();
	const TArray<FItemInstance> Items = { MakeItem(5) };
	const FString Expected = FItemSerializationUtils::SerializeInventory(TEXT("G93_Retries"), Items);

	const double Start = FPlatformTime::Seconds();
	ICGFItemStorageInterface::Execute_SaveInventoryAsync(Storage, TEXT("G93_Retries"), Items, Listener->SaveCallback());

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Server, Storage, Listener, Start, Expected]()
	{
		if (Listener->Saves.Num() == 0 && FPlatformTime::Seconds() - Start < WaitLimitSeconds)
		{
			return false;
		}

		if (TestEqual(TEXT("One result"), Listener->Saves.Num(), 1))
		{
			TestTrue(TEXT("The third attempt lands"), Listener->Saves[0].bSuccess);
		}
		TestEqual(TEXT("Three attempts reached the server"), Server->Posts.Num(), 3);
		TestEqual(TEXT("The stored document is the save"), Server->Stored, Expected);

		Server->Stop();
		Storage->RemoveFromRoot();
		Listener->RemoveFromRoot();
		return true;
	}));
	return true;
}

// ===========================================================================
// Overlapping saves: the last one requested lands; a load queued behind them reads it
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteStorage_LastSaveWins,
	"ItemInventory.Storage.Remote.LastSaveWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRemoteStorage_LastSaveWins::RunTest(const FString& Parameters)
{
	using namespace RemoteStorageTestHelpers;

	const FString OwnerId = TEXT("G93_LastSaveWins");
	TSharedRef<FTestInventoryServer> Server = MakeShared<FTestInventoryServer>();
	if (!TestTrue(TEXT("The test server binds its port"), Server->Start(OwnerId)))
	{
		return false;
	}

	URemoteItemStorage* Storage = MakeRemote(FString::Printf(TEXT("http://127.0.0.1:%u/g93"), ServerPort),
		/*MaxRetries*/ 3, /*RetryDelaySeconds*/ 0.05f);
	UStorageTestListener* Listener = MakeListener();

	// Three saves in one frame: A is sent, B waits behind it, C replaces B. Then a load, queued behind the saves.
	const TArray<FItemInstance> A = { MakeItem(1) };
	const TArray<FItemInstance> B = { MakeItem(2) };
	const TArray<FItemInstance> C = { MakeItem(3), MakeItem(4) };
	ICGFItemStorageInterface::Execute_SaveInventoryAsync(Storage, OwnerId, A, Listener->SaveCallback());
	ICGFItemStorageInterface::Execute_SaveInventoryAsync(Storage, OwnerId, B, Listener->SaveCallback());
	ICGFItemStorageInterface::Execute_SaveInventoryAsync(Storage, OwnerId, C, Listener->SaveCallback());
	ICGFItemStorageInterface::Execute_LoadInventoryAsync(Storage, OwnerId, Listener->LoadCallback());
	TestEqual(TEXT("Three operations queued (B merged into C)"), Storage->GetPendingRequestCount(), 3);

	const FString ExpectedA = FItemSerializationUtils::SerializeInventory(OwnerId, A);
	const FString ExpectedC = FItemSerializationUtils::SerializeInventory(OwnerId, C);
	const double Start = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Server, Listener, Start, ExpectedA, ExpectedC]()
	{
		if (Listener->Loads.Num() == 0 && FPlatformTime::Seconds() - Start < WaitLimitSeconds)
		{
			return false;
		}

		if (TestEqual(TEXT("The server saw A then C"), Server->Posts.Num(), 2))
		{
			TestEqual(TEXT("A first"), Server->Posts[0], ExpectedA);
			TestEqual(TEXT("C last; B never sent"), Server->Posts[1], ExpectedC);
		}
		TestEqual(TEXT("C is stored"), Server->Stored, ExpectedC);
		if (TestEqual(TEXT("Every save reported"), Listener->Saves.Num(), 3))
		{
			for (const UStorageTestListener::FResult& Result : Listener->Saves)
			{
				TestTrue(TEXT("Each save reports success (B by C's result)"), Result.bSuccess);
			}
		}
		if (TestEqual(TEXT("One load"), Listener->Loads.Num(), 1))
		{
			TestTrue(TEXT("The load succeeds"), Listener->Loads[0].bSuccess);
			TestEqual(TEXT("It reads C, the last save"), Listener->Loads[0].ItemCount, 2);
		}
		return true;
	}));

	// A save that fails while a newer one waits gives way to it: D gets a 503, E lands, and D reports E's result.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Server, Storage, Listener, OwnerId]()
	{
		Server->FailCodes = { 503 };
		Server->Posts.Reset();
		Listener->Saves.Reset();
		ICGFItemStorageInterface::Execute_SaveInventoryAsync(Storage, OwnerId, { MakeItem(6) }, Listener->SaveCallback());
		ICGFItemStorageInterface::Execute_SaveInventoryAsync(Storage, OwnerId, { MakeItem(7) }, Listener->SaveCallback());
		return true;
	}));

	const FString ExpectedD = FItemSerializationUtils::SerializeInventory(OwnerId, { MakeItem(6) });
	const FString ExpectedE = FItemSerializationUtils::SerializeInventory(OwnerId, { MakeItem(7) });
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Server, Storage, Listener, ExpectedD, ExpectedE,
		Deadline = FPlatformTime::Seconds() + WaitLimitSeconds + 30.0]()
	{
		if (Listener->Saves.Num() < 2 && FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}

		if (TestEqual(TEXT("The server saw D (refused) then E; D was not retried"), Server->Posts.Num(), 2))
		{
			TestEqual(TEXT("D first"), Server->Posts[0], ExpectedD);
			TestEqual(TEXT("E second"), Server->Posts[1], ExpectedE);
		}
		TestEqual(TEXT("E is stored"), Server->Stored, ExpectedE);
		if (TestEqual(TEXT("Both saves reported"), Listener->Saves.Num(), 2))
		{
			TestTrue(TEXT("D reports E's landing"), Listener->Saves[0].bSuccess);
			TestTrue(TEXT("E landed"), Listener->Saves[1].bSuccess);
		}

		Server->Stop();
		Storage->RemoveFromRoot();
		Listener->RemoveFromRoot();
		return true;
	}));
	return true;
}

// ===========================================================================
// The subsystem: SaveAll (the auto-save's path) returns at once against an unreachable server; the failure marks the
// inventory dirty again; a load queued behind the save fails and leaves the inventory as it was
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStorageSubsystem_SaveIsFireAndForget,
	"ItemInventory.Storage.Subsystem.SaveIsFireAndForget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FStorageSubsystem_SaveIsFireAndForget::RunTest(const FString& Parameters)
{
	using namespace RemoteStorageTestHelpers;

	const FString OwnerId = TEXT("G93_Subsystem");
	AddExpectedMessagePlain(TEXT("POST G93_Subsystem failed after 1 attempt(s)"), ELogVerbosity::Warning);
	AddExpectedMessagePlain(TEXT("GET G93_Subsystem failed after 1 attempt(s)"), ELogVerbosity::Warning);
	AddExpectedMessagePlain(TEXT("load of 'G93_Subsystem' failed; the inventory is left as it was"), ELogVerbosity::Warning);

	TSharedRef<CGFTesting::FTestGameInstance> Game = MakeShared<CGFTesting::FTestGameInstance>();
	UItemStorageSubsystem* Storage = Game->GetSubsystem<UItemStorageSubsystem>();
	if (!TestNotNull(TEXT("The storage subsystem"), Storage))
	{
		return false;
	}
	URemoteItemStorage* Remote = MakeRemote(UnreachableEndpoint, /*MaxRetries*/ 0, /*RetryDelaySeconds*/ 0.0f, Storage);
	TestTrue(TEXT("The remote backend is installed"), Storage->SetStorageBackend(Remote));
	AddExpectedMessagePlain(TEXT("SetStorageBackend refused"), ELogVerbosity::Warning);
	TestFalse(TEXT("An object that is not a backend is refused"), Storage->SetStorageBackend(Remote->GetClass()));

	UInventoryComponent* Comp = MakeInventory(Game->SpawnOwner(), 4);
	PlaceItemInSlot(Comp, MakeItem(9), 0);
	Comp->MarkDirty();
	Storage->RegisterInventory(Comp, OwnerId);

	UStorageTestListener* Listener = MakeListener();
	Storage->OnInventorySaved.AddDynamic(Listener, &UStorageTestListener::OnSaved);
	Storage->OnInventoryLoaded.AddDynamic(Listener, &UStorageTestListener::OnSubsystemLoaded);

	const double Start = FPlatformTime::Seconds();
	const int32 Started = Storage->SaveAll();
	const bool bLoadStarted = Storage->LoadInventory(Comp, OwnerId);
	const double CallSeconds = FPlatformTime::Seconds() - Start;

	TestEqual(TEXT("SaveAll starts the dirty inventory's save"), Started, 1);
	TestTrue(TEXT("The load starts"), bLoadStarted);
	TestTrue(FString::Printf(TEXT("Both return at once (%.3f s)"), CallSeconds), CallSeconds < 0.25);
	TestFalse(TEXT("Starting the save clears the dirty flag"), Comp->IsDirty());
	TestEqual(TEXT("Nothing reported yet"), Listener->Saves.Num() + Listener->Loads.Num(), 0);

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Game, Storage, Remote, Comp, Listener, Start, OwnerId]()
	{
		if (Listener->Loads.Num() == 0 && FPlatformTime::Seconds() - Start < WaitLimitSeconds)
		{
			return false;
		}

		if (TestEqual(TEXT("One save result"), Listener->Saves.Num(), 1))
		{
			TestFalse(TEXT("OnInventorySaved reports the failure"), Listener->Saves[0].bSuccess);
		}
		TestTrue(TEXT("The failed save marks the inventory dirty again"), Comp->IsDirty());
		if (TestEqual(TEXT("One load result, after the save"), Listener->Loads.Num(), 1))
		{
			TestFalse(TEXT("OnInventoryLoaded reports the failure"), Listener->Loads[0].bSuccess);
			TestTrue(TEXT("The load came after the save"), Listener->Loads[0].Time >= Listener->Saves[0].Time);
		}
		TestEqual(TEXT("The failed load left the item"), Comp->GetFilledSlotCount(), 1);

		// Nothing more to save at teardown (the game instance's shutdown would start another).
		Comp->ClearDirty();
		Storage->UnregisterInventory(Comp);
		Remote->RemoveFromRoot();
		Listener->RemoveFromRoot();
		return true;
	}));
	return true;
}

// ===========================================================================
// The local backend answers at once: the save is written and reported before SaveInventory returns, and a load fills
// a component before LoadInventory returns (the behaviour the auto-save always had)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStorageSubsystem_LocalRoundTrip,
	"ItemInventory.Storage.Subsystem.LocalRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FStorageSubsystem_LocalRoundTrip::RunTest(const FString& Parameters)
{
	using namespace RemoteStorageTestHelpers;

	const FString OwnerId = TEXT("G93_LocalRoundTrip");
	CGFTesting::FTestGameInstance Game;
	UItemStorageSubsystem* Storage = Game.GetSubsystem<UItemStorageSubsystem>();
	UItemDatabaseSubsystem* Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
	if (!TestNotNull(TEXT("The storage subsystem"), Storage) || !TestNotNull(TEXT("The item database"), Database))
	{
		return false;
	}

	ULocalItemStorage* Local = NewObject<ULocalItemStorage>(Storage);
	TestTrue(TEXT("The local backend is installed"), Storage->SetStorageBackend(Local));

	UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), TEXT("G93_Ore"));
	Definition->AddToRoot();
	Definition->MaxStackSize = 20;
	Database->RegisterDefinition(Definition->GetPrimaryAssetId(), Definition);

	UInventoryComponent* Source = MakeInventory(Game.SpawnOwner(), 4);
	PlaceItemInSlot(Source, Database->CreateItemInstance(Definition->GetPrimaryAssetId(), 7), 0);
	Source->MarkDirty();
	Storage->RegisterInventory(Source, OwnerId);

	UStorageTestListener* Listener = MakeListener();
	Storage->OnInventorySaved.AddDynamic(Listener, &UStorageTestListener::OnSaved);
	Storage->OnInventoryLoaded.AddDynamic(Listener, &UStorageTestListener::OnSubsystemLoaded);

	TestTrue(TEXT("The save starts"), Storage->SaveInventory(Source));
	if (TestEqual(TEXT("It is reported before SaveInventory returns"), Listener->Saves.Num(), 1))
	{
		TestTrue(TEXT("And it landed"), Listener->Saves[0].bSuccess);
	}
	TestFalse(TEXT("The inventory is clean"), Source->IsDirty());

	UInventoryComponent* Target = MakeInventory(Game.SpawnOwner(), 4);
	TestTrue(TEXT("The load starts"), Storage->LoadInventory(Target, OwnerId));
	if (TestEqual(TEXT("It is reported before LoadInventory returns"), Listener->Loads.Num(), 1))
	{
		TestTrue(TEXT("And it succeeded"), Listener->Loads[0].bSuccess);
	}
	TestEqual(TEXT("The loaded inventory holds the saved stack"), Target->GetItemCount(Definition->GetPrimaryAssetId()), 7);

	AddExpectedMessagePlain(TEXT("SaveInventory called for unregistered component"), ELogVerbosity::Warning);
	TestFalse(TEXT("An unregistered component is refused"), Storage->SaveInventory(Target));

	Storage->UnregisterInventory(Source);
	ICGFItemStorageInterface::Execute_DeleteInventory(Local, OwnerId);
	Definition->RemoveFromRoot();
	Listener->RemoveFromRoot();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
