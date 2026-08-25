#include "Tactics3DBoardPlayerController.h"

#include "TacticsBoardPlayerController.h"
#include "STacticsBoardPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Tactics3DBoardGameMode.h"
#include "Tactics3DBoardViewPawn.h"
#include "Tactics3DWorldBoardActor.h"
#include "TacticsGameInstance.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ATactics3DBoardPlayerController::ATactics3DBoardPlayerController()
{
	bShowMouseCursor = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATactics3DBoardPlayerController::BeginPlay()
{
	Super::BeginPlay();

	CachedGameInstance = Cast<UTacticsGameInstance>(GetGameInstance());

	ATacticsBoardPlayerController::ApplySlateFriendlyMouseCursor(this);
}

/** Binds click, pan, zoom, and hand hotkeys for the 3D match. */
void ATactics3DBoardPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent) {
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ATactics3DBoardPlayerController::OnPrimaryClick);
		InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ATactics3DBoardPlayerController::OnSecondaryClick);
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ATactics3DBoardPlayerController::OnToggleOptions);
		InputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &ATactics3DBoardPlayerController::OnToggleOptions);
		InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ATactics3DBoardPlayerController::OnShortcutUndo);
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ATactics3DBoardPlayerController::OnShortcutPass);
		InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ATactics3DBoardPlayerController::OnShortcutConfirm);
		InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ATactics3DBoardPlayerController::OnZoomIn);
		InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ATactics3DBoardPlayerController::OnZoomOut);
		InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ATactics3DBoardPlayerController::OnMiddleMousePressed);
		InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &ATactics3DBoardPlayerController::OnMiddleMouseReleased);
		auto BindPanKey = [this](const FKey& Key, void (ATactics3DBoardPlayerController::*OnPressed)(),
							  void (ATactics3DBoardPlayerController::*OnKeyReleased)()) {
			InputComponent->BindKey(Key, IE_Pressed, this, OnPressed);
			InputComponent->BindKey(Key, IE_Released, this, OnKeyReleased);
		};
		BindPanKey(EKeys::W, &ATactics3DBoardPlayerController::OnPanUpPressed, &ATactics3DBoardPlayerController::OnPanUpReleased);
		BindPanKey(EKeys::Up, &ATactics3DBoardPlayerController::OnPanUpPressed, &ATactics3DBoardPlayerController::OnPanUpReleased);
		BindPanKey(EKeys::S, &ATactics3DBoardPlayerController::OnPanDownPressed, &ATactics3DBoardPlayerController::OnPanDownReleased);
		BindPanKey(EKeys::Down, &ATactics3DBoardPlayerController::OnPanDownPressed, &ATactics3DBoardPlayerController::OnPanDownReleased);
		BindPanKey(EKeys::A, &ATactics3DBoardPlayerController::OnPanLeftPressed, &ATactics3DBoardPlayerController::OnPanLeftReleased);
		BindPanKey(EKeys::Left, &ATactics3DBoardPlayerController::OnPanLeftPressed, &ATactics3DBoardPlayerController::OnPanLeftReleased);
		BindPanKey(EKeys::D, &ATactics3DBoardPlayerController::OnPanRightPressed, &ATactics3DBoardPlayerController::OnPanRightReleased);
		BindPanKey(EKeys::Right, &ATactics3DBoardPlayerController::OnPanRightPressed, &ATactics3DBoardPlayerController::OnPanRightReleased);
		InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey1);
		InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey2);
		InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey3);
		InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey4);
		InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey5);
		InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey6);
		InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey7);
		InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey8);
		InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &ATactics3DBoardPlayerController::OnHandHotkey9);
	}
}

void ATactics3DBoardPlayerController::OnShortcutEscape()
{
	OnToggleOptions();
}

void ATactics3DBoardPlayerController::OnToggleOptions()
{
	if (UTacticsGameInstance* TGI = CachedGameInstance.Get()) {
		TGI->ToggleOptionsOverlay();
	}
}

void ATactics3DBoardPlayerController::OnShortcutCancel()
{
	if (UTacticsGameInstance* TGI = CachedGameInstance.Get()) {
		if (TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel()) {
			Panel->HandleShortcutKey(EKeys::RightMouseButton);
		}
	}
}

void ATactics3DBoardPlayerController::OnShortcutUndo()
{
	if (UTacticsGameInstance* TGI = CachedGameInstance.Get()) {
		if (TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel()) {
			Panel->HandleShortcutKey(EKeys::Z);
		}
	}
}

void ATactics3DBoardPlayerController::OnShortcutPass()
{
	if (UTacticsGameInstance* TGI = CachedGameInstance.Get()) {
		if (TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel()) {
			Panel->HandleShortcutKey(EKeys::P);
		}
	}
}

void ATactics3DBoardPlayerController::OnShortcutConfirm()
{
	if (UTacticsGameInstance* TGI = CachedGameInstance.Get()) {
		if (TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel()) {
			Panel->HandleShortcutKey(EKeys::Enter);
		}
	}
}

void ATactics3DBoardPlayerController::OnZoomIn()
{
	if (STacticsBoardPanel::IsHandScrollHovered()) {
		return;
	}
	if (ATactics3DBoardViewPawn* ViewPawn = Cast<ATactics3DBoardViewPawn>(GetPawn())) {
		ViewPawn->AdjustZoom(-1.f);
	}
}

void ATactics3DBoardPlayerController::OnZoomOut()
{
	if (STacticsBoardPanel::IsHandScrollHovered()) {
		return;
	}
	if (ATactics3DBoardViewPawn* ViewPawn = Cast<ATactics3DBoardViewPawn>(GetPawn())) {
		ViewPawn->AdjustZoom(1.f);
	}
}

void ATactics3DBoardPlayerController::OnMiddleMousePressed()
{
	bMiddleMousePanning = true;
	GetMousePosition(LastPanMouseX, LastPanMouseY);
}

void ATactics3DBoardPlayerController::OnMiddleMouseReleased()
{
	bMiddleMousePanning = false;
}

void ATactics3DBoardPlayerController::OnPanUpPressed() { bPanUp = true; }
void ATactics3DBoardPlayerController::OnPanUpReleased() { bPanUp = false; }
void ATactics3DBoardPlayerController::OnPanDownPressed() { bPanDown = true; }
void ATactics3DBoardPlayerController::OnPanDownReleased() { bPanDown = false; }
void ATactics3DBoardPlayerController::OnPanLeftPressed() { bPanLeft = true; }
void ATactics3DBoardPlayerController::OnPanLeftReleased() { bPanLeft = false; }
void ATactics3DBoardPlayerController::OnPanRightPressed() { bPanRight = true; }
void ATactics3DBoardPlayerController::OnPanRightReleased() { bPanRight = false; }

void ATactics3DBoardPlayerController::OnHandHotkey1() { ForwardHandHotkey(1); }
void ATactics3DBoardPlayerController::OnHandHotkey2() { ForwardHandHotkey(2); }
void ATactics3DBoardPlayerController::OnHandHotkey3() { ForwardHandHotkey(3); }
void ATactics3DBoardPlayerController::OnHandHotkey4() { ForwardHandHotkey(4); }
void ATactics3DBoardPlayerController::OnHandHotkey5() { ForwardHandHotkey(5); }
void ATactics3DBoardPlayerController::OnHandHotkey6() { ForwardHandHotkey(6); }
void ATactics3DBoardPlayerController::OnHandHotkey7() { ForwardHandHotkey(7); }
void ATactics3DBoardPlayerController::OnHandHotkey8() { ForwardHandHotkey(8); }
void ATactics3DBoardPlayerController::OnHandHotkey9() { ForwardHandHotkey(9); }

void ATactics3DBoardPlayerController::ForwardHandHotkey(const int32 Slot1Based)
{
	if (UTacticsGameInstance* TGI = CachedGameInstance.Get()) {
		if (TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel()) {
			Panel->HandleHandHotkeySlot(Slot1Based);
		}
	}
}

static ATactics3DWorldBoardActor* FindWorldBoard(UWorld* W)
{
	if (!W) {
		return nullptr;
	}
	if (ATactics3DBoardGameMode* GM = Cast<ATactics3DBoardGameMode>(W->GetAuthGameMode())) {
		if (ATactics3DWorldBoardActor* B = GM->WorldBoardActor.Get()) {
			return B;
		}
	}
	for (TActorIterator<ATactics3DWorldBoardActor> It(W); It; ++It) {
		return *It;
	}
	return nullptr;
}

ATactics3DWorldBoardActor* ATactics3DBoardPlayerController::ResolveWorldBoard() const
{
	if (ATactics3DWorldBoardActor* Cached = CachedWorldBoard.Get()) {
		return Cached;
	}
	ATactics3DWorldBoardActor* Board = FindWorldBoard(GetWorld());
	CachedWorldBoard = Board;
	return Board;
}

bool ATactics3DBoardPlayerController::TraceBoardCellUnderCursor(int& OutWx, int& OutWy) const
{
	UWorld* W = GetWorld();
	if (!W) {
		return false;
	}
	ATactics3DWorldBoardActor* Board = ResolveWorldBoard();
	if (!Board) {
		return false;
	}
	FVector WorldLoc, WorldDir;
	if (!DeprojectMousePositionToWorld(WorldLoc, WorldDir)) {
		return false;
	}
	const FVector TraceEnd = WorldLoc + WorldDir * 1.5e6f;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Tactics3DBoardTrace), true, this);
	W->LineTraceMultiByChannel(Hits, WorldLoc, TraceEnd, ECC_Visibility, Params);
	for (const FHitResult& Hit : Hits) {
		if (Board->ResolveWorldCellFromHit(Hit, OutWx, OutWy)) {
			return true;
		}
	}
	return false;
}

void ATactics3DBoardPlayerController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);

	if (ATactics3DBoardViewPawn* ViewPawn = Cast<ATactics3DBoardViewPawn>(GetPawn())) {
		if (bMiddleMousePanning && IsInputKeyDown(EKeys::MiddleMouseButton)) {
			float MouseX = 0.f;
			float MouseY = 0.f;
			if (GetMousePosition(MouseX, MouseY)) {
				const FVector2D PixelDelta(MouseX - LastPanMouseX, MouseY - LastPanMouseY);
				LastPanMouseX = MouseX;
				LastPanMouseY = MouseY;
				ViewPawn->PanViewByScreenPixels(PixelDelta);
			}
		} else {
			bMiddleMousePanning = false;
		}

		FVector2D PanDelta(0.f, 0.f);
		if (bPanUp) {
			PanDelta.Y += 1.f;
		}
		if (bPanDown) {
			PanDelta.Y -= 1.f;
		}
		if (bPanRight) {
			PanDelta.X += 1.f;
		}
		if (bPanLeft) {
			PanDelta.X -= 1.f;
		}
		if (!PanDelta.IsNearlyZero()) {
			ViewPawn->PanView(PanDelta.GetSafeNormal(), DeltaSeconds);
		}
	}

	UTacticsGameInstance* TGI = CachedGameInstance.Get();
	if (!TGI) {
		return;
	}
	TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel();
	if (!Panel.IsValid()) {
		return;
	}
	int HitWx = 0;
	int HitWy = 0;
	if (STacticsBoardPanel::ShouldSuppressBoardHoverUnderCursor()) {
		Panel->ClearBoardCellHover();
	} else if (TraceBoardCellUnderCursor(HitWx, HitWy)) {
		Panel->HandleBoardCellHover(HitWx, HitWy);
	} else {
		Panel->ClearBoardCellHover();
	}
}

void ATactics3DBoardPlayerController::OnPrimaryClick()
{
	if (STacticsBoardPanel::IsHandScrollHovered()) {
		return;
	}
	int HitWx = 0;
	int HitWy = 0;
	if (!TraceBoardCellUnderCursor(HitWx, HitWy)) {
		return;
	}
	UTacticsGameInstance* TGI = CachedGameInstance.Get();
	if (!TGI) {
		return;
	}
	if (TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel()) {
		const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
		Panel->HandleWorldGridCellClicked(HitWx, HitWy, bShift);
	}
	TGI->RequestTacticsBoardRefresh();
}

void ATactics3DBoardPlayerController::OnSecondaryClick()
{
	int HitWx = 0;
	int HitWy = 0;
	UTacticsGameInstance* TGI = CachedGameInstance.Get();
	if (!TGI) {
		return;
	}
	TSharedPtr<STacticsBoardPanel> Panel = TGI->GetTacticsBoardPanel();
	if (!Panel.IsValid()) {
		return;
	}
	if (TraceBoardCellUnderCursor(HitWx, HitWy)) {
		Panel->HandleWorldGridCellSecondaryClicked(HitWx, HitWy);
	} else {
		Panel->HandleShortcutKey(EKeys::RightMouseButton);
	}
	TGI->RequestTacticsBoardRefresh();
}
