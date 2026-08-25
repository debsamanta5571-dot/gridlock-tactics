#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TacticsMatchSubsystem.h"  // FCombatEncounter
#include "TacticsGameInstance.generated.h"

class ABattleVisualizationActor;
class STacticsBoardPanel;
class STacticsMainMenuPanel;
class STacticsDeckBuilderPanel;
class STacticsSettingsPanel;

/** Which full-screen flow the game instance is showing. */
UENUM()
enum class ETacticsAppScreen : uint8
{
	MainMenu,
	DeckBuilder,
	Match
};

/**
 * Main menu and deck builder use TacticsMaps levels; Play opens Template_Open (3D match) via OpenLevel.
 * Deck choice is applied when the match map loads.
 */
UCLASS()
class TACTICSGAMEUNREAL_API UTacticsGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/** Loads deck catalogs at engine startup. */
	virtual void Init() override;
	/** Applies packaged display settings, then queues the first UI attach. */
	virtual void StartGameInstance() override;
	/** Re-attaches menu or match UI after a map travel. */
	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
	/** Tears down network, battle viz, and viewport widgets. */
	virtual void Shutdown() override;

	ETacticsAppScreen GetCurrentScreen() const { return CurrentScreen; }
	/** True while the match map is the active screen. */
	bool ShouldRun3DMatchPresentation() const { return CurrentScreen == ETacticsAppScreen::Match; }

	FString GetLastMapTravelDebug() const { return LastMapTravelDebug; }

	/** Pre-match settings chosen on the main menu; applied to the next solo/host match. */
	struct FTacticsMatchSettings
	{
		bool bTeam2v2{false};
		bool bObjScanner{true};
		bool bObjOmni{true};
		bool bObjAether{true};
		bool bGiveFieldRequisition{false};
		/** Host: TCP listen port (default 8788). */
		int32 HostPort{8788};
		/** Host: bind 0.0.0.0 so LAN clients can join (default on for Host game). */
		bool bBindPublic{true};
		/** Optional shared secret for signed cli / client_deck frames. */
		FString RoomToken;
		/** Play vs AI: opponent deck key (Content/TacticsData/decks/<id>.json). */
		FString OpponentDeckId;
		/** Play vs AI: easy / normal / hard. */
		FString BotDifficulty{TEXT("normal")};
	};
	void SetPendingMatchSettings(const FTacticsMatchSettings& Settings) { PendingMatchSettings = Settings; }
	const FTacticsMatchSettings& GetPendingMatchSettings() const { return PendingMatchSettings; }

	/** Travels to the main menu map and shows the start panel. */
	void ShowMainMenu();
	/** Closes the match and returns to the start screen. */
	void ReturnToMainMenuFromMatch();
	/** Opens or closes the options overlay (Space). */
	void ToggleOptionsOverlay();
	/** Hides the options overlay if it is up. */
	void HideOptionsOverlay();
	bool IsOptionsOverlayVisible() const { return OptionsOverlay.IsValid(); }
	/** Exits the process (packaged game or standalone). */
	void RequestQuitGame();
	/** Travels to the deck builder map. */
	void ShowDeckBuilder();
	/** Starts a local Play vs AI match with the pending decks and difficulty. */
	void StartLocalMatchSolo();
	/** True while a local Play-vs-AI match is in progress (P2 is the MCTS bot). */
	bool IsSoloVsAiMatch() const { return bSoloVsAiMatch; }
	/** 8x12 sandbox: full catalog hand, omni zones, preset allies/enemies (ignores active deck). */
	void StartLocalSandboxMatch();
	/**
	 * Same as StartLocalSandboxMatch but every player's hand is seeded with all cards from one faction.
	 * FactionKey: set_code or legacy alias (gallantry, ingenuity, mythology), "core", "all".
	 */
	void StartLocalSandboxMatchWithFaction(const FString& FactionKey);
	/** Hosts a LAN match: listen on the pending port, then travel into the match map. */
	void StartLocalMatchAsHost();
	/** Joins a LAN host at JoinUrl, then travels into the match map. */
	void StartLocalMatchAsClient(const FString& JoinUrl);
	/** Host: open lobby on the main menu (listen only - match starts later). */
	void StartHostLobby();
	/** Client: join a host lobby URL while staying on the main menu. */
	void StartJoinLobby(const FString& JoinUrl);
	/** Leave lobby / stop host listen and return to idle main menu networking. */
	void LeaveMultiplayerLobby();
	/** Client: called when host sends match_begin - travel into the match map. */
	void EnterMatchFromLobbyAsClient();
	/** Host: after CommitLobby, travel into the match map without re-binding the listen socket. */
	void EnterMatchFromLobbyAsHost();
	bool IsPendingLobbyMatchEnter() const { return bPendingLobbyMatchEnter; }

	TSharedPtr<class STacticsBoardPanel> GetTacticsBoardPanel() const { return BoardPanel; }
	/** Rebuilds the match HUD from the current rules-core snapshot. */
	void RequestTacticsBoardRefresh();

	/** Opaque full-viewport combat curtain above board UI and 3D world. */
	void ShowCombatViewportCurtain(TSharedRef<SWidget> CombatContent);
	/** Removes the combat curtain and restores match HUD and world rendering. */
	void HideCombatViewportCurtain();
	bool IsCombatViewportCurtainActive() const { return CombatViewportCurtain.IsValid(); }

	/**
	 * Spawn the 3D battle visualization stage, switch the player camera to it,
	 * and show a Continue button.  OnContinue is called when the player clicks
	 * Continue on the last (or only) encounter.
	 */
	void EnterBattleVisualization(const TArray<FCombatEncounter>& Encounters,
	                               FSimpleDelegate OnContinue);
	/** Fire the stored Continue delegate without tearing down the battle stage (pre→post handoff). */
	void FireBattleVisualizationContinue();
	/** Reconfigure the live battle stage for new encounter data (same fight, resolved outcome). */
	void UpdateBattleVisualizationEncounters(const TArray<FCombatEncounter>& Encounters);
	/** Hide battle screens and destroy stage actors; optionally end the whole viz session. */
	void CloseBattleVisualizationView(bool bEndSession);
	/** Ends the battle visualization session and restores the match HUD. */
	void ExitBattleVisualization();
	bool IsBattleVisualizationActive() const { return BattleVisualizationActor.Get() != nullptr; }

private:
	/** Map to open for the current app screen. */
	enum class EMapFlowTarget : uint8
	{
		MainMenu,
		DeckBuilder,
		Match
	};

	/** Starts a local or hosted match after the pending settings are stored. */
	void EnterMatchExperience(bool bAsHost);
	/** Builds the match GameState once the match map has loaded. */
	void BootstrapMatchOnLoadedWorld(bool bStartHost);
	/** Opens the map for Target. Returns false if no candidate map is available. */
	bool TravelToMap(EMapFlowTarget Target);
	/** Shows the Slate panel that belongs to CurrentScreen. */
	void ApplyViewportUiForCurrentScreen();
	/** Removes main menu and deck builder widgets from the viewport. */
	void RemoveMenuAndBuilderUi();
	/** Attaches UI on the next tick so the world and viewport exist. */
	void AddUiDeferred();
	/** Attaches the match HUD after the match map is ready. */
	void AddBoardPanelDeferred();

	ETacticsAppScreen CurrentScreen{ETacticsAppScreen::MainMenu};
	bool bPendingMatchBootstrap{false};
	bool bPendingMatchStartHost{false};
	bool bPendingSandboxMatch{false};
	bool bPendingSoloVsAi{false};
	bool bSoloVsAiMatch{false};
	/** Joining a remote host: the local match ignores our map/objective settings (the host's snapshot
	 *  dictates them - only our deck matters) and we connect only once the match exists. */
	bool bPendingMatchJoinClient{false};
	/** Entering match from lobby: host already built GameState; client keeps WS and awaits snap. */
	bool bPendingLobbyMatchEnter{false};
	bool bPendingLobbyMatchAsHost{false};
	FString PendingJoinUrl;
	FString PendingSandboxFactionKey;
	FTacticsMatchSettings PendingMatchSettings;
	FString LastMapTravelDebug;
	TSharedPtr<STacticsMainMenuPanel> MainMenuPanel;
	TSharedPtr<STacticsDeckBuilderPanel> DeckBuilderPanel;
	TSharedPtr<STacticsBoardPanel> BoardPanel;
	TSharedPtr<STacticsSettingsPanel> OptionsOverlay;
	static constexpr int32 OptionsOverlayZOrder = 400000;
	TSharedPtr<class SWidget> CombatViewportCurtain;
	bool bSavedDisableWorldRendering{false};
	int32 UiAddAttempts{0};

	// ── 3D battle visualization ───────────────────────────────────────────────
	/** Adds the Continue button over the battle screens. */
	void AddBattleContinueWidget();
	/** Removes the Continue button. */
	void RemoveBattleContinueWidget();

	/** Builds / rebuilds the side-by-side render-target widget for both sides. */
	void ShowBattleScreens();
	/** Removes the side-by-side battle render-target widget. */
	void HideBattleScreens();
	/** Re-configure both side actors for the encounter at PendingBattleEncounterIndex. */
	void ConfigureBattleScreensForCurrentEncounter();
	/** Clears camera continuity and choreography-dedupe state for a new viz session. */
	void ResetBattleVisualizationSessionState();

	// Two genuinely separate single-fighter visualizations, each rendering into its
	// own render target; the two targets are shown side by side in BattleScreensWidget.
	TObjectPtr<ABattleVisualizationActor> BattleVisualizationActor;   // attacker side
	TObjectPtr<ABattleVisualizationActor> DefenderBattleViz;          // defender side
	TSharedPtr<struct FSlateBrush>        AttackerScreenBrush;
	TSharedPtr<struct FSlateBrush>        DefenderScreenBrush;
	TSharedPtr<SWidget>                   BattleScreensWidget;
	TSharedPtr<SWidget>                   BattleContinueWidget;
	TWeakObjectPtr<AActor>                SavedBattleViewTarget;
	TArray<FCombatEncounter>              PendingBattleEncounters;
	int32                                 PendingBattleEncounterIndex{0};
	FSimpleDelegate                       PendingBattleContinueDelegate;

	// Cross-view camera continuity.  Each battle view spawns a fresh BattleVisualizationActor, so
	// the previous view's final camera framing is remembered HERE and fed into the next actor
	// (SetExternalGlideOrigin) so the camera travels from the last battle's pose to the new one
	// instead of restarting from a synthetic origin every time.
	bool                                  bHasLastBattleCam{false};
	FVector                               LastBattleCamPosA{FVector::ZeroVector};
	FVector                               LastBattleCamPosB{FVector::ZeroVector};
	FRotator                              LastBattleCamRotA{FRotator::ZeroRotator};
	FRotator                              LastBattleCamRotB{FRotator::ZeroRotator};

	// Choreography dedupe: a UI re-entry can present the SAME resolved fight's result screen more
	// than once; without this the attack→counter lunge would replay each time.  We remember the
	// last resolved fight we already animated (by combatants + damage) and suppress the lunge if it
	// comes up again.  Both this and the camera-continuity state reset when the viz fully closes.
	bool                                  bHasLastChoreographedFight{false};
	FString                               LastChoreographedFightKey;

	static constexpr int32 CombatViewportZOrder = 300000;
};
