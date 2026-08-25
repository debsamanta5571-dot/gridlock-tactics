#pragma once

#include "CoreMinimal.h"
#include <memory>
#include <random>
#include <unordered_map>
#include "tactics/bot/bot_policy.hpp"
#include "tactics/bot/bot_session.hpp"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "tactics/common/match_defaults.hpp"
#include "tactics/core/game_state.hpp"
#include "TacticsBoardVisualDirty.h"
#include "TacticsAbilityVisualGroup.h"
#include "TacticsAbilityResolveFlash.h"

#include "TacticsMatchSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FTacticsBoardChanged);

DECLARE_MULTICAST_DELEGATE_ThreeParams(FTacticsNetworkAuthorityCommitted, int32 /*SeatPlayerId*/, const FString& /*Line*/,
                                       uint64 /*CommandSeq*/);

/** Grid-space pose for world unit actors (footprint centroid in world cell coordinates). */
struct FTacticsBoardUnitPose
{
	FString EntityId;
	int32 OwnerPlayerId{0};
	FString ArtId;
	float GridCenterX{0.f};
	float GridCenterY{0.f};
	int32 FootprintCellCount{1};
	int32 FootprintSpanX{1};
	int32 FootprintSpanY{1};
	bool bIsBase{false};
	/** 1-based passive-action order (1 = oldest). 0 when unranked. */
	int32 TurnOrderRank{0};
	/** Show taunt indicator on the 3D unit actor. */
	bool bHasTaunt{false};
	/** Post-cast flash on the selected unit (0 = off, 1 = peak). */
	float AbilityCastFlashAlpha{0.f};
	bool bAbilityCastFlashSuccess{false};
};

/** Visual terrain marker for one board cell. */
struct FTacticsBoardTerrainCell
{
	FIntPoint Cell{0, 0};
	FString TerrainName;
	float MovementCost{1.f};
	int32 DamageOnEnter{0};
	bool bIsVoid{false};
};

/** Combined per-cell presentation data for UI and 3D renderers. */
struct FTacticsBoardCellPresentation
{
	FIntPoint Cell{0, 0};
	FString Summary;
	FString TerrainName;
	bool bSelected{false};
	bool bPendingCliTarget{false};
	bool bReachableMove{false};
	bool bAttackTarget{false};
	bool bBoardTargetEnemy{false};
	bool bBoardTargetOther{false};
	bool bBoardTargetAoE{false};
	/** Batch-queue hover: caster / attacker footprint (gold). */
	bool bActionQueueHoverSource{false};
	/** Batch-queue hover: target unit footprint (red). */
	bool bActionQueueHoverTarget{false};
	/** Batch-queue hover: AoE blast footprint (orange). */
	bool bActionQueueHoverAoE{false};
	/** Purple ability targeting (distinct from spell blue/red). */
	bool bAbilityBoardTarget{false};
	bool bAbilityBoardTargetEnemy{false};
	/** Brief RGB resolve flash after an ability resolves on the stack. */
	bool bResolveFlash{false};
	float ResolveFlashAlpha{0.f};
	FLinearColor ResolveFlashColor{0.2f, 0.95f, 0.35f, 1.f};
	float ResolveFlashScale{0.f};
	/** Floating damage/heal label during ability resolve animation phase 2. */
	bool bAbilityDamagePopup{false};
	FString AbilityDamagePopupText;
	float AbilityDamagePopupAlpha{0.f};
	float AbilityDamagePopupScale{1.f};
	float AbilityDamagePopupOffsetY{0.f};
	FLinearColor AbilityDamagePopupColor{0.95f, 0.08f, 0.05f, 1.f};
	bool bHasUnit{false};
	bool bHasControllableUnit{false};
	bool bRoad{false};
	bool bRough{false};
	bool bDamagingTerrain{false};
	bool bVoidTerrain{false};
	bool bAetherTerrain{false};
	bool bScannerTerrain{false};
	bool bOmniEnergyTerrain{false};
	bool bDeployZone{false};
	bool bPendingMoveDestination{false};
	bool bPendingMoveOrigin{false};
	/** 1-based passive-action order badge for this cell (0 = hidden). */
	int32 TurnOrderRank{0};
	/** Catalog `art_id` for a unit on this cell (empty = text-only 2D token). */
	FString UnitArtId;
};

/** One visible spell/ability card on the stack, top-first for presentation. */
struct FTacticsStackItemUi
{
	FString StackId;
	FString SourceType;
	FString SourceName;
	FString EffectKey;
	FString Speed;
	FString TargetLine;
	int32 ControllerPlayerId{0};
};

/** Keyword or status-effect glossary line for the selected-card detail panel (name + rules body). */
struct FTacticsCardGlossaryEntry
{
	/** `kw:…` / `fx:…` dedupe key from `collect_card_glossary_entries`. */
	FString Key;
	FString Name;
	FString Body;
};

/** Live status stack on a selected board unit (active-effects pill row). */
struct FTacticsActiveEffectEntry
{
	FString Key;
	FString Name;
	FString Body;
	bool bNegative{false};
	bool bPositive{false};
};

/** Unit hover popup: header + stacked status pills anchored under the hovered unit. */
struct FTacticsUnitHoverPresentation
{
	bool bHasUnit{false};
	int32 WorldX{0};
	int32 WorldY{0};
	FString HeaderLine;
	FString OwnerLine;
	FString ActionHint;
	TArray<FTacticsActiveEffectEntry> Effects;
};

/** Runtime-granted passive not printed on the source card (board unit detail). */
struct FTacticsRuntimePassiveStrip
{
	FString Name;
	FString RulesText;
};

/** One entry in the pending-action queue shown in the Action Queue panel. */
struct FTacticsActionQueueEntryUi
{
	/** "spell" or "attack" */
	FString Kind;
	/** Display label - spell/ability name, or "Attack: <unit> → <x>,<y>" */
	FString Label;
	/** Sub-line: target description or ranged indicator */
	FString Detail;
	/** Player seat that owns this action */
	int32 ControllerPlayerId{0};
	/** Batch item id (e.g. batch_3) for counter targeting; empty for attacks */
	FString ItemId;
	/** spell / ability - used when validating counter targets */
	FString SourceType;
	/** Index into `GameState::phase_action_queue()` for hover targeting preview. */
	int32 QueueIndex{-1};
};

/** One card thumbnail in the batch-queue hover overlay (MTG Arena-style). */
struct FTacticsActionQueueHoverCardUi
{
	bool bVisible{false};
	FString Label;
	FString TypeLine;
	FString ArtId;
	/** Spell/ability rules prose (empty for units and attacks). */
	FString Description;
};

/** HP / attack comparison for a queued attack (attacker vs defender). */
struct FTacticsActionQueueAttackCompareUi
{
	bool bShow{false};
	/** Rich-text stat line for the attacker ({LIFE}, {MELEE}/{RANGED}, {ARMOR}). */
	FString AttackerStatsMarkup;
	/** Rich-text stat line for the defender; counter damage when applicable. */
	FString TargetStatsMarkup;
	/** Empty, "Counter", or "Return fire" - shown under defender stats when set. */
	FString TargetCounterLabel;
};

/** Source card → target card overlay while hovering a batch-queue row. */
struct FTacticsActionQueueHoverOverlayUi
{
	bool bShowPreview{false};
	bool bShowArrow{false};
	bool bIsAttack{false};
	/** Focus-spell casting unit (shown before the spell card). */
	FTacticsActionQueueHoverCardUi FocusCaster;
	FTacticsActionQueueHoverCardUi Source;
	/** One or more target cards (multicast copies, AoE victims, attack defender, etc.). */
	TArray<FTacticsActionQueueHoverCardUi> Targets;
	/** Glossary rows for source rules markup (`{GL:…}`, `{KW:…}`, speed icons). */
	TArray<FTacticsCardGlossaryEntry> SourceGlossary;
	FTacticsActionQueueAttackCompareUi AttackCompare;
};

/** Snapshot of one unit's vital stats for combat visualization. */
struct FCombatUnitSnapshot
{
	FString EntityId;
	FString DisplayName;
	FString ArtId;
	int32 Hp{0};
	int32 MaxHp{0};
	bool bAlive{true};
	/** Grid-space position of this unit on the board (-1 when unknown or off-board). */
	int32 GridX{-1};
	int32 GridY{-1};
	/** Team this unit belongs to (-1 when unknown). Used to split the two combat panels. */
	int32 Team{-1};
};

/** A single attack pairing resolved during attack_commit (before + after state). */
struct FCombatEncounter
{
	FCombatUnitSnapshot AttackerBefore;
	FCombatUnitSnapshot DefenderBefore;
	FCombatUnitSnapshot AttackerAfter;
	FCombatUnitSnapshot DefenderAfter;
	/** Damage the attacker dealt to the defender (full hit after mitigation, incl. overkill). */
	int32 AttackDamage{0};
	/** Damage the defender dealt back to the attacker (full counter hit after mitigation). */
	int32 CounterDamage{0};
	/** True when the primary attack roll was a crit (resolved encounter only). */
	bool bAttackWasCrit{false};
	/** True when the defender's counterattack roll was a crit (resolved encounter only). */
	bool bCounterWasCrit{false};
	/**
	 * True when this encounter represents the *resolved* outcome (AttackerAfter /
	 * DefenderAfter and damage deltas are populated).  False on the pre-roll
	 * snapshot shown before the dice are thrown (After fields mirror Before state).
	 * Use this flag instead of inferring resolution from damage values - a 0-damage
	 * hit would otherwise silently skip the choreography animation.
	 */
	bool bIsPostResolution{false};
	/**
	 * All entities on the board at the moment this encounter was captured.
	 * Used by the battlefield visualization to show every unit at its grid position.
	 * Empty for legacy encounter data (visualizer falls back to attacker+defender only).
	 */
	TArray<FCombatUnitSnapshot> AllBattlefieldUnits;
	/** Board dimensions at capture time (columns / rows). */
	int32 BoardCols{8};
	int32 BoardRows{8};
	/**
	 * Board origin (minimum occupied cell) at capture time.  All FCombatUnitSnapshot
	 * GridX / GridY values - including the attacker / defender - are RAW board
	 * coordinates; subtract (BoardMinX, BoardMinY) to get a 0-based grid index.
	 */
	int32 BoardMinX{0};
	int32 BoardMinY{0};
};

/** Runtime setup profile for demo/test/real match bootstrapping. */
struct FTacticsMatchSetupProfile
{
	FString GameId{TEXT("unreal_gui")};
	/** Legacy fields; `ResetMatchWithProfile` always uses the standard 8x12 base map. */
	int32 BoardWidth{tactics::kStandardBoardWidth};
	int32 BoardHeight{tactics::kStandardBoardHeight};
	bool bSeedDemoState{true};
	/** When true, auto-skips the Energy phase (sandbox uses pre-seeded omni zones). */
	bool bSkipEnergyToMain{false};
	bool bAutoFollowActiveSeat{true};
	/** 8x12 sandbox board, preset units/structures, full catalog hand, omni zones per seat. */
	bool bSandboxMatch{false};
	// ── Pre-match settings (see MatchConfig) ──
	/** false = standard 8x12 1v1 duel map; true = 16x8 2v2 team map (four bases). */
	bool bTeam2v2{false};
	/** Per-type objective toggles. */
	bool bObjScanner{true};
	bool bObjOmni{true};
	bool bObjAether{true};
	/** Deck id (decks/<id>.json stem); empty = default. Applied to every seat unless OpponentDeckId is set. */
	FString DeckId;
	/** Optional second deck for seat 2 (Play vs AI). Empty = same as DeckId. */
	FString OpponentDeckId;
	/** Going-second seats receive the Field Requisition card. */
	bool bGiveFieldRequisition{false};
};

/** Result of the match from the controlled player's perspective (for the win/lose screen). */
enum class ETacticsMatchResult : uint8
{
	InProgress,  ///< Match not decided yet (in 2v2, a team with any living base is still in it).
	Win,         ///< The controlled player's team won (all enemy bases destroyed).
	Loss,        ///< The controlled player's team lost.
	Draw,        ///< Match over with no winner (e.g. sudden-death tie).
};

/**
 * Holds the in-editor / PIE tactics match (same rules as tactics_master_cli) for UI layers.
 * Lives in TacticsCore so calls link against the same DLL as cpp_core (tactics::* are not exported).
 */
UCLASS()
class TACTICSCORE_API UTacticsMatchSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Seats created by `ResetDemoMatch` (P1..Pn); keep in sync with `tactics::kDefaultDemoSeatCount`. */
	static constexpr int32 KDefaultDemoPlayerCount = static_cast<int32>(tactics::kDefaultDemoSeatCount);

	~UTacticsMatchSubsystem();

	FTacticsBoardChanged OnBoardChanged;
	/** Lightweight tick while ability damage popups animate (avoids full Slate grid rebuild). */
	FTacticsBoardChanged OnAbilityDamagePopupsChanged;
	/** Targeting preview only (directional AoE hover) - avoids full board UI rebuild. */
	FTacticsBoardChanged OnTargetPreviewChanged;

	/** Host-only: after a committed authority CLI line; networking broadcasts `cmd` (+ periodic `snap`). */
	FTacticsNetworkAuthorityCommitted OnNetworkAuthorityCommitted;

	void ResetDemoMatch();

	/** Win/lose/draw from the controlled player's team perspective, or InProgress. Team-aware: in 2v2
	 *  the match is not over while your team still has any living base. */
	ETacticsMatchResult GetControlledMatchResult() const;

	/**
	 * Start a match from the pre-match settings screen (host / solo). Chooses the map (1v1 duel or 16x8
	 * 2v2), seeds the enabled objective tiles, loads the chosen deck, and applies the going-second Field
	 * Requisition toggle. Starts a clean match (no demo units). Deck ids are decks/<id>.json stems.
	 */
	void StartConfiguredMatch(bool bTeam2v2, bool bObjScanner, bool bObjOmni, bool bObjAether,
		const FString& DeckId, bool bGiveFieldRequisition);

	/** Deck ids (decks/<id>.json stems) available to pick in the settings screen. */
	TArray<FString> GetAvailableDeckIds() const;

	/** Serialize the active match deck list to JSON (for a joining client to send its deck). Empty if none. */
	FString GetActiveMatchDeckJson() const;
	/** Host-side: apply a joining client's deck-list JSON to their seat (rebuilds deck + opening hand). */
	/** Apply a validated deck list JSON to a seat (returns false if illegal / unloadable). */
	bool ApplyClientDeckToSeat(int32 Seat, const FString& DeckJson);

	/**
	 * Eagerly loads the project card catalogs (manifest + shards) so that faction data is
	 * available before a match starts - e.g. to populate the main-menu faction picker.
	 * Idempotent: safe to call multiple times; subsequent calls are very cheap.
	 */
	void EnsureProjectCatalogsLoaded();

	/**
	 * Returns playable factions (set_codes with at least one card) as (set_code, display_name).
	 * Empty registered shards are omitted. "core" is exposed separately in the sandbox UI.
	 */
	TArray<TPair<FString, FString>> ListAvailableFactions() const;

	/** 8x12 sandbox: allied/enemy units and structures, omni zones per turn, full catalog hand. */
	void ResetSandboxMatch();

	/**
	 * Same as ResetSandboxMatch but replaces every player's hand with all cards from one faction.
	 * FactionKey: set_code (e.g. 99th_dieselheart_company), legacy aliases (gallantry, ingenuity,
	 * mythology, the_lost_kingdom), "core", or "all".
	 */
	void ResetSandboxMatchWithFaction(const FString& FactionKey);

	/** Fresh demo with exactly `PlayerCount` consecutive seats (P1..P{PlayerCount}). */
	void ResetMatchToPlayerCount(int32 PlayerCount);
	void ResetMatchWithProfile(int32 PlayerCount, const FTacticsMatchSetupProfile& Profile);

	int32 GetMatchPlayerCount() const;

	/**
	 * Host WebSocket: grow the authoritative match so a remote human can occupy a seat.
	 * Uses `RequestedSeat` when that seat is not already taken by another live TCP peer; otherwise
	 * picks the smallest free seat >= 2 (see `SeatsTakenByOtherRemotes`).
	 * @return Actual seat id assigned (always >= 2 when a remote connects; host remains P1).
	 */
	int32 RegisterNetworkClientSeat(int32 RequestedSeat, const TSet<int32>* SeatsTakenByOtherRemotes = nullptr);

	int GetControlledPlayer() const { return ControlledPlayer; }
	void SetControlledPlayer(int PlayerId);
	/** True when this machine's home seat is the far side (P2): 3D board presents 180° so that base is screen-bottom. */
	bool ShouldFlipBoardPresentation() const;
	void SetAutoFollowActiveSeat(bool bEnabled);
	bool HasUnitSelected() const { return Selected != nullptr; }
	bool IsSelectedUnitControlled() const;
	bool IsSelectedUnitSilenced() const;
	bool IsSelectedUnitJammed() const;

	/** CLI targeting cell for deploy/cast/attack/move verbs (shared by Slate grid and 3D board picks). */
	void SetPendingCliWorldCell(int Wx, int Wy);
	void ClearPendingCliWorldCell();
	bool TryGetPendingCliWorldCell(int& OutWx, int& OutWy) const;

	/** When true, Slate hides the 2D cell grid; a world actor draws instanced tiles instead. */
	void SetUses3DBoardTiles(bool b);
	bool Uses3DBoardTiles() const { return bUses3DBoardTiles; }

	/** Toggle overlay showing 1-based passive-action order on units and structures (1 = oldest). */
	void SetTurnOrderViewEnabled(bool bEnabled);
	bool IsTurnOrderViewEnabled() const { return bTurnOrderViewEnabled; }
	int32 GetTurnOrderRankForEntityId(const FString& EntityId) const;
	int32 GetTurnOrderRankAtWorld(int WorldX, int WorldY) const;

	bool IsMatchReady() const;

	// ---- Phase queries -------------------------------------------------------
	/** True only during TurnPhase::Main (first main phase). */
	bool IsMainPhase() const;
	/** True only during TurnPhase::SecondMain. */
	bool IsSecondMainPhase() const;
	/** True during either main phase (Main or SecondMain). */
	bool IsAnyMainPhase() const;
	/** True during Energy / zone-pick phase. */
	bool IsEnergyPhase() const;
	/** True during a spell-reaction window (SpellWindow or SecondSpellWindow). */
	bool IsSpellWindowPhase() const;
	/** True during an attack-declaration phase (AttackDeclaration or BonusAttackDeclaration). */
	bool IsAnyAttackDeclarationPhase() const;
	/** True during a defense-reaction window (Defense or BonusDefense). */
	bool IsAnyDefensePhase() const;
	/** True during any reaction window (spell or defense). */
	bool IsAnyReactionWindowPhase() const;

	/** Human-readable phase label for the current phase. */
	FString PhaseLabel() const;
	/**
	 * Phase label for the on-screen banner, with the acting seat appended for turn/combat/reaction
	 * phases (e.g. "Main Phase - Player 1 (You)"). Updates dynamically as the active seat changes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|UI")
	FString GetPhaseBannerText() const;

	// ---- Player-state queries -----------------------------------------------
	/** Turn manager priority holder (0 if none). Not necessarily who you control. */
	int32 GetActivePlayerId() const;
	/** Who may cast reactive Fast spells / pass on the stack (0 if unset). */
	int32 GetReactionWindowPriorityPlayerId() const;
	int32 GetStackPriorityPlayerId() const;
	int32 GetPhaseActionQueueCount() const;
	/** True when the active main or attack-declaration phase and the controlled player may act. */
	bool CanControlledPlayerActInMainPhase() const;
	/** True when the controlled seat may pass in the current phase. */
	bool CanControlledPlayerPassPriority() const;
	/** True when the controlled seat may end the current main phase (active player, Main or SecondMain). */
	bool CanControlledPlayerEndTurn() const;
	/** True when the controlled seat may commit the attack queue (active player, attack-declaration phase). */
	bool CanControlledPlayerCommitAttacks() const;

	/** Multi-line stats block for the currently selected unit (empty if none). */
	FString FormatSelectedUnitStats() const;
	/** Unit hover popup content for a board cell (false when no unit). */
	bool TryBuildUnitHoverAtWorld(int WorldX, int WorldY, FTacticsUnitHoverPresentation& Out) const;
	/** Board hover: unit popup when present, otherwise contextual click hint only. */
	bool TryBuildBoardHoverAtWorld(int WorldX, int WorldY, FTacticsUnitHoverPresentation& OutUnit, FString& OutCellHint) const;
	/** Number of activated abilities on the selected unit (0 if none). */
	int32 GetSelectedUnitActivatedAbilityCount() const;
	/**
	 * 1-based index into the selected unit's `activated_abilities` list.
 * @param bNeedsBoardTarget true when CLI expects `ability <key> <col> <row>` (deal_damage / heal).
 * @param OutRangeToken `{ADJACENT}`, `{RANGE}n`, or empty when no range/adjacent icon applies.
 */
bool TryGetSelectedUnitActivatedAbilityUi(int32 Index1Based, FString& OutKey, FString& OutLabel, FString& OutSpeedTag, FString& OutCostLine,
	bool& bUsedThisTurn, bool& bNeedsBoardTarget, FString& OutRangeToken, int32& OutUsesRemaining, int32& OutUsesMax) const;
	/** Rules / targeting help for an activated ability (false when key unknown). Catalog lookup works without a board selection. */
	bool TryGetAbilityDescriptionForKey(const FString& AbilityKey, FString& OutDescription,
		bool bIncludeTargetingHints = true) const;

	/**
	 * Toggle between simple (default) and advanced/full card & ability descriptions.
	 * UI binds this to the Advanced-text checkbox / Shift-hold. Broadcasts a refresh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|UI")
	void SetShowAdvancedCardText(bool bAdvanced);
	UFUNCTION(BlueprintCallable, Category = "Tactics|UI")
	bool IsShowingAdvancedCardText() const { return bShowAdvancedCardText; }

	/** Match setting: allow a deployment to be undone (synced; default off). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	bool GetAllowDeploymentUndo() const;
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void SetAllowDeploymentUndo(bool bAllow);

	/** Seats present in the current match (sorted). */
	TArray<int32> GetMatchPlayerSeats() const;
	/** Team id for a seat (`team_of_seat`; default seat==team in FFA). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	int32 GetTeamForSeat(int32 Seat) const;
	/** Assign seat to team via authority CLI (`team <seat> <teamId>`). */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Settings")
	void SetTeamForSeat(int32 Seat, int32 TeamId);

	/** Number of passive abilities on the selected unit (0 if none). */
	int32 GetSelectedUnitPassiveCount() const;
	/**
	 * 1-based index into the selected unit's `passive_abilities` list.
	 * OutAppliesTo is "self", "allied_units", etc.
	 */
	bool TryGetSelectedUnitPassiveUi(int32 Index1Based, FString& OutName, FString& OutRulesText, FString& OutAppliesTo) const;

	bool GetMergedBounds(int& OutMinX, int& OutMinY, int& OutSpanX, int& OutSpanY) const;
	/** Human-readable map size / tile count (for UI diagnostics). */
	FString GetBoardMapDebugLine() const;

	/** All units on the board with footprint centroids (for 3D presentation). */
	void GatherBoardUnitPoses(TArray<FTacticsBoardUnitPose>& OutPoses) const;

	/** World cells occupied by demo obstacles (`entity_type` obstacle). */
	void GatherBoardObstacleCells(TArray<FIntPoint>& OutCells) const;

	/** Road / rough / other movement modifiers on board cells. */
	void GatherBoardTerrainCells(TArray<FTacticsBoardTerrainCell>& OutCells) const;
	bool IsRoadTerrainAtWorld(int WorldX, int WorldY) const;
	bool IsRoughTerrainAtWorld(int WorldX, int WorldY) const;
	bool IsDamagingTerrainAtWorld(int WorldX, int WorldY) const;
	bool IsVoidTerrainAtWorld(int WorldX, int WorldY) const;

	FString CellSummaryWorld(int WorldX, int WorldY) const;
	FTacticsBoardCellPresentation GetCellPresentationAtWorld(int WorldX, int WorldY) const;
	bool IsSelectedAtWorld(int WorldX, int WorldY) const;

	/** True when the selected unit can `move_preview` to this cell (main phase, moves left, no pending move). */
	bool IsReachableMoveCellAtWorld(int WorldX, int WorldY) const;
	bool IsPendingMoveDestinationCellAtWorld(int WorldX, int WorldY) const;
	bool IsPendingMoveOriginCellAtWorld(int WorldX, int WorldY) const;
	/** True when the selected unit can attack an enemy occupying this cell without moving. */
	bool IsAttackTargetCellAtWorld(int WorldX, int WorldY) const;
	bool IsBoardTargetEnemyHighlightAtWorld(int WorldX, int WorldY) const;
	bool IsBoardTargetOtherHighlightAtWorld(int WorldX, int WorldY) const;
	bool IsBoardTargetAoEHighlightAtWorld(int WorldX, int WorldY) const;
	bool IsActionQueueHoverSourceAtWorld(int WorldX, int WorldY) const;
	bool IsActionQueueHoverTargetAtWorld(int WorldX, int WorldY) const;
	bool IsActionQueueHoverAoEAtWorld(int WorldX, int WorldY) const;
	bool IsActionQueueHoverActive() const;
	/** True while aiming a directional area ability (Energy Wave, etc.). */
	bool IsDirectionalAreaBlastPreviewActive() const;
	/** True when hover should show an orange AoE blast footprint (directional, lobbed, push path, etc.). */
	bool IsBoardTargetAoEHoverPreviewActive() const;
	/** True while board-target highlights are for an armed activated ability (purple UX). */
	bool IsAbilityBoardTargetPreviewActive() const;
	/** Shared targeting UX bucket for one activated ability key on the selected unit. */
	ETacticsAbilityVisualGroup ResolveAbilityVisualGroup(const FString& AbilityKey) const;
	/** Brief success/fail tint on the selected unit after an ability CLI resolves. */
	void NotifyAbilityCastFlash(bool bSuccess);
	/** True when cpp_core paused before resolving an ability on the stack. */
	bool HasPendingAbilityResolveVisualization() const;
	/** Capture pending ability preview and start the 5s on-board animation. Returns false if not paused for ability. */
	bool TryBeginAbilityResolvePresentation();
	bool ApplyPausedAbilityVisualizationStep(FString& OutMessage);
	bool HasActiveAbilityResolvePresentation() const;
	bool TryGetAbilityResolvePresentationAtWorld(int WorldX, int WorldY, float& OutFlashAlpha, float& OutFlashScale,
		FLinearColor& OutFlashColor) const;
	void DrainAbilityDamagePopupEvents();
	void ReleasePendingDamagePopupsSynchronizedToWave(double Now);
	int32 FindWaveRingForPopupCell(const FIntPoint& Cell, const FString& EntityId = FString()) const;
	bool TryGetAbilityDamagePopupAtWorld(int WorldX, int WorldY, bool& OutShowPopup, FString& OutText, float& OutAlpha,
		float& OutScale, float& OutOffsetY, FLinearColor& OutColor) const;
	const TArray<FTacticsAbilityDamagePopup>& GetActiveAbilityDamagePopups() const { return ActiveAbilityDamagePopups; }
	void ClearBoardTargetPreview();
	bool SetBoardTargetPreviewForHandCard(int32 Index1Based);
	bool SetBoardTargetPreviewForReservesCard(int32 Index1Based);
	/** Highlights friendly units that can cast the armed Focus spell (before a caster is chosen). */
	bool SetBoardFocusCasterSelectionPreviewForHandCard(int32 Index1Based);
	bool SetBoardFocusCasterSelectionPreviewForReservesCard(int32 Index1Based);
	/** Phase 2 of push-direction spell targeting after the player picks a unit. */
	bool SetBoardPushDirectionPreviewForHandCard(int32 Index1Based, int32 TargetWorldX, int32 TargetWorldY);
	bool SetBoardPushDirectionPreviewForReservesCard(int32 Index1Based, int32 TargetWorldX, int32 TargetWorldY);
	bool SetBoardTargetPreviewForSelectedAbility(const FString& AbilityKey);

	/** True if this world cell has any inspectable unit/building. */
	/** True when this world coordinate has a board tile (sparse / jigsaw layouts omit gaps). */
	bool HasBoardCellAtWorld(int WorldX, int WorldY) const;
	bool IsDeployZoneCellForPlayer(int32 PlayerId, int32 WorldX, int32 WorldY) const;
	bool CanDeployHandCardAt(int32 HandIndex1Based, int32 WorldX, int32 WorldY) const;
	/** True when the armed unit card can deploy with anchor at this world cell (main phase, valid zone, unblocked). */
	bool IsDeployValidCellAtWorld(int WorldX, int WorldY) const;
	/** True when a hand or reserves unit/building card is armed for deploy preview. */
	bool IsDeployPreviewArmed() const;
	void ClearDeployPreview();
	/** Arm a unit card from hand and highlight every legal deploy anchor on the board. */
	bool SetDeployPreviewForHandCard(int32 Index1Based);
	bool SetDeployPreviewForReservesCard(int32 Index1Based);
	bool CanDeployReservesCardAt(int32 ReservesIndex1Based, int32 WorldX, int32 WorldY) const;

	bool HasUnitAtWorld(int WorldX, int WorldY) const;
	/** True if this world cell has a unit the controlled player can select for commands (main phase only). */
	bool HasControllableUnitAtWorld(int WorldX, int WorldY) const;

	/** True when this seat has a move preview awaiting confirm or cancel (rules lock other actions). */
	bool HasPendingMoveForControlledPlayer() const;
	/** Pending footprint rotation (quarters CW) for the controlled seat's preview; 0 if none. */
	int32 GetPendingMoveQuarterTurnsCw() const;

	bool TrySelectWorld(int WorldX, int WorldY, FString& OutMessage);
	/** 1-based CLI column/row (same as `select <col> <row>`) → world cell, then `TrySelectWorld`. */
	bool TrySelectFromCli1BasedCell(int32 Col1, int32 Row1, FString& OutMessage);
	bool TryMoveOrAttackWorld(int WorldX, int WorldY, FString& OutMessage);
	/** True when the selected controlled unit can spend a base attack action (dash / attack). */
	bool CanControlledUnitSpendAttackAction() const;
	/** Main phase only: base attack + move remaining for slow Defend. */
	bool CanControlledUnitDefend() const;
	/** Burst dash: base attack action, usable in main or attack declaration. */
	bool CanControlledUnitDash() const;
	bool CanControlledUnitRecover() const;
	bool TryDefendSelectedUnit(FString& OutMessage);
	bool TryDashSelectedUnit(FString& OutMessage);
	bool TryRecoverSelectedUnit(FString& OutMessage);
	bool CanControlledPlayerUndo() const;
	bool TryUndoLastAction(FString& OutMessage);
	bool CanControlledPlayerCancelBatchItem(const FString& ItemId, int32 ControllerPlayerId) const;
	bool TryCancelBatchItem(const FString& ItemId, FString& OutMessage);

	/** True if World has an enemy unit relative to the currently selected unit (for CLI attack vs move). */
	bool IsEnemyUnitAtWorldVsSelected(int WorldX, int WorldY) const;

	bool TryDeployWorld(int HandIndex1Based, int WorldX, int WorldY, FString& OutMessage);
	bool TrySkipEnergyZone(FString& OutMessage);
	bool TryEndTurn(FString& OutMessage);

	bool IsAwaitingHandDiscard() const;
	/** Player whose hand is shown in the strip (during end-of-turn discard, the player who must discard - may differ from Control). */
	int32 GetHandViewPlayerId() const;
	bool TryDiscardHandCard(int32 HandIndex1Based, FString& OutMessage);

	bool IsAwaitingScan() const;
	int32 GetPendingScanPlayerId() const;
	int32 GetPendingScanPeekCount() const;
	bool TryGetScanPeekCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutCostLine) const;
	/** Full detail for one scanned card (rules/art/stats) so the left panel can show it before the
	 *  player decides to discard - the same read-first view hand cards get. */
	bool TryGetScanPeekCardDetail(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutCostLine,
		FString& OutRules, FString& OutArtId, FString& OutStatTokens) const;
	bool TryScanDiscardAt(int32 Index1Based, FString& OutMessage);
	bool TryScanFinish(FString& OutMessage);

	bool IsAwaitingTerritoryLoot() const;
	bool TryTerritoryLootDiscard(int32 HandIndex1Based, FString& OutMessage);
	bool TryTerritoryLootSkip(FString& OutMessage);

	/**
	 * Total available energy for the controlled player, split per energy type into:
	 *   - OutFreeFloating: unrestricted energy already in the pool (player_energy - usable for anything).
	 *   - OutTaggedFloating: energy in restricted tagged pools (e.g. spell_ability - spells/abilities only).
	 *   - OutFromZones: potential energy producible by tapping the player's untapped energy zones.
	 * One parallel entry per energy type that has any nonzero amount in any bucket, ordered by the
	 * canonical billing order. OutTypeLabels holds the human-readable type name for each entry.
	 */
	void GetControlledAvailableEnergyCounters(TArray<FString>& OutTypeLabels, TArray<int32>& OutFreeFloating,
		TArray<int32>& OutTaggedFloating, TArray<int32>& OutFromZones) const;

	/** Energy phase: number of zone cards offered (0 if not applicable). */
	int32 GetPendingEnergyZoneChoiceCount() const;
	bool TryGetEnergyZoneChoiceUi(int32 Index1Based, FString& OutName, FString& OutProducesLine,
		FString& OutArtId) const;
	/** Full multi-line rules text for one offered territory (enter/groundwork/depleted + every "use
	 *  land" ability), for the left detail panel - the same read-before-you-commit view normal cards get. */
	bool TryGetEnergyZoneChoiceDetail(int32 Index1Based, FString& OutName, FString& OutRules,
		FString& OutArtId) const;
	/** Full rules text for a placed territory (0-based index into the controlled player's lands) - the
	 *  same detail-panel view, shown when a land is selected in the Territories rail. */
	bool TryGetPlacedTerritoryDetail(int32 TerritoryIndex, FString& OutName, FString& OutRules,
		FString& OutArtId) const;
	bool TryChooseEnergyZone(int32 Index1Based, FString& OutMessage);

	// ── Conquering Territories: placed territories + their "use land" abilities (0-based) ──
	/** Placed territories the controlled player owns that carry activatable land abilities. */
	int32 GetControlledTerritoryCount() const;
	bool TryGetTerritoryUi(int32 TerritoryIndex, FString& OutName, bool& bOutDepleted, bool& bOutUseAvailable,
		FString& OutArtId) const;
	int32 GetTerritoryLandAbilityCount(int32 TerritoryIndex) const;

	/** True while the controlled player has a placed-territory enter/groundwork effect awaiting a
	 *  target unit (e.g. "enter: grant 1/1 to any unit"). The next board click resolves it. */
	bool IsControlledPlayerAwaitingTerritoryTarget() const;
	/** Prompt describing the pending territory target (effect summary); empty when none. */
	FString GetTerritoryTargetPrompt() const;
	/** Display data for one land ability: name, cost line (empty = free), effect summary, whether
	 *  it is affordable + the shared use is available right now, and whether it needs a board target. */
	bool TryGetLandAbilityUi(int32 TerritoryIndex, int32 AbilityIndex, FString& OutName, FString& OutSpeedToken,
		FString& OutCostLine, FString& OutEffectLine, bool& bOutUsableNow, bool& bOutNeedsTarget) const;

	/** Same tokens as `tactics_master_cli` / `dispatch_master_cli_line` (updates controlled player & selection). */
	bool ExecMasterCliLine(const FString& Line, FString& OutMessage, bool bNotifyNetworkAuthority = true);

	/**
	 * Runs `dispatch_master_cli_line` starting with `PlayerId` as the controlled player ref (CLI `as` still applies).
	 * @param bNotifyNetworkAuthority When false, skips OnNetworkAuthorityCommitted (host applies remote CLI then pushes one snapshot separately).
	 */
	bool ExecMasterCliLineAsPlayer(int32 PlayerId, const FString& Line, FString& OutMessage, bool bNotifyNetworkAuthority = true);

	/**
	 * Host only: run one CLI line as the TCP/WebSocket peer seat without touching local `ControlledPlayer` / `Selected`.
	 * Keeps a separate selection pointer so remote `select` then `move` works; avoids host passing priority with the wrong player id.
	 */
	bool ExecRemoteTcpSeatCliLine(int32 SeatPlayerId, const FString& Line, FString& OutMessage);

	void ClearRemoteTcpSeatCliSession();
	/** Host: clear cached unit selection for one remote seat (e.g. WebSocket disconnect). */
	void ClearRemoteSeatUnitSelection(int32 SeatPlayerId);

	/** Same as `tactics::kNetworkCheckpointCommandInterval` (full snap every N authority commands). */
	static constexpr int32 NetworkCheckpointCommandInterval = 64;

	/** Host: full rules snapshot as `{ "t":"snap", "payload": ... }` JSON text for WebSocket. */
	FString ExportNetworkWireSnapshotJson() const;

	/** Host: increment `network_snap_seq` before exporting a checkpoint snapshot (not used for resync/connect). */
	void BumpNetworkSnapSeqForCheckpoint();

	void MarkBoardVisualDirty(ETacticsBoardVisualDirty Flags);
	uint32 ConsumeBoardVisualDirtyMask();

	/** Fingerprint strings for multiplayer catalog parity checks. */
	FString GetAbilityCatalogFingerprint() const;
	FString GetCardCatalogFingerprint() const;

	/** Shared-secret `cli` auth helpers - HMAC-SHA256 over the host-issued nonce, seat,
	 *  client-monotonic counter, and line (see tactics::compute_cli_auth_digest_utf8). */
	FString GenerateCliAuthNonce() const;
	FString ComputeCliSig(int32 SeatId, const FString& Line, const FString& RoomToken, const FString& AuthNonce, uint64 Ctr) const;
	bool VerifyCliSig(int32 SeatId, const FString& Line, const FString& Sig, const FString& RoomToken, const FString& AuthNonce,
		uint64 Ctr) const;

	/** Host: `{ "t":"cmd", "v":3, "seq", "seat", "line" }` wire JSON for WebSocket broadcast. */
	FString WrapNetworkCommandWireJson(int32 SeatPlayerId, const FString& Line, uint64 CommandSeq) const;

	/** Client (or host applying remote): replace GameState from wire JSON or raw inner snapshot. Clears selection, then restores the controlled player's selected unit if that entity still exists. */
	bool ApplyNetworkWireSnapshotJson(const FString& WireJson, FString& ErrOut);
	/** Client: apply `{t:snap_delta}` JSON patch against the most recent applied snapshot. */
	bool ApplyNetworkWireSnapshotDeltaJson(const FString& WireJson, FString& ErrOut);

	/** Client: apply one authoritative `cmd` frame (must be `LastAppliedCommandSeq + 1`). */
	bool ApplyNetworkCommandWireJson(const FString& WireJson, FString& ErrOut);

	uint64 GetLastAppliedCommandSeq() const { return LastAppliedCommandSeq; }
	/** Host authority journal length (bumps on every committed CLI). Used to refuse late client_deck. */
	uint64 GetMatchAuthorityCommandSeq() const;

	/** 1-based CLI column/row strings for `parse_grid_cell_1based_world` (merged bounds). */
	bool GetCliCellTokens(int WorldX, int WorldY, FString& OutCol, FString& OutRow) const;

	/** Number of cards shown in the hand strip (normally the controlled player; during end-turn discard, the player who must discard). */
	int32 GetControlledHandCount() const;
	int32 GetControlledReservesCount() const;
	bool TryGetReservesCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutCostLine, FString& OutRulesLine) const;
	/** 1-based reserves index. Returns total energy cost (sum of all types) for sorting. -1 if out of range. */
	int32 GetReservesCardTotalCost(int32 Index1Based) const;
	bool TryGetReservesCardArtId(int32 Index1Based, FString& OutArtId) const;
	bool TryGetReservesSpellRequiresFocusCaster(int32 Index1Based, bool& bOutRequiresFocus) const;
	bool TryGetReservesSpellRequiresStackTarget(int32 Index1Based, bool& bOutRequiresStackTarget) const;
	bool TryGetReservesSpellRequiresPlayerSeatTarget(int32 Index1Based, bool& bOutRequiresPlayerSeatTarget) const;
	bool TryGetReservesSpellRequiresBoardCell(int32 Index1Based, bool& bOutRequiresCell) const;

	/** 1-based hand index. Fills display strings for the hand bar (UTF-8 card text → FString). */
	bool TryGetHandCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutCostLine, FString& OutRulesLine) const;
	/** Compact unit stat line as inline-icon tokens ("{LIFE} N, {MELEE} X-Y, {MOVE} M, {ARMOR} A" when armor > 0). False for non-units. */
	bool TryGetHandCardStatTokens(int32 Index1Based, FString& OutStats) const;
	bool TryGetReservesCardStatTokens(int32 Index1Based, FString& OutStats) const;
	/** Catalog `uses_per_turn` for a hand/reserves unit card ability (reference display; OutUsesRemaining = OutUsesMax). */
	bool TryGetDetailCardAbilityUses(int32 Index1Based, bool bFromReserves, const FString& AbilityBlockName,
		int32 ActivatedAbilityIndex, int32& OutUsesRemaining, int32& OutUsesMax) const;
	/** Speed/range/cost strip for a hand/reserves unit card ability (includes `{ATTACK}` when applicable). */
	bool TryGetDetailCardAbilityMetadataStrip(int32 Index1Based, bool bFromReserves, const FString& AbilityBlockName,
		int32 ActivatedAbilityIndex, FString& OutMetadataStrip) const;
	/** 1-based hand index. Returns the total energy cost (sum of all energy types) for sorting. -1 if index is out of range. */
	int32 GetHandCardTotalCost(int32 Index1Based) const;
	/** 1-based hand index. Returns catalog art path id (e.g. ingenuity/.../grease_monkeys). */
	bool TryGetHandCardArtId(int32 Index1Based, FString& OutArtId) const;
	/** Selected board unit's source card strings and `art_id` (false if no selection or no catalog card). */
	bool TryGetSelectedUnitCardUi(FString& OutName, FString& OutTypeTag, FString& OutCostLine, FString& OutRulesLine, FString& OutArtId) const;
	/** Card-printed rules only (catalog strips, no runtime keyword/passive injection). For board detail composition. */
	bool TryGetSelectedUnitCardRulesBase(FString& OutRulesLine) const;
	/** `{KW:…}` strip for keywords gained in play (aura/temp), or empty. */
	FString GetSelectedUnitGainedKeywordsStrip() const;
	/** Passives granted after deploy (e.g. Hyperactive Scanning) not on the card definition. */
	void GetSelectedUnitRuntimePassiveStrips(TArray<FTacticsRuntimePassiveStrip>& OutStrips) const;
	/** Stat icon row for the selected unit's source card (same tokens as hand/reserves detail). */
	bool TryGetSelectedUnitCardStatTokens(FString& OutStats) const;
	/** Live move/attack/reaction availability stacks for board unit detail (`ui/actions/*`, used then ready). */
	bool TryGetSelectedUnitActionIconStacks(TArray<FString>& OutMoveArtIds, TArray<FString>& OutAttackArtIds,
		TArray<FString>& OutReactionArtIds) const;
	/** Keyword and status-effect definitions for a hand card (1-based index). Passives stay in card rules text. */
	void GetHandCardGlossaryEntries(int32 Index1Based, TArray<FTacticsCardGlossaryEntry>& OutEntries) const;
	/** Glossary rows referenced by `{KW:}` / `{GL:}` / `{FX:}` markers in authored or composed rules text. */
	void GetRulesTextGlossaryEntries(const FString& Rules, TArray<FTacticsCardGlossaryEntry>& OutEntries) const;
	/** Same glossary for a reserves card (1-based index). */
	void GetReservesCardGlossaryEntries(int32 Index1Based, TArray<FTacticsCardGlossaryEntry>& OutEntries) const;
	/** Glossary for the selected unit's source card definition. */
	void GetSelectedUnitCardGlossaryEntries(TArray<FTacticsCardGlossaryEntry>& OutEntries) const;
	/** Live status stacks on the selected board unit (empty when no selection). */
	void GetSelectedUnitActiveEffectEntries(TArray<FTacticsActiveEffectEntry>& OutEntries) const;

	/** Discard pile for a seat (1-based index, top of list = most recent). */
	int32 GetPlayerDiscardCount(int32 PlayerId) const;
	bool TryGetPlayerDiscardCardUi(int32 PlayerId, int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutRulesLine) const;
	int32 GetControlledDiscardCount() const;
	bool TryGetDiscardCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutRulesLine) const;

	/** Purgatory zone for a seat (exile-like; 1-based index, top of list = most recent). */
	int32 GetPlayerPurgatoryCount(int32 PlayerId) const;
	bool TryGetPlayerPurgatoryCardUi(int32 PlayerId, int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutRulesLine) const;
	int32 GetControlledPurgatoryCount() const;
	bool TryGetPurgatoryCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutRulesLine) const;
	/**
	 * If the hand slot is a spell, sets whether it requires a board cell to cast (matches `CastSpellAction` rules).
	 * @return false if index invalid or card is not a spell.
	 */
	bool TryGetHandSpellRequiresBoardCell(int32 Index1Based, bool& bOutRequiresCell) const;
	/** Modal spells (`spell.modes`): choose-one at cast time. */
	bool TryGetHandSpellIsModal(int32 Index1Based, bool& bOutIsModal) const;
	int32 GetHandSpellModalModeCount(int32 Index1Based) const;
	bool TryGetHandSpellModalModeUi(int32 Index1Based, int32 ModeIndex0, FString& OutLabel, FString& OutRules) const;
	bool TryGetHandSpellModeRequiresBoardCell(int32 Index1Based, int32 ModeIndex0, bool& bOutRequiresCell) const;
	bool SetBoardTargetPreviewForHandCardMode(int32 Index1Based, int32 ModeIndex0);
	bool TryGetReservesSpellIsModal(int32 Index1Based, bool& bOutIsModal) const;
	int32 GetReservesSpellModalModeCount(int32 Index1Based) const;
	bool TryGetReservesSpellModalModeUi(int32 Index1Based, int32 ModeIndex0, FString& OutLabel, FString& OutRules) const;
	bool TryGetReservesSpellModeRequiresBoardCell(int32 Index1Based, int32 ModeIndex0, bool& bOutRequiresCell) const;
	bool SetBoardTargetPreviewForReservesCardMode(int32 Index1Based, int32 ModeIndex0);
	/** slow / fast / burst for a hand spell card (false if not a spell). */
	bool TryGetHandSpellSpeedTag(int32 Index1Based, FString& OutSpeedTag) const;
	/** slow / fast / burst for a reserves spell card (false if not a spell). */
	bool TryGetReservesSpellSpeedTag(int32 Index1Based, FString& OutSpeedTag) const;
	/** True when the hand spell has the Focus keyword (must cast from a selected friendly unit). */
	bool TryGetHandSpellRequiresFocusCaster(int32 Index1Based, bool& bOutRequiresFocus) const;
	/** True when a non-Focus damaging hand spell must be cast through Insatiable Focus while that unit is on board. */
	bool TryGetHandSpellRequiresForcedFocusCaster(int32 Index1Based, bool& bOutRequires) const;
	bool TryGetReservesSpellRequiresForcedFocusCaster(int32 Index1Based, bool& bOutRequires) const;
	bool TryGetHandSpellUsesDirectionalAim(int32 Index1Based, bool& bOutUsesDirectionalAim) const;
	bool TryGetHandSpellUsesPushDirectionAim(int32 Index1Based, bool& bOutUsesPushDirectionAim) const;
	bool TryGetReservesSpellUsesDirectionalAim(int32 Index1Based, bool& bOutUsesDirectionalAim) const;
	bool TryGetReservesSpellUsesPushDirectionAim(int32 Index1Based, bool& bOutUsesPushDirectionAim) const;
	bool TryGetHandSpellRequiresStackTarget(int32 Index1Based, bool& bOutRequiresStackTarget) const;
	bool TryGetHandSpellRequiresPlayerSeatTarget(int32 Index1Based, bool& bOutRequiresPlayerSeatTarget) const;
	/** Multicast amount (1 if none). bOutPerCopyTargets when each copy needs its own target pick. */
	bool TryGetHandSpellMulticastInfo(int32 Index1Based, int32& OutAmount, bool& bOutPerCopyTargets) const;
	bool TryGetReservesSpellMulticastInfo(int32 Index1Based, int32& OutAmount, bool& bOutPerCopyTargets) const;
	/** Variable X at cast: energy type label, minimum X, and max affordable X for the controlled player. */
	bool TryGetHandSpellXCostInfo(int32 Index1Based, bool& bOutHasXCost, FString& OutEnergyType, int32& OutMinX,
		int32& OutMaxAffordableX) const;
	bool TryGetReservesSpellXCostInfo(int32 Index1Based, bool& bOutHasXCost, FString& OutEnergyType, int32& OutMinX,
		int32& OutMaxAffordableX) const;
	bool TryGetSelectedAbilityXCostInfo(const FString& AbilityKey, bool& bOutHasXCost, FString& OutEnergyType, int32& OutMinX,
		int32& OutMaxAffordableX) const;
	/** Pending attack declarations for the controlled player in the open attack batch (entity id + label). */
	void GetControlledOpenAttackUndeclareOptions(TArray<FString>& OutEntityIds, TArray<FString>& OutLabels) const;
	/** Team assignment summary for status display (empty when default 1v1 seating). */
	FString FormatTeamAssignmentSummary() const;
	/** Batched spell total cost on a stack/batch item (Echo Spell copies use this as X). */
	bool TryGetStackItemBatchedSpellTotalCost(const FString& ItemId, int32& OutTotalCost) const;
	bool CanHandSpellTargetStackSourceType(int32 Index1Based, const FString& SourceType) const;
	bool CanReservesSpellTargetStackSourceType(int32 Index1Based, const FString& SourceType) const;
	bool TryGetSelectedAbilityRequiresStackTarget(const FString& AbilityKey, bool& bOutRequiresStackTarget) const;
	bool TryGetSelectedAbilityTargetsEmptyCell(const FString& AbilityKey, bool& bOutTargetsEmptyCell) const;
	bool CanSelectedAbilityTargetStackSourceType(const FString& AbilityKey, const FString& SourceType) const;

	/** Stack entries from top (resolves next) to bottom; empty stack yields one placeholder line. */
	void GetSpellStackUiLines(TArray<FString>& OutTopFirstLines) const;
	void GetSpellStackUiItems(TArray<FTacticsStackItemUi>& OutTopFirstItems) const;
	/**
	 * Combined action-queue for the Action Queue panel:
	 * - Main/SecondMain: pending_spell_declarations_ (batched before SpellWindow opens)
	 * - AttackDeclaration/BonusAttackDeclaration: attack_phase_queue_ entries
	 * - SpellWindow/SecondSpellWindow/Defense/BonusDefense: live stack reactions
	 * Returns entries in declaration order (first declared at index 0).
	 */
	void GetActionQueueUiEntries(TArray<FTacticsActionQueueEntryUi>& OutEntries) const;
	/** Hover a batch-queue row to show a temporary source → target card overlay (cleared on leave). */
	void SetActionQueueHoverIndex(int32 QueueIndex);
	void ClearActionQueueHoverPreview();
	/** Click a batch-queue row to pin/unpin the overlay (stays on screen for glossary hover). */
	void ToggleActionQueuePinIndex(int32 QueueIndex);
	void ClearActionQueuePinPreview();
	bool IsActionQueuePreviewPinned() const;
	int32 GetPinnedActionQueueIndex() const;
	/** Clears hover when the queue index is stale after a rebuild. */
	void SyncActionQueueHoverAfterQueueChange();
	bool TryGetActionQueueHoverOverlay(FTacticsActionQueueHoverOverlayUi& OutPreview) const;
	/** Auto-presented opponent card play (center overlay); empty when inactive. */
	bool TryGetActiveOpponentPlayPresentation(FTacticsActionQueueHoverOverlayUi& OutPreview, FString& OutBanner) const;

	// ── Combat Visualization ─────────────────────────────────────────────────
	/** True while the phase batch queue is frozen before an attack resolves. */
	bool IsCombatVisualizationPaused() const;
	/** True when the phase batch queue still has attack entries awaiting resolution. */
	bool HasPendingAttacksInQueue() const;
	/** True during Defense or BonusDefense (not Spell Window). */
	bool IsDefenseReactionPhase() const;
	/** Local play: pass priority for each seat until the Defense window closes. */
	void AutoPassDefenseWindowUntilClosed(FString& OutLog);
	/** Snapshot the single attack encounter currently paused for visualization. */
	bool CaptureCombatVizPauseEncounter(FCombatEncounter& OutEncounter) const;
	/** Resume the paused attack (runs `resolve_attack` and continues the batch queue). */
	bool ResumeCombatVisualization(FString& OutMessage);
	/** Fill AttackerAfter / DefenderAfter and compute HP deltas after an attack resolves. */
	void FillCombatEncountersAfter(TArray<FCombatEncounter>& InOutEncounters) const;

	// ── Bot auto-play (headless MCTS in-editor) ───────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	bool IsBotAutoPlayEnabled() const { return bBotAutoPlayEnabled; }
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	void SetBotAutoPlayEnabled(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	bool IsBotSeatEnabled(int32 Seat) const;
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	void SetBotSeatEnabled(int32 Seat, bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	FString GetBotPolicyName() const { return BotPolicyName; }
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	void SetBotPolicyName(const FString& PolicyName);
	/** easy = random legal, normal = default MCTS, hard = deeper MCTS. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	void SetBotDifficulty(const FString& Difficulty);
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	FString GetBotDifficulty() const { return BotDifficulty; }
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	FString GetLastBotActionLog() const { return LastBotActionLog; }
	/** Run one bot action when a bot-controlled seat should act. Returns true if an action ran. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Bot")
	bool TryExecuteBotStep(FString& OutMessage);

	// ── Passive Attack Visualization (mortar shots etc.) ─────────────────────
	/** True if any passive attack viz events (e.g. mortar shots) are queued. */
	bool HasPendingPassiveAttackVizEvents() const;
	/**
	 * Pop the oldest queued passive viz event and build an FCombatEncounter from it.
	 * The encounter is always post-resolution (bIsPostResolution=true, damage already applied).
	 * Returns false if the queue is empty.
	 */
	bool TryConsumePassiveAttackVizEvent(FCombatEncounter& OutEncounter);

	void ClearSelection();
	/** Clears only the UI selection pointer (no game/pending mutation). WebSocket client: optimistic deselect before host ack. */
	void ClearSelectedUnitOnly();

private:
	void RequestBroadcastRefresh();
	bool TickRefreshFlush(float DeltaTime);
	void BroadcastRefresh();
	void RebuildTurnOrderRanks();
	void SyncControlledPlayerToActiveSeat();
	void RebuildReachableMoveHighlights();
	void RebuildPendingMoveFootprintHighlights();
	void RebuildAttackTargetHighlights();
	void RebuildBoardTargetHighlights();
	void RebuildActionQueueHoverHighlights();
	void RebuildBoardTargetAoEPreview();
	void RebuildDeployZoneHighlights();
	void ClearAbilityTargetPreviewArtifacts();
	void SkipEnergyUntilMainOrCap();
	bool TickAbilityDamagePopups(float DeltaTime);
	void EnsureAbilityDamagePopupTicker();
	void PruneExpiredAbilityDamagePopups();
	bool TickAbilityResolvePresentation(float DeltaTime);
	void EnsureAbilityResolvePresentationTicker();
	void FinishAbilityResolvePresentation();
	bool TickResolveFlashRefresh(float DeltaTime);
	void EnsureResolveFlashRefreshTicker();
	void PruneExpiredResolveFlash();

	TUniquePtr<tactics::GameState> Game;
	uint64 LastAppliedNetworkSnapSeq{0};
	uint64 LastAppliedCommandSeq{0};
	std::optional<uint64> LastAppliedSnapshotBaseSeq;
	std::string LastAppliedSnapshotInnerUtf8;
	uint32 PendingBoardVisualDirty = static_cast<uint32>(ETacticsBoardVisualDirty::All);
	FString ExpectedCatalogFingerprint;
	bool bRefreshFlushScheduled{false};
	FTSTicker::FDelegateHandle RefreshFlushTickerHandle;
	FTSTicker::FDelegateHandle ResolveFlashRefreshTickerHandle;
	FTSTicker::FDelegateHandle AbilityResolvePresentationTickerHandle;
	FTSTicker::FDelegateHandle AbilityDamagePopupTickerHandle;
	int ControlledPlayer{1};
	bool bAutoFollowActiveSeat{true};
	/** When true, append advanced clarifications after the normal description; false shows normal only. */
	bool bShowAdvancedCardText{false};
	std::optional<int> FixedControlledPlayer;
	std::shared_ptr<tactics::Unit> Selected;
	bool bUses3DBoardTiles{false};
	bool bTurnOrderViewEnabled{false};
	TMap<FString, int32> TurnOrderRankByEntityId;
	int PendingCliWx{-1};
	int PendingCliWy{-1};
	/** Host: per-seat unit selection for remote CLI (select/move/attack) - one entry per connected client seat. */
	std::unordered_map<int32, std::shared_ptr<tactics::Unit>> RemoteSeatUnitSelections;
	/** Cached move destinations for the current selection (world cell coordinates). */
	TSet<FIntPoint> ReachableMoveCells;
	TSet<FIntPoint> PendingMoveDestinationCells;
	TSet<FIntPoint> PendingMoveOriginCells;
	/** Cached enemy occupied cells attackable by the current selection from its committed position. */
	TSet<FIntPoint> AttackTargetCells;
	std::optional<tactics::BoardTargetKind> BoardTargetPreviewKind;
	/** When set, board-target highlights are constrained to this selected activated ability key. */
	FString BoardTargetPreviewAbilityKey;
	/** Effect key for an armed spell preview (e.g. helix_damage). */
	FString BoardTargetPreviewSpellEffectKey;
	FString BoardTargetPreviewSpellShapeKey;
	std::map<std::string, int> BoardTargetPreviewSpellPayload;
	int32 BoardTargetPreviewSpellMaxRange{4};
	/** When previewing a Focus spell from Selected, max Chebyshev range (0 = unlimited). */
	bool bBoardTargetPreviewUsesFocusCaster{false};
	/** True while arming a Focus spell before the player picks a casting unit. */
	bool bBoardTargetPreviewSelectingFocusCaster{false};
	bool bBoardTargetPreviewForcedDamageSpellFocus{false};
	/** Push-direction spell: waiting for adjacent direction cell after unit was chosen. */
	bool bBoardTargetPreviewSelectingPushDirection{false};
	int32 BoardTargetPushEntityWorldX{-1};
	int32 BoardTargetPushEntityWorldY{-1};
	std::vector<std::string> BoardTargetPreviewRequireUnitTypes;
	int32 BoardTargetFocusRange{0};
	TSet<FIntPoint> BoardTargetEnemyCells;
	TSet<FIntPoint> BoardTargetOtherCells;
	/** Cells that would take damage from the current directional area aim (hover). */
	TSet<FIntPoint> BoardTargetAoEBlastCells;
	/** Footprints highlighted while hovering / pinning a batch-queue row. */
	TSet<FIntPoint> ActionQueueHoverSourceCells;
	TSet<FIntPoint> ActionQueueHoverTargetCells;
	TSet<FIntPoint> ActionQueueHoverAoECells;
	int32 HoveredActionQueueIndex{-1};
	int32 PinnedActionQueueIndex{-1};
	struct FOpponentPlayPresentationSlot
	{
		FTacticsActionQueueHoverOverlayUi Overlay;
		FString Banner;
	};
	TArray<FOpponentPlayPresentationSlot> OpponentPlayPresentationQueue;
	TOptional<FOpponentPlayPresentationSlot> ActiveOpponentPlayPresentation;
	double ActiveOpponentPlayPresentationUntilTime{0.0};
	TSet<FString> SeenOpponentPlayFingerprints;
	FTSTicker::FDelegateHandle OpponentPlayPresentationTickerHandle;
	TSet<int32> BotControlledSeats;
	bool bBotAutoPlayEnabled{false};
	FString BotPolicyName{TEXT("mcts")};
	FString BotDifficulty{TEXT("normal")};
	tactics::bot::MctsConfig BotMctsConfig{};
	std::unique_ptr<tactics::bot::IBotPolicy> BotPolicy;
	tactics::bot::BotSession BotSession;
	std::mt19937 BotRng{42};
	FTSTicker::FDelegateHandle BotAutoPlayTickerHandle;
	double BotNextStepEarliestTime{0.0};
	/** Pause between non-move bot actions (deploy/cast/ability/attack). At 0.12s plays flashed by
	 *  faster than a human could read them ("spells cast for no reason"); ~0.4s lets each land. */
	static constexpr double BotStepIntervalSec = 0.4;
	/** Extra pause after a bot unit move so humans can follow repositioning (units also glide,
	 *  see ATacticsBoardUnitActor). */
	static constexpr double BotMoveStepIntervalSec = 0.9;
	static constexpr int32 BotMaxMoveActionsPerManeuverPhase = 1;
	tactics::TurnPhase BotMovePacingPhase{tactics::TurnPhase::Energy};
	int32 BotMovePacingActingSeat{0};
	int32 BotMoveActionsThisPhase{0};
	FString LastBotActionLog;
	void ResetOpponentPlayPresentationState(bool bSeedFingerprintsFromQueue);
	/** Local seat the UI represents (fixed MP seat, not transient dispatch controlled id). */
	int32 GetLocalViewingPlayerId() const;
	void PresentOpponentPlayForAuthorityCommand(int32 ActingPlayerId, int32 ViewingPlayerId, size_t QueueSizeBefore);
	void EnqueueOpponentPlayPresentation(FTacticsActionQueueHoverOverlayUi Overlay, int32 PlayerId);
	void TryStartNextOpponentPlayPresentation();
	void AdvanceOpponentPlayPresentation();
	bool TickOpponentPlayPresentation(float DeltaTime);
	void EnsureOpponentPlayPresentationTicker();
	void ResetBotRuntimeState();
	void RebuildBotPolicyInstance();
	bool ShouldRunBotAutoStep() const;
	bool TickBotAutoPlay(float DeltaTime);
	void EnsureBotAutoPlayTicker();
	void StopBotAutoPlayTicker();
	void ScheduleBotStepAfter(double DelaySec);
	void SyncBotMovePacingForCurrentPhase();
	bool ShouldSkipBotMoveActionsForPacing() const;
	void NoteBotMoveActionExecuted();
	bool TryBuildHoverOverlayForQueueIndex(int32 QueueIndex, FTacticsActionQueueHoverOverlayUi& OutPreview) const;
	bool TryBuildHoverOverlayForStackItem(const tactics::StackItem& Item, FTacticsActionQueueHoverOverlayUi& OutPreview) const;
	FString AbilityCastFlashEntityId;
	bool bAbilityCastFlashSuccess{false};
	double AbilityCastFlashStartTime{0.0};
	FTacticsAbilityResolveStaging PendingAbilityResolveStaging;
	bool bHasPendingAbilityResolveStaging{false};
	TOptional<FTacticsAbilityResolvePresentation> ActiveAbilityResolvePresentation;
	TArray<FTacticsAbilityDamagePopup> PendingAbilityDamagePopups;
	TArray<FTacticsAbilityDamagePopup> ActiveAbilityDamagePopups;
	TOptional<FTacticsBoardResolveFlash> ActiveResolveFlash;
	/** 1-based hand index of unit card armed for deploy; 0 when none. */
	int32 ArmedDeployHandIndex1Based{0};
	/** 1-based reserves index armed for deploy; 0 when none. */
	int32 ArmedDeployReservesIndex1Based{0};
	/** 1-based hand index of spell card armed for board-target preview; 0 when none. */
	int32 ArmedBoardTargetHandIndex1Based{0};
	/** 1-based reserves index of spell card armed for board-target preview; 0 when none. */
	int32 ArmedBoardTargetReservesIndex1Based{0};
	TSet<FIntPoint> DeployValidCells;
};
