#include "TacticsMatchSubsystem.h"
#include "TacticsProjectContentReader.h"

#include "tactics/actions/actions.hpp"
#include "tactics/actions/move_resolution.hpp"
#include "tactics/apps/master_cli_dispatch.hpp"
#include "tactics/apps/sandbox_match.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/board/board.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/content/card_glossary.hpp"
#include "tactics/content/glossary_copy.hpp"
#include "tactics/content/project_content.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/actions/board_targeting.hpp"
#include "tactics/combat/ability_resolve_viz.hpp"
#include "tactics/entities/entity.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/common/types.hpp"
#include "tactics/core/passive_action_order.hpp"
#include "tactics/core.hpp"
#include "tactics/sync/match_sync.hpp"
#include "tactics/sync/match_auth.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/status_effect_catalog.hpp"

#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"

#include "TacticsMatchSubsystem_Internal.h"

static_assert(UTacticsMatchSubsystem::NetworkCheckpointCommandInterval == tactics::kNetworkCheckpointCommandInterval,
	"Keep UE NetworkCheckpointCommandInterval in sync with cpp_core match_sync.hpp");


bool UTacticsMatchSubsystem::ExecMasterCliLine(const FString& Line, FString& OutMessage, bool bNotifyNetworkAuthority)
{
	return ExecMasterCliLineAsPlayer(ControlledPlayer, Line, OutMessage, bNotifyNetworkAuthority);
}

bool UTacticsMatchSubsystem::ExecMasterCliLineAsPlayer(int32 PlayerId, const FString& Line, FString& OutMessage, bool bNotifyNetworkAuthority)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const int32 ViewingPlayerId = GetLocalViewingPlayerId();
	const size_t QueueSizeBefore = Game->phase_action_queue().size();
	const bool bCardPlayLine = IsCardPlayAuthorityLine(Line);
	const FTCHARToUTF8 Utf8(*Line);
	const std::string LineUtf8(Utf8.Get(), Utf8.Length());

	int Controlled = PlayerId;
	std::shared_ptr<tactics::Unit> Sel = Selected;
	std::ostringstream Oss;
	const bool Quit = tactics::dispatch_master_cli_line(*Game, Controlled, Sel, LineUtf8, Oss, {}, nullptr);
	ControlledPlayer = Controlled;
	Selected = Sel;

	OutMessage = StdToF(Oss.str());
	if (bCardPlayLine) {
		PresentOpponentPlayForAuthorityCommand(PlayerId, ViewingPlayerId, QueueSizeBefore);
	}
	BroadcastRefresh();
	if (bNotifyNetworkAuthority && Game) {
		const uint64_t CmdSeq = Game->record_authority_command(PlayerId, LineUtf8);
		OnNetworkAuthorityCommitted.Broadcast(PlayerId, Line, CmdSeq);
	}

	return Quit;
}

void UTacticsMatchSubsystem::ClearRemoteTcpSeatCliSession()
{
	RemoteSeatUnitSelections.clear();
}

void UTacticsMatchSubsystem::ClearRemoteSeatUnitSelection(int32 SeatPlayerId)
{
	RemoteSeatUnitSelections.erase(SeatPlayerId);
}

bool UTacticsMatchSubsystem::ExecRemoteTcpSeatCliLine(int32 SeatPlayerId, const FString& Line, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const int32 ViewingPlayerId = GetLocalViewingPlayerId();
	const size_t QueueSizeBefore = Game->phase_action_queue().size();
	const bool bCardPlayLine = IsCardPlayAuthorityLine(Line);
	const FTCHARToUTF8 Utf8(*Line);
	const std::string LineUtf8(Utf8.Get(), Utf8.Length());

	int Controlled = SeatPlayerId;
	std::shared_ptr<tactics::Unit>& Sel = RemoteSeatUnitSelections[SeatPlayerId];
	std::ostringstream Oss;
	const bool Quit = tactics::dispatch_master_cli_line(*Game, Controlled, Sel, LineUtf8, Oss, {});
	OutMessage = StdToF(Oss.str());
	if (bCardPlayLine) {
		PresentOpponentPlayForAuthorityCommand(SeatPlayerId, ViewingPlayerId, QueueSizeBefore);
	}
	RequestBroadcastRefresh();
	const uint64_t CmdSeq = Game->record_authority_command(SeatPlayerId, LineUtf8);
	OnNetworkAuthorityCommitted.Broadcast(SeatPlayerId, Line, CmdSeq);
	return Quit;
}

void UTacticsMatchSubsystem::MarkBoardVisualDirty(const ETacticsBoardVisualDirty Flags)
{
	PendingBoardVisualDirty |= static_cast<uint32>(Flags);
}

uint32 UTacticsMatchSubsystem::ConsumeBoardVisualDirtyMask()
{
	const uint32 mask = PendingBoardVisualDirty;
	PendingBoardVisualDirty = static_cast<uint32>(ETacticsBoardVisualDirty::None);
	// Avoid treating "no explicit dirty bits" as a full layout rebuild - that reprojects every
	// unit and snaps mid-glide poses after the first turn. Refresh units + highlights instead.
	return mask ? mask
				: (static_cast<uint32>(ETacticsBoardVisualDirty::Units)
					| static_cast<uint32>(ETacticsBoardVisualDirty::Highlights));
}

FString UTacticsMatchSubsystem::GetAbilityCatalogFingerprint() const
{
	return FString(UTF8_TO_TCHAR(tactics::ability_catalog_fingerprint_utf8().c_str()));
}

FString UTacticsMatchSubsystem::GetCardCatalogFingerprint() const
{
	return FString(UTF8_TO_TCHAR(tactics::card_catalog_fingerprint_utf8().c_str()));
}

FString UTacticsMatchSubsystem::GenerateCliAuthNonce() const
{
	return FString(UTF8_TO_TCHAR(tactics::generate_auth_nonce_hex().c_str()));
}

FString UTacticsMatchSubsystem::ComputeCliSig(const int32 SeatId, const FString& Line, const FString& RoomToken,
	const FString& AuthNonce, const uint64 Ctr) const
{
	if (RoomToken.IsEmpty() || Line.IsEmpty() || SeatId <= 0) {
		return FString();
	}
	const FTCHARToUTF8 LineUtf8(*Line);
	const std::string digest = tactics::compute_cli_auth_digest_utf8(
		std::string(TCHAR_TO_UTF8(*RoomToken)),
		std::string(TCHAR_TO_UTF8(*AuthNonce)),
		static_cast<int>(SeatId),
		Ctr,
		std::string(LineUtf8.Get(), LineUtf8.Length()));
	return FString(UTF8_TO_TCHAR(digest.c_str()));
}

bool UTacticsMatchSubsystem::VerifyCliSig(const int32 SeatId, const FString& Line, const FString& Sig, const FString& RoomToken,
	const FString& AuthNonce, const uint64 Ctr) const
{
	if (RoomToken.IsEmpty()) {
		return Sig.IsEmpty();
	}
	if (Sig.IsEmpty() || Line.IsEmpty() || SeatId <= 0) {
		return false;
	}
	const FTCHARToUTF8 LineUtf8(*Line);
	return tactics::verify_cli_auth_digest_utf8(
		std::string(TCHAR_TO_UTF8(*RoomToken)),
		std::string(TCHAR_TO_UTF8(*AuthNonce)),
		static_cast<int>(SeatId),
		Ctr,
		std::string(LineUtf8.Get(), LineUtf8.Length()),
		std::string(TCHAR_TO_UTF8(*Sig)));
}

uint64 UTacticsMatchSubsystem::GetMatchAuthorityCommandSeq() const
{
	return Game ? static_cast<uint64>(Game->match_command_seq()) : 0ull;
}

void UTacticsMatchSubsystem::BumpNetworkSnapSeqForCheckpoint()
{
	if (Game) {
		Game->bump_network_snap_seq();
	}
}

FString UTacticsMatchSubsystem::ExportNetworkWireSnapshotJson() const
{
	if (!Game) {
		return FString();
	}
	const uint64_t snap_seq = Game->network_snap_seq();
	const std::string inner = Game->build_match_snapshot_utf8();
	const std::string wire = tactics::wrap_match_snapshot_for_network_utf8(inner, snap_seq);
	return FString(UTF8_TO_TCHAR(wire.c_str()));
}

FString UTacticsMatchSubsystem::WrapNetworkCommandWireJson(const int32 SeatPlayerId, const FString& Line, const uint64 CommandSeq) const
{
	if (Line.IsEmpty() || CommandSeq == 0) {
		return FString();
	}
	const FTCHARToUTF8 LineUtf8(*Line);
	const std::string wire = tactics::wrap_match_command_for_network_utf8(
		CommandSeq, SeatPlayerId, std::string(LineUtf8.Get(), LineUtf8.Length()));
	return FString(UTF8_TO_TCHAR(wire.c_str()));
}

bool UTacticsMatchSubsystem::ApplyNetworkWireSnapshotJson(const FString& WireJson, FString& ErrOut)
{
	const FTCHARToUTF8 Utf8(*WireJson);
	const std::string wire(Utf8.Get(), Utf8.Length());
	std::string inner;
	std::optional<uint64_t> wire_seq;
	std::string unwrap_err;
	if (!tactics::unwrap_snap_wire_utf8_for_replace(wire, inner, wire_seq, unwrap_err)) {
		ErrOut = FString(UTF8_TO_TCHAR(unwrap_err.c_str()));
		return false;
	}
	if (wire_seq.has_value() && *wire_seq <= LastAppliedNetworkSnapSeq) {
		return true;
	}

	try {
		const nlohmann::json payload = nlohmann::json::parse(inner);
		const int bw = payload.at("board_width").get<int>();
		const int bh = payload.at("board_height").get<int>();
		if (bw == 8 && bh == 8) {
			ErrOut = TEXT("Refusing outdated 8x8 snapshot. Rebuild the editor, restart PIE, and use Reset Demo Match.");
			return false;
		}
		if (bw == tactics::kStandardBoardWidth && bh == tactics::kStandardBoardHeight) {
			if (payload.contains("board_cell_count") && payload.at("board_cell_count").get<int>() != 80) {
				ErrOut = TEXT(
					"Refusing legacy solid 8x12 snapshot (wrong tile count). Host: Reset Demo Match after a full editor rebuild.");
				return false;
			}
			if (!payload.contains("board_layout_id") ||
				payload.at("board_layout_id").get<std::string>() != tactics::kDefaultBoardLayoutId) {
				ErrOut = TEXT(
					"Refusing outdated 8x12 snapshot (missing or wrong layout id). Host: Reset Demo Match after a full editor rebuild.");
				return false;
			}
		}
	} catch (const std::exception& ex) {
		ErrOut = FString(UTF8_TO_TCHAR(ex.what()));
		return false;
	}

	bAutoFollowActiveSeat = false;
	FixedControlledPlayer = ControlledPlayer > 0 ? std::optional<int>{ControlledPlayer} : std::nullopt;
	std::string selection_entity_id;
	if (Selected) {
		selection_entity_id = Selected->entity_id;
	}
	Selected.reset();
	BoardTargetPreviewKind.reset();
	BoardTargetEnemyCells.Empty();
	BoardTargetOtherCells.Empty();
	RemoteSeatUnitSelections.clear();
	ClearPendingCliWorldCell();

	std::string err;
	std::unique_ptr<tactics::GameState> slot;
	if (Game) {
		slot.reset(Game.Release());
	}
	const auto try_rebind_selection = [this, &selection_entity_id]() {
		if (selection_entity_id.empty() || !Game) {
			return;
		}
		const auto It = Game->board.all_entities_map.find(selection_entity_id);
		if (It == Game->board.all_entities_map.end() || !It->second) {
			return;
		}
		if (auto U = std::dynamic_pointer_cast<tactics::Unit>(It->second)) {
			Selected = std::move(U);
		}
	};
	if (!tactics::load_game_from_snapshot_utf8(slot, inner, err)) {
		Game.Reset(slot.release());
		ErrOut = FString(UTF8_TO_TCHAR(err.c_str()));
		try_rebind_selection();
		BroadcastRefresh();
		return false;
	}
	Game.Reset(slot.release());
	Game->repair_standard_duel_board_geometry_if_needed();
	if (wire_seq.has_value()) {
		LastAppliedNetworkSnapSeq = *wire_seq;
		LastAppliedSnapshotBaseSeq = *wire_seq;
	} else if (Game) {
		LastAppliedNetworkSnapSeq = Game->network_snap_seq();
		LastAppliedSnapshotBaseSeq = LastAppliedNetworkSnapSeq;
	}
	LastAppliedSnapshotInnerUtf8 = inner;
	if (Game) {
		LastAppliedCommandSeq = Game->match_command_seq();
	}
	try_rebind_selection();
	ResetOpponentPlayPresentationState(true);
	RequestBroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::ApplyNetworkWireSnapshotDeltaJson(const FString& WireJson, FString& ErrOut)
{
	const FTCHARToUTF8 Utf8(*WireJson);
	const std::string wire(Utf8.Get(), Utf8.Length());
	uint64_t base_seq = 0;
	uint64_t snap_seq = 0;
	std::string delta_json;
	std::string err;
	if (!tactics::parse_match_snapshot_delta_wire_utf8(wire, base_seq, snap_seq, delta_json, err)) {
		ErrOut = FString(UTF8_TO_TCHAR(err.c_str()));
		return false;
	}
	if (snap_seq <= LastAppliedNetworkSnapSeq) {
		return true;
	}
	if (!LastAppliedSnapshotBaseSeq.has_value() || *LastAppliedSnapshotBaseSeq != base_seq || LastAppliedSnapshotInnerUtf8.empty()) {
		ErrOut = FString::Printf(TEXT("Delta base mismatch (have %llu, got %llu). Request resync."),
			static_cast<unsigned long long>(LastAppliedSnapshotBaseSeq.value_or(0)),
			static_cast<unsigned long long>(base_seq));
		return false;
	}
	try {
		const nlohmann::json base = nlohmann::json::parse(LastAppliedSnapshotInnerUtf8);
		const nlohmann::json delta = nlohmann::json::parse(delta_json);
		const nlohmann::json patched = base.patch(delta);
		const std::string patched_inner = patched.dump();
		const std::string patched_wire = tactics::wrap_match_snapshot_for_network_utf8(patched_inner, snap_seq);
		if (patched_wire.empty()) {
			ErrOut = TEXT("Failed to wrap patched delta snapshot.");
			return false;
		}
		return ApplyNetworkWireSnapshotJson(FString(UTF8_TO_TCHAR(patched_wire.c_str())), ErrOut);
	} catch (const std::exception& ex) {
		ErrOut = FString(UTF8_TO_TCHAR(ex.what()));
		return false;
	}
}

bool UTacticsMatchSubsystem::ApplyNetworkCommandWireJson(const FString& WireJson, FString& ErrOut)
{
	const FTCHARToUTF8 Utf8(*WireJson);
	const std::string wire(Utf8.Get(), Utf8.Length());
	uint64_t seq = 0;
	int seat = 0;
	std::string line_utf8;
	std::string err;
	if (!tactics::parse_match_command_wire_utf8(wire, seq, seat, line_utf8, err)) {
		ErrOut = FString(UTF8_TO_TCHAR(err.c_str()));
		return false;
	}
	if (!Game) {
		ErrOut = TEXT("No match.");
		return false;
	}
	if (seq <= LastAppliedCommandSeq) {
		return true;
	}
	if (seq != LastAppliedCommandSeq + 1) {
		ErrOut = FString::Printf(TEXT("Command sequence gap (have %llu, got %llu). Request resync."),
			static_cast<unsigned long long>(LastAppliedCommandSeq),
			static_cast<unsigned long long>(seq));
		return false;
	}
	const FString Line = FString(UTF8_TO_TCHAR(line_utf8.c_str()));
	FString Out;
	ExecMasterCliLineAsPlayer(seat, Line, Out, false);
	LastAppliedCommandSeq = seq;
	BroadcastRefresh();
	return true;
}


