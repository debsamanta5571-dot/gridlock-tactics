#include "TacticsWebSocketSubsystem.h"
#include "IWebSocket.h"

#include "TacticsDeckLibrarySubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsMatchSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
constexpr int32 kWireProtocolVersionLobby = 4;

int32 TeamForSeat(const bool bTeam2v2, const int32 Seat)
{
	if (!bTeam2v2) {
		return Seat <= 1 ? 1 : 2;
	}
	return (Seat == 1 || Seat == 3) ? 1 : 2;
}
}  // namespace

int32 UTacticsWebSocketSubsystem::TeamIdForLobbySeat(const bool bTeam2v2, const int32 SeatId)
{
	return TeamForSeat(bTeam2v2, SeatId);
}

void UTacticsWebSocketSubsystem::InitLobbySeats(const bool bTeam2v2)
{
	LobbySeats.Reset();
	const int32 Count = bTeam2v2 ? 4 : 2;
	for (int32 S = 1; S <= Count; ++S) {
		FTacticsLobbySeatState Row;
		Row.SeatId = S;
		Row.TeamId = TeamForSeat(bTeam2v2, S);
		LobbySeats.Add(Row);
	}
}

int32 UTacticsWebSocketSubsystem::FindLobbySeatIndex(const int32 SeatId) const
{
	for (int32 i = 0; i < LobbySeats.Num(); ++i) {
		if (LobbySeats[i].SeatId == SeatId) {
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UTacticsWebSocketSubsystem::AllocateLobbySeat(const int32 RequestedSeat, const bool bAllowReplaceReadyPeer)
{
	// Avoid Windows macro collision on identifier Cap.
	const int32 SeatCap = LobbySeats.Num();
	if (SeatCap < 2) {
		return 0;
	}
	auto IsTaken = [&](const int32 Seat) -> bool {
		const int32 Idx = FindLobbySeatIndex(Seat);
		return Idx == INDEX_NONE || LobbySeats[Idx].bOccupied;
	};

	// Never steal an occupied lobby seat (reconnect is a separate future feature).
	(void)bAllowReplaceReadyPeer;
	if (RequestedSeat >= 2 && RequestedSeat <= SeatCap && !IsTaken(RequestedSeat)) {
		return RequestedSeat;
	}
	for (int32 S = 2; S <= SeatCap; ++S) {
		if (!IsTaken(S)) {
			return S;
		}
	}
	return 0;
}

void UTacticsWebSocketSubsystem::ClearLobbySeatByPeerSeat(const int32 SeatId)
{
	const int32 Idx = FindLobbySeatIndex(SeatId);
	if (Idx == INDEX_NONE) {
		return;
	}
	FTacticsLobbySeatState& Row = LobbySeats[Idx];
	if (Row.bIsHost) {
		return;
	}
	Row.bOccupied = false;
	Row.bReady = false;
	Row.DisplayName.Empty();
	Row.DeckName.Empty();
	Row.DeckJson.Empty();
}

FString UTacticsWebSocketSubsystem::BuildLobbyStateJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("t"), TEXT("lobby_state"));
	Root->SetNumberField(TEXT("v"), kWireProtocolVersionLobby);
	Root->SetBoolField(TEXT("can_start"), CanHostStartMatch());

	TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
	Settings->SetBoolField(TEXT("team2v2"), LobbySettings.bTeam2v2);
	Settings->SetBoolField(TEXT("obj_scanner"), LobbySettings.bObjScanner);
	Settings->SetBoolField(TEXT("obj_omni"), LobbySettings.bObjOmni);
	Settings->SetBoolField(TEXT("obj_aether"), LobbySettings.bObjAether);
	Settings->SetBoolField(TEXT("field_req"), LobbySettings.bGiveFieldRequisition);
	Settings->SetNumberField(TEXT("port"), LobbySettings.HostPort);
	Root->SetObjectField(TEXT("settings"), Settings);

	TArray<TSharedPtr<FJsonValue>> SeatsArr;
	for (const FTacticsLobbySeatState& S : LobbySeats) {
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("seat"), S.SeatId);
		Row->SetNumberField(TEXT("team"), S.TeamId);
		Row->SetBoolField(TEXT("occupied"), S.bOccupied);
		Row->SetBoolField(TEXT("ready"), S.bReady);
		Row->SetBoolField(TEXT("is_host"), S.bIsHost);
		Row->SetStringField(TEXT("name"), S.DisplayName);
		Row->SetStringField(TEXT("deck_name"), S.DeckName);
		// Never broadcast full deck JSON in lobby_state (size + spoiler); decks travel on match_begin.
		SeatsArr.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("seats"), SeatsArr);

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

void UTacticsWebSocketSubsystem::BroadcastLobbyState()
{
	if (!IsHosting() || !IsInLobby()) {
		OnLobbyChanged.Broadcast();
		return;
	}
	const FString Wire = BuildLobbyStateJson();
	for (const TSharedPtr<FTacticsHostWsPeer>& Peer : HostPeers) {
		if (Peer.IsValid() && Peer->Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
			Peer->SendJson(Wire);
		}
	}
	OnLobbyChanged.Broadcast();
}

void UTacticsWebSocketSubsystem::ApplyLobbyStateFromJson(const TSharedPtr<FJsonObject>& Root)
{
	if (!Root.IsValid()) {
		return;
	}
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj && SettingsObj->IsValid()) {
		(*SettingsObj)->TryGetBoolField(TEXT("team2v2"), LobbySettings.bTeam2v2);
		(*SettingsObj)->TryGetBoolField(TEXT("obj_scanner"), LobbySettings.bObjScanner);
		(*SettingsObj)->TryGetBoolField(TEXT("obj_omni"), LobbySettings.bObjOmni);
		(*SettingsObj)->TryGetBoolField(TEXT("obj_aether"), LobbySettings.bObjAether);
		(*SettingsObj)->TryGetBoolField(TEXT("field_req"), LobbySettings.bGiveFieldRequisition);
	}

	const TArray<TSharedPtr<FJsonValue>>* SeatsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("seats"), SeatsArr) || !SeatsArr) {
		return;
	}
	TArray<FTacticsLobbySeatState> Next;
	for (const TSharedPtr<FJsonValue>& V : *SeatsArr) {
		const TSharedPtr<FJsonObject> Row = V.IsValid() ? V->AsObject() : nullptr;
		if (!Row.IsValid()) {
			continue;
		}
		FTacticsLobbySeatState S;
		double SeatNum = 0., TeamNum = 0.;
		Row->TryGetNumberField(TEXT("seat"), SeatNum);
		Row->TryGetNumberField(TEXT("team"), TeamNum);
		S.SeatId = static_cast<int32>(SeatNum);
		S.TeamId = static_cast<int32>(TeamNum);
		Row->TryGetBoolField(TEXT("occupied"), S.bOccupied);
		Row->TryGetBoolField(TEXT("ready"), S.bReady);
		Row->TryGetBoolField(TEXT("is_host"), S.bIsHost);
		Row->TryGetStringField(TEXT("name"), S.DisplayName);
		Row->TryGetStringField(TEXT("deck_name"), S.DeckName);
		// Preserve our local deck JSON if this is our seat.
		const int32 OldIdx = FindLobbySeatIndex(S.SeatId);
		if (OldIdx != INDEX_NONE && S.SeatId == ClientRemoteSeatPlayerId) {
			S.DeckJson = LobbySeats[OldIdx].DeckJson;
		}
		Next.Add(S);
	}
	if (Next.Num() > 0) {
		LobbySeats = MoveTemp(Next);
	}
	OnLobbyChanged.Broadcast();
}

bool UTacticsWebSocketSubsystem::CanHostStartMatch() const
{
	if (!IsHosting() || !IsInLobby()) {
		return false;
	}
	bool bHostHasDeck = false;
	int32 Occupied = 0;
	for (const FTacticsLobbySeatState& S : LobbySeats) {
		if (!S.bOccupied) {
			continue;
		}
		++Occupied;
		if (S.DeckJson.IsEmpty()) {
			return false;
		}
		if (S.bIsHost) {
			bHostHasDeck = true;
		}
	}
	const int32 Required = LobbySettings.bTeam2v2 ? 4 : 2;
	return bHostHasDeck && Occupied >= Required;
}

bool UTacticsWebSocketSubsystem::BeginHostLobby(const FTacticsLobbyMatchSettings& Settings,
	const FString& HostDeckName, const FString& HostDeckJson)
{
	LeaveLobby();
	LobbySettings = Settings;
	SetRoomToken(Settings.RoomToken);
	const int32 Port = Settings.HostPort > 0 ? Settings.HostPort : 8788;
	StartHost(Port, Settings.bBindPublic, /*bResetMatch=*/false);
	if (!IsHosting()) {
		LobbyStatusMessage = TEXT("Failed to start host listen socket.");
		SessionPhase = ETacticsNetSessionPhase::Offline;
		return false;
	}
	SessionPhase = ETacticsNetSessionPhase::Lobby;
	InitLobbySeats(Settings.bTeam2v2);
	FTacticsLobbySeatState& HostSeat = LobbySeats[0];
	HostSeat.bOccupied = true;
	HostSeat.bIsHost = true;
	HostSeat.bReady = false;
	HostSeat.DisplayName = TEXT("Host");
	HostSeat.DeckName = HostDeckName;
	HostSeat.DeckJson = HostDeckJson;
	LobbyStatusMessage = FString::Printf(TEXT("Lobby open on %s:%d - waiting for players."),
		Settings.bBindPublic ? TEXT("LAN") : TEXT("localhost"), Port);
	BroadcastLobbyState();
	return true;
}

void UTacticsWebSocketSubsystem::BeginJoinLobby(const FString& Url, const FString& PreferredDeckName,
	const FString& PreferredDeckJson)
{
	LeaveLobby();
	PendingJoinDeckName = PreferredDeckName;
	PendingJoinDeckJson = PreferredDeckJson;
	SessionPhase = ETacticsNetSessionPhase::Lobby;
	LobbyStatusMessage = TEXT("Connecting to lobby…");
	ConnectClient(Url);
	OnLobbyChanged.Broadcast();
}

void UTacticsWebSocketSubsystem::LeaveLobby()
{
	const bool bWasLobby = IsInLobby() || IsInNetworkMatch();
	SessionPhase = ETacticsNetSessionPhase::Offline;
	LobbySeats.Reset();
	LobbyStatusMessage.Empty();
	PendingJoinDeckName.Empty();
	PendingJoinDeckJson.Empty();
	bPendingEnterMatchFromLobby = false;
	if (bWasLobby || IsHosting() || IsClientConnectedToHost() || IsClientConnecting()) {
		Disconnect();
		StopHost();
	}
	OnLobbyChanged.Broadcast();
}

void UTacticsWebSocketSubsystem::LobbySelectSeat(const int32 SeatId)
{
	if (IsInLobby() && IsHosting()) {
		const int32 CurIdx = FindLobbySeatIndex(1);  // host fixed to seat 1 for authority
		(void)CurIdx;
		LobbyStatusMessage = TEXT("Host always plays seat P1 (Team A).");
		OnLobbyChanged.Broadcast();
		return;
	}
	if (IsInLobby() && Role == ETacticsWsRole::ClientRemote) {
		ClientRemoteSeatPlayerId = SeatId;
		ClientSendLobbyClaim();
	}
}

void UTacticsWebSocketSubsystem::LobbySetDeck(const FString& DeckName, const FString& DeckJson)
{
	if (!IsInLobby()) {
		return;
	}
	if (IsHosting()) {
		const int32 Idx = FindLobbySeatIndex(1);
		if (Idx != INDEX_NONE) {
			LobbySeats[Idx].DeckName = DeckName;
			LobbySeats[Idx].DeckJson = DeckJson;
			LobbySeats[Idx].bReady = false;
		}
		BroadcastLobbyState();
		return;
	}
	PendingJoinDeckName = DeckName;
	PendingJoinDeckJson = DeckJson;
	const int32 Idx = FindLobbySeatIndex(ClientRemoteSeatPlayerId);
	if (Idx != INDEX_NONE) {
		LobbySeats[Idx].DeckName = DeckName;
		LobbySeats[Idx].DeckJson = DeckJson;
		LobbySeats[Idx].bReady = false;
	}
	ClientSendLobbyClaim();
}

void UTacticsWebSocketSubsystem::LobbySetReady(const bool bReady)
{
	if (!IsInLobby()) {
		return;
	}
	if (IsHosting()) {
		const int32 Idx = FindLobbySeatIndex(1);
		if (Idx != INDEX_NONE) {
			if (bReady && LobbySeats[Idx].DeckJson.IsEmpty()) {
				LobbyStatusMessage = TEXT("Choose a deck before ready.");
				OnLobbyChanged.Broadcast();
				return;
			}
			LobbySeats[Idx].bReady = bReady;
		}
		BroadcastLobbyState();
		return;
	}
	const int32 Idx = FindLobbySeatIndex(ClientRemoteSeatPlayerId);
	if (Idx != INDEX_NONE) {
		if (bReady && LobbySeats[Idx].DeckJson.IsEmpty() && PendingJoinDeckJson.IsEmpty()) {
			LobbyStatusMessage = TEXT("Choose a deck before ready.");
			OnLobbyChanged.Broadcast();
			return;
		}
		LobbySeats[Idx].bReady = bReady;
	}
	ClientSendLobbyClaim();
}

void UTacticsWebSocketSubsystem::ClientSendLobbyClaim()
{
	if (Role != ETacticsWsRole::ClientRemote || !ClientWebSocket.IsValid() || !IsInLobby()) {
		return;
	}
	const int32 Seat = ClientRemoteSeatPlayerId;
	const int32 Idx = FindLobbySeatIndex(Seat);
	FString DeckName = PendingJoinDeckName;
	FString DeckJson = PendingJoinDeckJson;
	bool bReady = false;
	if (Idx != INDEX_NONE) {
		if (!LobbySeats[Idx].DeckJson.IsEmpty()) {
			DeckJson = LobbySeats[Idx].DeckJson;
			DeckName = LobbySeats[Idx].DeckName;
		}
		bReady = LobbySeats[Idx].bReady;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("t"), TEXT("lobby_claim"));
	Root->SetNumberField(TEXT("v"), kWireProtocolVersionLobby);
	Root->SetNumberField(TEXT("seat"), Seat);
	Root->SetBoolField(TEXT("ready"), bReady);
	Root->SetStringField(TEXT("deck_name"), DeckName);
	Root->SetStringField(TEXT("deck"), DeckJson);
	Root->SetStringField(TEXT("name"), FString::Printf(TEXT("P%d"), Seat));
	if (!RoomToken.IsEmpty()) {
		UTacticsMatchSubsystem* Match = nullptr;
		if (UGameInstance* GI = GetGameInstance()) {
			Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
		}
		const uint64 Ctr = ++ClientCliCtr;
		Root->SetNumberField(TEXT("ctr"), static_cast<double>(Ctr));
		if (Match) {
			const FString Sig = Match->ComputeCliSig(Seat, DeckJson.IsEmpty() ? TEXT("lobby") : DeckJson, RoomToken,
				ClientAuthNonce, Ctr);
			if (!Sig.IsEmpty()) {
				Root->SetStringField(TEXT("sig"), Sig);
			}
		}
	}
	FString Wire;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Wire);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	ClientWebSocket->Send(Wire);
}

void UTacticsWebSocketSubsystem::HandleLobbyClaimFromPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer,
	const TSharedPtr<FJsonObject>& Root)
{
	if (!IsHosting() || !IsInLobby() || !Peer.IsValid() || !Root.IsValid()) {
		return;
	}
	double SeatNum = Peer->SeatId;
	Root->TryGetNumberField(TEXT("seat"), SeatNum);
	int32 WantSeat = static_cast<int32>(SeatNum);
	if (WantSeat < 2 || WantSeat > LobbySeats.Num()) {
		WantSeat = Peer->SeatId;
	}

	FString DeckJson, DeckName, Name;
	Root->TryGetStringField(TEXT("deck"), DeckJson);
	Root->TryGetStringField(TEXT("deck_name"), DeckName);
	Root->TryGetStringField(TEXT("name"), Name);
	bool bReady = false;
	Root->TryGetBoolField(TEXT("ready"), bReady);

	if (!RoomToken.IsEmpty()) {
		FString Reject;
		const FString AuthPayload = DeckJson.IsEmpty() ? TEXT("lobby") : DeckJson;
		// Sign/verify against the seat claimed in this frame (WantSeat), not the pre-switch peer seat.
		if (!VerifyPeerFrameAuth(Peer, Root, WantSeat, TEXT("deck"), AuthPayload, Reject)) {
			HostSendCliAckToPeer(Peer, Reject);
			return;
		}
	}

	// Seat switch: only into empty seats (or stay on current).
	if (WantSeat != Peer->SeatId) {
		const int32 WantIdx = FindLobbySeatIndex(WantSeat);
		if (WantIdx == INDEX_NONE || LobbySeats[WantIdx].bOccupied) {
			HostSendCliAckToPeer(Peer, TEXT("Seat unavailable"));
			WantSeat = Peer->SeatId;
		} else {
			ClearLobbySeatByPeerSeat(Peer->SeatId);
			Peer->SeatId = WantSeat;
		}
	}

	const int32 Idx = FindLobbySeatIndex(Peer->SeatId);
	if (Idx == INDEX_NONE) {
		return;
	}
	FTacticsLobbySeatState& Row = LobbySeats[Idx];
	Row.bOccupied = true;
	Row.bIsHost = false;
	Row.DisplayName = Name.IsEmpty() ? FString::Printf(TEXT("P%d"), Peer->SeatId) : Name;
	if (!DeckJson.IsEmpty()) {
		FString DeckErr;
		UTacticsDeckLibrarySubsystem* Lib =
			GetGameInstance() ? GetGameInstance()->GetSubsystem<UTacticsDeckLibrarySubsystem>() : nullptr;
		if (!Lib || !Lib->ValidateDeckJson(DeckJson, DeckErr)) {
			HostSendCliAckToPeer(Peer,
				FString::Printf(TEXT("Illegal deck: %s"), DeckErr.IsEmpty() ? TEXT("unknown") : *DeckErr));
			bReady = false;
		} else {
			Row.DeckJson = DeckJson;
			Row.DeckName = DeckName.IsEmpty() ? TEXT("(custom)") : DeckName;
		}
	}
	if (bReady && Row.DeckJson.IsEmpty()) {
		bReady = false;
		HostSendCliAckToPeer(Peer, TEXT("Choose a deck before ready"));
	}
	Row.bReady = bReady;
	BroadcastLobbyState();
}

bool UTacticsWebSocketSubsystem::HostStartMatchFromLobby(FString& OutError)
{
	if (!CanHostStartMatch()) {
		OutError = TEXT("Cannot start - need a full lobby and a deck in every occupied seat.");
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(GI);
	UTacticsMatchSubsystem* Match = GI ? GI->GetSubsystem<UTacticsMatchSubsystem>() : nullptr;
	UTacticsDeckLibrarySubsystem* Lib = GI ? GI->GetSubsystem<UTacticsDeckLibrarySubsystem>() : nullptr;
	if (!TGI || !Match) {
		OutError = TEXT("Match subsystems unavailable.");
		return false;
	}

	// Validate every occupied seat's deck before wiping/building GameState.
	for (const FTacticsLobbySeatState& S : LobbySeats) {
		if (!S.bOccupied || S.DeckJson.IsEmpty()) {
			continue;
		}
		FString DeckErr;
		if (!Lib || !Lib->ValidateDeckJson(S.DeckJson, DeckErr)) {
			OutError = FString::Printf(TEXT("Seat P%d deck illegal: %s"), S.SeatId, *DeckErr);
			return false;
		}
	}

	// Seed active deck from host seat (library key) so ResetMatchWithProfile has a legal default;
	// lobby JSON is then applied per-seat via MatchSubsystem (linked in TacticsCore).
	const int32 HostIdx = FindLobbySeatIndex(1);
	if (Lib) {
		FString Err;
		if (HostIdx != INDEX_NONE && !LobbySeats[HostIdx].DeckName.IsEmpty()) {
			Lib->SetActiveDeckKey(LobbySeats[HostIdx].DeckName);
		}
		if (!Lib->ApplyActiveDeckForMatch(Err)) {
			OutError = Err.IsEmpty() ? TEXT("Host deck failed to apply.") : Err;
			return false;
		}
	}

	FTacticsMatchSetupProfile Profile;
	Profile.bSeedDemoState = false;
	Profile.bAutoFollowActiveSeat = false;
	Profile.bTeam2v2 = LobbySettings.bTeam2v2;
	Profile.bObjScanner = LobbySettings.bObjScanner;
	Profile.bObjOmni = LobbySettings.bObjOmni;
	Profile.bObjAether = LobbySettings.bObjAether;
	Profile.bGiveFieldRequisition = LobbySettings.bGiveFieldRequisition;
	const int32 Seats = LobbySettings.bTeam2v2 ? 4 : 2;
	Match->ResetMatchWithProfile(Seats, Profile);
	Match->SetControlledPlayer(1);

	HostSeatsWithClientDeckLocked.Empty();
	for (const FTacticsLobbySeatState& S : LobbySeats) {
		if (!S.bOccupied || S.DeckJson.IsEmpty()) {
			continue;
		}
		if (!Match->ApplyClientDeckToSeat(S.SeatId, S.DeckJson)) {
			OutError = FString::Printf(TEXT("Failed to apply deck for seat P%d."), S.SeatId);
			return false;
		}
		if (!S.bIsHost) {
			HostSeatsWithClientDeckLocked.Add(S.SeatId);
		}
	}

	// Build match_begin with per-seat decks for clients that need local apply before snap.
	TSharedPtr<FJsonObject> Begin = MakeShared<FJsonObject>();
	Begin->SetStringField(TEXT("t"), TEXT("match_begin"));
	Begin->SetNumberField(TEXT("v"), kWireProtocolVersionLobby);
	TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
	Settings->SetBoolField(TEXT("team2v2"), LobbySettings.bTeam2v2);
	Settings->SetBoolField(TEXT("obj_scanner"), LobbySettings.bObjScanner);
	Settings->SetBoolField(TEXT("obj_omni"), LobbySettings.bObjOmni);
	Settings->SetBoolField(TEXT("obj_aether"), LobbySettings.bObjAether);
	Settings->SetBoolField(TEXT("field_req"), LobbySettings.bGiveFieldRequisition);
	Begin->SetObjectField(TEXT("settings"), Settings);
	TArray<TSharedPtr<FJsonValue>> SeatsArr;
	for (const FTacticsLobbySeatState& S : LobbySeats) {
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("seat"), S.SeatId);
		Row->SetNumberField(TEXT("team"), S.TeamId);
		Row->SetBoolField(TEXT("occupied"), S.bOccupied);
		Row->SetStringField(TEXT("deck_name"), S.DeckName);
		if (S.bOccupied && !S.DeckJson.IsEmpty()) {
			Row->SetStringField(TEXT("deck"), S.DeckJson);
		}
		SeatsArr.Add(MakeShared<FJsonValueObject>(Row));
	}
	Begin->SetArrayField(TEXT("seats"), SeatsArr);
	FString BeginWire;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&BeginWire);
	FJsonSerializer::Serialize(Begin.ToSharedRef(), Writer);

	SessionPhase = ETacticsNetSessionPhase::Match;
	for (const TSharedPtr<FTacticsHostWsPeer>& Peer : HostPeers) {
		if (Peer.IsValid() && Peer->Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
			Peer->SendJson(BeginWire);
		}
	}
	LastHostSnapshotSeq.reset();
	HostSnapshotCache.Reset();
	HostBroadcastSnapshotToAllPeers(true);
	LobbyStatusMessage = TEXT("Match starting…");
	OnLobbyChanged.Broadcast();
	return true;
}

void UTacticsWebSocketSubsystem::HandleMatchBeginFromHost(const TSharedPtr<FJsonObject>& Root)
{
	if (!Root.IsValid() || Role != ETacticsWsRole::ClientRemote) {
		return;
	}
	ApplyLobbyStateFromJson(Root);
	const TArray<TSharedPtr<FJsonValue>>* SeatsArr = nullptr;
	if (Root->TryGetArrayField(TEXT("seats"), SeatsArr) && SeatsArr) {
		for (const TSharedPtr<FJsonValue>& V : *SeatsArr) {
			const TSharedPtr<FJsonObject> Row = V.IsValid() ? V->AsObject() : nullptr;
			if (!Row.IsValid()) {
				continue;
			}
			double SeatNum = 0.;
			Row->TryGetNumberField(TEXT("seat"), SeatNum);
			const int32 Seat = static_cast<int32>(SeatNum);
			FString DeckJson;
			if (Seat == ClientRemoteSeatPlayerId && Row->TryGetStringField(TEXT("deck"), DeckJson)) {
				PendingJoinDeckJson = DeckJson;
			}
		}
	}
	SessionPhase = ETacticsNetSessionPhase::Match;
	bPendingEnterMatchFromLobby = true;
	LobbyStatusMessage = TEXT("Match starting…");
	OnLobbyChanged.Broadcast();
	if (UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(GetGameInstance())) {
		TGI->EnterMatchFromLobbyAsClient();
	}
}

void UTacticsWebSocketSubsystem::RequestMatchResyncAfterLobbyEnter()
{
	bPendingEnterMatchFromLobby = false;
	if (Role != ETacticsWsRole::ClientRemote || !IsClientConnectedToHost()) {
		return;
	}
	SendClientResyncRequest();
}
