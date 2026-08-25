#include "TacticsGameInstance.h"
#include "TacticsCardText.h"

#include "TacticsBoardPlayerController.h"
#include "BattleVisualizationActor.h"
#include "SCombatViewportCurtain.h"
#include "STacticsBoardPanel.h"
#include "STacticsDeckBuilderPanel.h"
#include "STacticsMainMenuPanel.h"
#include "TacticsDeckLibrarySubsystem.h"
#include "TacticsMapNavigator.h"
#include "TacticsMapSettings.h"
#include "TacticsMatchSubsystem.h"
#include "TacticsWebSocketSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "TimerManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void UTacticsGameInstance::Init()
{
	// Catalogs must load before any match UI reads card names.
	Super::Init();
	CurrentScreen = ETacticsAppScreen::MainMenu;
	if (UTacticsDeckLibrarySubsystem* Lib = GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
		Lib->EnsureGameplayCatalogsLoaded();
	}
}

void UTacticsGameInstance::StartGameInstance()
{
	Super::StartGameInstance();
#if !WITH_EDITOR
	// Packaged builds otherwise inherit a low ResolutionQuality from the engine default.
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		const FIntPoint Desktop = Settings->GetDesktopResolution();
		if (Desktop.X >= 1280 && Desktop.Y >= 720)
		{
			Settings->SetScreenResolution(Desktop);
		}
		Settings->SetOverallScalabilityLevel(3);
		Settings->SetResolutionScaleValueEx(100.f);
		Settings->SetVSyncEnabled(true);
		Settings->ApplySettings(false);
	}
#endif
	UiAddAttempts = 0;
	GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UTacticsGameInstance::AddUiDeferred));
}

void UTacticsGameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
	Super::OnWorldChanged(OldWorld, NewWorld);
	if (!NewWorld) {
		return;
	}
	UiAddAttempts = 0;
	if (bPendingMatchBootstrap && CurrentScreen == ETacticsAppScreen::Match) {
		bPendingMatchBootstrap = false;
		const bool bHost = bPendingMatchStartHost;
		bPendingMatchStartHost = false;
		GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this, bHost]() { BootstrapMatchOnLoadedWorld(bHost); }));
	}
	GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UTacticsGameInstance::AddUiDeferred));
}

bool UTacticsGameInstance::TravelToMap(const EMapFlowTarget Target)
{
	const UTacticsMapSettings& Settings = UTacticsMapSettings::Get();
	TArray<FString> Candidates;
	FString GameModeOptions;

	switch (Target) {
		case EMapFlowTarget::MainMenu:
			Candidates = {Settings.MainMenuMap};
			GameModeOptions = TacticsMapNavigator::BuildGameModeOptions(Settings.MenuGameMode.ToString());
			break;
		case EMapFlowTarget::DeckBuilder:
			Candidates = {Settings.DeckBuilderMap, Settings.MainMenuMap};
			GameModeOptions = TacticsMapNavigator::BuildGameModeOptions(Settings.MenuGameMode.ToString());
			break;
		case EMapFlowTarget::Match:
			Candidates = Settings.GetMatchMapCandidates();
			GameModeOptions = TacticsMapNavigator::BuildGameModeOptions(Settings.MatchGameMode.ToString());
			break;
	}

	FString ResolvedAsset;
	FString ResolvedPackage;
	FString ResolveDebug;
	if (!TacticsMapNavigator::ResolveFirstAvailableMap(Candidates, ResolvedAsset, ResolvedPackage, ResolveDebug)) {
		LastMapTravelDebug = ResolveDebug;
		UE_LOG(LogTemp, Warning, TEXT("%s"), *LastMapTravelDebug);
		return false;
	}

	const FTacticsMapOpenResult OpenResult =
		TacticsMapNavigator::OpenMapForGameInstance(this, ResolvedAsset, GameModeOptions);
	LastMapTravelDebug = FString::Printf(TEXT("%s | %s"), *ResolveDebug, *OpenResult.DebugMessage);
	UE_LOG(LogTemp, Log, TEXT("Tactics travel: %s"), *LastMapTravelDebug);
	return OpenResult.bOpened;
}

void UTacticsGameInstance::RemoveMenuAndBuilderUi()
{
	if (UGameViewportClient* ViewportClient = GetGameViewportClient()) {
		if (MainMenuPanel.IsValid()) {
			ViewportClient->RemoveViewportWidgetContent(MainMenuPanel.ToSharedRef());
		}
		if (DeckBuilderPanel.IsValid()) {
			ViewportClient->RemoveViewportWidgetContent(DeckBuilderPanel.ToSharedRef());
		}
	}
	MainMenuPanel.Reset();
	DeckBuilderPanel.Reset();
}

void UTacticsGameInstance::ApplyViewportUiForCurrentScreen()
{
	UGameViewportClient* ViewportClient = GetGameViewportClient();
	if (!ViewportClient) {
		return;
	}

	auto EnsureSlateMouseCursor = [this]() {
		if (UWorld* World = GetWorld()) {
			ATacticsBoardPlayerController::ApplySlateFriendlyMouseCursor(World->GetFirstPlayerController());
		}
	};

	switch (CurrentScreen) {
		case ETacticsAppScreen::MainMenu:
			RemoveMenuAndBuilderUi();
			if (BoardPanel.IsValid()) {
				ViewportClient->RemoveViewportWidgetContent(BoardPanel.ToSharedRef());
			}
			MainMenuPanel = SNew(STacticsMainMenuPanel).GameInstance(this);
			ViewportClient->AddViewportWidgetContent(MainMenuPanel.ToSharedRef(), 200000);
			if (MainMenuPanel.IsValid()) {
				MainMenuPanel->RefreshUi();
			}
			EnsureSlateMouseCursor();
			break;
		case ETacticsAppScreen::DeckBuilder:
			RemoveMenuAndBuilderUi();
			if (BoardPanel.IsValid()) {
				ViewportClient->RemoveViewportWidgetContent(BoardPanel.ToSharedRef());
			}
			DeckBuilderPanel = SNew(STacticsDeckBuilderPanel).GameInstance(this);
			ViewportClient->AddViewportWidgetContent(DeckBuilderPanel.ToSharedRef(), 200000);
			EnsureSlateMouseCursor();
			break;
		case ETacticsAppScreen::Match:
			RemoveMenuAndBuilderUi();
			if (!BoardPanel.IsValid()) {
				AddBoardPanelDeferred();
			} else {
				ViewportClient->AddViewportWidgetContent(BoardPanel.ToSharedRef(), 100000);
			}
			break;
	}
}

void UTacticsGameInstance::AddUiDeferred()
{
	if (!GetGameViewportClient()) {
		if (++UiAddAttempts < 180) {
			GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UTacticsGameInstance::AddUiDeferred));
		}
		return;
	}
	ApplyViewportUiForCurrentScreen();
	UiAddAttempts = 0;
}

void UTacticsGameInstance::ShowMainMenu()
{
	CurrentScreen = ETacticsAppScreen::MainMenu;
	bPendingLobbyMatchEnter = false;
	bPendingLobbyMatchAsHost = false;
	bPendingSoloVsAi = false;
	bSoloVsAiMatch = false;
	if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
		// Don't tear down an active lobby when refreshing the menu chrome.
		if (!Net->IsInLobby()) {
			Net->Disconnect();
			Net->StopHost();
		}
	}
	RemoveMenuAndBuilderUi();
	if (!TravelToMap(EMapFlowTarget::MainMenu)) {
		ApplyViewportUiForCurrentScreen();
	}
}

void UTacticsGameInstance::ShowDeckBuilder()
{
	CurrentScreen = ETacticsAppScreen::DeckBuilder;
	RemoveMenuAndBuilderUi();
	if (!TravelToMap(EMapFlowTarget::DeckBuilder)) {
		ApplyViewportUiForCurrentScreen();
	}
}

void UTacticsGameInstance::BootstrapMatchOnLoadedWorld(const bool bStartHost)
{
	const bool bSandbox = bPendingSandboxMatch;
	bPendingSandboxMatch = false;
	const bool bLobbyEnter = bPendingLobbyMatchEnter;
	const bool bLobbyHost = bPendingLobbyMatchAsHost;
	bPendingLobbyMatchEnter = false;
	bPendingLobbyMatchAsHost = false;

	if (!bSandbox && !bLobbyHost) {
		if (UTacticsDeckLibrarySubsystem* Lib = GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
			FString Err;
			if (!Lib->ApplyActiveDeckForMatch(Err)) {
				UE_LOG(LogTemp, Warning, TEXT("Active deck: %s"), *Err);
			}
		}
	}

	if (UTacticsMatchSubsystem* Match = GetSubsystem<UTacticsMatchSubsystem>()) {
		if (bSandbox) {
			const FString FactionKey = PendingSandboxFactionKey;
			PendingSandboxFactionKey = TEXT("");
			Match->ResetSandboxMatchWithFaction(FactionKey);
		} else if (bLobbyHost) {
			// Host already built GameState in HostStartMatchFromLobby - keep it.
			Match->SetAutoFollowActiveSeat(false);
			Match->SetControlledPlayer(1);
		} else if (bLobbyEnter) {
			// Client already received match_begin (+ often a snap) while still on the menu.
			// Do NOT ResetMatchWithProfile here - that wipes the authority snapshot.
			Match->SetAutoFollowActiveSeat(false);
			if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
				const int32 Seat = Net->GetClientRemoteSeatPlayerId();
				Match->SetControlledPlayer(Seat > 0 ? Seat : 2);
				Net->RequestMatchResyncAfterLobbyEnter();
			}
		} else if (bPendingMatchJoinClient) {
			FTacticsMatchSetupProfile Profile;
			Profile.bSeedDemoState = false;
			Profile.bAutoFollowActiveSeat = false;
			Profile.bTeam2v2 = PendingMatchSettings.bTeam2v2;
			Profile.bObjScanner = PendingMatchSettings.bObjScanner;
			Profile.bObjOmni = PendingMatchSettings.bObjOmni;
			Profile.bObjAether = PendingMatchSettings.bObjAether;
			Profile.bGiveFieldRequisition = PendingMatchSettings.bGiveFieldRequisition;
			Match->ResetMatchWithProfile(PendingMatchSettings.bTeam2v2 ? 4 : 2, Profile);
		} else {
			FTacticsMatchSetupProfile Profile;
			Profile.bSeedDemoState = false;
			Profile.bAutoFollowActiveSeat = true;
			Profile.bTeam2v2 = PendingMatchSettings.bTeam2v2;
			Profile.bObjScanner = PendingMatchSettings.bObjScanner;
			Profile.bObjOmni = PendingMatchSettings.bObjOmni;
			Profile.bObjAether = PendingMatchSettings.bObjAether;
			Profile.bGiveFieldRequisition = PendingMatchSettings.bGiveFieldRequisition;
			if (UTacticsDeckLibrarySubsystem* Lib = GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
				Profile.DeckId = Lib->GetActiveDeckKey();
			}
			if (bPendingSoloVsAi && !PendingMatchSettings.OpponentDeckId.IsEmpty()) {
				Profile.OpponentDeckId = PendingMatchSettings.OpponentDeckId;
			}
			Match->ResetMatchWithProfile(PendingMatchSettings.bTeam2v2 ? 4 : 2, Profile);
		}

		if (bPendingSoloVsAi) {
			bPendingSoloVsAi = false;
			bSoloVsAiMatch = true;
			Match->SetControlledPlayer(1);
			Match->SetAutoFollowActiveSeat(true);
			Match->SetBotSeatEnabled(2, true);
			Match->SetBotDifficulty(PendingMatchSettings.BotDifficulty);
			Match->SetBotAutoPlayEnabled(true);
		} else {
			bSoloVsAiMatch = false;
		}
	}

	if (bStartHost && !bLobbyHost) {
		if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
			const FTacticsMatchSettings& S = PendingMatchSettings;
			Net->SetRoomToken(S.RoomToken);
			const int32 Port = S.HostPort > 0 ? S.HostPort : 8788;
			Net->StartHost(Port, S.bBindPublic, /*bResetMatch=*/false);
		}
	}
	if (bPendingMatchJoinClient && !bLobbyEnter) {
		const FString Url = PendingJoinUrl;
		bPendingMatchJoinClient = false;
		PendingJoinUrl.Empty();
		if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
			Net->ConnectClient(Url);
		}
	} else if (bPendingMatchJoinClient) {
		bPendingMatchJoinClient = false;
		PendingJoinUrl.Empty();
	}

	ApplyViewportUiForCurrentScreen();
	RequestTacticsBoardRefresh();
}

void UTacticsGameInstance::EnterMatchExperience(const bool bAsHost)
{
	CurrentScreen = ETacticsAppScreen::Match;
	bPendingMatchBootstrap = true;
	bPendingMatchStartHost = bAsHost;

	if (UTacticsDeckLibrarySubsystem* Lib = GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
		FString Err;
		Lib->ApplyActiveDeckForMatch(Err);
	}

	RemoveMenuAndBuilderUi();

	if (TravelToMap(EMapFlowTarget::Match)) {
		return;
	}

	bPendingMatchBootstrap = false;
	bPendingMatchStartHost = false;
	LastMapTravelDebug += TEXT(" | Match map travel failed.");
	BootstrapMatchOnLoadedWorld(bAsHost);
}

void UTacticsGameInstance::StartLocalMatchSolo()
{
	bPendingSandboxMatch = false;
	bPendingSoloVsAi = true;
	bSoloVsAiMatch = false;
	EnterMatchExperience(false);
}

void UTacticsGameInstance::StartLocalSandboxMatch()
{
	StartLocalSandboxMatchWithFaction(TEXT("all"));
}

void UTacticsGameInstance::StartLocalSandboxMatchWithFaction(const FString& FactionKey)
{
	PendingSandboxFactionKey = FactionKey;
	bPendingSandboxMatch = true;
	bPendingSoloVsAi = false;
	bSoloVsAiMatch = false;
	EnterMatchExperience(false);
}

void UTacticsGameInstance::StartLocalMatchAsHost()
{
	StartHostLobby();
}

void UTacticsGameInstance::StartLocalMatchAsClient(const FString& JoinUrl)
{
	StartJoinLobby(JoinUrl);
}

void UTacticsGameInstance::StartHostLobby()
{
	bPendingSandboxMatch = false;
	FString DeckName;
	FString DeckJson;
	if (UTacticsDeckLibrarySubsystem* Lib = GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
		DeckName = Lib->GetActiveDeckKey();
		FString Err;
		if (!Lib->ExportDeckJsonByKey(DeckName, DeckJson, Err)) {
			UE_LOG(LogTemp, Warning, TEXT("Host lobby deck: %s"), *Err);
		}
	}
	if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
		FTacticsLobbyMatchSettings LobbyCfg;
		LobbyCfg.bTeam2v2 = PendingMatchSettings.bTeam2v2;
		LobbyCfg.bObjScanner = PendingMatchSettings.bObjScanner;
		LobbyCfg.bObjOmni = PendingMatchSettings.bObjOmni;
		LobbyCfg.bObjAether = PendingMatchSettings.bObjAether;
		LobbyCfg.bGiveFieldRequisition = PendingMatchSettings.bGiveFieldRequisition;
		LobbyCfg.HostPort = PendingMatchSettings.HostPort;
		LobbyCfg.bBindPublic = PendingMatchSettings.bBindPublic;
		LobbyCfg.RoomToken = PendingMatchSettings.RoomToken;
		if (!Net->BeginHostLobby(LobbyCfg, DeckName, DeckJson)) {
			UE_LOG(LogTemp, Error, TEXT("BeginHostLobby failed"));
		}
	}
	CurrentScreen = ETacticsAppScreen::MainMenu;
	ApplyViewportUiForCurrentScreen();
	RequestTacticsBoardRefresh();
}

void UTacticsGameInstance::StartJoinLobby(const FString& JoinUrl)
{
	bPendingSandboxMatch = false;
	FString DeckName;
	FString DeckJson;
	if (UTacticsDeckLibrarySubsystem* Lib = GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
		DeckName = Lib->GetActiveDeckKey();
		FString Err;
		Lib->ExportDeckJsonByKey(DeckName, DeckJson, Err);
	}
	if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
		Net->SetRoomToken(PendingMatchSettings.RoomToken);
		const FString Url = JoinUrl.IsEmpty() ? TEXT("ws://127.0.0.1:8788/") : JoinUrl;
		Net->BeginJoinLobby(Url, DeckName, DeckJson);
	}
	CurrentScreen = ETacticsAppScreen::MainMenu;
	ApplyViewportUiForCurrentScreen();
}

void UTacticsGameInstance::LeaveMultiplayerLobby()
{
	if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
		Net->LeaveLobby();
	}
	bPendingLobbyMatchEnter = false;
	bPendingLobbyMatchAsHost = false;
	ApplyViewportUiForCurrentScreen();
}

void UTacticsGameInstance::EnterMatchFromLobbyAsHost()
{
	bPendingSandboxMatch = false;
	bPendingLobbyMatchEnter = true;
	bPendingLobbyMatchAsHost = true;
	if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
		const FTacticsLobbyMatchSettings& L = Net->GetLobbySettings();
		PendingMatchSettings.bTeam2v2 = L.bTeam2v2;
		PendingMatchSettings.bObjScanner = L.bObjScanner;
		PendingMatchSettings.bObjOmni = L.bObjOmni;
		PendingMatchSettings.bObjAether = L.bObjAether;
		PendingMatchSettings.bGiveFieldRequisition = L.bGiveFieldRequisition;
		PendingMatchSettings.HostPort = L.HostPort;
		PendingMatchSettings.bBindPublic = L.bBindPublic;
		PendingMatchSettings.RoomToken = L.RoomToken;
	}
	EnterMatchExperience(false);  // already hosting - do not StartHost again
}

void UTacticsGameInstance::EnterMatchFromLobbyAsClient()
{
	bPendingSandboxMatch = false;
	bPendingLobbyMatchEnter = true;
	bPendingLobbyMatchAsHost = false;
	bPendingMatchJoinClient = false;
	if (UTacticsWebSocketSubsystem* Net = GetSubsystem<UTacticsWebSocketSubsystem>()) {
		const FTacticsLobbyMatchSettings& L = Net->GetLobbySettings();
		PendingMatchSettings.bTeam2v2 = L.bTeam2v2;
		PendingMatchSettings.bObjScanner = L.bObjScanner;
		PendingMatchSettings.bObjOmni = L.bObjOmni;
		PendingMatchSettings.bObjAether = L.bObjAether;
		PendingMatchSettings.bGiveFieldRequisition = L.bGiveFieldRequisition;
	}
	EnterMatchExperience(false);
}

void UTacticsGameInstance::AddBoardPanelDeferred()
{
	if (BoardPanel.IsValid() && CurrentScreen == ETacticsAppScreen::Match) {
		if (UGameViewportClient* ViewportClient = GetGameViewportClient()) {
			ViewportClient->AddViewportWidgetContent(BoardPanel.ToSharedRef(), 100000);
		}
		return;
	}

	if (!GetSubsystem<UTacticsMatchSubsystem>()) {
		if (++UiAddAttempts < 180) {
			GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UTacticsGameInstance::AddBoardPanelDeferred));
		}
		return;
	}

	if (UGameViewportClient* ViewportClient = GetGameViewportClient()) {
		BoardPanel = SNew(STacticsBoardPanel).Subsystem(GetSubsystem<UTacticsMatchSubsystem>());
		if (CurrentScreen == ETacticsAppScreen::Match) {
			ViewportClient->AddViewportWidgetContent(BoardPanel.ToSharedRef(), 100000);
		}
		UiAddAttempts = 0;
		return;
	}

	if (++UiAddAttempts < 180) {
		GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UTacticsGameInstance::AddBoardPanelDeferred));
	}
}

void UTacticsGameInstance::RequestTacticsBoardRefresh()
{
	if (BoardPanel.IsValid()) {
		BoardPanel->RefreshBoardUi();
	}
}

void UTacticsGameInstance::ShowCombatViewportCurtain(TSharedRef<SWidget> CombatContent)
{
	UGameViewportClient* ViewportClient = GetGameViewportClient();
	if (!ViewportClient) {
		return;
	}

	HideCombatViewportCurtain();

	if (BoardPanel.IsValid()) {
		BoardPanel->SetMatchUiHidden(true);
	}

	bSavedDisableWorldRendering = ViewportClient->bDisableWorldRendering;
	ViewportClient->bDisableWorldRendering = true;

	CombatViewportCurtain =
		SNew(SCombatViewportCurtain)
		[
			CombatContent
		];
	ViewportClient->AddViewportWidgetContent(CombatViewportCurtain.ToSharedRef(), CombatViewportZOrder);
}

void UTacticsGameInstance::HideCombatViewportCurtain()
{
	const bool bHadCurtain = CombatViewportCurtain.IsValid();
	if (UGameViewportClient* ViewportClient = GetGameViewportClient()) {
		if (bHadCurtain) {
			ViewportClient->RemoveViewportWidgetContent(CombatViewportCurtain.ToSharedRef());
		}
		ViewportClient->bDisableWorldRendering = bSavedDisableWorldRendering;
	}
	CombatViewportCurtain.Reset();

	if (BoardPanel.IsValid()) {
		const bool bWasMatchUiHidden = BoardPanel->IsMatchUiHidden();
		BoardPanel->SetMatchUiHidden(false);
		if (bHadCurtain || bWasMatchUiHidden) {
			BoardPanel->RefreshBoardUi();
		}
	}
}

void UTacticsGameInstance::ResetBattleVisualizationSessionState()
{
	bHasLastBattleCam = false;
	bHasLastChoreographedFight = false;
	LastChoreographedFightKey.Reset();
}

void UTacticsGameInstance::Shutdown()
{
	if (UTacticsWebSocketSubsystem* Ws = GetSubsystem<UTacticsWebSocketSubsystem>()) {
		Ws->Disconnect();
		Ws->StopHost();
	}
	if (UGameViewportClient* ViewportClient = GetGameViewportClient()) {
		HideCombatViewportCurtain();
		RemoveBattleContinueWidget();
		HideBattleScreens();
		if (BoardPanel.IsValid()) {
			ViewportClient->RemoveViewportWidgetContent(BoardPanel.ToSharedRef());
		}
	}
	RemoveMenuAndBuilderUi();
	BoardPanel.Reset();
	Super::Shutdown();
}

// ── 3D Battle Visualization ──────────────────────────────────────────────────

void UTacticsGameInstance::EnterBattleVisualization(
	const TArray<FCombatEncounter>& Encounters, FSimpleDelegate OnContinue)
{
	if (Encounters.Num() == 0) {
		// Nothing to show - fire the callback immediately.
		OnContinue.ExecuteIfBound();
		RequestTacticsBoardRefresh();
		return;
	}

	UWorld* World = GetWorld();
	if (!World) { return; }

	// Tear down any previous battle stages that weren't cleaned up.
	if (IsValid(BattleVisualizationActor)) {
		BattleVisualizationActor->Destroy();
		BattleVisualizationActor = nullptr;
	}
	if (IsValid(DefenderBattleViz)) {
		DefenderBattleViz->Destroy();
		DefenderBattleViz = nullptr;
	}
	RemoveBattleContinueWidget();
	HideBattleScreens();

	// ── Store data ────────────────────────────────────────────────────────────
	PendingBattleEncounters          = Encounters;
	PendingBattleEncounterIndex      = 0;
	PendingBattleContinueDelegate    = OnContinue;

	// ── Spawn ONE battle stage that renders the whole melee (both fighters + all ──
	//    bystanders) into a single landscape render target.  Melee is shown as one
	//    screen with a divider line down the middle, not two split screens.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector BattleStage(ABattleVisualizationActor::kStageWorldX, 0.f, 0.f);
	BattleVisualizationActor = World->SpawnActor<ABattleVisualizationActor>(
		BattleStage, FRotator::ZeroRotator, SpawnParams);

	ConfigureBattleScreensForCurrentEncounter();

	// ── Keep the main camera where it is; the battle is shown via the two ──────
	//    render-target screens drawn over a full-viewport curtain.
	if (BoardPanel.IsValid()) {
		BoardPanel->SetMatchUiHidden(true);
	}
	ShowBattleScreens();
	AddBattleContinueWidget();
}

void UTacticsGameInstance::ConfigureBattleScreensForCurrentEncounter()
{
	if (PendingBattleEncounters.Num() == 0) { return; }
	const int32 Idx = FMath::Clamp(PendingBattleEncounterIndex, 0,
	                               PendingBattleEncounters.Num() - 1);
	const FCombatEncounter& Enc = PendingBattleEncounters[Idx];

	if (IsValid(BattleVisualizationActor)) {
		// Seed the camera glide from the PREVIOUS battle view's final framing so the camera travels
		// battle-to-battle (and so the pre-roll → resolved re-show of the SAME fight is detected as
		// no-move and doesn't replay the establishing animation).  See SetExternalGlideOrigin.
		if (bHasLastBattleCam) {
			BattleVisualizationActor->SetExternalGlideOrigin(LastBattleCamPosA, LastBattleCamPosB);
			// Also seed the previous orientation so the camera SLERPs (not snaps) between battles of
			// different orientation - e.g. a vertical→horizontal transition swings smoothly.
			BattleVisualizationActor->SetExternalGlideRotation(LastBattleCamRotA, LastBattleCamRotB);
		}

		// Dedupe the attack→counter lunge: a UI re-entry can present the same resolved fight's
		// result screen more than once.  Only animate the FIRST time we see a given resolved fight;
		// a repeat shows the settled result without replaying the lunge.
		bool bAllowChoreography = true;
		if (Enc.bIsPostResolution) {
			const FString FightKey = FString::Printf(TEXT("%s|%s|%d|%d"),
				*Enc.AttackerBefore.EntityId, *Enc.DefenderBefore.EntityId,
				Enc.AttackDamage, Enc.CounterDamage);
			if (bHasLastChoreographedFight && FightKey == LastChoreographedFightKey) {
				bAllowChoreography = false;
			} else {
				LastChoreographedFightKey  = FightKey;
				bHasLastChoreographedFight = true;
			}
		}
		BattleVisualizationActor->SetChoreographyAllowed(bAllowChoreography);

		BattleVisualizationActor->PlayEncounter(Enc);
		// Remember where this view settled so the next spawned actor can glide from here.
		LastBattleCamPosA = BattleVisualizationActor->GetCamFramingA();
		LastBattleCamPosB = BattleVisualizationActor->GetCamFramingB();
		LastBattleCamRotA = BattleVisualizationActor->GetCamRotA();
		LastBattleCamRotB = BattleVisualizationActor->GetCamRotB();
		bHasLastBattleCam = true;
	}
}

void UTacticsGameInstance::ShowBattleScreens()
{
	UGameViewportClient* VC = GetGameViewportClient();
	if (!VC) { return; }

	// Two cameras over one shared field: the attacker screen (left) and defender screen
	// (right), shown side-by-side with a thin divider between them.  Each is a live render
	// target that keeps updating as the cameras glide between battles in the queue.
	UTextureRenderTarget2D* AtkRT = IsValid(BattleVisualizationActor)
		? BattleVisualizationActor->GetRenderTarget() : nullptr;
	UTextureRenderTarget2D* DefRT = IsValid(BattleVisualizationActor)
		? BattleVisualizationActor->GetRenderTargetB() : nullptr;

	const FVector2D AtkSize(AtkRT ? FVector2D(AtkRT->SizeX, AtkRT->SizeY) : FVector2D(820.f, 1000.f));
	const FVector2D DefSize(DefRT ? FVector2D(DefRT->SizeX, DefRT->SizeY) : FVector2D(820.f, 1000.f));

	AttackerScreenBrush = MakeShared<FSlateBrush>();
	AttackerScreenBrush->SetResourceObject(AtkRT);
	AttackerScreenBrush->ImageSize = AtkSize;
	AttackerScreenBrush->DrawAs = ESlateBrushDrawType::Image;

	DefenderScreenBrush = MakeShared<FSlateBrush>();
	DefenderScreenBrush->SetResourceObject(DefRT);
	DefenderScreenBrush->ImageSize = DefSize;
	DefenderScreenBrush->DrawAs = ESlateBrushDrawType::Image;

	BattleScreensWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f))
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage).Image(AttackerScreenBrush.Get())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Fill)
			[
				SNew(SBox).WidthOverride(5.f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.95f))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage).Image(DefenderScreenBrush.Get())
			]
		];

	VC->AddViewportWidgetContent(BattleScreensWidget.ToSharedRef(),
	                              CombatViewportZOrder);
}

void UTacticsGameInstance::HideBattleScreens()
{
	if (UGameViewportClient* VC = GetGameViewportClient()) {
		if (BattleScreensWidget.IsValid()) {
			VC->RemoveViewportWidgetContent(BattleScreensWidget.ToSharedRef());
		}
	}
	BattleScreensWidget.Reset();
	AttackerScreenBrush.Reset();
	DefenderScreenBrush.Reset();
}

void UTacticsGameInstance::FireBattleVisualizationContinue()
{
	if (PendingBattleContinueDelegate.IsBound()) {
		PendingBattleContinueDelegate.Execute();
	}
}

void UTacticsGameInstance::UpdateBattleVisualizationEncounters(const TArray<FCombatEncounter>& Encounters)
{
	if (Encounters.Num() == 0) {
		return;
	}
	PendingBattleEncounters = Encounters;
	PendingBattleEncounterIndex = 0;
	if (!IsValid(BattleVisualizationActor)) {
		EnterBattleVisualization(Encounters, PendingBattleContinueDelegate);
		return;
	}
	ConfigureBattleScreensForCurrentEncounter();
	RemoveBattleContinueWidget();
	AddBattleContinueWidget();
}

void UTacticsGameInstance::CloseBattleVisualizationView(const bool bEndSession)
{
	RemoveBattleContinueWidget();
	HideBattleScreens();

	if (IsValid(BattleVisualizationActor)) {
		BattleVisualizationActor->Destroy();
		BattleVisualizationActor = nullptr;
	}
	if (IsValid(DefenderBattleViz)) {
		DefenderBattleViz->Destroy();
		DefenderBattleViz = nullptr;
	}
	SavedBattleViewTarget.Reset();

	if (bEndSession) {
		if (BoardPanel.IsValid()) {
			BoardPanel->SetMatchUiHidden(false);
		}
		ResetBattleVisualizationSessionState();
		PendingBattleEncounters.Empty();
		PendingBattleEncounterIndex = 0;
		PendingBattleContinueDelegate.Unbind();
		RequestTacticsBoardRefresh();
	}
	// Partial close (fight-to-fight handoff): caller presents the next pause without a board
	// refresh here - RequestTacticsBoardRefresh + SetMatchUiHidden(false) raced PresentPaused
	// and could spawn the next pre-roll then immediately tear it down (skipped / double-zoom fights).
}

void UTacticsGameInstance::ExitBattleVisualization()
{
	CloseBattleVisualizationView(true);

	if (BoardPanel.IsValid()) {
		BoardPanel->SetCombatScreenActive(false);
	}
}

void UTacticsGameInstance::AddBattleContinueWidget()
{
	UGameViewportClient* VC = GetGameViewportClient();
	if (!VC) { return; }

	const int32 EncCount     = PendingBattleEncounters.Num();
	const int32 EncIdxOneBased = PendingBattleEncounterIndex + 1;
	FString BtnLabel = (EncCount > 1)
		? FString::Printf(TEXT("Continue  (%d / %d)"), EncIdxOneBased, EncCount)
		: TEXT("Continue");

	BattleContinueWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0.f, 0.f, 0.f, 60.f)
		[
			SNew(SBox)
			.WidthOverride(200.f)
			.HeightOverride(48.f)
			[
				SNew(SButton)
				.ButtonColorAndOpacity(FLinearColor(0.14f, 0.10f, 0.06f, 0.92f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([this]() -> FReply
				{
					++PendingBattleEncounterIndex;
					if (PendingBattleEncounterIndex < PendingBattleEncounters.Num()) {
						ConfigureBattleScreensForCurrentEncounter();
						RemoveBattleContinueWidget();
						AddBattleContinueWidget();
					} else {
						FireBattleVisualizationContinue();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(BtnLabel))
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 14))
					.ColorAndOpacity(FLinearColor(0.92f, 0.82f, 0.60f, 1.f))
				]
			]
		];

	VC->AddViewportWidgetContent(BattleContinueWidget.ToSharedRef(),
	                              CombatViewportZOrder + 1);
}

void UTacticsGameInstance::RemoveBattleContinueWidget()
{
	if (UGameViewportClient* VC = GetGameViewportClient()) {
		if (BattleContinueWidget.IsValid()) {
			VC->RemoveViewportWidgetContent(BattleContinueWidget.ToSharedRef());
		}
	}
	BattleContinueWidget.Reset();
}
