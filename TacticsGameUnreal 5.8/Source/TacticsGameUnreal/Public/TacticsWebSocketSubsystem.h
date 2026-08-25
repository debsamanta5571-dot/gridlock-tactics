#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TacticsMatchSubsystem.h"
#include <optional>
#include <string>
#include "TacticsWebSocketSubsystem.generated.h"

class FSocket;
class IWebSocket;
class FJsonObject;

/** Host lobby settings (mirrors menu FTacticsMatchSettings without including GameInstance). */
struct FTacticsLobbyMatchSettings
{
	bool bTeam2v2{false};
	bool bObjScanner{true};
	bool bObjOmni{true};
	bool bObjAether{true};
	bool bGiveFieldRequisition{false};
	int32 HostPort{8788};
	bool bBindPublic{true};
	FString RoomToken;
};

enum class ETacticsHostWsPeerPhase : uint8
{
	HttpHandshake,
	WebSocketReady
};

enum class ETacticsNetSessionPhase : uint8
{
	Offline,
	Lobby,
	Match
};

/** One TCP connection upgraded to WebSocket (browser or UE IWebSocket client). */
struct FTacticsHostWsPeer final : public TSharedFromThis<FTacticsHostWsPeer>
{
	FSocket* TcpSocket = nullptr;
	ETacticsHostWsPeerPhase Phase = ETacticsHostWsPeerPhase::HttpHandshake;
	TArray<uint8> HandshakeAccum;
	TArray<uint8> RxScratch;
	TArray<uint8> Utf8FragmentAssembly;
	TArray<uint8> TxBuffer;
	/** 0 until seat assigned at handshake. */
	int32 SeatId = 0;
	bool bClientDeckApplied = false;
	FString AuthNonce;
	uint64 LastCliCtr = 0;

	~FTacticsHostWsPeer();
	void CloseSocket();
	bool QueueBytes(const uint8* Data, int32 Len);
	bool FlushTx();
	bool SendJson(const FString& JsonUtf16);
};

UENUM()
enum class ETacticsWsRole : uint8
{
	None UMETA(DisplayName = "Offline"),
	HostP1 UMETA(DisplayName = "Host (authority)"),
	ClientRemote UMETA(DisplayName = "Client (remote seat)")
};

/** One seat row in the pre-match lobby. */
struct FTacticsLobbySeatState
{
	int32 SeatId = 0;
	int32 TeamId = 0;
	FString DisplayName;
	FString DeckName;
	FString DeckJson;
	bool bOccupied = false;
	bool bReady = false;
	bool bIsHost = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FTacticsCliAckFromHost, const FString& /*Message*/);
DECLARE_MULTICAST_DELEGATE(FTacticsLobbyChanged);

/**
 * Host-authoritative sync: lobby → match_begin → cmd/snap journal.
 * Remote seats send cli / lobby_claim; host replies and broadcasts.
 */
UCLASS()
class TACTICSGAMEUNREAL_API UTacticsWebSocketSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Closes host listen and client sockets. */
	virtual void Deinitialize() override;

	FTacticsCliAckFromHost OnCliAckFromHost;
	FTacticsLobbyChanged OnLobbyChanged;

	/** Listens for LAN clients on Port. */
	void StartHost(int32 Port = 8788, bool bBindPublic = true, bool bResetMatch = true);
	/** Stops listening and drops connected peers. */
	void StopHost();

	void SetRoomToken(const FString& Token) { RoomToken = Token; }
	const FString& GetRoomToken() const { return RoomToken; }
	int32 GetListenPort() const { return ListenPort; }
	bool IsListeningPublic() const { return bListenPublic; }

	/** Connects to a host WebSocket URL (ws://host:port/). */
	void ConnectClient(const FString& Url);
	/** Closes the client WebSocket. */
	void Disconnect();

	ETacticsWsRole GetRole() const { return Role; }
	bool IsHosting() const { return Role == ETacticsWsRole::HostP1 && ListenSocket != nullptr; }
	bool IsClientConnectedToHost() const;

	int32 GetClientRemoteSeatPlayerId() const { return ClientRemoteSeatPlayerId; }
	int32 GetRemoteWebSocketReadyPeerCount() const;

	bool IsClientConnecting() const { return bClientConnecting; }
	const FString& GetClientConnectionError() const { return ClientConnectionError; }

	void AuthorityExecCliLine(const FString& Line, FString& OutMessage);
	void ClientSendCliLine(const FString& Line);
	void NotifyClientsDemoReset();

	// ── Lobby ──────────────────────────────────────────────────────────────
	ETacticsNetSessionPhase GetSessionPhase() const { return SessionPhase; }
	bool IsInLobby() const { return SessionPhase == ETacticsNetSessionPhase::Lobby; }
	bool IsInNetworkMatch() const { return SessionPhase == ETacticsNetSessionPhase::Match; }

	/** Host: open listen socket and enter lobby (no match yet). */
	bool BeginHostLobby(const FTacticsLobbyMatchSettings& Settings, const FString& HostDeckName,
		const FString& HostDeckJson);
	/** Client: connect for lobby (stay on menu until match_begin). */
	void BeginJoinLobby(const FString& Url, const FString& PreferredDeckName, const FString& PreferredDeckJson);
	void LeaveLobby();

	const TArray<FTacticsLobbySeatState>& GetLobbySeats() const { return LobbySeats; }
	const FTacticsLobbyMatchSettings& GetLobbySettings() const { return LobbySettings; }
	FString GetLobbyStatusMessage() const { return LobbyStatusMessage; }
	bool CanHostStartMatch() const;

	/** Host or local seat edits (host applies immediately; client sends lobby_claim). */
	void LobbySelectSeat(int32 SeatId);
	void LobbySetDeck(const FString& DeckName, const FString& DeckJson);
	void LobbySetReady(bool bReady);
	/** Host only: build match, broadcast match_begin + snap, return true on success. */
	bool HostStartMatchFromLobby(FString& OutError);

	/** Client: after lobby→match map travel, request a fresh snap if needed. */
	void RequestMatchResyncAfterLobbyEnter();

private:
	bool TickHostSockets(float DeltaTime);
	void PollSockets();
	void ResetHostPeerRxState();

	void ScheduleDispatchInboundJson(FString Json, bool bFromTcpClient, int32 TcpSeatHint, TWeakPtr<FTacticsHostWsPeer> ReplyPeerWeak);
	void DispatchInboundJsonOnGameThread(const FString& Json, bool bFromTcpClient, int32 TcpSeatHint, TWeakPtr<FTacticsHostWsPeer> ReplyPeerWeak);

	void HandleClientConnected(const FString& Url);
	void HandleClientConnectionError(const FString& Err);
	void HandleClientClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void FailClientSession(const FString& Reason, bool bReturnToMenu);
	void NotifyHostPeerRosterChanged();
	bool VerifyPeerFrameAuth(const TSharedPtr<FTacticsHostWsPeer>& Peer, const TSharedPtr<FJsonObject>& Root, int32 Seat,
		const FString& PayloadKey, const FString& PayloadValue, FString& OutRejectMsg);

	void TryBindAuthorityDelegate();
	void OnAuthorityCommittedFromMatch(int32 SeatPlayerId, const FString& Line, uint64 CommandSeq);

	void HostBroadcastSnapshotToAllPeers(bool bBumpSnapSeq = false);
	void HostBroadcastCommandToAllPeers(int32 SeatPlayerId, const FString& Line, uint64 CommandSeq);
	void HostSendSnapshotToPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer);
	void HostSendCliAckToPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer, const FString& Message);
	void SendClientResyncRequest();
	void SendClientDeck();
	bool TickClientResyncRetry(float DeltaTime);

	bool CompleteWsHandshakeForPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer, const FString& HttpBlock);
	void DisconnectPeerAtIndex(int32 Index, bool bClearMatchSelection);
	void DisconnectAllPeers(bool bClearSelections);

	void InitLobbySeats(bool bTeam2v2);
	static int32 TeamIdForLobbySeat(bool bTeam2v2, int32 SeatId);
	int32 FindLobbySeatIndex(int32 SeatId) const;
	int32 AllocateLobbySeat(int32 RequestedSeat, bool bAllowReplaceReadyPeer);
	void ClearLobbySeatByPeerSeat(int32 SeatId);
	void BroadcastLobbyState();
	FString BuildLobbyStateJson() const;
	void ApplyLobbyStateFromJson(const TSharedPtr<FJsonObject>& Root);
	void HandleLobbyClaimFromPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer, const TSharedPtr<FJsonObject>& Root);
	void ClientSendLobbyClaim();
	void HandleMatchBeginFromHost(const TSharedPtr<FJsonObject>& Root);

	FDelegateHandle AuthorityDelegateHandle;

	FSocket* ListenSocket = nullptr;
	TArray<TSharedPtr<FTacticsHostWsPeer>> HostPeers;

	TSharedPtr<IWebSocket> ClientWebSocket;

	ETacticsWsRole Role = ETacticsWsRole::None;
	ETacticsNetSessionPhase SessionPhase = ETacticsNetSessionPhase::Offline;
	int32 ListenPort = 8788;
	bool bListenPublic = true;

	FString RoomToken;
	TSet<int32> HostSeatsWithClientDeckLocked;

	bool bClientConnecting = false;
	FString ClientConnectionError;
	bool bIgnoreNextClientClosedEvent = false;

	bool bHostPollTickerRegistered = false;
	FTSTicker::FDelegateHandle HostPollTickerHandle;
	std::optional<uint64> LastHostSnapshotSeq;
	TSharedPtr<struct FTacticsHostSnapshotCache> HostSnapshotCache;

	FString ClientAuthNonce;
	uint64 ClientCliCtr = 0;

	int32 ClientRemoteSeatPlayerId = 1;
	bool bClientWelcomeReceived = false;
	bool bClientResyncPending = false;
	int32 ClientResyncAttempts = 0;
	FTSTicker::FDelegateHandle ClientResyncTickerHandle;

	FTacticsLobbyMatchSettings LobbySettings;
	TArray<FTacticsLobbySeatState> LobbySeats;
	FString LobbyStatusMessage;
	FString PendingJoinDeckName;
	FString PendingJoinDeckJson;
	/** After match_begin, GameInstance should enter match without reconnecting. */
	bool bPendingEnterMatchFromLobby = false;
};
