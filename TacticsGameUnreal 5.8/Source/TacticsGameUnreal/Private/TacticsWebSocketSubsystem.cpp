#include "TacticsWebSocketSubsystem.h"

#include "TacticsGameInstance.h"
#include "TacticsMatchSubsystem.h"
#include "tactics/common/match_defaults.hpp"
#include "tactics/core/game_state.hpp"

#include "Async/Async.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "IPAddress.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include <nlohmann/json.hpp>

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#include "Windows/HideWindowsPlatformTypes.h"
#endif

/** Previous broadcast snapshot as a parsed DOM (pimpl target of TacticsWebSocketSubsystem.h). */
struct FTacticsHostSnapshotCache
{
	nlohmann::json Dom;
};

namespace
{
constexpr int32 kWireProtocolVersion = tactics::kNetworkWireVersion;

/** Reads seat=N from a handshake or URL query, or DefaultSeat if missing. */
int32 ParseSeatFromText(const FString& Text, int32 DefaultSeat)
{
	const int32 Idx = Text.Find(TEXT("seat="), ESearchCase::IgnoreCase);
	if (Idx == INDEX_NONE) {
		return DefaultSeat;
	}
	int32 Start = Idx + 5;
	while (Start < Text.Len() && FChar::IsWhitespace(Text[Start])) {
		++Start;
	}
	FString Digits;
	for (; Start < Text.Len() && FChar::IsDigit(Text[Start]); ++Start) {
		Digits.AppendChar(Text[Start]);
	}
	if (Digits.IsEmpty()) {
		return DefaultSeat;
	}
	return FMath::Max(1, FCString::Atoi(*Digits));
}

/** Remote CLI seats are P2..PN (host is never over TCP). */
int32 SanitizeRemoteCliSeat(int32 Seat, UTacticsMatchSubsystem* Match)
{
	if (!Match || Seat < 2) {
		return 2;
	}
	const int32 N = Match->GetMatchPlayerCount();
	if (N < 2) {
		return 2;
	}
	return FMath::Clamp(Seat, 2, N);
}

bool Sha1Digest(const ANSICHAR* Data, int32 Len, uint8 Out20[20])
{
#if PLATFORM_WINDOWS
	HCRYPTPROV Prov = 0;
	HCRYPTHASH Hash = 0;
	if (!CryptAcquireContext(&Prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		return false;
	}
	if (!CryptCreateHash(Prov, CALG_SHA1, 0, 0, &Hash)) {
		CryptReleaseContext(Prov, 0);
		return false;
	}
	if (!CryptHashData(Hash, reinterpret_cast<const BYTE*>(Data), Len, 0)) {
		CryptDestroyHash(Hash);
		CryptReleaseContext(Prov, 0);
		return false;
	}
	DWORD HashLen = 20;
	if (!CryptGetHashParam(Hash, HP_HASHVAL, Out20, &HashLen, 0)) {
		CryptDestroyHash(Hash);
		CryptReleaseContext(Prov, 0);
		return false;
	}
	CryptDestroyHash(Hash);
	CryptReleaseContext(Prov, 0);
	return true;
#else
	FSHA1::HashBuffer(reinterpret_cast<const uint8*>(Data), Len, Out20);
	return true;
#endif
}

FString MakeAcceptKey(FString ClientKey)
{
	ClientKey.TrimStartInline();
	ClientKey.TrimEndInline();
	const FString Concat = ClientKey + TEXT("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
	FTCHARToUTF8 Utf8(*Concat);
	uint8 Digest[20]{};
	if (!Sha1Digest(Utf8.Get(), Utf8.Length(), Digest)) {
		return {};
	}
	return FBase64::Encode(Digest, 20);
}

bool ParseHttpHeaderValue(const FString& Block, const TCHAR* Key, FString& OutValue)
{
	const FString Pattern = FString::Printf(TEXT("%s: "), Key);
	int32 Idx = Block.Find(Pattern, ESearchCase::IgnoreCase);
	if (Idx == INDEX_NONE) {
		return false;
	}
	const FString Rest = Block.Mid(Idx + Pattern.Len());
	int32 Cr = INDEX_NONE;
	int32 Nl = INDEX_NONE;
	const bool HasCr = Rest.FindChar(TEXT('\r'), Cr);
	const bool HasNl = Rest.FindChar(TEXT('\n'), Nl);
	int32 LineEnd = INDEX_NONE;
	if (HasCr && HasNl) {
		LineEnd = FMath::Min(Cr, Nl);
	} else if (HasCr) {
		LineEnd = Cr;
	} else if (HasNl) {
		LineEnd = Nl;
	}
	if (LineEnd != INDEX_NONE) {
		OutValue = Rest.Left(LineEnd).TrimStartAndEnd();
	} else {
		OutValue = Rest.TrimStartAndEnd();
	}
	return true;
}

/** RFC6455: cap incoming text payload (snapshots stay well below this). */
constexpr uint64 kMaxWsTextPayloadBytes = 32ull * 1024 * 1024;
/** Hard cap on buffered unsent bytes per peer - a stalled client is dropped past this. */
constexpr int32 kMaxPeerTxBufferBytes = 128 * 1024 * 1024;

/** Encode a server→client WS frame (unmasked) and queue it on the peer. False → disconnect. */
bool SendWsFrame(FTacticsHostWsPeer& Peer, const uint8 Opcode, const uint8* Payload, const int32 PayloadLen)
{
	if (PayloadLen < 0 || static_cast<uint64>(PayloadLen) > kMaxWsTextPayloadBytes) {
		UE_LOG(LogTemp, Error, TEXT("TacticsWS: frame payload size invalid or too large (%d bytes)"), PayloadLen);
		return false;
	}
	TArray<uint8> Frame;
	Frame.Reserve(14 + PayloadLen);
	Frame.Add(static_cast<uint8>(0x80 | Opcode));
	if (PayloadLen < 126) {
		Frame.Add(static_cast<uint8>(PayloadLen));
	} else if (PayloadLen < 65536) {
		Frame.Add(126);
		Frame.Add(static_cast<uint8>((PayloadLen >> 8) & 0xff));
		Frame.Add(static_cast<uint8>(PayloadLen & 0xff));
	} else {
		Frame.Add(127);
		const uint64 Len = static_cast<uint64>(PayloadLen);
		for (int B = 7; B >= 0; --B) {
			Frame.Add(static_cast<uint8>((Len >> (B * 8)) & 0xff));
		}
	}
	if (PayloadLen > 0) {
		Frame.Append(Payload, PayloadLen);
	}
	return Peer.QueueBytes(Frame.GetData(), Frame.Num());
}

bool SendWsTextFrame(FTacticsHostWsPeer& Peer, const FString& Message)
{
	FTCHARToUTF8 Utf8(*Message);
	return SendWsFrame(Peer, 0x01, reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

bool SendWsPong(FTacticsHostWsPeer& Peer, const TArray<uint8>& Payload)
{
	return SendWsFrame(Peer, 0x0A, Payload.GetData(), Payload.Num());
}

FString BuildJsonCliAck(const FString& Msg)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("t"), TEXT("cli_ack"));
	Root->SetNumberField(TEXT("v"), kWireProtocolVersion);
	Root->SetStringField(TEXT("msg"), Msg);
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

FString BuildJsonCli(int32 SeatId, const FString& Line, const FString& Token, UTacticsMatchSubsystem* Match,
	const FString& AuthNonce, const uint64 Ctr)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("t"), TEXT("cli"));
	Root->SetNumberField(TEXT("v"), kWireProtocolVersion);
	Root->SetNumberField(TEXT("seat"), SeatId);
	Root->SetStringField(TEXT("line"), Line);
	if (!Token.IsEmpty()) {
		Root->SetStringField(TEXT("token"), Token);
		Root->SetNumberField(TEXT("ctr"), static_cast<double>(Ctr));
		if (Match) {
			const FString Sig = Match->ComputeCliSig(SeatId, Line, Token, AuthNonce, Ctr);
			if (!Sig.IsEmpty()) {
				Root->SetStringField(TEXT("sig"), Sig);
			}
		}
	}
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

FString BuildJsonClientDeck(const FString& DeckJson, int32 SeatId, const FString& Token, UTacticsMatchSubsystem* Match,
	const FString& AuthNonce, const uint64 Ctr)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("t"), TEXT("client_deck"));
	Root->SetNumberField(TEXT("v"), kWireProtocolVersion);
	Root->SetStringField(TEXT("deck"), DeckJson);
	if (!Token.IsEmpty()) {
		Root->SetNumberField(TEXT("seat"), SeatId);
		Root->SetNumberField(TEXT("ctr"), static_cast<double>(Ctr));
		if (Match) {
			const FString Sig = Match->ComputeCliSig(SeatId, DeckJson, Token, AuthNonce, Ctr);
			if (!Sig.IsEmpty()) {
				Root->SetStringField(TEXT("sig"), Sig);
			}
		}
	}
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

FString BuildWelcomeJson(int32 SeatId, int32 PlayerCount, const FString& AbilityFp, const FString& CardFp, const FString& AuthNonce)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("t"), TEXT("welcome"));
	Root->SetNumberField(TEXT("v"), kWireProtocolVersion);
	Root->SetNumberField(TEXT("seat"), SeatId);
	Root->SetNumberField(TEXT("player_count"), PlayerCount);
	Root->SetStringField(TEXT("authority"), TEXT("unreal_host_p1"));
	Root->SetBoolField(TEXT("content_catalog_loaded"), true);
	Root->SetStringField(TEXT("ability_catalog_fingerprint"), AbilityFp);
	Root->SetStringField(TEXT("card_catalog_fingerprint"), CardFp);
	if (!AuthNonce.IsEmpty()) {
		Root->SetStringField(TEXT("auth_nonce"), AuthNonce);
	}
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

}  // namespace

FTacticsHostWsPeer::~FTacticsHostWsPeer()
{
	CloseSocket();
}

void FTacticsHostWsPeer::CloseSocket()
{
	if (TcpSocket) {
		if (ISocketSubsystem* SockSys = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) {
			SockSys->DestroySocket(TcpSocket);
		}
	}
	TcpSocket = nullptr;
	Phase = ETacticsHostWsPeerPhase::HttpHandshake;
	HandshakeAccum.Empty();
	RxScratch.Empty();
	Utf8FragmentAssembly.Empty();
	TxBuffer.Empty();
}

bool FTacticsHostWsPeer::FlushTx()
{
	while (TcpSocket && TxBuffer.Num() > 0) {
		int32 ThisSent = 0;
		const bool bOk = TcpSocket->Send(TxBuffer.GetData(), TxBuffer.Num(), ThisSent);
		if (bOk && ThisSent > 0) {
			TxBuffer.RemoveAt(0, ThisSent, EAllowShrinking::No);
			continue;
		}
		// Non-blocking socket: a full kernel buffer is not an error - retry next poll tick.
		ISocketSubsystem* SockSys = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SockSys && SockSys->GetLastErrorCode() == SE_EWOULDBLOCK) {
			return true;
		}
		return false;
	}
	return true;
}

bool FTacticsHostWsPeer::QueueBytes(const uint8* Data, const int32 Len)
{
	if (!TcpSocket || Len < 0) {
		return false;
	}
	if (TxBuffer.Num() + Len > kMaxPeerTxBufferBytes) {
		UE_LOG(LogTemp, Warning, TEXT("TacticsWS: peer P%d tx backlog over %d bytes; dropping peer"), SeatId, kMaxPeerTxBufferBytes);
		return false;
	}
	if (Len > 0) {
		TxBuffer.Append(Data, Len);
	}
	return FlushTx();
}

bool FTacticsHostWsPeer::SendJson(const FString& JsonUtf16)
{
	if (TcpSocket && Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
		return SendWsTextFrame(*this, JsonUtf16);
	}
	return true;
}

void UTacticsWebSocketSubsystem::HostBroadcastSnapshotToAllPeers(const bool bBumpSnapSeq)
{
	if (Role != ETacticsWsRole::HostP1) {
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI) {
		return;
	}
	UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	if (!Match) {
		return;
	}
	if (bBumpSnapSeq) {
		Match->BumpNetworkSnapSeqForCheckpoint();
	}
	const FString FullWire = Match->ExportNetworkWireSnapshotJson();
	if (FullWire.IsEmpty()) {
		UE_LOG(LogTemp, Error, TEXT("TacticsWS: snapshot export returned empty wire; clients will not sync"));
		return;
	}
	FString WireToSend = FullWire;
	const FTCHARToUTF8 FullWireUtf8(*FullWire);
	std::string full_wire(FullWireUtf8.Get(), FullWireUtf8.Length());
	std::string inner_snapshot;
	std::optional<uint64_t> snap_seq;
	try {
		const nlohmann::json outer = nlohmann::json::parse(full_wire);
		if (outer.contains("t") && outer["t"].is_string() && outer["t"].get<std::string>() == "snap"
			&& outer.contains("payload")) {
			inner_snapshot = outer["payload"].dump();
			if (outer.contains("snap_seq")) {
				if (outer["snap_seq"].is_number_unsigned()) {
					snap_seq = outer["snap_seq"].get<uint64_t>();
				} else if (outer["snap_seq"].is_number_integer()) {
					snap_seq = static_cast<uint64_t>(FMath::Max<int64>(0, outer["snap_seq"].get<int64_t>()));
				}
			}
		}
	} catch (const std::exception&) {
		// Fall back to full snapshot wire.
	}
	if (snap_seq.has_value()) {
		try {
			nlohmann::json next = nlohmann::json::parse(inner_snapshot);
			// Send a delta whenever every ready peer shares the previous broadcast as a base -
			// checkpoint bumps included (a full snapshot every 64 commands wasted bandwidth).
			if (LastHostSnapshotSeq.has_value() && HostSnapshotCache.IsValid() && !HostSnapshotCache->Dom.is_null()
				&& *LastHostSnapshotSeq < *snap_seq) {
				nlohmann::json delta_wire_root = nlohmann::json::object();
				delta_wire_root["t"] = "snap_delta";
				delta_wire_root["v"] = kWireProtocolVersion;
				delta_wire_root["base_seq"] = *LastHostSnapshotSeq;
				delta_wire_root["snap_seq"] = *snap_seq;
				delta_wire_root["delta"] = nlohmann::json::diff(HostSnapshotCache->Dom, next);
				const std::string delta_wire = delta_wire_root.dump();
				if (!delta_wire.empty() && delta_wire.size() < full_wire.size()) {
					WireToSend = FString(UTF8_TO_TCHAR(delta_wire.c_str()));
				}
			}
			LastHostSnapshotSeq = *snap_seq;
			if (!HostSnapshotCache.IsValid()) {
				HostSnapshotCache = MakeShared<FTacticsHostSnapshotCache>();
			}
			HostSnapshotCache->Dom = std::move(next);
		} catch (const std::exception&) {
			// Fall back to full snapshot wire; drop the stale delta base.
			LastHostSnapshotSeq.reset();
			HostSnapshotCache.Reset();
		}
	}
	for (int32 i = HostPeers.Num() - 1; i >= 0; --i) {
		const TSharedPtr<FTacticsHostWsPeer>& P = HostPeers[i];
		if (P.IsValid() && P->TcpSocket && P->Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
			if (!SendWsTextFrame(*P, WireToSend)) {
				DisconnectPeerAtIndex(i, true);
			}
		}
	}
}

void UTacticsWebSocketSubsystem::HostSendCliAckToPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer, const FString& Message)
{
	if (!Peer.IsValid() || !Peer->TcpSocket || Peer->Phase != ETacticsHostWsPeerPhase::WebSocketReady) {
		return;
	}
	FString Trunc = Message;
	if (Trunc.Len() > 4096) {
		Trunc = Trunc.Left(4096) + TEXT("…");
	}
	if (!SendWsTextFrame(*Peer, BuildJsonCliAck(Trunc))) {
		const int32 Index = HostPeers.IndexOfByKey(Peer);
		if (Index != INDEX_NONE) {
			DisconnectPeerAtIndex(Index, true);
		}
	}
}

void UTacticsWebSocketSubsystem::DisconnectPeerAtIndex(int32 Index, bool bClearMatchSelection)
{
	if (!HostPeers.IsValidIndex(Index)) {
		return;
	}
	TSharedPtr<FTacticsHostWsPeer> P = HostPeers[Index];
	const int32 Seat = P.IsValid() ? P->SeatId : 0;
	if (bClearMatchSelection && Seat > 0) {
		if (UGameInstance* GI = GetGameInstance()) {
			if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
				Match->ClearRemoteSeatUnitSelection(Seat);
			}
		}
	}
	if (IsInLobby() && Seat > 0) {
		ClearLobbySeatByPeerSeat(Seat);
	}
	if (P.IsValid()) {
		P->CloseSocket();
	}
	HostPeers.RemoveAtSwap(Index);
	if (IsInLobby()) {
		BroadcastLobbyState();
	} else {
		NotifyHostPeerRosterChanged();
	}
}

void UTacticsWebSocketSubsystem::NotifyHostPeerRosterChanged()
{
	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Match->OnBoardChanged.Broadcast();
		}
	}
}

bool UTacticsWebSocketSubsystem::VerifyPeerFrameAuth(const TSharedPtr<FTacticsHostWsPeer>& Peer, const TSharedPtr<FJsonObject>& Root,
	const int32 Seat, const FString& /*PayloadKey*/, const FString& PayloadValue, FString& OutRejectMsg)
{
	if (RoomToken.IsEmpty()) {
		return true;
	}
	if (!Peer.IsValid()) {
		OutRejectMsg = TEXT("Rejected: missing peer");
		return false;
	}
	FString Sig;
	if (!Root.IsValid() || !Root->TryGetStringField(TEXT("sig"), Sig)) {
		OutRejectMsg = TEXT("Rejected: missing signature");
		return false;
	}
	double CtrNum = 0.;
	Root->TryGetNumberField(TEXT("ctr"), CtrNum);
	const uint64 Ctr = CtrNum > 0. ? static_cast<uint64>(CtrNum) : 0;
	if (Ctr <= Peer->LastCliCtr) {
		OutRejectMsg = TEXT("Rejected: stale counter");
		return false;
	}
	UTacticsMatchSubsystem* Match = nullptr;
	if (UGameInstance* GI = GetGameInstance()) {
		Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	}
	if (!Match || !Match->VerifyCliSig(Seat, PayloadValue, Sig, RoomToken, Peer->AuthNonce, Ctr)) {
		OutRejectMsg = TEXT("Rejected: invalid signature");
		return false;
	}
	Peer->LastCliCtr = Ctr;
	return true;
}

void UTacticsWebSocketSubsystem::DisconnectAllPeers(bool bClearSelections)
{
	if (bClearSelections) {
		if (UGameInstance* GI = GetGameInstance()) {
			if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
				Match->ClearRemoteTcpSeatCliSession();
			}
		}
	}
	for (int32 i = HostPeers.Num() - 1; i >= 0; --i) {
		DisconnectPeerAtIndex(i, false);
	}
	HostPeers.Empty();
	ResetHostPeerRxState();
}

void UTacticsWebSocketSubsystem::Deinitialize()
{
	OnCliAckFromHost.Clear();

	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Match->OnNetworkAuthorityCommitted.Remove(AuthorityDelegateHandle);
		}
	}
	AuthorityDelegateHandle.Reset();

	Disconnect();
	StopHost();
	Super::Deinitialize();
}

void UTacticsWebSocketSubsystem::TryBindAuthorityDelegate()
{
	if (AuthorityDelegateHandle.IsValid()) {
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI) {
		return;
	}
	UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	if (!Match) {
		return;
	}
	AuthorityDelegateHandle =
		Match->OnNetworkAuthorityCommitted.AddUObject(this, &UTacticsWebSocketSubsystem::OnAuthorityCommittedFromMatch);
}

void UTacticsWebSocketSubsystem::OnAuthorityCommittedFromMatch(const int32 SeatPlayerId, const FString& Line, const uint64 CommandSeq)
{
	HostBroadcastCommandToAllPeers(SeatPlayerId, Line, CommandSeq);
	if (CommandSeq == 0 || CommandSeq % UTacticsMatchSubsystem::NetworkCheckpointCommandInterval == 0) {
		HostBroadcastSnapshotToAllPeers(true);
	}
}

void UTacticsWebSocketSubsystem::HostBroadcastCommandToAllPeers(const int32 SeatPlayerId, const FString& Line, const uint64 CommandSeq)
{
	if (Role != ETacticsWsRole::HostP1 || Line.IsEmpty() || CommandSeq == 0) {
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI) {
		return;
	}
	UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	if (!Match) {
		return;
	}
	const FString Wire = Match->WrapNetworkCommandWireJson(SeatPlayerId, Line, CommandSeq);
	if (Wire.IsEmpty()) {
		return;
	}
	for (int32 i = HostPeers.Num() - 1; i >= 0; --i) {
		const TSharedPtr<FTacticsHostWsPeer>& P = HostPeers[i];
		if (P.IsValid() && P->TcpSocket && P->Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
			if (!SendWsTextFrame(*P, Wire)) {
				DisconnectPeerAtIndex(i, true);
			}
		}
	}
}

void UTacticsWebSocketSubsystem::HostSendSnapshotToPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer)
{
	if (!Peer.IsValid() || !Peer->TcpSocket || Peer->Phase != ETacticsHostWsPeerPhase::WebSocketReady) {
		return;
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI) {
		return;
	}
	UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	if (!Match) {
		return;
	}
	const FString Wire = Match->ExportNetworkWireSnapshotJson();
	if (!Wire.IsEmpty() && !SendWsTextFrame(*Peer, Wire)) {
		const int32 Index = HostPeers.IndexOfByKey(Peer);
		if (Index != INDEX_NONE) {
			DisconnectPeerAtIndex(Index, true);
		}
	}
}

void UTacticsWebSocketSubsystem::ResetHostPeerRxState()
{
	/** Legacy single-buffer clear - per-peer buffers live on FTacticsHostWsPeer. */
}

void UTacticsWebSocketSubsystem::ScheduleDispatchInboundJson(FString Json, bool bFromTcpClient, int32 TcpSeatHint,
														   TWeakPtr<FTacticsHostWsPeer> ReplyPeerWeak)
{
	Json.TrimStartInline();
	Json.TrimEndInline();
	if (Json.IsEmpty()) {
		return;
	}

	TWeakObjectPtr<UTacticsWebSocketSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, Json = MoveTemp(Json), bFromTcpClient, TcpSeatHint, ReplyPeerWeak]() {
		if (!WeakThis.IsValid()) {
			return;
		}
		WeakThis->DispatchInboundJsonOnGameThread(Json, bFromTcpClient, TcpSeatHint, ReplyPeerWeak);
	});
}

void UTacticsWebSocketSubsystem::DispatchInboundJsonOnGameThread(const FString& Json, bool bFromTcpClient, int32 TcpSeatHint,
																TWeakPtr<FTacticsHostWsPeer> ReplyPeerWeak)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) {
		UE_LOG(LogTemp, Warning, TEXT("TacticsWS: JSON parse failed (tcp=%d, len=%d) preview: %.200s"),
			bFromTcpClient ? 1 : 0,
			Json.Len(),
			*Json.Left(200));
		return;
	}

	FString Type;
	if (!Root->TryGetStringField(TEXT("t"), Type)) {
		return;
	}

	double WireVersion = 0.;
	const bool bHasWireVersion = Root->TryGetNumberField(TEXT("v"), WireVersion);
	if (bFromTcpClient && Type == TEXT("cli") && !bHasWireVersion) {
		return;
	}
	if (bHasWireVersion) {
		if (static_cast<int32>(WireVersion) != kWireProtocolVersion) {
			UE_LOG(LogTemp, Warning, TEXT("TacticsWS: ignoring message with unsupported wire v=%d"), static_cast<int32>(WireVersion));
			return;
		}
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI) {
		return;
	}
	UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	if (!Match) {
		return;
	}

	// Host: lobby claim (seat / deck / ready) before match_begin.
	if (Type == TEXT("lobby_claim") && bFromTcpClient) {
		HandleLobbyClaimFromPeer(ReplyPeerWeak.Pin(), Root);
		return;
	}
	if (Type == TEXT("lobby_state") && !bFromTcpClient) {
		SessionPhase = ETacticsNetSessionPhase::Lobby;
		ApplyLobbyStateFromJson(Root);
		if (Role == ETacticsWsRole::ClientRemote) {
			LobbyStatusMessage = TEXT("Waiting for host.");
			OnLobbyChanged.Broadcast();
		}
		return;
	}
	if (Type == TEXT("match_begin") && !bFromTcpClient) {
		HandleMatchBeginFromHost(Root);
		return;
	}

	// Host: a joining client sent its chosen deck - apply once per seat; refuse late reconnect rewrites.
	if (Type == TEXT("client_deck") && bFromTcpClient) {
		if (IsInLobby()) {
			return;  // lobby uses lobby_claim for decks
		}
		const TSharedPtr<FTacticsHostWsPeer> ReplyPeer = ReplyPeerWeak.Pin();
		FString DeckJson;
		if (!Root->TryGetStringField(TEXT("deck"), DeckJson) || TcpSeatHint < 2) {
			return;
		}
		if (HostSeatsWithClientDeckLocked.Contains(TcpSeatHint)
			|| (ReplyPeer.IsValid() && ReplyPeer->bClientDeckApplied)) {
			if (ReplyPeer.IsValid()) {
				HostSendCliAckToPeer(ReplyPeer, TEXT("Rejected: client_deck already applied for this seat"));
			}
			return;
		}
		if (Match->GetMatchAuthorityCommandSeq() > 0) {
			// Match already journaling - late joiners keep the host's seeded deck for that seat.
			if (ReplyPeer.IsValid()) {
				HostSendCliAckToPeer(ReplyPeer, TEXT("Rejected: client_deck locked after match progressed"));
			}
			HostSeatsWithClientDeckLocked.Add(TcpSeatHint);
			return;
		}
		if (ReplyPeer.IsValid()) {
			FString Reject;
			if (!VerifyPeerFrameAuth(ReplyPeer, Root, TcpSeatHint, TEXT("deck"), DeckJson, Reject)) {
				HostSendCliAckToPeer(ReplyPeer, Reject);
				return;
			}
		} else if (!RoomToken.IsEmpty()) {
			return;
		}
		if (!Match->ApplyClientDeckToSeat(TcpSeatHint, DeckJson)) {
			if (ReplyPeer.IsValid()) {
				HostSendCliAckToPeer(ReplyPeer, TEXT("Rejected: client_deck failed validation"));
			}
			return;
		}
		if (ReplyPeer.IsValid()) {
			ReplyPeer->bClientDeckApplied = true;
		}
		HostSeatsWithClientDeckLocked.Add(TcpSeatHint);
		HostBroadcastSnapshotToAllPeers(true);
		return;
	}

	if (Type == TEXT("cli_ack") && !bFromTcpClient) {
		FString Msg;
		if (Root->TryGetStringField(TEXT("msg"), Msg)) {
			OnCliAckFromHost.Broadcast(Msg);
		}
		return;
	}

	if (Type == TEXT("welcome") && !bFromTcpClient) {
		double SeatNum = 0.;
		if (Root->TryGetNumberField(TEXT("seat"), SeatNum)) {
			ClientRemoteSeatPlayerId = static_cast<int32>(SeatNum);
		}
		FString RemoteFp;
		bool bCatalogMismatch = false;
		if (Root->TryGetStringField(TEXT("ability_catalog_fingerprint"), RemoteFp)) {
			const FString LocalFp = Match ? Match->GetAbilityCatalogFingerprint() : FString();
			if (!RemoteFp.Equals(LocalFp, ESearchCase::CaseSensitive)) {
				bCatalogMismatch = true;
				ClientConnectionError = FString::Printf(
					TEXT("Ability catalog mismatch (host %s vs local %s). Use matching Content/TacticsData builds."),
					*RemoteFp,
					*LocalFp);
				UE_LOG(LogTemp, Error, TEXT("TacticsWS: %s"), *ClientConnectionError);
			}
		}
		FString RemoteCardFp;
		if (Root->TryGetStringField(TEXT("card_catalog_fingerprint"), RemoteCardFp)) {
			const FString LocalCardFp = Match ? Match->GetCardCatalogFingerprint() : FString();
			if (!RemoteCardFp.Equals(LocalCardFp, ESearchCase::CaseSensitive)) {
				bCatalogMismatch = true;
				ClientConnectionError = FString::Printf(
					TEXT("Card catalog mismatch (host %s vs local %s)."),
					*RemoteCardFp,
					*LocalCardFp);
				UE_LOG(LogTemp, Error, TEXT("TacticsWS: %s"), *ClientConnectionError);
			}
		}
		if (bCatalogMismatch) {
			FailClientSession(ClientConnectionError, true);
			return;
		}
		ClientAuthNonce.Empty();
		Root->TryGetStringField(TEXT("auth_nonce"), ClientAuthNonce);
		ClientCliCtr = 0;
		bClientWelcomeReceived = true;
		Match->SetAutoFollowActiveSeat(false);
		Match->SetControlledPlayer(ClientRemoteSeatPlayerId);
		if (IsInLobby()) {
			const int32 Idx = FindLobbySeatIndex(ClientRemoteSeatPlayerId);
			if (Idx != INDEX_NONE) {
				LobbySeats[Idx].DeckName = PendingJoinDeckName;
				LobbySeats[Idx].DeckJson = PendingJoinDeckJson;
			}
			LobbyStatusMessage = TEXT("Waiting for host.");
			ClientSendLobbyClaim();
		} else {
			SendClientDeck();
		}
		return;
	}

	if (Type == TEXT("snap")) {
		FString Err;
		if (Role == ETacticsWsRole::ClientRemote && bClientWelcomeReceived) {
			Match->SetAutoFollowActiveSeat(false);
			Match->SetControlledPlayer(ClientRemoteSeatPlayerId);
		}
		if (!Match->ApplyNetworkWireSnapshotJson(Json, Err)) {
			UE_LOG(LogTemp, Warning, TEXT("TacticsWS: snapshot apply failed: %s"), *Err);
			if (Role == ETacticsWsRole::ClientRemote) {
				OnCliAckFromHost.Broadcast(FString::Printf(TEXT("[sync] Snapshot failed: %s"), *Err));
			}
		} else {
			bClientResyncPending = false;
		}
		return;
	}

	if (Type == TEXT("snap_delta")) {
		FString Err;
		if (Role == ETacticsWsRole::ClientRemote && bClientWelcomeReceived) {
			Match->SetAutoFollowActiveSeat(false);
			Match->SetControlledPlayer(ClientRemoteSeatPlayerId);
		}
		if (!Match->ApplyNetworkWireSnapshotDeltaJson(Json, Err)) {
			UE_LOG(LogTemp, Warning, TEXT("TacticsWS: delta snapshot apply failed: %s"), *Err);
			if (Role == ETacticsWsRole::ClientRemote && ClientWebSocket.IsValid()) {
				bClientResyncPending = true;
				ClientResyncAttempts = 0;
				SendClientResyncRequest();
			}
		} else {
			bClientResyncPending = false;
		}
		return;
	}

	if (Type == TEXT("cmd") && !bFromTcpClient) {
		FString Err;
		if (Role == ETacticsWsRole::ClientRemote && bClientWelcomeReceived) {
			Match->SetAutoFollowActiveSeat(false);
			Match->SetControlledPlayer(ClientRemoteSeatPlayerId);
		}
		if (!Match->ApplyNetworkCommandWireJson(Json, Err)) {
			UE_LOG(LogTemp, Warning, TEXT("TacticsWS: command apply failed: %s"), *Err);
			if (Role == ETacticsWsRole::ClientRemote && ClientWebSocket.IsValid()) {
				bClientResyncPending = true;
				ClientResyncAttempts = 0;
				SendClientResyncRequest();
				if (!ClientResyncTickerHandle.IsValid()) {
					ClientResyncTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
						FTickerDelegate::CreateUObject(this, &UTacticsWebSocketSubsystem::TickClientResyncRetry), 1.5f);
				}
			}
		}
		return;
	}

	if (Type == TEXT("resync") && bFromTcpClient) {
		if (const TSharedPtr<FTacticsHostWsPeer> ReplyPeer = ReplyPeerWeak.Pin()) {
			HostSendSnapshotToPeer(ReplyPeer);
		}
		return;
	}

	if (bFromTcpClient && Type == TEXT("cli")) {
		int32 Seat = TcpSeatHint >= 2 ? TcpSeatHint : 2;
		Seat = SanitizeRemoteCliSeat(Seat, Match);

		FString Line;
		if (!Root->TryGetStringField(TEXT("line"), Line)) {
			return;
		}

		if (!RoomToken.IsEmpty()) {
			const TSharedPtr<FTacticsHostWsPeer> ReplyPeer = ReplyPeerWeak.Pin();
			FString Reject;
			if (!VerifyPeerFrameAuth(ReplyPeer, Root, Seat, TEXT("line"), Line, Reject)) {
				if (ReplyPeer.IsValid()) {
					HostSendCliAckToPeer(ReplyPeer, Reject);
				}
				return;
			}
		}
		FString Out;
		Match->ExecRemoteTcpSeatCliLine(Seat, Line, Out);
		if (const TSharedPtr<FTacticsHostWsPeer> ReplyPeer = ReplyPeerWeak.Pin()) {
			HostSendCliAckToPeer(ReplyPeer, Out);
		}
		UE_LOG(LogTemp, Verbose, TEXT("TacticsWS: remote seat P%d cli: %s"), Seat, *Line);
		return;
	}
}

void UTacticsWebSocketSubsystem::AuthorityExecCliLine(const FString& Line, FString& OutMessage)
{
	TryBindAuthorityDelegate();
	UGameInstance* GI = GetGameInstance();
	if (!GI) {
		OutMessage = TEXT("No game instance.");
		return;
	}
	UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	if (!Match) {
		OutMessage = TEXT("No match.");
		return;
	}
	Match->ExecMasterCliLine(Line, OutMessage);
}

void UTacticsWebSocketSubsystem::HandleClientConnected(const FString& Url)
{
	bClientConnecting = false;
	bClientWelcomeReceived = false;
	bClientResyncPending = false;
	ClientConnectionError.Empty();
	UE_LOG(LogTemp, Log, TEXT("TacticsWS: client connected %s (requested seat hint P%d)"), *Url, ClientRemoteSeatPlayerId);
	Role = ETacticsWsRole::ClientRemote;
	if (IsInLobby()) {
		LobbyStatusMessage = TEXT("Waiting for host.");
	}
	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Match->SetAutoFollowActiveSeat(false);
		}
	}
	OnLobbyChanged.Broadcast();
	// Deck is sent after welcome (seat + auth_nonce known). Sending earlier breaks room-token auth.
}

void UTacticsWebSocketSubsystem::HandleClientConnectionError(const FString& Err)
{
	UE_LOG(LogTemp, Warning, TEXT("TacticsWS: join failed: %s"), *Err);
	FailClientSession(
		TEXT("Join failed - no server hosting at that address, or the match is full."),
		true);
}

void UTacticsWebSocketSubsystem::HandleClientClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	bClientConnecting = false;
	if (bIgnoreNextClientClosedEvent) {
		bIgnoreNextClientClosedEvent = false;
		return;
	}
	if (Role != ETacticsWsRole::ClientRemote && !ClientWebSocket.IsValid()) {
		return;
	}
	const FString Msg = (!bWasClean || StatusCode != 1000)
		? FString::Printf(TEXT("Disconnected from host (%d): %s"), StatusCode, Reason.IsEmpty() ? TEXT("(no reason)") : *Reason)
		: TEXT("Disconnected from host.");
	FailClientSession(Msg, true);
}

void UTacticsWebSocketSubsystem::FailClientSession(const FString& Reason, const bool bReturnToMenu)
{
	bClientConnecting = false;
	bClientWelcomeReceived = false;
	bClientResyncPending = false;
	ClientResyncAttempts = 0;
	if (ClientResyncTickerHandle.IsValid()) {
		FTSTicker::GetCoreTicker().RemoveTicker(ClientResyncTickerHandle);
		ClientResyncTickerHandle.Reset();
	}
	ClientConnectionError = Reason;
	SessionPhase = ETacticsNetSessionPhase::Offline;
	LobbySeats.Reset();
	UE_LOG(LogTemp, Error, TEXT("TacticsWS: %s"), *Reason);
	if (ClientWebSocket.IsValid()) {
		bIgnoreNextClientClosedEvent = true;
		ClientWebSocket->Close();
		ClientWebSocket.Reset();
	}
	if (Role == ETacticsWsRole::ClientRemote) {
		Role = ETacticsWsRole::None;
	}
	OnLobbyChanged.Broadcast();
	if (bReturnToMenu) {
		if (UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(GetGameInstance())) {
			TGI->ShowMainMenu();
		}
	} else if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Match->OnBoardChanged.Broadcast();
		}
	}
}

bool UTacticsWebSocketSubsystem::IsClientConnectedToHost() const
{
	return Role == ETacticsWsRole::ClientRemote && ClientWebSocket.IsValid() && ClientWebSocket->IsConnected();
}

/** Binds a TCP listen socket and accepts LAN WebSocket clients. */
void UTacticsWebSocketSubsystem::StartHost(int32 Port, bool bBindPublic, bool bResetMatch)
{
	StopHost();
	LastHostSnapshotSeq.reset();
	HostSnapshotCache.Reset();
	HostSeatsWithClientDeckLocked.Empty();
	ISocketSubsystem* SockSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SockSubsystem) {
		UE_LOG(LogTemp, Error, TEXT("TacticsWS: no socket subsystem"));
		return;
	}

	ListenSocket = SockSubsystem->CreateSocket(NAME_Stream, TEXT("TacticsWsListen"), false);
	if (!ListenSocket) {
		return;
	}
	ListenSocket->SetReuseAddr(true);
	ListenSocket->SetNonBlocking(true);

	TSharedRef<FInternetAddr> Addr = SockSubsystem->CreateInternetAddr();
	bool bOk = true;
	bListenPublic = bBindPublic;
	Addr->SetIp(bBindPublic ? TEXT("0.0.0.0") : TEXT("127.0.0.1"), bOk);
	Addr->SetPort(Port);

	if (!ListenSocket->Bind(*Addr) || !ListenSocket->Listen(16)) {
		SockSubsystem->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
		UE_LOG(LogTemp, Error, TEXT("TacticsWS: bind/listen failed on port %d"), Port);
		return;
	}

	ListenPort = Port;
	Role = ETacticsWsRole::HostP1;

	TryBindAuthorityDelegate();

	if (bResetMatch) {
		if (UGameInstance* GI = GetGameInstance()) {
			if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
				FTacticsMatchSetupProfile Profile;
				Profile.bAutoFollowActiveSeat = false;
				Profile.bSeedDemoState = false;
				int32 Seats = 2;
				if (const UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(GI)) {
					const UTacticsGameInstance::FTacticsMatchSettings& S = TGI->GetPendingMatchSettings();
					Profile.bTeam2v2 = S.bTeam2v2;
					Profile.bObjScanner = S.bObjScanner;
					Profile.bObjOmni = S.bObjOmni;
					Profile.bObjAether = S.bObjAether;
					Profile.bGiveFieldRequisition = S.bGiveFieldRequisition;
					Seats = S.bTeam2v2 ? 4 : 2;
				}
				Match->ResetMatchWithProfile(Seats, Profile);
				Match->SetControlledPlayer(1);
			}
		}
	} else if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Match->SetAutoFollowActiveSeat(false);
			Match->SetControlledPlayer(1);
		}
	}

	HostPollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticsWebSocketSubsystem::TickHostSockets),
		0.f);
	bHostPollTickerRegistered = true;

	const TCHAR* BindDesc = bBindPublic ? TEXT("0.0.0.0 (LAN)") : TEXT("127.0.0.1 (local only)");
	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* M = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			UE_LOG(LogTemp, Log,
				TEXT("TacticsWS: host listening on %s:%d (authority P1). Seats P1..P%d. Clients: ws://<host-ip>:%d/"),
				BindDesc,
				Port,
				M->GetMatchPlayerCount(),
				Port);
		} else {
			UE_LOG(LogTemp, Log, TEXT("TacticsWS: host listening on %s:%d (authority P1)."), BindDesc, Port);
		}
	} else {
		UE_LOG(LogTemp, Log, TEXT("TacticsWS: host listening on %s:%d (authority P1)."), BindDesc, Port);
	}
}

bool UTacticsWebSocketSubsystem::TickHostSockets(float DeltaTime)
{
	if (!ListenSocket && HostPeers.Num() == 0) {
		return false;
	}
	PollSockets();
	return ListenSocket != nullptr || HostPeers.Num() > 0;
}

void UTacticsWebSocketSubsystem::StopHost()
{
	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			if (AuthorityDelegateHandle.IsValid()) {
				Match->OnNetworkAuthorityCommitted.Remove(AuthorityDelegateHandle);
				AuthorityDelegateHandle.Reset();
			}
		}
	}

	DisconnectAllPeers(true);
	HostSeatsWithClientDeckLocked.Empty();

	if (bHostPollTickerRegistered) {
		FTSTicker::GetCoreTicker().RemoveTicker(HostPollTickerHandle);
		HostPollTickerHandle = FTSTicker::FDelegateHandle();
		bHostPollTickerRegistered = false;
	}
	ISocketSubsystem* SockSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (ListenSocket && SockSubsystem) {
		SockSubsystem->DestroySocket(ListenSocket);
	}
	ListenSocket = nullptr;
	LastHostSnapshotSeq.reset();
	HostSnapshotCache.Reset();
	if (Role == ETacticsWsRole::HostP1) {
		Role = ETacticsWsRole::None;
	}
	if (SessionPhase != ETacticsNetSessionPhase::Lobby || !IsClientConnectedToHost()) {
		// Host stop while not in a client lobby session.
		if (Role == ETacticsWsRole::None && !ClientWebSocket.IsValid()) {
			SessionPhase = ETacticsNetSessionPhase::Offline;
		}
	}
}

void UTacticsWebSocketSubsystem::ConnectClient(const FString& Url)
{
	ClientConnectionError.Empty();  // a fresh join attempt starts without the previous failure
	Disconnect();

	// Default 1 so a headless C++ host can assign the first joiner as P1; Unreal editor host remaps to P2+ in RegisterNetworkClientSeat.
	ClientRemoteSeatPlayerId = ParseSeatFromText(Url, 1);
	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Match->SetAutoFollowActiveSeat(false);
		}
	}

	if (!FModuleManager::Get().IsModuleLoaded("WebSockets")) {
		FModuleManager::Get().LoadModule("WebSockets");
	}
	FWebSocketsModule& Mod = FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

	ClientWebSocket = Mod.CreateWebSocket(Url, TArray<FString>(), TMap<FString, FString>());
	if (!ClientWebSocket.IsValid()) {
		FailClientSession(TEXT("WebSocket create failed."), true);
		return;
	}

	ClientConnectionError.Empty();

	TWeakObjectPtr<UTacticsWebSocketSubsystem> WeakSelf(this);

	ClientWebSocket->OnConnected().AddLambda([WeakSelf, Url]() {
		const FString UrlCopy = Url;
		AsyncTask(ENamedThreads::GameThread, [WeakSelf, UrlCopy]() {
			if (!WeakSelf.IsValid()) {
				return;
			}
			WeakSelf->HandleClientConnected(UrlCopy);
		});
	});

	ClientWebSocket->OnMessage().AddLambda([WeakSelf](const FString& Message) {
		if (!WeakSelf.IsValid()) {
			return;
		}
		WeakSelf->ScheduleDispatchInboundJson(Message, false, 0, {});
	});

	ClientWebSocket->OnConnectionError().AddLambda([WeakSelf](const FString& Err) {
		const FString ErrCopy = Err;
		AsyncTask(ENamedThreads::GameThread, [WeakSelf, ErrCopy]() {
			if (!WeakSelf.IsValid()) {
				return;
			}
			WeakSelf->HandleClientConnectionError(ErrCopy);
		});
	});

	ClientWebSocket->OnClosed().AddLambda([WeakSelf](int32 StatusCode, const FString& Reason, bool bWasClean) {
		AsyncTask(ENamedThreads::GameThread, [WeakSelf, StatusCode, Reason, bWasClean]() {
			if (!WeakSelf.IsValid()) {
				return;
			}
			WeakSelf->HandleClientClosed(StatusCode, Reason, bWasClean);
		});
	});

	bClientConnecting = true;
	ClientWebSocket->Connect();
}

void UTacticsWebSocketSubsystem::Disconnect()
{
	bClientConnecting = false;
	bClientWelcomeReceived = false;
	bClientResyncPending = false;
	ClientResyncAttempts = 0;
	if (ClientResyncTickerHandle.IsValid()) {
		FTSTicker::GetCoreTicker().RemoveTicker(ClientResyncTickerHandle);
		ClientResyncTickerHandle.Reset();
	}
	// Keep ClientConnectionError so the main menu can show why we left (cleared on next ConnectClient).
	if (ClientWebSocket.IsValid()) {
		bIgnoreNextClientClosedEvent = true;
		ClientWebSocket->Close();
		ClientWebSocket.Reset();
	}
	if (Role == ETacticsWsRole::ClientRemote) {
		Role = ETacticsWsRole::None;
	}
}

bool UTacticsWebSocketSubsystem::CompleteWsHandshakeForPeer(const TSharedPtr<FTacticsHostWsPeer>& Peer, const FString& HttpBlock)
{
	if (!Peer.IsValid() || !Peer->TcpSocket) {
		return false;
	}
	FString Key;
	if (!ParseHttpHeaderValue(HttpBlock, TEXT("Sec-WebSocket-Key"), Key)) {
		UE_LOG(LogTemp, Warning, TEXT("TacticsWS: missing Sec-WebSocket-Key"));
		return false;
	}
	const FString Accept = MakeAcceptKey(Key);
	if (Accept.IsEmpty()) {
		return false;
	}

	// Assign the seat BEFORE the WebSocket upgrade: a full match is refused with an HTTP error, so the
	// joining client fails to connect outright instead of being dropped into a dead session.
	const int32 RequestedSeat = ParseSeatFromText(HttpBlock, 2);
	const bool bExplicitSeatRequest = HttpBlock.Find(TEXT("seat="), ESearchCase::IgnoreCase) != INDEX_NONE;
	int32 AssignedSeat = 0;
	if (IsInLobby()) {
		AssignedSeat = AllocateLobbySeat(RequestedSeat, bExplicitSeatRequest);
	} else {
		TSet<int32> SeatsTakenByOtherRemotes;
		for (const TSharedPtr<FTacticsHostWsPeer>& Other : HostPeers) {
			if (!Other.IsValid() || Other.Get() == Peer.Get()) {
				continue;
			}
			if (Other->SeatId >= 2) {
				if (bExplicitSeatRequest && Other->SeatId == RequestedSeat
					&& Other->Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
					continue;
				}
				SeatsTakenByOtherRemotes.Add(Other->SeatId);
			}
		}
		if (UGameInstance* GI = GetGameInstance()) {
			if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
				AssignedSeat = Match->RegisterNetworkClientSeat(RequestedSeat, &SeatsTakenByOtherRemotes);
			}
		}
	}
	if (AssignedSeat < 2) {
		const FString Busy =
			TEXT("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
		FTCHARToUTF8 BusyUtf8(*Busy);
		Peer->QueueBytes(reinterpret_cast<const uint8*>(BusyUtf8.Get()), BusyUtf8.Length());
		UE_LOG(LogTemp, Warning, TEXT("TacticsWS: refusing join - match/lobby is full (no open seat)."));
		return false;
	}
	// Reserve immediately so a concurrent handshake cannot take the same seat.
	Peer->SeatId = AssignedSeat;
	Peer->bClientDeckApplied = false;
	if (IsInLobby()) {
		const int32 Idx = FindLobbySeatIndex(AssignedSeat);
		if (Idx != INDEX_NONE) {
			LobbySeats[Idx].bOccupied = true;
			LobbySeats[Idx].bIsHost = false;
			LobbySeats[Idx].DisplayName = FString::Printf(TEXT("P%d"), AssignedSeat);
			LobbySeats[Idx].bReady = false;
		}
	}

	const FString Response = FString::Printf(
		TEXT("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n"),
		*Accept);
	FTCHARToUTF8 Utf8(*Response);
	if (!Peer->QueueBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length())) {
		return false;
	}

	Peer->Phase = ETacticsHostWsPeerPhase::WebSocketReady;
	Peer->AuthNonce.Empty();
	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Peer->AuthNonce = Match->GenerateCliAuthNonce();
		}
	}
	Peer->LastCliCtr = 0;

	for (int32 i = HostPeers.Num() - 1; i >= 0; --i) {
		const TSharedPtr<FTacticsHostWsPeer>& Other = HostPeers[i];
		if (!Other.IsValid() || Other.Get() == Peer.Get()) {
			continue;
		}
		if (Other->Phase == ETacticsHostWsPeerPhase::WebSocketReady && Other->SeatId == Peer->SeatId && Other->TcpSocket) {
			UE_LOG(LogTemp, Log, TEXT("TacticsWS: replacing prior connection for seat P%d"), Peer->SeatId);
			DisconnectPeerAtIndex(i, true);
		}
	}

	TryBindAuthorityDelegate();

	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* Match = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			Match->ClearRemoteSeatUnitSelection(Peer->SeatId);
		}
	}

	int32 PlayerCount = IsInLobby() ? LobbySeats.Num() : 0;
	if (PlayerCount < 1) {
		if (UGameInstance* GI = GetGameInstance()) {
			if (UTacticsMatchSubsystem* M = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
				PlayerCount = M->GetMatchPlayerCount();
			}
		}
	}
	FString AbilityFp;
	FString CardFp;
	if (UGameInstance* GI = GetGameInstance()) {
		if (UTacticsMatchSubsystem* M = GI->GetSubsystem<UTacticsMatchSubsystem>()) {
			AbilityFp = M->GetAbilityCatalogFingerprint();
			CardFp = M->GetCardCatalogFingerprint();
		}
	}
	if (!Peer->SendJson(BuildWelcomeJson(Peer->SeatId, PlayerCount, AbilityFp, CardFp,
			RoomToken.IsEmpty() ? FString() : Peer->AuthNonce))) {
		return false;
	}
	if (IsInLobby()) {
		BroadcastLobbyState();
		NotifyHostPeerRosterChanged();
		UE_LOG(LogTemp, Log, TEXT("TacticsWS: lobby peer ready seat P%d"), Peer->SeatId);
		return true;
	}
	LastHostSnapshotSeq.reset();
	HostSnapshotCache.Reset();
	HostBroadcastSnapshotToAllPeers(false);
	NotifyHostPeerRosterChanged();
	UE_LOG(LogTemp, Log, TEXT("TacticsWS: peer ready seat P%d"), Peer->SeatId);
	return true;
}

void UTacticsWebSocketSubsystem::PollSockets()
{
	ISocketSubsystem* SockSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (ListenSocket) {
		while (FSocket* NewSock = ListenSocket->Accept(TEXT("TacticsWsPeer"))) {
			NewSock->SetNonBlocking(true);
			const TSharedPtr<FTacticsHostWsPeer> Peer = MakeShared<FTacticsHostWsPeer>();
			Peer->TcpSocket = NewSock;
			Peer->Phase = ETacticsHostWsPeerPhase::HttpHandshake;
			Peer->HandshakeAccum.Empty();
			Peer->RxScratch.Empty();
			HostPeers.Add(Peer);
			UE_LOG(LogTemp, Log, TEXT("TacticsWS: accepted TCP peer (handshake)"));
		}
	}

	for (int32 i = HostPeers.Num() - 1; i >= 0; --i) {
		TSharedPtr<FTacticsHostWsPeer> Peer = HostPeers[i];
		if (!Peer.IsValid() || !Peer->TcpSocket) {
			DisconnectPeerAtIndex(i, true);
			continue;
		}

		// Retry buffered output first - a full kernel buffer parks frames here (backpressure).
		if (Peer->TxBuffer.Num() > 0 && !Peer->FlushTx()) {
			UE_LOG(LogTemp, Warning, TEXT("TacticsWS: peer P%d send failed; disconnecting"), Peer->SeatId);
			DisconnectPeerAtIndex(i, true);
			continue;
		}

		if (Peer->Phase == ETacticsHostWsPeerPhase::HttpHandshake) {
			uint32 Pending = 0;
			if (!Peer->TcpSocket->HasPendingData(Pending) || Pending < 1) {
				continue;
			}
			uint8 Buf[16384]{};
			int32 Read = 0;
			if (!Peer->TcpSocket->Recv(Buf, sizeof(Buf), Read) || Read < 1) {
				continue;
			}
			Peer->HandshakeAccum.Append(Buf, Read);
			if (Peer->HandshakeAccum.Num() > 64 * 1024) {
				UE_LOG(LogTemp, Warning, TEXT("TacticsWS: oversized HTTP handshake; disconnecting"));
				DisconnectPeerAtIndex(i, true);
				continue;
			}
			// Find the header terminator in the raw bytes: converting the whole buffer to
			// text would mangle any binary WS frames pipelined after the upgrade request.
			auto FindByteSeq = [](const TArray<uint8>& Haystack, const char* Needle, const int32 NeedleLen) -> int32 {
				for (int32 At = 0; At + NeedleLen <= Haystack.Num(); ++At) {
					bool bMatch = true;
					for (int32 k = 0; k < NeedleLen; ++k) {
						if (Haystack[At + k] != static_cast<uint8>(Needle[k])) {
							bMatch = false;
							break;
						}
					}
					if (bMatch) {
						return At;
					}
				}
				return INDEX_NONE;
			};
			int32 TermIdx = FindByteSeq(Peer->HandshakeAccum, "\r\n\r\n", 4);
			int32 TermLen = 4;
			if (TermIdx == INDEX_NONE) {
				TermIdx = FindByteSeq(Peer->HandshakeAccum, "\n\n", 2);
				TermLen = 2;
			}
			if (TermIdx != INDEX_NONE) {
				FUTF8ToTCHAR HttpChars(reinterpret_cast<const ANSICHAR*>(Peer->HandshakeAccum.GetData()), TermIdx);
				const FString HeaderBlock(HttpChars.Length(), HttpChars.Get());
				// Bytes pipelined after the upgrade request are the first WS frames - keep them.
				TArray<uint8> Leftover;
				const int32 LeftoverStart = TermIdx + TermLen;
				if (LeftoverStart < Peer->HandshakeAccum.Num()) {
					Leftover.Append(Peer->HandshakeAccum.GetData() + LeftoverStart, Peer->HandshakeAccum.Num() - LeftoverStart);
				}
				Peer->HandshakeAccum.Empty();
				if (!CompleteWsHandshakeForPeer(Peer, HeaderBlock)) {
					UE_LOG(LogTemp, Warning, TEXT("TacticsWS: handshake failed"));
					DisconnectPeerAtIndex(i, true);
					continue;
				}
				if (Leftover.Num() > 0) {
					Peer->RxScratch.Append(Leftover);
				}
			}
			continue;
		}

		if (Peer->Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
			bool bDisconnectPeer = false;
			bool bFirstPass = true;
			for (;;) {
				// Parse-first: bytes pipelined behind the handshake are already in RxScratch.
				if (!bFirstPass || Peer->RxScratch.Num() < 2) {
					uint32 Pending = 0;
					if (!Peer->TcpSocket->HasPendingData(Pending) || Pending < 1) {
						break;
					}
					uint8 Buf[16384]{};
					int32 Read = 0;
					if (!Peer->TcpSocket->Recv(Buf, sizeof(Buf), Read) || Read < 1) {
						break;
					}
					Peer->RxScratch.Append(Buf, Read);
				}
				bFirstPass = false;
				while (Peer->RxScratch.Num() >= 2) {
					uint8 Mask[4]{};
					const uint8 B0 = Peer->RxScratch[0];
					const uint8 B1 = Peer->RxScratch[1];
					const bool Fin = (B0 & 0x80) != 0;
					const uint8 Opcode = B0 & 0x0f;
					const bool Masked = (B1 & 0x80) != 0;
					uint64 PayloadLen7 = B1 & 0x7f;
					int32 Idx = 2;
					uint64 PayloadLen = PayloadLen7;
					if (PayloadLen7 == 126) {
						if (Peer->RxScratch.Num() < 4) {
							break;
						}
						PayloadLen = (static_cast<uint64>(Peer->RxScratch[2]) << 8) | Peer->RxScratch[3];
						Idx = 4;
					} else if (PayloadLen7 == 127) {
						if (Peer->RxScratch.Num() < 10) {
							break;
						}
						uint64 Len64 = 0;
						for (int B = 0; B < 8; ++B) {
							Len64 = (Len64 << 8) | Peer->RxScratch[2 + B];
						}
						if (Len64 > kMaxWsTextPayloadBytes) {
							UE_LOG(LogTemp, Warning, TEXT("TacticsWS: frame payload too large (%llu bytes)"),
								static_cast<unsigned long long>(Len64));
							Peer->RxScratch.Empty();
							bDisconnectPeer = true;
							break;
						}
						PayloadLen = Len64;
						Idx = 10;
					}
					if (Masked) {
						if (Peer->RxScratch.Num() < Idx + 4) {
							break;
						}
						FMemory::Memcpy(Mask, &Peer->RxScratch[Idx], 4);
						Idx += 4;
					}
					{
						const int64 Need = static_cast<int64>(Idx) + static_cast<int64>(PayloadLen);
						if (Need > Peer->RxScratch.Num() || Need > INT_MAX) {
							break;
						}
					}
					TArray<uint8> Payload;
					Payload.SetNumUninitialized(static_cast<int32>(PayloadLen));
					for (uint64 b = 0; b < PayloadLen; ++b) {
						uint8 Byte = Peer->RxScratch[Idx + static_cast<int32>(b)];
						if (Masked) {
							Byte ^= Mask[static_cast<int32>(b % 4)];
						}
						Payload[static_cast<int32>(b)] = Byte;
					}
					const int32 FrameTotal = Idx + static_cast<int32>(PayloadLen);
					Peer->RxScratch.RemoveAt(0, FrameTotal, EAllowShrinking::No);

					if (Opcode == 0x8) {
						DisconnectPeerAtIndex(i, true);
						bDisconnectPeer = true;
						break;
					}
					if (Opcode == 0x9) {
						if (!SendWsPong(*Peer, Payload)) {
							DisconnectPeerAtIndex(i, true);
							bDisconnectPeer = true;
							break;
						}
						continue;
					}
					if (Opcode == 0xA) {
						continue;
					}

					if (Opcode == 0x1) {
						if (Fin) {
							if (Peer->Utf8FragmentAssembly.Num() > 0) {
								Peer->Utf8FragmentAssembly.Empty();
							}
							if (Payload.Num() > 0) {
								FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(Payload.GetData()), Payload.Num());
								const FString Json(Convert.Length(), Convert.Get());
								ScheduleDispatchInboundJson(Json, true, Peer->SeatId, Peer);
							}
						} else {
							Peer->Utf8FragmentAssembly = MoveTemp(Payload);
						}
						continue;
					}
					if (Opcode == 0x0) {
						Peer->Utf8FragmentAssembly.Append(Payload);
						if (Fin) {
							if (Peer->Utf8FragmentAssembly.Num() > 0) {
								FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(Peer->Utf8FragmentAssembly.GetData()),
									Peer->Utf8FragmentAssembly.Num());
								const FString Json(Convert.Length(), Convert.Get());
								ScheduleDispatchInboundJson(Json, true, Peer->SeatId, Peer);
							}
							Peer->Utf8FragmentAssembly.Empty();
						}
						continue;
					}

					UE_LOG(LogTemp, Verbose, TEXT("TacticsWS: ignored opcode %u"), static_cast<unsigned>(Opcode));
				}
				if (bDisconnectPeer) {
					break;
				}
			}
			if (bDisconnectPeer) {
				continue;
			}
		}
	}
}

int32 UTacticsWebSocketSubsystem::GetRemoteWebSocketReadyPeerCount() const
{
	int32 n = 0;
	for (const TSharedPtr<FTacticsHostWsPeer>& P : HostPeers) {
		if (P.IsValid() && P->TcpSocket && P->Phase == ETacticsHostWsPeerPhase::WebSocketReady) {
			++n;
		}
	}
	return n;
}

void UTacticsWebSocketSubsystem::NotifyClientsDemoReset()
{
	HostBroadcastSnapshotToAllPeers(true);
}

void UTacticsWebSocketSubsystem::ClientSendCliLine(const FString& Line)
{
	if (Role != ETacticsWsRole::ClientRemote || !ClientWebSocket.IsValid()) {
		return;
	}
	UTacticsMatchSubsystem* Match = nullptr;
	if (UGameInstance* GI = GetGameInstance()) {
		Match = GI->GetSubsystem<UTacticsMatchSubsystem>();
	}
	ClientWebSocket->Send(BuildJsonCli(ClientRemoteSeatPlayerId, Line, RoomToken, Match, ClientAuthNonce, ++ClientCliCtr));
}

void UTacticsWebSocketSubsystem::SendClientDeck()
{
	if (!ClientWebSocket.IsValid()) {
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UTacticsMatchSubsystem* Match = GI ? GI->GetSubsystem<UTacticsMatchSubsystem>() : nullptr;
	if (!Match) {
		return;
	}
	const FString DeckJson = Match->GetActiveMatchDeckJson();
	if (DeckJson.IsEmpty()) {
		return;  // no chosen deck; host keeps the default for this seat
	}
	const FString Wire = BuildJsonClientDeck(
		DeckJson, ClientRemoteSeatPlayerId, RoomToken, Match, ClientAuthNonce, ++ClientCliCtr);
	ClientWebSocket->Send(Wire);
}

void UTacticsWebSocketSubsystem::SendClientResyncRequest()
{
	if (!ClientWebSocket.IsValid()) {
		return;
	}
	TSharedPtr<FJsonObject> RootResync = MakeShared<FJsonObject>();
	RootResync->SetStringField(TEXT("t"), TEXT("resync"));
	RootResync->SetNumberField(TEXT("v"), kWireProtocolVersion);
	FString ResyncWire;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResyncWire);
	FJsonSerializer::Serialize(RootResync.ToSharedRef(), Writer);
	ClientWebSocket->Send(ResyncWire);
}

bool UTacticsWebSocketSubsystem::TickClientResyncRetry(float /*DeltaTime*/)
{
	if (!bClientResyncPending || Role != ETacticsWsRole::ClientRemote || !ClientWebSocket.IsValid()) {
		if (ClientResyncTickerHandle.IsValid()) {
			FTSTicker::GetCoreTicker().RemoveTicker(ClientResyncTickerHandle);
			ClientResyncTickerHandle.Reset();
		}
		return false;
	}
	if (ClientResyncAttempts >= 8) {
		OnCliAckFromHost.Broadcast(TEXT("[sync] Resync failed after multiple attempts."));
		bClientResyncPending = false;
		if (ClientResyncTickerHandle.IsValid()) {
			FTSTicker::GetCoreTicker().RemoveTicker(ClientResyncTickerHandle);
			ClientResyncTickerHandle.Reset();
		}
		return false;
	}
	SendClientResyncRequest();
	++ClientResyncAttempts;
	return true;
}
