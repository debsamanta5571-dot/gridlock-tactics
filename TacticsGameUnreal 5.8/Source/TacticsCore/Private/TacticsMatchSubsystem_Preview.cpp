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


void UTacticsMatchSubsystem::RebuildActionQueueHoverHighlights()
{
	ActionQueueHoverSourceCells.Empty();
	ActionQueueHoverTargetCells.Empty();
	ActionQueueHoverAoECells.Empty();
	if (!Game) {
		return;
	}
	const int32 PreviewIndex = PinnedActionQueueIndex >= 0 ? PinnedActionQueueIndex : HoveredActionQueueIndex;
	if (PreviewIndex < 0) {
		return;
	}
	const auto& Queue = Game->phase_action_queue();
	if (PreviewIndex >= static_cast<int32>(Queue.size())) {
		return;
	}
	const tactics::GameState::AttackPhaseEntry& Entry = Queue[static_cast<size_t>(PreviewIndex)];
	if (Entry.is_attack) {
		AppendEntityFootprintById(*Game, Entry.attack.attacker_id, ActionQueueHoverSourceCells);
		if (const auto Target = Game->board.entity_at(Entry.attack.target_x, Entry.attack.target_y)) {
			AppendEntityFootprintCells(*Target, ActionQueueHoverTargetCells);
		} else if (Game->board.get_square(Entry.attack.target_x, Entry.attack.target_y)) {
			ActionQueueHoverTargetCells.Add(FIntPoint(Entry.attack.target_x, Entry.attack.target_y));
		}
		return;
	}

	const tactics::StackItem& Item = Entry.spell_item;
	if (!Item.source_entity_id.empty()
		&& (Item.source_type == "focus_spell" || Item.source_type == "ability")) {
		AppendEntityFootprintById(*Game, Item.source_entity_id, ActionQueueHoverSourceCells);
	}

	TSet<FIntPoint> TargetFootprints;
	if (!Item.multicast_cast_id.empty()) {
		for (const tactics::GameState::AttackPhaseEntry& CopyEntry : Queue) {
			if (CopyEntry.is_attack || CopyEntry.spell_item.multicast_cast_id != Item.multicast_cast_id) {
				continue;
			}
			AppendStackItemTargetFootprints(*Game, CopyEntry.spell_item, TargetFootprints);
		}
	} else {
		const tactics::AbilityResolveVizPreview VizPreview = tactics::build_ability_resolve_viz_preview(*Game, Item);
		if (!VizPreview.blast_cells.empty()) {
			for (const tactics::AbilityResolveVizBlastCell& Blast : VizPreview.blast_cells) {
				const FIntPoint Cell(Blast.grid_x, Blast.grid_y);
				ActionQueueHoverAoECells.Add(Cell);
				if (const auto EntityPtr = Game->board.entity_at(Blast.grid_x, Blast.grid_y)) {
					if (EntityPtr->current_health > 0) {
						AppendEntityFootprintCells(*EntityPtr, TargetFootprints);
					}
				}
			}
		} else {
			AppendStackItemTargetFootprints(*Game, Item, TargetFootprints);
		}
	}
	ActionQueueHoverTargetCells = MoveTemp(TargetFootprints);
}

void UTacticsMatchSubsystem::SetActionQueueHoverIndex(const int32 QueueIndex)
{
	HoveredActionQueueIndex = QueueIndex;
	RebuildActionQueueHoverHighlights();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights);
	// Target-preview only - a full OnBoardChanged rebuilds the batch queue rows and drops hover.
	OnTargetPreviewChanged.Broadcast();
}

void UTacticsMatchSubsystem::ClearActionQueueHoverPreview()
{
	HoveredActionQueueIndex = -1;
	RebuildActionQueueHoverHighlights();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights);
	OnTargetPreviewChanged.Broadcast();
}

void UTacticsMatchSubsystem::ToggleActionQueuePinIndex(const int32 QueueIndex)
{
	if (QueueIndex < 0) {
		return;
	}
	PinnedActionQueueIndex = PinnedActionQueueIndex == QueueIndex ? -1 : QueueIndex;
	RebuildActionQueueHoverHighlights();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights);
	OnTargetPreviewChanged.Broadcast();
}

void UTacticsMatchSubsystem::ClearActionQueuePinPreview()
{
	PinnedActionQueueIndex = -1;
	RebuildActionQueueHoverHighlights();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights);
	OnTargetPreviewChanged.Broadcast();
}

bool UTacticsMatchSubsystem::IsActionQueuePreviewPinned() const
{
	return PinnedActionQueueIndex >= 0;
}

int32 UTacticsMatchSubsystem::GetPinnedActionQueueIndex() const
{
	return PinnedActionQueueIndex;
}

void UTacticsMatchSubsystem::SyncActionQueueHoverAfterQueueChange()
{
	if (!Game) {
		HoveredActionQueueIndex = -1;
		PinnedActionQueueIndex = -1;
		return;
	}
	const int32 QueueSize = static_cast<int32>(Game->phase_action_queue().size());
	if (HoveredActionQueueIndex >= QueueSize) {
		HoveredActionQueueIndex = -1;
	}
	if (PinnedActionQueueIndex >= QueueSize) {
		PinnedActionQueueIndex = -1;
	}
	RebuildActionQueueHoverHighlights();
}

bool UTacticsMatchSubsystem::TryBuildHoverOverlayForQueueIndex(const int32 QueueIndex,
	FTacticsActionQueueHoverOverlayUi& OutPreview) const
{
	OutPreview = {};
	if (!Game || QueueIndex < 0) {
		return false;
	}
	const auto& Queue = Game->phase_action_queue();
	if (QueueIndex >= static_cast<int32>(Queue.size())) {
		return false;
	}
	return BuildHoverOverlayForQueueEntry(*Game, Queue[static_cast<size_t>(QueueIndex)], Queue, bShowAdvancedCardText,
		OutPreview);
}

bool UTacticsMatchSubsystem::TryBuildHoverOverlayForStackItem(const tactics::StackItem& Item,
	FTacticsActionQueueHoverOverlayUi& OutPreview) const
{
	if (!Game) {
		return false;
	}
	return BuildHoverOverlayForStackItem(*Game, Item, Game->phase_action_queue(), bShowAdvancedCardText, OutPreview);
}

bool UTacticsMatchSubsystem::TryGetActionQueueHoverOverlay(FTacticsActionQueueHoverOverlayUi& OutPreview) const
{
	const int32 PreviewIndex = PinnedActionQueueIndex >= 0 ? PinnedActionQueueIndex : HoveredActionQueueIndex;
	return TryBuildHoverOverlayForQueueIndex(PreviewIndex, OutPreview);
}

void UTacticsMatchSubsystem::ResetOpponentPlayPresentationState(const bool bSeedFingerprintsFromQueue)
{
	OpponentPlayPresentationQueue.Reset();
	ActiveOpponentPlayPresentation.Reset();
	ActiveOpponentPlayPresentationUntilTime = 0.0;
	SeenOpponentPlayFingerprints.Reset();
	if (OpponentPlayPresentationTickerHandle.IsValid()) {
		FTSTicker::GetCoreTicker().RemoveTicker(OpponentPlayPresentationTickerHandle);
		OpponentPlayPresentationTickerHandle.Reset();
	}
	if (!bSeedFingerprintsFromQueue || !Game) {
		return;
	}
	for (const tactics::GameState::AttackPhaseEntry& Entry : Game->phase_action_queue()) {
		SeenOpponentPlayFingerprints.Add(BuildPhaseQueueEntryFingerprint(Entry));
	}
}

int32 UTacticsMatchSubsystem::GetLocalViewingPlayerId() const
{
	if (!bAutoFollowActiveSeat && FixedControlledPlayer && *FixedControlledPlayer > 0) {
		return *FixedControlledPlayer;
	}
	return ControlledPlayer > 0 ? ControlledPlayer : 1;
}

namespace {

constexpr double kOpponentPlayPresentationDurationSec = 2.0;

}  // namespace

void UTacticsMatchSubsystem::EnqueueOpponentPlayPresentation(FTacticsActionQueueHoverOverlayUi Overlay, const int32 PlayerId)
{
	if (!Overlay.bShowPreview || PlayerId <= 0) {
		return;
	}
	FOpponentPlayPresentationSlot Slot;
	Slot.Overlay = MoveTemp(Overlay);
	Slot.Banner = FString::Printf(TEXT("Player %d played"), PlayerId);
	OpponentPlayPresentationQueue.Add(MoveTemp(Slot));
	TryStartNextOpponentPlayPresentation();
}

void UTacticsMatchSubsystem::TryStartNextOpponentPlayPresentation()
{
	if (ActiveOpponentPlayPresentation.IsSet() || OpponentPlayPresentationQueue.IsEmpty()) {
		return;
	}
	ActiveOpponentPlayPresentation = OpponentPlayPresentationQueue[0];
	OpponentPlayPresentationQueue.RemoveAt(0);
	ActiveOpponentPlayPresentationUntilTime = FPlatformTime::Seconds() + kOpponentPlayPresentationDurationSec;
	EnsureOpponentPlayPresentationTicker();
}

void UTacticsMatchSubsystem::AdvanceOpponentPlayPresentation()
{
	ActiveOpponentPlayPresentation.Reset();
	ActiveOpponentPlayPresentationUntilTime = 0.0;
	TryStartNextOpponentPlayPresentation();
	if (!ActiveOpponentPlayPresentation.IsSet() && OpponentPlayPresentationQueue.IsEmpty()
		&& OpponentPlayPresentationTickerHandle.IsValid()) {
		FTSTicker::GetCoreTicker().RemoveTicker(OpponentPlayPresentationTickerHandle);
		OpponentPlayPresentationTickerHandle.Reset();
	}
}

void UTacticsMatchSubsystem::EnsureOpponentPlayPresentationTicker()
{
	if (OpponentPlayPresentationTickerHandle.IsValid()) {
		return;
	}
	OpponentPlayPresentationTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticsMatchSubsystem::TickOpponentPlayPresentation), 0.05f);
}

bool UTacticsMatchSubsystem::TickOpponentPlayPresentation(float /*DeltaTime*/)
{
	if (!ActiveOpponentPlayPresentation.IsSet()) {
		return OpponentPlayPresentationQueue.Num() > 0;
	}
	if (FPlatformTime::Seconds() >= ActiveOpponentPlayPresentationUntilTime) {
		AdvanceOpponentPlayPresentation();
		MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights);
		OnBoardChanged.Broadcast();
	}
	return ActiveOpponentPlayPresentation.IsSet() || OpponentPlayPresentationQueue.Num() > 0;
}

void UTacticsMatchSubsystem::PresentOpponentPlayForAuthorityCommand(const int32 ActingPlayerId,
	const int32 ViewingPlayerId, const size_t QueueSizeBefore)
{
	if (!Game || ActingPlayerId <= 0 || ActingPlayerId == ViewingPlayerId) {
		return;
	}

	const auto& Queue = Game->phase_action_queue();
	const auto PresentQueueEntry = [&](const tactics::GameState::AttackPhaseEntry& Entry) {
		const FString Fingerprint = BuildPhaseQueueEntryFingerprint(Entry);
		if (SeenOpponentPlayFingerprints.Contains(Fingerprint)) {
			return;
		}
		SeenOpponentPlayFingerprints.Add(Fingerprint);
		FTacticsActionQueueHoverOverlayUi Overlay;
		if (BuildHoverOverlayForQueueEntry(*Game, Entry, Queue, bShowAdvancedCardText, Overlay)) {
			EnqueueOpponentPlayPresentation(MoveTemp(Overlay), ActingPlayerId);
		}
	};

	if (Queue.size() > QueueSizeBefore) {
		for (size_t i = QueueSizeBefore; i < Queue.size(); ++i) {
			PresentQueueEntry(Queue[i]);
		}
		return;
	}

	if (tactics::StackItem PausedSpell;
		Game->try_get_pending_combat_visualization_spell(PausedSpell)
		&& PausedSpell.controller_id == ActingPlayerId) {
		const FString Fingerprint = BuildPausedBlazingFingerprint(PausedSpell);
		if (SeenOpponentPlayFingerprints.Contains(Fingerprint)) {
			return;
		}
		SeenOpponentPlayFingerprints.Add(Fingerprint);
		FTacticsActionQueueHoverOverlayUi Overlay;
		if (BuildHoverOverlayForStackItem(*Game, PausedSpell, Queue, bShowAdvancedCardText, Overlay)) {
			EnqueueOpponentPlayPresentation(MoveTemp(Overlay), ActingPlayerId);
		}
	}
}

bool UTacticsMatchSubsystem::TryGetActiveOpponentPlayPresentation(FTacticsActionQueueHoverOverlayUi& OutPreview,
	FString& OutBanner) const
{
	if (!ActiveOpponentPlayPresentation.IsSet()) {
		return false;
	}
	OutPreview = ActiveOpponentPlayPresentation->Overlay;
	OutBanner = ActiveOpponentPlayPresentation->Banner;
	return OutPreview.bShowPreview;
}

bool UTacticsMatchSubsystem::IsAbilityBoardTargetPreviewActive() const
{
	return !BoardTargetPreviewAbilityKey.IsEmpty();
}

bool UTacticsMatchSubsystem::IsBoardTargetAoEHoverPreviewActive() const
{
	if (!BoardTargetPreviewAbilityKey.IsEmpty() && Selected && IsSelectedUnitControlled()) {
		tactics::AbilitySpec Ability;
		if (try_get_selected_ability_spec(*Selected, TCHAR_TO_UTF8(*BoardTargetPreviewAbilityKey), Ability)) {
			return tactics::effect_supports_aoe_blast_preview(Ability.effect_key);
		}
	}
	if (!BoardTargetPreviewSpellEffectKey.IsEmpty() && !bBoardTargetPreviewSelectingFocusCaster) {
		return tactics::effect_supports_aoe_blast_preview(TCHAR_TO_UTF8(*BoardTargetPreviewSpellEffectKey));
	}
	return false;
}

bool UTacticsMatchSubsystem::IsDirectionalAreaBlastPreviewActive() const
{
	return IsBoardTargetAoEHoverPreviewActive();
}

ETacticsAbilityVisualGroup UTacticsMatchSubsystem::ResolveAbilityVisualGroup(const FString& AbilityKey) const
{
	bool bRequiresStack = false;
	if (TryGetSelectedAbilityRequiresStackTarget(AbilityKey, bRequiresStack) && bRequiresStack) {
		return ETacticsAbilityVisualGroup::StackTarget;
	}
	if (!Selected) {
		return ETacticsAbilityVisualGroup::Instant;
	}
	tactics::AbilitySpec Ability;
	if (!try_get_selected_ability_spec(*Selected, TCHAR_TO_UTF8(*AbilityKey), Ability)) {
		return ETacticsAbilityVisualGroup::Instant;
	}
	if (tactics::effect_uses_directional_aim(Ability.effect_key)) {
		return ETacticsAbilityVisualGroup::DirectionalAim;
	}
	if (tactics::effect_key_targets_empty_cell(Ability.effect_key)) {
		return ETacticsAbilityVisualGroup::EmptyCellTarget;
	}
	if (tactics::effect_key_uses_lobbed_aoe_center(Ability.effect_key)) {
		return ETacticsAbilityVisualGroup::LobbedAoeCenter;
	}
	if (tactics::effect_key_uses_push_direction_aim(Ability.effect_key)) {
		return ETacticsAbilityVisualGroup::PushDirection;
	}
	if (tactics::ability_requires_board_target(Ability)) {
		return ETacticsAbilityVisualGroup::BoardEntityTarget;
	}
	return ETacticsAbilityVisualGroup::Instant;
}

void UTacticsMatchSubsystem::NotifyAbilityCastFlash(const bool bSuccess)
{
	if (!Selected || Selected->entity_id.empty()) {
		return;
	}
	AbilityCastFlashEntityId = UTF8_TO_TCHAR(Selected->entity_id.c_str());
	bAbilityCastFlashSuccess = bSuccess;
	AbilityCastFlashStartTime = FPlatformTime::Seconds();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Units);
	RequestBroadcastRefresh();
}

void UTacticsMatchSubsystem::ClearAbilityTargetPreviewArtifacts()
{
	BoardTargetPreviewAbilityKey.Empty();
	BoardTargetAoEBlastCells.Empty();
}

void UTacticsMatchSubsystem::PruneExpiredResolveFlash()
{
	if (!ActiveResolveFlash.IsSet()) {
		return;
	}
	const double Elapsed = FPlatformTime::Seconds() - ActiveResolveFlash->StartTime;
	if (Elapsed >= static_cast<double>(FTacticsBoardResolveFlash::DurationSec)) {
		ActiveResolveFlash.Reset();
	}
}

bool UTacticsMatchSubsystem::HasPendingAbilityResolveVisualization() const
{
	if (!Game) {
		return false;
	}
	return Game->is_combat_visualization_paused()
		&& Game->combat_viz_pause_kind() == tactics::GameState::CombatVizPauseKind::Ability;
}

bool UTacticsMatchSubsystem::HasActiveAbilityResolvePresentation() const
{
	const double Now = FPlatformTime::Seconds();
	if (!ActiveAbilityDamagePopups.IsEmpty() || !PendingAbilityDamagePopups.IsEmpty()) {
		return true;
	}
	if (!ActiveAbilityResolvePresentation.IsSet()) {
		return false;
	}
	const FTacticsAbilityResolvePresentation& Pres = *ActiveAbilityResolvePresentation;
	return Pres.IsInWavePhase(Now);
}

bool UTacticsMatchSubsystem::TryBeginAbilityResolvePresentation()
{
	if (!Game || ActiveAbilityResolvePresentation.IsSet()) {
		return false;
	}
	tactics::AbilityResolveVizPreview Preview;
	if (!Game->try_get_pending_ability_resolve_viz(Preview)) {
		return false;
	}
	FTacticsAbilityResolvePresentation Pres;
	Pres.StartTime = FPlatformTime::Seconds();
	Pres.bDirectionalWave = Preview.directional_wave;
	Pres.bFriendlyEffect = Preview.friendly_effect;
	for (const tactics::AbilityResolveVizBlastCell& Blast : Preview.blast_cells) {
		FTacticsAbilityWaveCell WaveCell;
		WaveCell.Cell = FIntPoint(Blast.grid_x, Blast.grid_y);
		WaveCell.WaveRing = Blast.wave_depth;
		Pres.MaxWaveRing = FMath::Max(Pres.MaxWaveRing, Blast.wave_depth);
		Pres.WaveCells.Add(WaveCell);
	}
	if (Pres.WaveCells.IsEmpty()) {
		return false;
	}
	ActiveAbilityResolvePresentation = MoveTemp(Pres);
	FString ApplyOut;
	if (!ApplyPausedAbilityVisualizationStep(ApplyOut)) {
		ActiveAbilityResolvePresentation.Reset();
		return false;
	}
	EnsureAbilityResolvePresentationTicker();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights | ETacticsBoardVisualDirty::Units);
	OnBoardChanged.Broadcast();
	return true;
}

bool UTacticsMatchSubsystem::ApplyPausedAbilityVisualizationStep(FString& OutMessage)
{
	if (!Game || !ActiveAbilityResolvePresentation.IsSet()) {
		OutMessage = TEXT("No active ability presentation.");
		return false;
	}
	FTacticsAbilityResolvePresentation& Pres = *ActiveAbilityResolvePresentation;
	if (Pres.bAbilityApplied) {
		return true;
	}
	const auto Result = Game->apply_paused_ability_visualization_step();
	OutMessage = StdToF(Result.message);
	if (!Result.ok) {
		return false;
	}
	Pres.bAbilityApplied = true;
	DrainAbilityDamagePopupEvents();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights | ETacticsBoardVisualDirty::Units);
	OnBoardChanged.Broadcast();
	return true;
}

void UTacticsMatchSubsystem::DrainAbilityDamagePopupEvents()
{
	if (!Game) {
		return;
	}
	const double Now = FPlatformTime::Seconds();
	tactics::AbilityResolveVizHit Hit;
	int32 NumDrained = 0;
	while (Game->try_consume_ability_damage_popup_event(Hit)) {
		FTacticsAbilityDamagePopup Popup;
		Popup.EntityId = UTF8_TO_TCHAR(Hit.entity_id.c_str());
		Popup.Cell = FIntPoint(Hit.grid_x, Hit.grid_y);
		Popup.Amount = Hit.amount;
		Popup.bHeal = Hit.is_heal;
		if (!Hit.event_label.empty()) {
			Popup.EventLabel = UTF8_TO_TCHAR(Hit.event_label.c_str());
		}
		Popup.StartTime = Now;
		ActiveAbilityDamagePopups.Add(MoveTemp(Popup));
		++NumDrained;
	}
	if (NumDrained > 0) {
		EnsureAbilityDamagePopupTicker();
		OnAbilityDamagePopupsChanged.Broadcast();
		MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights | ETacticsBoardVisualDirty::Units);
		OnTargetPreviewChanged.Broadcast();
	} else if (!ActiveAbilityDamagePopups.IsEmpty()) {
		EnsureAbilityDamagePopupTicker();
	}
}

int32 UTacticsMatchSubsystem::FindWaveRingForPopupCell(const FIntPoint& Cell, const FString& EntityId) const
{
	if (!ActiveAbilityResolvePresentation.IsSet()) {
		return INDEX_NONE;
	}
	const FTacticsAbilityResolvePresentation& Pres = *ActiveAbilityResolvePresentation;
	int32 Ring = Pres.FindWaveRingForCell(Cell);
	if (!EntityId.IsEmpty()) {
		TArray<FTacticsBoardUnitPose> Poses;
		GatherBoardUnitPoses(Poses);
		for (const FTacticsBoardUnitPose& Pose : Poses) {
			if (Pose.EntityId != EntityId) {
				continue;
			}
			const int32 SpanX = FMath::Max(1, Pose.FootprintSpanX);
			const int32 SpanY = FMath::Max(1, Pose.FootprintSpanY);
			const int32 MinX = FMath::FloorToInt(Pose.GridCenterX - static_cast<float>(SpanX - 1) * 0.5f);
			const int32 MinY = FMath::FloorToInt(Pose.GridCenterY - static_cast<float>(SpanY - 1) * 0.5f);
			for (int32 Dy = 0; Dy < SpanY; ++Dy) {
				for (int32 Dx = 0; Dx < SpanX; ++Dx) {
					const FIntPoint FootprintCell(MinX + Dx, MinY + Dy);
					const int32 FootRing = Pres.FindWaveRingForCell(FootprintCell);
					if (FootRing == INDEX_NONE) {
						continue;
					}
					Ring = Ring == INDEX_NONE ? FootRing : FMath::Min(Ring, FootRing);
				}
			}
			break;
		}
	}
	return Ring;
}

void UTacticsMatchSubsystem::ReleasePendingDamagePopupsSynchronizedToWave(const double Now)
{
	if (!ActiveAbilityResolvePresentation.IsSet() || PendingAbilityDamagePopups.IsEmpty()) {
		return;
	}
	const FTacticsAbilityResolvePresentation& Pres = *ActiveAbilityResolvePresentation;
	const double BaseTime = Pres.StartTime;
	bool bReleasedAny = false;
	for (int32 i = PendingAbilityDamagePopups.Num() - 1; i >= 0; --i) {
		FTacticsAbilityDamagePopup& Popup = PendingAbilityDamagePopups[i];
		const int32 Ring = FMath::Max(0, Popup.WaveRing);
		const double RingStart = static_cast<double>(Ring) * static_cast<double>(FTacticsAbilityResolvePresentation::RowStaggerSec);
		if ((Now - BaseTime) + KINDA_SMALL_NUMBER < RingStart) {
			continue;
		}
		Popup.StartTime = Now;
		ActiveAbilityDamagePopups.Add(MoveTemp(Popup));
		PendingAbilityDamagePopups.RemoveAtSwap(i);
		bReleasedAny = true;
	}
	if (bReleasedAny) {
		EnsureAbilityDamagePopupTicker();
		OnAbilityDamagePopupsChanged.Broadcast();
		MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights | ETacticsBoardVisualDirty::Units);
		OnTargetPreviewChanged.Broadcast();
	}
}

void UTacticsMatchSubsystem::PruneExpiredAbilityDamagePopups()
{
	const double Now = FPlatformTime::Seconds();
	ActiveAbilityDamagePopups.RemoveAll([&Now](const FTacticsAbilityDamagePopup& Popup) {
		return Popup.IsExpired(Now);
	});
}

void UTacticsMatchSubsystem::EnsureAbilityDamagePopupTicker()
{
	if (AbilityDamagePopupTickerHandle.IsValid()) {
		return;
	}
	AbilityDamagePopupTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticsMatchSubsystem::TickAbilityDamagePopups), 0.033f);
}

bool UTacticsMatchSubsystem::TickAbilityDamagePopups(float /*DeltaTime*/)
{
	PruneExpiredAbilityDamagePopups();
	if (ActiveAbilityDamagePopups.IsEmpty()) {
		if (AbilityDamagePopupTickerHandle.IsValid()) {
			FTSTicker::GetCoreTicker().RemoveTicker(AbilityDamagePopupTickerHandle);
			AbilityDamagePopupTickerHandle.Reset();
		}
		return false;
	}
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights | ETacticsBoardVisualDirty::Units);
	OnAbilityDamagePopupsChanged.Broadcast();
	OnTargetPreviewChanged.Broadcast();
	return true;
}

bool UTacticsMatchSubsystem::TryGetAbilityDamagePopupAtWorld(const int WorldX, const int WorldY, bool& OutShowPopup, FString& OutText,
	float& OutAlpha, float& OutScale, float& OutOffsetY, FLinearColor& OutColor) const
{
	OutShowPopup = false;
	OutText.Empty();
	OutAlpha = 0.f;
	OutScale = 0.f;
	OutOffsetY = 0.f;
	OutColor = FTacticsAbilityResolvePresentation::WaveColor;
	const double Now = FPlatformTime::Seconds();
	const FIntPoint Cell(WorldX, WorldY);
	for (const FTacticsAbilityDamagePopup& Popup : ActiveAbilityDamagePopups) {
		if (Popup.Cell != Cell) {
			continue;
		}
		float Alpha = 0.f;
		float Scale = 0.f;
		float OffsetY = 0.f;
		Popup.GetVisual(Now, Alpha, Scale, OffsetY);
		if (Alpha <= 0.01f) {
			continue;
		}
		OutShowPopup = true;
		OutText = Popup.GetDisplayText();
		OutAlpha = Alpha;
		OutScale = Scale;
		OutOffsetY = OffsetY;
		OutColor = Popup.GetDisplayColor(1.f);
		return true;
	}
	return false;
}

bool UTacticsMatchSubsystem::TryGetAbilityResolvePresentationAtWorld(const int WorldX, const int WorldY, float& OutFlashAlpha,
	float& OutFlashScale, FLinearColor& OutFlashColor) const
{
	OutFlashAlpha = 0.f;
	OutFlashScale = 0.f;
	OutFlashColor = FTacticsAbilityResolvePresentation::WaveColor;
	if (!ActiveAbilityResolvePresentation.IsSet()) {
		return false;
	}
	const FTacticsAbilityResolvePresentation& Pres = *ActiveAbilityResolvePresentation;
	const double Now = FPlatformTime::Seconds();
	const FIntPoint Cell(WorldX, WorldY);
	Pres.GetWaveCellVisual(Now, Cell, OutFlashAlpha, OutFlashScale);
	if (OutFlashAlpha > 0.01f) {
		OutFlashColor = Pres.ActiveWaveColor();
	}
	return OutFlashAlpha > 0.01f;
}

void UTacticsMatchSubsystem::EnsureAbilityResolvePresentationTicker()
{
	if (AbilityResolvePresentationTickerHandle.IsValid()) {
		return;
	}
	AbilityResolvePresentationTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticsMatchSubsystem::TickAbilityResolvePresentation), 0.033f);
}

void UTacticsMatchSubsystem::FinishAbilityResolvePresentation()
{
	if (!ActiveAbilityResolvePresentation.IsSet()) {
		return;
	}
	ActiveAbilityResolvePresentation.Reset();
	PendingAbilityDamagePopups.Reset();
	if (AbilityResolvePresentationTickerHandle.IsValid()) {
		FTSTicker::GetCoreTicker().RemoveTicker(AbilityResolvePresentationTickerHandle);
		AbilityResolvePresentationTickerHandle.Reset();
	}
	FString ResumeOut;
	ResumeCombatVisualization(ResumeOut);
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights | ETacticsBoardVisualDirty::Units);
	OnBoardChanged.Broadcast();
	if (HasPendingAbilityResolveVisualization()) {
		TryBeginAbilityResolvePresentation();
	}
}

bool UTacticsMatchSubsystem::TickAbilityResolvePresentation(float /*DeltaTime*/)
{
	if (!ActiveAbilityResolvePresentation.IsSet()) {
		if (AbilityResolvePresentationTickerHandle.IsValid()) {
			FTSTicker::GetCoreTicker().RemoveTicker(AbilityResolvePresentationTickerHandle);
			AbilityResolvePresentationTickerHandle.Reset();
		}
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	FTacticsAbilityResolvePresentation& Pres = *ActiveAbilityResolvePresentation;
	PruneExpiredAbilityDamagePopups();
	const bool bPopupsActive = !ActiveAbilityDamagePopups.IsEmpty();
	if (Pres.IsComplete(Now) && !bPopupsActive) {
		FinishAbilityResolvePresentation();
		return false;
	}
	if (bPopupsActive) {
		EnsureAbilityDamagePopupTicker();
		OnAbilityDamagePopupsChanged.Broadcast();
		MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights | ETacticsBoardVisualDirty::Units);
	}
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights);
	OnTargetPreviewChanged.Broadcast();
	return true;
}

void UTacticsMatchSubsystem::EnsureResolveFlashRefreshTicker()
{
	if (ResolveFlashRefreshTickerHandle.IsValid()) {
		return;
	}
	ResolveFlashRefreshTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticsMatchSubsystem::TickResolveFlashRefresh), 0.033f);
}

bool UTacticsMatchSubsystem::TickResolveFlashRefresh(float /*DeltaTime*/)
{
	PruneExpiredResolveFlash();
	if (!ActiveResolveFlash.IsSet()) {
		if (ResolveFlashRefreshTickerHandle.IsValid()) {
			FTSTicker::GetCoreTicker().RemoveTicker(ResolveFlashRefreshTickerHandle);
			ResolveFlashRefreshTickerHandle.Reset();
		}
		return false;
	}
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Highlights);
	OnBoardChanged.Broadcast();
	return true;
}

