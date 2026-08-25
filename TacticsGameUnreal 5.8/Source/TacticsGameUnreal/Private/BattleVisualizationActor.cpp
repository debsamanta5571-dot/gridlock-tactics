#include "BattleVisualizationActor.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TacticsCardArtUi.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// ── Camera layout constants (relative to actor root) ─────────────────────────
constexpr float kCameraOffsetY   = -2150.f;  // far pull-back - frames both side-by-side panels
constexpr float kCameraOffsetZ   = 1780.f;   // high vantage above the stage
constexpr float kCameraPitch     =  -40.f;   // downward tilt onto fighters + boards
constexpr float kCameraYaw       =   90.f;   // forward = +Y (look at the portraits)
constexpr float kCameraFov       =   86.f;   // wide enough to frame both panels + the seam

// ── Per-side render target (each actor renders one screen) ────────────────────
constexpr int32 kRenderTargetW   =  900;     // off-screen render target width  (one screen)
constexpr int32 kRenderTargetH   = 1040;     // off-screen render target height (one screen)
constexpr float kSideFovAngle    =   74.f;   // horizontal FOV for a single-fighter screen (wide field)
constexpr float kSideCamOffsetY  = -2600.f;  // pull-back framing the widely-spaced team
constexpr float kSideCamOffsetZ  = 1750.f;
constexpr float kSideCamPitch    =  -32.f;
constexpr float kSideFighterInsetX = 210.f;  // fighter nudged toward the inner (seam) edge

// ── Portrait geometry ─────────────────────────────────────────────────────────
// Plane normal is +Z; roll +90° turns it to -Y (toward the camera) while the
// plane's local +X stays world +X (image left→right) and local +Y maps to world
// +Z (image bottom→top), keeping the art upright.
const FRotator kPortraitRot      {0.f, 0.f, 90.f};

constexpr float kSpriteHeightCm  =  300.f;   // default portrait height (ApplyPortraitArt fallback)
constexpr float kPlaneBaseSizeCm = 100.f;    // engine Plane mesh is 100×100 cm at scale 1

// ── Two combat panels (one per fighter), split by a central seam ──────────────
// The stage is split into two side-by-side mini-battlefields: the attacker's panel on
// the screen-left (world +X) and the defender's on the screen-right (world -X), divided
// by a dark seam at X=0.  Each panel has its *own* board grid the same size as the live
// tactics board, with that fighter's team placed on it at their real (GridX, GridY); the
// fighter itself stands tall at the panel's inner (seam) edge, facing across the divide.
constexpr float kBoardCellW      =  170.f;   // X spacing between columns (compact - two boards fit)
constexpr float kBoardCellD      =  165.f;   // Y (depth) spacing between rows
constexpr float kBoardCenterY    =  300.f;   // depth of each board's centre (≈ camera focal point)
constexpr float kBoardZ          =   -6.f;   // tile height (just above the main floor)
constexpr float kBoardUnitHeightCm = 150.f;  // bystander portrait height (shorter)
constexpr float kBoardUnitZ      = kBoardZ + kBoardUnitHeightCm * 0.5f;  // bystander stands on the tile
constexpr float kBoardDuelHeightCm = 300.f;  // active combatant portrait height (taller - the focus)
constexpr float kBoardDuelZ      = kBoardZ + kBoardDuelHeightCm * 0.5f;  // duelist stands on the tile
constexpr float kDuelHeadTopZ    = kBoardZ + kBoardDuelHeightCm;         // top of a duelist's head
constexpr int32 kBoardMaxDim     =   16;     // safety clamp on board dimensions

constexpr float kPanelGap        =  420.f;   // empty width straddling the seam between the panels
constexpr float kDuelInset       =   70.f;   // how far the fighter sits inside its panel from the seam
constexpr float kDuelFaceYawDeg  =   22.f;   // inward yaw so the two fighters face each other

// ── Faithful single-battlefield layout (per-screen spectator view) ────────────
// Every real unit is placed at its true board cell in ONE shared grid (no per-team
// mini-board), all at the SAME world size as the fighters - distance/perspective is
// what makes far units read smaller, giving a real sense of scale.  The grid is
// oriented so the POV fighter→opponent line runs horizontally (side-on duel) with the
// opponent toward +X, and the duel midpoint sits front-and-centre; the rest of the
// board fans out behind at its true relative offsets (so it matches what that fighter
// would actually see spectating the battlefield).
constexpr float kFieldCellW       = 520.f;   // horizontal spacing between columns (widely spaced for scale)
constexpr float kFieldCellD       = 560.f;   // depth spacing between rows (recede into the screen)
constexpr float kFieldFocusDepth  = 320.f;   // depth of this side's fighter (in front, near the camera)
constexpr float kFieldFighterX    = 760.f;   // fighter's outward offset from screen centre (toward its edge)
constexpr float kFieldMinDepth    =  90.f;   // never place a unit behind / on top of the camera

// ── Single-screen battlefield layout (derived from BattleLevel.umap) ──────────
// BattleLevel.umap authoring reference (the spacing the designer hand-placed):
//   • Two opposing fighters face across X, ~1430 apart  (kGut5 X=-769.65 vs CounterSniper X=+659.95)
//   • Same-team ranks line up along Y (depth), ~2200 apart  (Y: 20 / -2300 / -4460)
//   • Camera (PlayerStart) centred at X=0, pulled back along -Y, ~310 up, looking +Y down the line
// We reproduce this in ONE shared scene: the active duel is the FRONT pair (nearest the
// camera) at ∓kFieldFrontX; every other unit (BOTH teams, no filtering) is placed by its
// TRUE board offset rotated so the attacker→defender line runs horizontally, with off-axis
// units pushed DEEPER (+Y) so they recede behind the duel - same world size as the fighters,
// distance/perspective alone giving the sense of scale.
constexpr float kArenaFrontDepth  =  150.f;  // duel depth - right in front of the low camera
// Bystanders live on a DISCRETE GRID (one unit per tile).  The board is rotated so the
// attacker→defender line is screen-horizontal: the duel-axis component picks the COLUMN
// (world X), the perpendicular component picks the DEPTH ROW (world Y), and the whole grid
// is pushed behind the duelists.  (col,row) come straight from the unique board cell, so no
// two units can ever share a tile.
constexpr float kArenaAllyAcross  = 2400.f;  // world X per grid column (sideways spacing)
constexpr float kArenaAllyDepth   = 2400.f;  // world Y per grid row (depth spacing)
constexpr float kArenaAllyBack    = 2400.f;  // gap between the duel and the first grid row
// The two duelists sit on their OWN grid columns (±0.5 cells from the midpoint) so a unit
// standing directly behind a fighter lines up with them - no offset between battle and army.
constexpr float kArenaFighterX    = kArenaAllyAcross * 0.5f;  // active fighter's outward offset

// ── Persistent arena cell spacing ─────────────────────────────────────────────
// One cohesive battlefield is built ONCE; every unit sits on its true board cell mapped through
// these.  The cameras then travel from duel to duel across this fixed board (the other units +
// checker ground are the landmarks that make the camera movement read).  Board X → world X
// (screen left↔right under the yaw-90 cameras); board Y → world Y (recede from the cameras).
constexpr float kArenaCellW       = 2400.f;  // world X per board column
constexpr float kArenaCellD       = 2400.f;  // world Y per board row

// ── Arena scenery (checkerboard ground, red stripe, sky backdrop) ─────────────
constexpr float kArenaTileCm      =  600.f;  // checkerboard tile size
constexpr int32 kArenaTileCols    =   15;    // ground tiles across X (centred on 0)
constexpr int32 kArenaTileRows    =   16;    // ground tiles along Y (depth)
constexpr float kArenaGroundZ     =   -6.f;  // ground height (units stand just above it)
constexpr float kArenaStripeY     =  560.f;  // red stripe depth (just behind the front fighters)
constexpr float kArenaStripeDepth =  240.f;  // red stripe thickness (Y)
// Sky is a real SkyAtmosphere (spawned in BeginPlay), not faked scenery planes.
const FLinearColor kArenaSkyColor  {0.45f, 0.66f, 0.92f, 1.f};  // render-target clear (sky-blue fallback)

// ── Single-screen scene capture (one full-width screen, divider drawn in UI) ──
constexpr int32 kWideRenderTargetW = 1500;   // landscape render target (both fighters + a divider line)
constexpr int32 kWideRenderTargetH =  950;
constexpr float kWideCamOffsetY    = -2900.f; // pulled back so the two battlers sit centred (~±0.55 ndc)
constexpr float kWideCamOffsetZ    =   720.f; // low vantage, looking across the field
constexpr float kWideCamPitch      =   -12.f; // gentle downward tilt
constexpr float kWideCamFov        =    72.f; // frames the centred duelists

// ── Shared accurate field + dual tracking cameras ────────────────────────────
// ONE grid holds every unit at its TRUE board cell (col→world X, row→world depth),
// all the same world size.  Two cameras glide over this one field: one framed on the
// attacker, one on the defender, so even widely-spaced (ranged) battlers each get a
// clear screen.  The layout is identical every encounter, so rebuilding the field under
// the cameras is invisible - only the cameras move, gliding to the next battle.
// Per-fighter camera framing (camera sits behind its fighter on -Y, looking +Y so the
// billboard portraits stay face-on; a small X lead leaves room toward the opponent).
constexpr float kFrameCamPullback = 1180.f;  // camera distance back (-Y) from its fighter
constexpr float kFrameCamHeight   =  560.f;  // camera height above the field
constexpr float kFrameCamPitch    =  -19.f;  // gentle downward tilt
constexpr float kFrameCamFov      =   60.f;  // per-screen FOV
constexpr float kFrameCamLeadX    =  300.f;  // sideways lead toward the opponent
constexpr int32 kSideScreenW      =  820;    // per-side render target width
constexpr int32 kSideScreenH      = 1000;    // per-side render target height
// The arena is built ONCE with every unit on its true cell (see SetupBattlefield), so each
// Continue moves the cameras ACROSS that persistent board from the previous duel to the next -
// the surrounding units / checker ground make the travel visible.  Glide time scales with how far
// across the board the next fight is, clamped to a comfortable range.
constexpr float kCamGlideSpeed    = 9000.f;  // world cm/s the cameras travel between duels
constexpr float kCamGlideMin      =   0.45f; // shortest glide
constexpr float kCamGlideMax      =   1.6f;  // longest glide
// When the next duel is (almost) the same spot - e.g. a pre-roll matchup followed by its resolved
// version, or a unit fighting twice from the same cell - there is no cross-board move to make, so
// a straight old→new glide would read as a snap.  In that case synthesise a visible establishing
// push-in: start pulled back / raised from the new framing and ease in.
constexpr float kCamSameSpotTravel =  350.f;  // below this old→new distance, push in instead
constexpr float kCamEstablishBack  =  950.f;  // extra -Y pullback for the establishing push-in
constexpr float kCamEstablishUp    =  300.f;  // extra +Z height for the establishing push-in

// ── Central seam / divider ────────────────────────────────────────────────────
constexpr float kSeamWidthCm     =   34.f;   // thickness of the divider bar
constexpr float kSeamHeightCm    = 1250.f;   // tall enough to split the whole view
constexpr float kSeamY           = kBoardCenterY - 200.f;  // sit slightly toward the camera

/** Half-width (in cm) of one panel's board grid. */
inline float PanelHalfWidth(int32 Cols)
{
	return Cols * kBoardCellW * 0.5f;
}
/** X of the panel centre for the given sign (+1 = attacker / screen-left, -1 = defender). */
inline float PanelCenterX(int32 Cols, float Sign)
{
	return Sign * (PanelHalfWidth(Cols) + kPanelGap * 0.5f);
}
/** Stage-local X for a column within a panel centred at CenterX. col 0 → +X edge. */
inline float PanelCellX(int32 GridX, int32 Cols, float CenterX)
{
	return CenterX + ((Cols - 1) * 0.5f - GridX) * kBoardCellW;
}
/** Stage-local Y (depth) for board row GridY, centred so the board straddles kBoardCenterY. */
inline float BoardCellY(int32 GridY, int32 Rows)
{
	return kBoardCenterY + (GridY - (Rows - 1) * 0.5f) * kBoardCellD;
}

// ── Floor ─────────────────────────────────────────────────────────────────────
constexpr float kFloorScale      =  30.f;    // 30×30 m floor
constexpr float kFloorOffsetZ    = -10.f;    // just below root so sprites sit on it

// ── Labels (float above the duelists' heads) ──────────────────────────────────
constexpr float kNameLabelZ      = kDuelHeadTopZ + 60.f;
constexpr float kRoleLabelZ      = kDuelHeadTopZ + 8.f;
// Text faces +X by default; yaw -90° turns the face to -Y (toward the camera)
// while keeping the text upright and reading left-to-right.
const FRotator kTextRot          {0.f, -90.f, 0.f};

// ── Choreography constants ────────────────────────────────────────────────────
constexpr float kDamageLabelZ    = kDuelHeadTopZ + 35.f;

constexpr float kLungeReach      = 650.f;   // shared-scene lunge: front fighter drives toward the seam
constexpr float kSideLungeReach  = 460.f;   // per-screen lunge: fighter leaps toward the seam
constexpr float kSeamLungeReach  = 500.f;   // dual-camera "attack the middle": leap toward the seam
constexpr float kLungeHopCm      =  45.f;
constexpr float kRecoilCm        =  55.f;
constexpr float kDamageRiseCm    =  95.f;
constexpr FColor kDamageLabelColor     {255, 70, 55};
constexpr FColor kCritDamageLabelColor {255, 215, 50};

static void ShowFloatingDamage(UTextRenderComponent* Label, const int32 Damage, const bool bCrit)
{
	if (!Label || Damage < 0) {
		return;
	}
	// 0 damage (e.g. fully absorbed by armor) still shows "0" so the player sees the hit landed.
	const FString Number = Damage > 0 ? FString::Printf(TEXT("-%d"), Damage) : TEXT("0");
	Label->SetText(FText::FromString(bCrit
		? FString::Printf(TEXT("CRIT!\n%s"), *Number)
		: Number));
	Label->SetTextRenderColor(bCrit ? kCritDamageLabelColor : kDamageLabelColor);
	Label->SetVisibility(true);
}
constexpr float kDeathFallDeg    = -90.f;

// Phase durations (seconds).
constexpr float kPhaseIntro       = 0.30f;
constexpr float kPhaseLungeOut    = 0.16f;
constexpr float kPhaseImpact      = 0.45f;
constexpr float kPhaseReturn      = 0.20f;
constexpr float kPhaseGap         = 0.14f;
constexpr float kPhaseSettle      = 0.55f;

/** Smoothstep ease for 0..1 progress. */
inline float Smooth01(float T)
{
	T = FMath::Clamp(T, 0.f, 1.f);
	return T * T * (3.f - 2.f * T);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Static asset loaders (cached, GC-rooted)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

UStaticMesh* LoadPlaneMesh()
{
	static UStaticMesh* Cached = nullptr;
	static bool bRooted = false;
	if (Cached) { return Cached; }
	Cached = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Cached && !bRooted) { Cached->AddToRoot(); bRooted = true; }
	return Cached;
}

UMaterial* LoadMaterialFromCandidates(const TCHAR* const* Paths, int32 Count)
{
	for (int32 I = 0; I < Count; ++I) {
		if (UMaterial* M = LoadObject<UMaterial>(nullptr, Paths[I])) {
			return M;
		}
	}
	return nullptr;
}

UMaterial* LoadBattleFlatMaterial()
{
	static UMaterial* Cached = nullptr;
	if (Cached) { return Cached; }
	static const TCHAR* Candidates[] = {
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"),
		TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"),
		TEXT("/Game/Red.Red"),
	};
	Cached = LoadMaterialFromCandidates(Candidates, UE_ARRAY_COUNT(Candidates));
	return Cached;
}

void TintFlatMaterial(UMaterialInstanceDynamic* MID, const FLinearColor& Color)
{
	if (!MID) { return; }
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
	MID->SetVectorParameterValue(TEXT("EmissiveColor"), Color);
}

/**
 * Engine checkerboard / grid floor material (the gray checker seen in UE template levels).
 * World-aligned UVs, so a single large plane tiles automatically.  Cached + rooted.
 */
UMaterialInterface* LoadGroundCheckerMaterial()
{
	static UMaterialInterface* Cached = nullptr;
	if (Cached) { return Cached; }
	static const TCHAR* Candidates[] = {
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"), // UE5 gray checker grid
		TEXT("/Game/LevelPrototyping/Materials/M_PrototypeGrid.M_PrototypeGrid"),             // grid parent
		TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"),                  // grid fallback
	};
	for (const TCHAR* Path : Candidates) {
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path)) {
			M->AddToRoot();
			Cached = M;
			UE_LOG(LogTemp, Warning, TEXT("[BattleViz] ground checker material = %s"), Path);
			break;
		}
	}
	return Cached;
}

/**
 * First available material that shows the card texture *without* scene lighting.
 * Cached + rooted so it survives GC; retries on failure (don't cache nullptr).
 */
UMaterialInterface* LoadPortraitUnlitMaterial()
{
	static UMaterialInterface* Cached = nullptr;
	static bool bRooted = false;
	if (Cached) { return Cached; }
	static const TCHAR* Candidates[] = {
		TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"),
		TEXT("/Paper2D/DefaultSpriteMaterial.DefaultSpriteMaterial"),
		TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"),
		TEXT("/Game/TacticsData/Materials/M_CardPortrait.M_CardPortrait"),
	};
	for (const TCHAR* Path : Candidates) {
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path)) {
			if (!bRooted) { M->AddToRoot(); bRooted = true; }
			Cached = M;
			UE_LOG(LogTemp, Warning, TEXT("[BattleViz] portrait base material = %s"), Path);
			break;
		}
	}
	if (!Cached) {
		UE_LOG(LogTemp, Error, TEXT("[BattleViz] no portrait base material could be loaded!"));
	}
	return Cached;
}

/**
 * Defeated-unit settle: topple straight backward (away from the camera) pivoting
 * about the portrait's bottom edge, and fade toward dark red.
 */
void ApplyDeathFall(UStaticMeshComponent* Comp, UMaterialInstanceDynamic* MID,
                    const FVector& HomeLoc, float Progress, float HeightCm,
                    const FVector& RightAxis, const FVector& AwayDir, const FQuat& FaceQuat)
{
	const float Fall = Progress * Progress;
	// Topple backward (away from the camera) by pivoting about the portrait's bottom edge.  Rotating
	// the upright plane about the camera-RIGHT axis tips its top toward +Forward (away from camera),
	// so this works for any per-encounter camera orientation, not just the old fixed -Y view.
	const float AngleRad = FMath::DegreesToRadians(FMath::Abs(kDeathFallDeg) * Fall);
	const float HalfH = HeightCm * 0.5f;

	if (Comp) {
		FVector Loc = HomeLoc + AwayDir * (HalfH * FMath::Sin(AngleRad));
		Loc.Z = (HomeLoc.Z - HalfH) + HalfH * FMath::Cos(AngleRad);
		Comp->SetRelativeLocation(Loc);

		const FQuat Tip = FQuat(RightAxis, AngleRad);
		Comp->SetRelativeRotation(Tip * FaceQuat);
	}
	if (MID) {
		const FLinearColor Dead =
			FMath::Lerp(FLinearColor::White, FLinearColor(0.30f, 0.07f, 0.07f, 1.f), Fall);
		MID->SetVectorParameterValue(TEXT("EmissiveColor"), Dead);
		MID->SetVectorParameterValue(TEXT("Color"), Dead);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Dead);
	}
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

ABattleVisualizationActor::ABattleVisualizationActor()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// ── Camera ──────────────────────────────────────────────────────────────────
	BattleCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("BattleCamera"));
	BattleCamera->SetupAttachment(RootComponent);
	BattleCamera->SetRelativeLocation(FVector(0.f, kCameraOffsetY, kCameraOffsetZ));
	BattleCamera->SetRelativeRotation(FRotator(kCameraPitch, kCameraYaw, 0.f));
	BattleCamera->SetFieldOfView(kCameraFov);

	// ── Scene capture: renders this side's scene into the off-screen render target ─
	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(RootComponent);
	SceneCapture->SetRelativeLocation(FVector(0.f, kSideCamOffsetY, kSideCamOffsetZ));
	SceneCapture->SetRelativeRotation(FRotator(kSideCamPitch, kCameraYaw, 0.f));
	SceneCapture->FOVAngle = kSideFovAngle;
	SceneCapture->ProjectionType = ECameraProjectionMode::Perspective;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->bAlwaysPersistRenderingState = true;
	// Render the real SkyAtmosphere (a screen-space sky) into the capture so the background is a
	// proper sky gradient instead of a black void.  Height/volumetric fog stay off - the match map
	// has none, and disabling them avoids any distant fade.
	SceneCapture->ShowFlags.SetAtmosphere(true);
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);

	// ── Second scene capture: the defender-tracking camera (same shared field) ────
	SceneCaptureB = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureB"));
	SceneCaptureB->SetupAttachment(RootComponent);
	SceneCaptureB->SetRelativeLocation(FVector(0.f, kSideCamOffsetY, kSideCamOffsetZ));
	SceneCaptureB->SetRelativeRotation(FRotator(kSideCamPitch, kCameraYaw, 0.f));
	SceneCaptureB->FOVAngle = kFrameCamFov;
	SceneCaptureB->ProjectionType = ECameraProjectionMode::Perspective;
	SceneCaptureB->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCaptureB->bCaptureEveryFrame = true;
	SceneCaptureB->bCaptureOnMovement = false;
	SceneCaptureB->bAlwaysPersistRenderingState = true;
	SceneCaptureB->ShowFlags.SetAtmosphere(true);
	SceneCaptureB->ShowFlags.SetFog(false);
	SceneCaptureB->ShowFlags.SetVolumetricFog(false);

	// ── Floor (white platform) ───────────────────────────────────────────────────
	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorMesh"));
	FloorMesh->SetupAttachment(RootComponent);
	FloorMesh->SetRelativeLocation(FVector(0.f, 0.f, kFloorOffsetZ));
	FloorMesh->SetRelativeScale3D(FVector(kFloorScale, kFloorScale, 1.f));
	FloorMesh->SetCastShadow(false);
	FloorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// ── Text labels ─────────────────────────────────────────────────────────────
	// Initial X = 0 (centre); SetupBattlefield repositions them to the active pair.
	auto MakeLabel = [&](const TCHAR* Name, float XOffset, float ZOffset,
	                     float WorldSize, FColor Color) -> UTextRenderComponent*
	{
		UTextRenderComponent* T = CreateDefaultSubobject<UTextRenderComponent>(Name);
		T->SetupAttachment(RootComponent);
		T->SetRelativeLocation(FVector(XOffset, 0.f, ZOffset));
		T->SetRelativeRotation(kTextRot);
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetWorldSize(WorldSize);
		T->SetTextRenderColor(Color);
		T->SetCastShadow(false);
		T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return T;
	};
	AtkNameLabel = MakeLabel(TEXT("AtkNameLabel"),  0.f, kNameLabelZ, 55.f, FColor(235, 225, 200));
	DefNameLabel = MakeLabel(TEXT("DefNameLabel"),  0.f, kNameLabelZ, 55.f, FColor(235, 225, 200));
	AtkRoleLabel = MakeLabel(TEXT("AtkRoleLabel"),  0.f, kRoleLabelZ, 45.f, FColor(240, 130, 40));
	DefRoleLabel = MakeLabel(TEXT("DefRoleLabel"),  0.f, kRoleLabelZ, 45.f, FColor(90, 165, 240));

	AtkDamageLabel = MakeLabel(TEXT("AtkDamageLabel"), 0.f, kDamageLabelZ, 85.f, FColor(255, 70, 55));
	DefDamageLabel = MakeLabel(TEXT("DefDamageLabel"), 0.f, kDamageLabelZ, 85.f, FColor(255, 70, 55));
	AtkDamageLabel->SetVisibility(false);
	DefDamageLabel->SetVisibility(false);

	// ── Fill light ───────────────────────────────────────────────────────────────
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(RootComponent);
	FillLight->SetRelativeLocation(FVector(0.f, kCameraOffsetY * 0.4f, kCameraOffsetZ + 250.f));
	FillLight->SetIntensity(28000.f);
	FillLight->SetLightColor(FLinearColor(1.0f, 0.93f, 0.82f));
	FillLight->SetAttenuationRadius(4000.f);
	FillLight->SetCastShadows(false);

	// ── Assign plane mesh to floor (portraits are created at runtime) ─────────────
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded()) {
		FloorMesh->SetStaticMesh(PlaneFinder.Object);
	}

	// ── Font for labels ───────────────────────────────────────────────────────────
	static ConstructorHelpers::FObjectFinder<UFont> FontFinder(
		TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpriteMatFinder(
		TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatColFinder(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GridMatFinder(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	(void)SpriteMatFinder;
	(void)FlatColFinder;
	(void)GridMatFinder;
	if (FontFinder.Succeeded()) {
		AtkNameLabel->SetFont(FontFinder.Object);
		DefNameLabel->SetFont(FontFinder.Object);
		AtkRoleLabel->SetFont(FontFinder.Object);
		DefRoleLabel->SetFont(FontFinder.Object);
		AtkDamageLabel->SetFont(FontFinder.Object);
		DefDamageLabel->SetFont(FontFinder.Object);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Material helpers
// ─────────────────────────────────────────────────────────────────────────────

void ABattleVisualizationActor::ApplyStageFloorStyle()
{
	if (!FloorMesh) { return; }
	if (UMaterial* FlatMat = LoadBattleFlatMaterial()) {
		if (!FloorMID) {
			FloorMID = UMaterialInstanceDynamic::Create(FlatMat, this);
		}
		TintFlatMaterial(FloorMID, FLinearColor(0.97f, 0.97f, 0.98f, 1.f));
		FloorMesh->SetMaterial(0, FloorMID);
		FloorMesh->MarkRenderStateDirty();
	}
}

void ABattleVisualizationActor::ApplyPortraitArt(
	UStaticMeshComponent* Comp, TObjectPtr<UMaterialInstanceDynamic>& MID,
	const FString& ArtId, float HeightCm)
{
	if (!Comp) { return; }

	const float TargetH = (HeightCm > 0.f) ? HeightCm : kSpriteHeightCm;

	UTexture2D* Tex = ArtId.IsEmpty() ? nullptr : TacticsCardArtUi::LoadCardArtTexture(ArtId);
	UE_LOG(LogTemp, Warning, TEXT("[BattleViz] ApplyPortraitArt art='%s' tex=%s size=%dx%d"),
		*ArtId, Tex ? TEXT("OK") : TEXT("NULL"),
		Tex ? Tex->GetSizeX() : 0, Tex ? Tex->GetSizeY() : 0);
	if (!Tex) {
		Comp->SetVisibility(false);
		return;
	}

	if (!MID) {
		if (UMaterialInterface* Base = LoadPortraitUnlitMaterial()) {
			MID = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	MID = TacticsCardArtUi::ApplyCardArtToMesh(Comp, Tex, MID, this);

	const int32 TexW = Tex->GetSizeX();
	const int32 TexH = Tex->GetSizeY();
	const float HeightScale = TargetH / kPlaneBaseSizeCm;
	const float Aspect = (TexH > 0) ? (static_cast<float>(TexW) / static_cast<float>(TexH)) : 1.f;
	Comp->SetRelativeScale3D(FVector(HeightScale * Aspect, HeightScale, 1.f));

	Comp->SetVisibility(true);
	Comp->MarkRenderStateDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic portrait management
// ─────────────────────────────────────────────────────────────────────────────

void ABattleVisualizationActor::DestroyAllUnitPortraits()
{
	for (TObjectPtr<UStaticMeshComponent>& Comp : UnitPortraits) {
		if (IsValid(Comp)) {
			Comp->DestroyComponent();
		}
	}
	UnitPortraits.Empty();
	UnitMIDs.Empty();
	UnitEntityIds.Empty();
	UnitHomeLocations.Empty();
	AtkUnitIdx = -1;
	DefUnitIdx = -1;
}

int32 ABattleVisualizationActor::SpawnUnitPortrait(
	const FCombatUnitSnapshot& Unit, const FVector& HomeLoc, float HeightCm)
{
	const int32 Idx = UnitPortraits.Num();

	UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(
		this, *FString::Printf(TEXT("UnitPortrait_%d"), Idx));
	if (!ensure(P)) {
		UnitPortraits.Add(nullptr);
		UnitMIDs.Add(nullptr);
		return Idx;
	}
	P->SetupAttachment(RootComponent);
	P->SetRelativeLocation(HomeLoc);
	P->SetRelativeRotation(kPortraitRot);
	P->SetCastShadow(false);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->SetReceivesDecals(false);
	if (UStaticMesh* Plane = LoadPlaneMesh()) {
		P->SetStaticMesh(Plane);
	}
	P->RegisterComponent();

	UnitPortraits.Add(P);
	UnitMIDs.Add(nullptr);

	ApplyPortraitArt(P, UnitMIDs[Idx], Unit.ArtId, HeightCm);
	return Idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Background board (our own grid, sized to the live tactics board)
// ─────────────────────────────────────────────────────────────────────────────

void ABattleVisualizationActor::DestroyBackgroundBoard()
{
	for (TObjectPtr<UStaticMeshComponent>& Tile : BoardTiles) {
		if (IsValid(Tile)) {
			Tile->DestroyComponent();
		}
	}
	BoardTiles.Empty();
}

void ABattleVisualizationActor::BuildBackgroundBoard(int32 Cols, int32 Rows, int32 PanelId, float CenterX)
{
	Cols = FMath::Clamp(Cols, 1, kBoardMaxDim);
	Rows = FMath::Clamp(Rows, 1, kBoardMaxDim);

	// Two shared tints for the checkerboard.
	if (UMaterial* Flat = LoadBattleFlatMaterial()) {
		if (!BoardTileLightMID) { BoardTileLightMID = UMaterialInstanceDynamic::Create(Flat, this); }
		if (!BoardTileDarkMID)  { BoardTileDarkMID  = UMaterialInstanceDynamic::Create(Flat, this); }
		TintFlatMaterial(BoardTileLightMID, FLinearColor(0.80f, 0.82f, 0.88f, 1.f));
		TintFlatMaterial(BoardTileDarkMID,  FLinearColor(0.34f, 0.39f, 0.54f, 1.f));
	}

	UStaticMesh* Plane = LoadPlaneMesh();
	const float ScaleX = kBoardCellW / kPlaneBaseSizeCm;
	const float ScaleY = kBoardCellD / kPlaneBaseSizeCm;

	for (int32 Cy = 0; Cy < Rows; ++Cy) {
		for (int32 Cx = 0; Cx < Cols; ++Cx) {
			UStaticMeshComponent* Tile = NewObject<UStaticMeshComponent>(
				this, *FString::Printf(TEXT("BoardTile_%d_%d_%d"), PanelId, Cx, Cy));
			if (!Tile) { continue; }
			Tile->SetupAttachment(RootComponent);
			Tile->SetRelativeLocation(FVector(PanelCellX(Cx, Cols, CenterX), BoardCellY(Cy, Rows), kBoardZ));
			Tile->SetRelativeScale3D(FVector(ScaleX, ScaleY, 1.f));
			Tile->SetCastShadow(false);
			Tile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Tile->SetReceivesDecals(false);
			if (Plane) { Tile->SetStaticMesh(Plane); }
			Tile->RegisterComponent();

			const bool bLight = ((Cx + Cy) & 1) == 0;
			if (UMaterialInstanceDynamic* M = bLight ? BoardTileLightMID : BoardTileDarkMID) {
				Tile->SetMaterial(0, M);
			}
			BoardTiles.Add(Tile);
		}
	}
}

void ABattleVisualizationActor::BuildSeamDivider()
{
	// A tall, thin, dark camera-facing bar at X=0 that visually splits the two panels.
	if (!SeamDivider) {
		SeamDivider = NewObject<UStaticMeshComponent>(this, TEXT("SeamDivider"));
		if (!SeamDivider) { return; }
		SeamDivider->SetupAttachment(RootComponent);
		SeamDivider->SetRelativeRotation(kPortraitRot);  // stand vertical, face the camera
		SeamDivider->SetCastShadow(false);
		SeamDivider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SeamDivider->SetReceivesDecals(false);
		if (UStaticMesh* Plane = LoadPlaneMesh()) { SeamDivider->SetStaticMesh(Plane); }
		SeamDivider->RegisterComponent();
	}
	SeamDivider->SetRelativeLocation(FVector(0.f, kSeamY, kBoardZ + kSeamHeightCm * 0.5f));
	SeamDivider->SetRelativeScale3D(
		FVector(kSeamWidthCm / kPlaneBaseSizeCm, kSeamHeightCm / kPlaneBaseSizeCm, 1.f));
	if (UMaterial* Flat = LoadBattleFlatMaterial()) {
		if (!SeamDividerMID) { SeamDividerMID = UMaterialInstanceDynamic::Create(Flat, this); }
		TintFlatMaterial(SeamDividerMID, FLinearColor(0.05f, 0.05f, 0.07f, 1.f));
		SeamDivider->SetMaterial(0, SeamDividerMID);
	}
	SeamDivider->SetVisibility(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Battlefield setup
// ─────────────────────────────────────────────────────────────────────────────

void ABattleVisualizationActor::SetupBattlefield(const FCombatEncounter& Encounter)
{
	DestroyAllUnitPortraits();
	DestroyBackgroundBoard();
	if (SeamDivider) { SeamDivider->SetVisibility(false); }  // divider is now drawn in the UI

	// Capture the board origin / size ONCE; every unit (and later, camera framing) maps its raw
	// cell through ArenaCellToWorld using these, so the whole arena stays in one fixed frame.
	ArenaMinX = Encounter.BoardMinX;
	ArenaMinY = Encounter.BoardMinY;
	ArenaCols = FMath::Clamp(Encounter.BoardCols, 1, kBoardMaxDim);
	ArenaRows = FMath::Clamp(Encounter.BoardRows, 1, kBoardMaxDim);
	FieldCols = ArenaCols;
	FieldRows = ArenaRows;

	// ── Arena scenery: checkerboard ground + red stripe + sky backdrop ───────────
	// Reproduces the BattleLevel.umap look: a tiled floor receding into the distance,
	// a red band across the front, and a gray-blue wall under the sky-coloured clear.
	// All pieces are plane meshes stored in BoardTiles (destroyed each encounter).
	if (FloorMesh) { FloorMesh->SetVisibility(false); }  // replaced by the checkerboard
	if (UStaticMesh* Plane = LoadPlaneMesh()) {
		UMaterial* Flat = LoadBattleFlatMaterial();
		if (Flat) {
			if (!BoardTileLightMID) { BoardTileLightMID = UMaterialInstanceDynamic::Create(Flat, this); }
			if (!BoardTileDarkMID)  { BoardTileDarkMID  = UMaterialInstanceDynamic::Create(Flat, this); }
			TintFlatMaterial(BoardTileLightMID, FLinearColor(0.82f, 0.84f, 0.88f, 1.f));
			TintFlatMaterial(BoardTileDarkMID,  FLinearColor(0.30f, 0.34f, 0.45f, 1.f));
		}
		auto MakeSceneryPlane = [&](const TCHAR* Name, const FVector& Loc, const FRotator& Rot,
		                            const FVector& Scale, UMaterialInterface* Mat) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
			if (!C) { return nullptr; }
			C->SetupAttachment(RootComponent);
			C->SetRelativeLocation(Loc);
			C->SetRelativeRotation(Rot);
			C->SetRelativeScale3D(Scale);
			C->SetCastShadow(false);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetReceivesDecals(false);
			C->SetStaticMesh(Plane);
			C->RegisterComponent();
			if (Mat) { C->SetMaterial(0, Mat); }
			BoardTiles.Add(C);
			return C;
		};
		// Large checkerboard ground: the engine's gray checker floor material on one huge plane
		// that runs out to the horizon and fills the frame edge-to-edge.  World-aligned UVs tile
		// the checker automatically, and a flat plane meets the sky exactly at the horizon line,
		// so the ground/sky seam is clean (a finite tile grid left black wherever it didn't reach).
		UMaterialInterface* CheckerMat = LoadGroundCheckerMaterial();
		UMaterialInstanceDynamic* GroundFallbackMID = nullptr;
		if (!CheckerMat && Flat) {
			GroundFallbackMID = UMaterialInstanceDynamic::Create(Flat, this);
			TintFlatMaterial(GroundFallbackMID, FLinearColor(0.58f, 0.62f, 0.69f, 1.f));
		}
		{
			constexpr float kGroundSize = 160000.f;  // huge: reaches the horizon in every direction
			MakeSceneryPlane(TEXT("ArenaGround"),
				FVector(0.f, kGroundSize * 0.25f, kArenaGroundZ), FRotator::ZeroRotator,
				FVector(kGroundSize / kPlaneBaseSizeCm, kGroundSize / kPlaneBaseSizeCm, 1.f),
				CheckerMat ? CheckerMat : static_cast<UMaterialInterface*>(GroundFallbackMID));
		}
		// (Sky is a real SkyAtmosphere spawned in BeginPlay - so no faked backdrop planes are
		// needed; they only faded to black at distance anyway.  The shared field places every
		// unit on its TRUE board cell, so no arena front-stripe / front-and-centre duel.)
	}

	// ── One cohesive battlefield: every unit on its TRUE board cell ──────────────
	// This is built ONCE and kept for the whole encounter queue.  Each unit (both teams,
	// including the duelists) is placed at its real (GridX,GridY) via ArenaCellToWorld, so the
	// board is an accurate, persistent arena.  Per encounter, PlayEncounter resolves that
	// fight's two duelists among these persistent portraits and glides the cameras over to them
	// - the surrounding units + checker ground are the landmarks that make the travel visible.
	for (const FCombatUnitSnapshot& Unit : Encounter.AllBattlefieldUnits) {
		const FVector Home = ArenaCellToWorld(Unit.GridX, Unit.GridY, kBoardDuelZ);
		SpawnUnitPortrait(Unit, Home, kBoardDuelHeightCm);
		UnitEntityIds.Add(Unit.EntityId);
		UnitHomeLocations.Add(Home);
	}

	// Legacy / empty snapshot fallback: make sure at least the first encounter's two duelists
	// exist on the board even when AllBattlefieldUnits wasn't populated.
	auto EnsureUnit = [&](const FCombatUnitSnapshot& U)
	{
		if (U.EntityId.IsEmpty() || FindUnitIndexByEntityId(U.EntityId) >= 0) { return; }
		const FVector Home = ArenaCellToWorld(U.GridX, U.GridY, kBoardDuelZ);
		SpawnUnitPortrait(U, Home, kBoardDuelHeightCm);
		UnitEntityIds.Add(U.EntityId);
		UnitHomeLocations.Add(Home);
	};
	EnsureUnit(Encounter.AttackerBefore);
	EnsureUnit(Encounter.DefenderBefore);

	ApplyStageFloorStyle();
}

// Map a raw board cell to world space in the persistent arena: board centred on the actor
// origin, columns spread along world X (screen left↔right), rows receding along world Y.
FVector ABattleVisualizationActor::ArenaCellToWorld(int32 GridX, int32 GridY, float StandZ) const
{
	const float Cx = float(GridX - ArenaMinX);
	const float Cy = float(GridY - ArenaMinY);
	const float WX = (Cx - (float(ArenaCols) - 1.f) * 0.5f) * kArenaCellW;
	const float WY = (Cy - (float(ArenaRows) - 1.f) * 0.5f) * kArenaCellD;
	return FVector(WX, WY, StandZ);
}

int32 ABattleVisualizationActor::FindUnitIndexByEntityId(const FString& EntityId) const
{
	if (EntityId.IsEmpty()) { return -1; }
	for (int32 i = 0; i < UnitEntityIds.Num(); ++i) {
		if (UnitEntityIds[i] == EntityId) { return i; }
	}
	return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Split combat view - one fighter per actor, rendered to a render target
// ─────────────────────────────────────────────────────────────────────────────

void ABattleVisualizationActor::EnsureRenderTarget()
{
	if (!RenderTarget) {
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("BattleSideRT"));
		RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		RenderTarget->ClearColor = kArenaSkyColor;  // sky shows above the backdrop / floor horizon
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->InitAutoFormat(kRenderTargetW, kRenderTargetH);
		RenderTarget->UpdateResourceImmediate(true);
	}
	if (SceneCapture) {
		SceneCapture->TextureTarget = RenderTarget;
	}
}

// Create / bind BOTH per-side render targets (attacker camera → RenderTarget, defender
// camera → RenderTargetB).  Each is a portrait-ish screen; the UI shows them side-by-side.
void ABattleVisualizationActor::EnsureRenderTargets()
{
	if (!RenderTarget) {
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("BattleRT_Atk"));
		RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		RenderTarget->ClearColor = kArenaSkyColor;
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->InitAutoFormat(kSideScreenW, kSideScreenH);
		RenderTarget->UpdateResourceImmediate(true);
	}
	if (!RenderTargetB) {
		RenderTargetB = NewObject<UTextureRenderTarget2D>(this, TEXT("BattleRT_Def"));
		RenderTargetB->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		RenderTargetB->ClearColor = kArenaSkyColor;
		RenderTargetB->bAutoGenerateMips = false;
		RenderTargetB->InitAutoFormat(kSideScreenW, kSideScreenH);
		RenderTargetB->UpdateResourceImmediate(true);
	}
	if (SceneCapture)  { SceneCapture->TextureTarget  = RenderTarget;  }
	if (SceneCaptureB) { SceneCaptureB->TextureTarget = RenderTargetB; }
}

// Aim each camera at its front fighter, looking +Y (portraits stay face-on) with a small
// sideways lead toward the opponent so the fighter has room to lunge.  The duelists sit on their
// TRUE board cells in the persistent arena, so each camera frames its own fighter wherever it
// stands.  First encounter snaps; later ones glide across the board to the next duel.
void ABattleVisualizationActor::FrameCamerasForEncounter(bool bIsPostResolution)
{
	const FVector AtkW = AtkHomeLoc;  // attacker's true-cell home (set in PlayEncounter)
	const FVector DefW = DefHomeLoc;  // defender's true-cell home

	// ── Per-encounter camera orientation: always present the duel LEFT→RIGHT ─────────────────────
	// A fixed yaw only reads cleanly when the two cells differ in screen-X.  For a "vertical" fight
	// (cells differing in depth) the opponent ends up directly behind the fighter, so you'd see the
	// unit you're attacking in the background.  Instead, orient both cameras so their RIGHT vector
	// runs along the attacker→defender line: that line then maps to the screen's horizontal axis, the
	// opponent is pushed far off to the side (off-screen at the 2400 spacing), and the lunge toward
	// the seam always reads as a clean left/right clash regardless of the units' real board cells.
	FVector AB = DefW - AtkW;
	AB.Z = 0.f;
	const float ABLen = AB.Size();
	EncCamRight   = (ABLen > KINDA_SMALL_NUMBER) ? (AB / ABLen) : FVector(-1.f, 0.f, 0.f);
	// Camera forward ⟂ Right (Right = screen-horizontal); Forward = Right × Up.
	EncCamForward = FVector::CrossProduct(EncCamRight, FVector::UpVector).GetSafeNormal();
	const float EncYaw = EncCamForward.Rotation().Yaw;

	// Each camera sits pulled back along -Forward from its fighter, raised to eye height, and nudged
	// sideways so the fighter sits off-centre with room to lunge toward the seam: the attacker on the
	// screen-left (camera shifted +Right) lunging right, the defender on the screen-right lunging left.
	auto Framing = [&](const FVector& Mine, float LeadSign) -> FVector
	{
		FVector P = Mine - EncCamForward * kFrameCamPullback + EncCamRight * (LeadSign * kFrameCamLeadX);
		P.Z = kFrameCamHeight;
		return P;
	};

	CamPosTgt[0] = Framing(AtkW, +1.f);  // attacker camera - fighter screen-left, lunges right
	CamPosTgt[1] = Framing(DefW, -1.f);  // defender camera - fighter screen-right, lunges left
	CamRotTgt[0] = FRotator(kFrameCamPitch, EncYaw, 0.f);
	CamRotTgt[1] = FRotator(kFrameCamPitch, EncYaw, 0.f);
	// Glide START orientation: SLERP from the previous battle's orientation (so a vertical→horizontal
	// transition swings smoothly) or, on the very first view, start already at the target.
	CamRotCur[0] = bHasExternalGlideRot ? ExternalGlideRot[0] : CamRotTgt[0];
	CamRotCur[1] = bHasExternalGlideRot ? ExternalGlideRot[1] : CamRotTgt[1];

	// The attacker's lunge axis is the camera's right vector (toward the seam); the defender's counter
	// uses -LungeDir.  (Portrait/label billboarding is applied below, tracking the camera yaw.)
	LungeDir   = EncCamRight;
	LungeReach = kSeamLungeReach;

	if (SceneCapture)  { SceneCapture->FOVAngle  = kFrameCamFov; }
	if (SceneCaptureB) { SceneCaptureB->FOVAngle = kFrameCamFov; }

	// Whether to play the attack→counter lunge this time.  Only resolved encounters animate, and
	// the GameInstance gates it (SetChoreographyAllowed) to dedupe a re-presented result screen so
	// the same fight never lunges twice.  Consume the one-shot gate immediately.
	const bool bDoLunge = bIsPostResolution && bChoreographyAllowed;
	bChoreographyAllowed = true;

	// Host UI spawns a new BattleVisualizationActor per encounter view, so framing
	// continuity lives on the GameInstance (SetExternalGlideOrigin).
	// If we have the previous battle's camera, pan from it. Otherwise push in straight.
	if (bHasExternalGlideOrigin) {
		CamPosCur[0] = ExternalGlideOrigin[0];
		CamPosCur[1] = ExternalGlideOrigin[1];
	} else {
		// Push in along the camera's own -Forward (not a hardcoded -Y), so the establishing dolly
		// stays a clean straight push-in for any per-encounter camera orientation.
		const FVector PushBack = -EncCamForward * kCamEstablishBack + FVector(0.f, 0.f, kCamEstablishUp);
		CamPosCur[0] = CamPosTgt[0] + PushBack;
		CamPosCur[1] = CamPosTgt[1] + PushBack;
	}

	const float Travel = FMath::Max((CamPosTgt[0] - CamPosCur[0]).Size(),
	                                (CamPosTgt[1] - CamPosCur[1]).Size());

	if (Travel < kCamSameSpotTravel) {
		// The same fight re-shown (pre-roll matchup → its resolved outcome is the SAME two cells),
		// so there is no real move to make.  Snap to the framing and start the choreography
		// immediately - do NOT replay the establishing animation a second time for the same fight.
		CamPosCur[0] = CamPosTgt[0];
		CamPosCur[1] = CamPosTgt[1];
		CamRotCur[0] = CamRotTgt[0];
		CamRotCur[1] = CamRotTgt[1];
		if (SceneCapture)  { SceneCapture->SetRelativeLocationAndRotation(CamPosCur[0], CamRotTgt[0]); }
		if (SceneCaptureB) { SceneCaptureB->SetRelativeLocationAndRotation(CamPosCur[1], CamRotTgt[1]); }
		ApplyPortraitFacing(EncYaw);   // settled - face the target camera directly
		bCamTransitioning    = false;
		bHasFramedOnce       = true;
		bPendingChoreography  = false;
		CamGlideElapsed      = 0.f;
		if (bDoLunge) {
			Phase = EBattlePhase::Intro;  PhaseTime = 0.f;
		} else if (bIsPostResolution) {
			// Suppressed duplicate result - show the settled outcome (dead units toppled), no lunge.
			Phase = EBattlePhase::Settle; PhaseTime = kPhaseSettle;
		} else {
			Phase = EBattlePhase::Idle;   PhaseTime = 0.f;
		}
		return;
	}

	// Apply the start pose immediately so the very first captured frame is the away/wide shot,
	// not a snap to the final framing.  Start at the previous orientation (CamRotCur); the glide
	// slerps it to the target.  Billboard the sprites to the START yaw so they begin face-on.
	if (SceneCapture)  { SceneCapture->SetRelativeLocationAndRotation(CamPosCur[0], CamRotCur[0]); }
	if (SceneCaptureB) { SceneCaptureB->SetRelativeLocationAndRotation(CamPosCur[1], CamRotCur[1]); }
	ApplyPortraitFacing(CamRotCur[0].Yaw);

	CamGlideDuration  = FMath::Clamp(Travel / kCamGlideSpeed, kCamGlideMin, kCamGlideMax);
	bHasFramedOnce    = true;
	bCamTransitioning = true;
	CamGlideElapsed   = 0.f;
	// Hold the lunge until the glide lands so it plays once the view has settled.
	bPendingChoreography = bDoLunge;
	if (!bDoLunge && bIsPostResolution) {
		// Suppressed duplicate result - show the settled outcome once the glide lands… but since the
		// choreography is gated off, just present the settled pose now.
		Phase = EBattlePhase::Settle; PhaseTime = kPhaseSettle;
	} else {
		Phase = EBattlePhase::Idle;   PhaseTime = 0.f;
	}
}

void ABattleVisualizationActor::SetExternalGlideOrigin(const FVector& PosA, const FVector& PosB)
{
	ExternalGlideOrigin[0]   = PosA;
	ExternalGlideOrigin[1]   = PosB;
	bHasExternalGlideOrigin  = true;
}

void ABattleVisualizationActor::SetExternalGlideRotation(const FRotator& RotA, const FRotator& RotB)
{
	ExternalGlideRot[0]   = RotA;
	ExternalGlideRot[1]   = RotB;
	bHasExternalGlideRot  = true;
}

// Billboard every unit portrait (and the floating labels) to face a camera at CamYawDeg, upright.
// kPortraitRot already faces the fixed yaw-kCameraYaw camera; spinning that known-good upright pose
// about the vertical axis by the yaw delta keeps the art upright with no handedness flip.  Called
// every glide tick with the interpolated yaw so the sprites stay face-on while the camera swings.
void ABattleVisualizationActor::ApplyPortraitFacing(float CamYawDeg)
{
	EncFaceQuat = FQuat(FVector::UpVector, FMath::DegreesToRadians(CamYawDeg - kCameraYaw))
	            * kPortraitRot.Quaternion();
	for (TObjectPtr<UStaticMeshComponent>& Comp : UnitPortraits) {
		if (IsValid(Comp)) { Comp->SetRelativeRotation(EncFaceQuat); }
	}
	// Text reads from its -Forward side, so a camera at yaw θ wants the label yaw θ+180.
	const FRotator LabelFace(0.f, CamYawDeg + 180.f, 0.f);
	for (UTextRenderComponent* L : { AtkNameLabel, DefNameLabel, AtkRoleLabel, DefRoleLabel,
	                                 AtkDamageLabel, DefDamageLabel }) {
		if (L) { L->SetRelativeRotation(LabelFace); }
	}
}

// Ease both cameras toward their targets; when the glide lands, kick off deferred choreography.
void ABattleVisualizationActor::UpdateCameraTransition(float DeltaSeconds)
{
	if (!bCamTransitioning) { return; }
	CamGlideElapsed += DeltaSeconds;
	const float T = FMath::Clamp(CamGlideElapsed / FMath::Max(CamGlideDuration, 0.01f), 0.f, 1.f);
	const float S = Smooth01(T);
	// Interpolate the YAW SCALAR directly along the shortest angular path (pitch is constant, roll 0).
	// Do NOT slerp the full quaternion and read back .Rotator().Yaw: near a 180° yaw delta the
	// quaternion→Euler decomposition flips, which snapped the billboard facing (sprites appeared
	// flipped).  A plain shortest-path angle lerp has no such ambiguity.
	float NowYaw = 0.f;
	for (int32 i = 0; i < 2; ++i) {
		const FVector P = FMath::Lerp(CamPosCur[i], CamPosTgt[i], S);
		const float DeltaYaw  = FRotator::NormalizeAxis(CamRotTgt[i].Yaw - CamRotCur[i].Yaw);
		const float YawNow    = CamRotCur[i].Yaw + DeltaYaw * S;
		const float PitchNow  = FMath::Lerp(CamRotCur[i].Pitch, CamRotTgt[i].Pitch, S);
		if (i == 0) { NowYaw = YawNow; }
		USceneCaptureComponent2D* Cap = (i == 0) ? SceneCapture.Get() : SceneCaptureB.Get();
		if (Cap) { Cap->SetRelativeLocationAndRotation(P, FRotator(PitchNow, YawNow, 0.f)); }
	}
	// Keep the sprites face-on to the camera as it swings (both cameras share orientation).
	ApplyPortraitFacing(NowYaw);
	if (T >= 1.f) {
		CamPosCur[0] = CamPosTgt[0];
		CamPosCur[1] = CamPosTgt[1];
		CamRotCur[0] = CamRotTgt[0];
		CamRotCur[1] = CamRotTgt[1];
		ApplyPortraitFacing(CamRotTgt[0].Yaw);   // settle exactly on the target facing
		bCamTransitioning = false;
		if (bPendingChoreography) {
			Phase = EBattlePhase::Intro;
			PhaseTime = 0.f;
			bPendingChoreography = false;
		}
	}
}

void ABattleVisualizationActor::ConfigureSide(const FCombatEncounter& Encounter, bool bAttackerSide)
{
	bIsAttackerSide = bAttackerSide;
	EnsureRenderTarget();

	DestroyAllUnitPortraits();
	DestroyBackgroundBoard();

	const int32 Cols = FMath::Clamp(Encounter.BoardCols, 1, kBoardMaxDim);
	const int32 Rows = FMath::Clamp(Encounter.BoardRows, 1, kBoardMaxDim);
	const int32 MinX = Encounter.BoardMinX;
	const int32 MinY = Encounter.BoardMinY;

	// This screen's fighter and its opponent (the other duelist, NOT shown on this screen).
	const FCombatUnitSnapshot& Me  = bAttackerSide ? Encounter.AttackerBefore : Encounter.DefenderBefore;
	const FCombatUnitSnapshot& Opp = bAttackerSide ? Encounter.DefenderBefore : Encounter.AttackerBefore;
	const int32 MyTeam = Me.Team;

	// ── Board→world mapping, anchored on THIS fighter ────────────────────────────
	// The fighter sits toward its own outer edge (attacker = screen-left, defender =
	// screen-right) and the rest of its team is placed by their TRUE board offset from
	// the fighter, so the layout matches what that unit would see.  The board axis the
	// duel runs along becomes screen-horizontal, oriented so the (off-screen) opponent
	// lies toward the centre seam - i.e. the direction this fighter lunges.
	const float MeCol = float(Me.GridX  - MinX), MeRow = float(Me.GridY  - MinY);
	const float OpCol = float(Opp.GridX - MinX), OpRow = float(Opp.GridY - MinY);

	const float MeSignX = bAttackerSide ? -1.f : +1.f;  // attacker hugs -X, defender +X
	const float dCol = OpCol - MeCol;
	const float dRow = OpRow - MeRow;
	const bool  bDuelAlongCols = FMath::Abs(dCol) >= FMath::Abs(dRow);
	float RawDelta = bDuelAlongCols ? dCol : dRow;
	if (FMath::Abs(RawDelta) < KINDA_SMALL_NUMBER) { RawDelta = 1.f; }
	// Orient the horizontal axis so the opponent maps toward the seam (-MeSignX side).
	const float AxisSign = -MeSignX * FMath::Sign(RawDelta);

	auto CellToWorld = [&](float RawX, float RawY, float StandZ) -> FVector
	{
		const float AlongMe = (bDuelAlongCols ? (RawX - MeCol) : (RawY - MeRow)) * AxisSign;
		const float PerpMe  = (bDuelAlongCols ? (RawY - MeRow) : (RawX - MeCol));
		const float WX = MeSignX * kFieldFighterX + AlongMe * kFieldCellW;
		const float WY = FMath::Max(kFieldMinDepth, kFieldFocusDepth + PerpMe * kFieldCellD);
		return FVector(WX, WY, StandZ);
	};

	// ── Checkerboard tiles only under THIS team's footprint (own cells) ──────────
	if (UMaterial* Flat = LoadBattleFlatMaterial()) {
		if (!BoardTileLightMID) { BoardTileLightMID = UMaterialInstanceDynamic::Create(Flat, this); }
		if (!BoardTileDarkMID)  { BoardTileDarkMID  = UMaterialInstanceDynamic::Create(Flat, this); }
		TintFlatMaterial(BoardTileLightMID, FLinearColor(0.80f, 0.82f, 0.88f, 1.f));
		TintFlatMaterial(BoardTileDarkMID,  FLinearColor(0.34f, 0.39f, 0.54f, 1.f));
	}
	if (UStaticMesh* Plane = LoadPlaneMesh()) {
		const float TileScaleX = kFieldCellW / kPlaneBaseSizeCm;
		const float TileScaleY = kFieldCellD / kPlaneBaseSizeCm;
		for (int32 Cy = 0; Cy < Rows; ++Cy) {
			for (int32 Cx = 0; Cx < Cols; ++Cx) {
				UStaticMeshComponent* Tile = NewObject<UStaticMeshComponent>(
					this, *FString::Printf(TEXT("FieldTile_%d_%d"), Cx, Cy));
				if (!Tile) { continue; }
				Tile->SetupAttachment(RootComponent);
				Tile->SetRelativeLocation(CellToWorld(float(Cx), float(Cy), kBoardZ));
				Tile->SetRelativeScale3D(FVector(TileScaleX, TileScaleY, 1.f));
				Tile->SetCastShadow(false);
				Tile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Tile->SetReceivesDecals(false);
				Tile->SetStaticMesh(Plane);
				Tile->RegisterComponent();
				const bool bLight = ((Cx + Cy) & 1) == 0;
				if (UMaterialInstanceDynamic* M = bLight ? BoardTileLightMID : BoardTileDarkMID) {
					Tile->SetMaterial(0, M);
				}
				BoardTiles.Add(Tile);
			}
		}
	}
	if (SeamDivider) { SeamDivider->SetVisibility(false); }

	// ── Only THIS fighter's team is shown on this screen (never the opponent) ────
	int32   MeIdx  = -1;
	FVector MeHome = CellToWorld(MeCol, MeRow, kBoardDuelZ);
	for (const FCombatUnitSnapshot& Unit : Encounter.AllBattlefieldUnits) {
		const bool bIsMe = (Unit.EntityId == Me.EntityId);
		if (!bIsMe && MyTeam >= 0 && Unit.Team != MyTeam) { continue; }  // hide other team
		const FVector Home = CellToWorld(float(Unit.GridX - MinX), float(Unit.GridY - MinY), kBoardDuelZ);
		const int32 Idx = SpawnUnitPortrait(Unit, Home, kBoardDuelHeightCm);
		UnitEntityIds.Add(Unit.EntityId);
		UnitHomeLocations.Add(Home);
		if (bIsMe) { MeIdx = Idx; MeHome = Home; }
	}
	// If the fighter wasn't in the battlefield snapshot, spawn it explicitly.
	if (MeIdx < 0) {
		MeIdx = SpawnUnitPortrait(Me, MeHome, kBoardDuelHeightCm);
		UnitEntityIds.Add(Me.EntityId);
		UnitHomeLocations.Add(MeHome);
	}

	// Lunge geometry: toward the opponent's (off-screen) real cell at the seam.
	const FVector OppHome = CellToWorld(OpCol, OpRow, kBoardDuelZ);
	FVector ToOpp = OppHome - MeHome; ToOpp.Z = 0.f;
	const float OppDist = ToOpp.Size();
	MeLungeDir   = (OppDist > KINDA_SMALL_NUMBER) ? (ToOpp / OppDist) : FVector(-MeSignX, 0.f, 0.f);
	MeLungeReach = FMath::Clamp(OppDist * 0.45f, 120.f, kSideLungeReach);

	// Use this side's name/role labels; hide the opposite side's (each actor has both sets).
	UTextRenderComponent* NameLabel = bAttackerSide ? AtkNameLabel : DefNameLabel;
	UTextRenderComponent* RoleLabel = bAttackerSide ? AtkRoleLabel : DefRoleLabel;
	UTextRenderComponent* OffName   = bAttackerSide ? DefNameLabel : AtkNameLabel;
	UTextRenderComponent* OffRole   = bAttackerSide ? DefRoleLabel : AtkRoleLabel;
	if (NameLabel) {
		NameLabel->SetRelativeLocation(FVector(MeHome.X, MeHome.Y, kNameLabelZ));
		NameLabel->SetText(FText::FromString(Me.DisplayName.IsEmpty() ? TEXT("?") : Me.DisplayName));
		NameLabel->SetVisibility(true);
	}
	if (RoleLabel) {
		RoleLabel->SetRelativeLocation(FVector(MeHome.X, MeHome.Y, kRoleLabelZ));
		RoleLabel->SetText(FText::FromString(bAttackerSide ? TEXT("ATK") : TEXT("DEF")));
		RoleLabel->SetVisibility(true);
	}
	if (OffName) { OffName->SetVisibility(false); }
	if (OffRole) { OffRole->SetVisibility(false); }

	// This side's damage number floats over its own fighter; hide both until impact.
	UTextRenderComponent* MyDmgLabel  = bAttackerSide ? AtkDamageLabel : DefDamageLabel;
	UTextRenderComponent* OffDmgLabel = bAttackerSide ? DefDamageLabel : AtkDamageLabel;
	if (MyDmgLabel) {
		MyDmgLabel->SetRelativeLocation(FVector(MeHome.X, MeHome.Y, kDamageLabelZ));
		MyDmgLabel->SetVisibility(false);
	}
	if (OffDmgLabel) { OffDmgLabel->SetVisibility(false); }

	ApplyStageFloorStyle();

	// ── Per-screen choreography inputs ────────────────────────────────────────
	bSideMode = true;
	MeUnitIdx = MeIdx;
	MeHomeLoc = MeHome;
	// MeLungeDir / MeLungeReach were computed above from the opponent's real cell.

	AnimAttackDamage     = Encounter.AttackDamage;
	AnimCounterDamage    = Encounter.CounterDamage;
	bAnimAttackWasCrit   = Encounter.bAttackWasCrit;
	bAnimCounterWasCrit  = Encounter.bCounterWasCrit;
	bAnimDefenderDies    = !Encounter.DefenderAfter.bAlive;
	bAnimAttackerDies    = !Encounter.AttackerAfter.bAlive;

	// Pre-roll matchup is static (Idle); the resolved encounter plays the full sequence.
	Phase = Encounter.bIsPostResolution ? EBattlePhase::Intro : EBattlePhase::Idle;
	PhaseTime = 0.f;
}

void ABattleVisualizationActor::BeginPlay()
{
	Super::BeginPlay();

	// Give the captured scene a real sky.  The match map has no SkyAtmosphere, so the scene
	// capture renders a black void behind the arena.  Spawn a sun + SkyAtmosphere once, so the
	// capture shows a proper sky-blue gradient - the BattleLevel look - instead of black.  A real
	// SkyAtmosphere is a screen-space sky, so it fills the whole background regardless of distance
	// (faked far-away planes just faded out).
	if (UWorld* W = GetWorld()) {
		bool bHasSky = false;
		for (TActorIterator<ASkyAtmosphere> It(W); It; ++It) { bHasSky = true; break; }
		if (!bHasSky) {
			// Sun: a Directional Light defaults to acting as the atmosphere sun light, so the
			// SkyAtmosphere picks it up automatically and colours the sky from its angle.
			if (ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(
					ADirectionalLight::StaticClass(), FVector(0.f, 0.f, 2000.f),
					FRotator(-38.f, 55.f, 0.f))) {
				if (UDirectionalLightComponent* Dlc =
						Cast<UDirectionalLightComponent>(Sun->GetLightComponent())) {
					Dlc->SetIntensity(6.f);
				}
			}
			FActorSpawnParameters Sp;
			Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			W->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity, Sp);
		}
	}

	ApplyStageFloorStyle();
}

// ─────────────────────────────────────────────────────────────────────────────
// PlayEncounter
// ─────────────────────────────────────────────────────────────────────────────

/** Builds the shared battlefield and starts the camera glide for this fight. */
void ABattleVisualizationActor::PlayEncounter(const FCombatEncounter& Encounter)
{
	// Single-screen mode: both fighters animate in ONE shared scene (Tick path, not TickSide).
	bSideMode = false;

	// Two cameras render the SAME shared field into two side-by-side off-screen targets.
	EnsureRenderTargets();

	// Build the persistent accurate arena ONCE (every unit on its TRUE board cell); reuse it for
	// every later encounter so the camera can travel across a stable battlefield instead of a
	// scene that's torn down and rebuilt (which made the camera movement invisible / snap).
	if (!bArenaBuilt) {
		SetupBattlefield(Encounter);
		bArenaBuilt = true;
	}

	// Resolve THIS encounter's duelists among the persistent portraits (by EntityId).  If a
	// duelist somehow isn't on the board yet (legacy data), drop it onto its true cell now.
	AtkUnitIdx = FindUnitIndexByEntityId(Encounter.AttackerBefore.EntityId);
	DefUnitIdx = FindUnitIndexByEntityId(Encounter.DefenderBefore.EntityId);
	auto EnsureDuelist = [&](const FCombatUnitSnapshot& U) -> int32
	{
		int32 Idx = FindUnitIndexByEntityId(U.EntityId);
		if (Idx < 0) {
			const FVector Home = ArenaCellToWorld(U.GridX, U.GridY, kBoardDuelZ);
			Idx = SpawnUnitPortrait(U, Home, kBoardDuelHeightCm);
			UnitEntityIds.Add(U.EntityId);
			UnitHomeLocations.Add(Home);
		}
		return Idx;
	};
	if (AtkUnitIdx < 0) { AtkUnitIdx = EnsureDuelist(Encounter.AttackerBefore); }
	if (DefUnitIdx < 0) { DefUnitIdx = EnsureDuelist(Encounter.DefenderBefore); }

	// Keep the duelists accurate to THIS encounter's cells (units can move between fights), and
	// refresh their resting home positions used by the choreography.
	if (AtkUnitIdx >= 0 && AtkUnitIdx < UnitHomeLocations.Num()) {
		UnitHomeLocations[AtkUnitIdx] =
			ArenaCellToWorld(Encounter.AttackerBefore.GridX, Encounter.AttackerBefore.GridY, kBoardDuelZ);
	}
	if (DefUnitIdx >= 0 && DefUnitIdx < UnitHomeLocations.Num()) {
		UnitHomeLocations[DefUnitIdx] =
			ArenaCellToWorld(Encounter.DefenderBefore.GridX, Encounter.DefenderBefore.GridY, kBoardDuelZ);
	}

	// Cache choreography inputs.
	AnimAttackDamage    = Encounter.AttackDamage;
	AnimCounterDamage   = Encounter.CounterDamage;
	bAnimAttackWasCrit  = Encounter.bAttackWasCrit;
	bAnimCounterWasCrit = Encounter.bCounterWasCrit;
	bAnimDefenderDies   = !Encounter.DefenderAfter.bAlive;
	bAnimAttackerDies   = !Encounter.AttackerAfter.bAlive;

	// Home locations are the duelists' real board cells.
	AtkHomeLoc = (AtkUnitIdx >= 0 && AtkUnitIdx < UnitHomeLocations.Num())
		? UnitHomeLocations[AtkUnitIdx] : FVector(0.f, kBoardCenterY, kBoardDuelZ);
	DefHomeLoc = (DefUnitIdx >= 0 && DefUnitIdx < UnitHomeLocations.Num())
		? UnitHomeLocations[DefUnitIdx] : FVector(0.f, kBoardCenterY, kBoardDuelZ);

	// Move the ATK/DEF name + role labels onto this encounter's duelists.
	if (AtkNameLabel) {
		AtkNameLabel->SetRelativeLocation(FVector(AtkHomeLoc.X, AtkHomeLoc.Y, kNameLabelZ));
		AtkNameLabel->SetText(FText::FromString(
			Encounter.AttackerBefore.DisplayName.IsEmpty() ? TEXT("?") : Encounter.AttackerBefore.DisplayName));
	}
	if (DefNameLabel) {
		DefNameLabel->SetRelativeLocation(FVector(DefHomeLoc.X, DefHomeLoc.Y, kNameLabelZ));
		DefNameLabel->SetText(FText::FromString(
			Encounter.DefenderBefore.DisplayName.IsEmpty() ? TEXT("?") : Encounter.DefenderBefore.DisplayName));
	}
	if (AtkRoleLabel) {
		AtkRoleLabel->SetRelativeLocation(FVector(AtkHomeLoc.X, AtkHomeLoc.Y, kRoleLabelZ));
		AtkRoleLabel->SetText(FText::FromString(TEXT("ATK")));
	}
	if (DefRoleLabel) {
		DefRoleLabel->SetRelativeLocation(FVector(DefHomeLoc.X, DefHomeLoc.Y, kRoleLabelZ));
		DefRoleLabel->SetText(FText::FromString(TEXT("DEF")));
	}

	// "Attack the middle" illusion: each fighter lunges along the screen-horizontal axis toward the
	// seam between the two screens (attacker right, defender left), so they converge in the middle.
	// LungeDir / LungeReach are derived from this encounter's camera basis in FrameCamerasForEncounter
	// (below), since the camera orientation - and therefore the screen-horizontal axis - is chosen
	// per encounter so every duel reads left→right.

	// Reset portrait transforms and floating damage numbers.
	if (AtkUnitIdx >= 0 && AtkUnitIdx < UnitPortraits.Num() && IsValid(UnitPortraits[AtkUnitIdx])) {
		UnitPortraits[AtkUnitIdx]->SetRelativeLocation(AtkHomeLoc);
	}
	if (DefUnitIdx >= 0 && DefUnitIdx < UnitPortraits.Num() && IsValid(UnitPortraits[DefUnitIdx])) {
		UnitPortraits[DefUnitIdx]->SetRelativeLocation(DefHomeLoc);
	}
	if (AtkDamageLabel) {
		AtkDamageLabel->SetRelativeLocation(FVector(AtkHomeLoc.X, AtkHomeLoc.Y, kDamageLabelZ));
		AtkDamageLabel->SetVisibility(false);
	}
	if (DefDamageLabel) {
		DefDamageLabel->SetRelativeLocation(FVector(DefHomeLoc.X, DefHomeLoc.Y, kDamageLabelZ));
		DefDamageLabel->SetVisibility(false);
	}

	// Aim both cameras at this encounter's fighters.  First battle snaps + starts the
	// choreography; subsequent battles glide the cameras over and defer choreography until
	// the glide lands (so the lunge plays once the view has arrived at the new battle).
	//   bIsPostResolution=false → static matchup before the dice roll (Idle, no anim)
	//   bIsPostResolution=true  → resolved outcome with full choreography (Intro → ...)
	FrameCamerasForEncounter(Encounter.bIsPostResolution);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void ABattleVisualizationActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Single-fighter side screens use their own choreography path.
	if (bSideMode) { TickSide(DeltaSeconds); return; }

	// Glide the two tracking cameras toward the current encounter's framing.  While a glide
	// is in flight the choreography is held Idle; UpdateCameraTransition releases it on arrival.
	UpdateCameraTransition(DeltaSeconds);

	if (Phase == EBattlePhase::Idle) { return; }
	PhaseTime += DeltaSeconds;

	// For choreography phases we need valid combatant portraits.
	if (AtkUnitIdx < 0 || DefUnitIdx < 0 ||
	    AtkUnitIdx >= UnitPortraits.Num() || DefUnitIdx >= UnitPortraits.Num()) {
		return;
	}
	UStaticMeshComponent* AtkPortrait = UnitPortraits[AtkUnitIdx].Get();
	UStaticMeshComponent* DefPortrait = UnitPortraits[DefUnitIdx].Get();
	UMaterialInstanceDynamic* AtkPortraitMID = UnitMIDs[AtkUnitIdx].Get();
	UMaterialInstanceDynamic* DefPortraitMID = UnitMIDs[DefUnitIdx].Get();
	if (!AtkPortrait || !DefPortrait) { return; }

	switch (Phase) {
		case EBattlePhase::Intro: {
			if (PhaseTime >= kPhaseIntro) {
				Phase = EBattlePhase::AtkLungeOut;
				PhaseTime = 0.f;
			}
			break;
		}

		// ── Attacker strikes the defender ───────────────────────────────────────
		case EBattlePhase::AtkLungeOut: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseLungeOut, 0.f, 1.f);
			const FVector Target = AtkHomeLoc + LungeDir * LungeReach;
			FVector Loc = FMath::Lerp(AtkHomeLoc, Target, Smooth01(Raw));
			Loc.Z += FMath::Sin(PI * Raw) * kLungeHopCm;
			AtkPortrait->SetRelativeLocation(Loc);
			if (PhaseTime >= kPhaseLungeOut) {
				AtkPortrait->SetRelativeLocation(Target);
				ShowFloatingDamage(DefDamageLabel, AnimAttackDamage, bAnimAttackWasCrit);
				Phase = EBattlePhase::AtkImpact;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::AtkImpact: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseImpact, 0.f, 1.f);
			// Defender knocked back in the same direction as the lunge.
			const float Recoil = (1.f - Smooth01(Raw)) * kRecoilCm;
			DefPortrait->SetRelativeLocation(DefHomeLoc + LungeDir * Recoil);
			if (DefDamageLabel && DefDamageLabel->IsVisible()) {
				DefDamageLabel->SetRelativeLocation(
					FVector(DefHomeLoc.X, DefHomeLoc.Y, kDamageLabelZ + Smooth01(Raw) * kDamageRiseCm));
			}
			if (PhaseTime >= kPhaseImpact) {
				DefPortrait->SetRelativeLocation(DefHomeLoc);
				Phase = EBattlePhase::AtkReturn;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::AtkReturn: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseReturn, 0.f, 1.f);
			const FVector From = AtkHomeLoc + LungeDir * LungeReach;
			FVector Loc = FMath::Lerp(From, AtkHomeLoc, Smooth01(Raw));
			Loc.Z += FMath::Sin(PI * Raw) * kLungeHopCm * 0.4f;
			AtkPortrait->SetRelativeLocation(Loc);
			if (PhaseTime >= kPhaseReturn) {
				AtkPortrait->SetRelativeLocation(AtkHomeLoc);
				const bool bNeedsPause = AnimCounterDamage > 0 || bAnimDefenderDies || bAnimAttackerDies;
				Phase = bNeedsPause ? EBattlePhase::Gap : EBattlePhase::Settle;
				PhaseTime = 0.f;
			}
			break;
		}

		case EBattlePhase::Gap: {
			if (PhaseTime >= kPhaseGap) {
				Phase = (AnimCounterDamage > 0) ? EBattlePhase::CtrLungeOut : EBattlePhase::Settle;
				PhaseTime = 0.f;
			}
			break;
		}

		// ── Defender counter-strikes the attacker ───────────────────────────────
		case EBattlePhase::CtrLungeOut: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseLungeOut, 0.f, 1.f);
			// Counter direction is opposite to the original lunge.
			const FVector CtrDir = -LungeDir;
			const FVector Target = DefHomeLoc + CtrDir * LungeReach;
			FVector Loc = FMath::Lerp(DefHomeLoc, Target, Smooth01(Raw));
			Loc.Z += FMath::Sin(PI * Raw) * kLungeHopCm;
			DefPortrait->SetRelativeLocation(Loc);
			if (PhaseTime >= kPhaseLungeOut) {
				DefPortrait->SetRelativeLocation(Target);
				ShowFloatingDamage(AtkDamageLabel, AnimCounterDamage, bAnimCounterWasCrit);
				Phase = EBattlePhase::CtrImpact;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::CtrImpact: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseImpact, 0.f, 1.f);
			const FVector CtrDir = -LungeDir;
			const float Recoil = (1.f - Smooth01(Raw)) * kRecoilCm;
			AtkPortrait->SetRelativeLocation(AtkHomeLoc + CtrDir * Recoil);
			if (AtkDamageLabel && AtkDamageLabel->IsVisible()) {
				AtkDamageLabel->SetRelativeLocation(
					FVector(AtkHomeLoc.X, AtkHomeLoc.Y, kDamageLabelZ + Smooth01(Raw) * kDamageRiseCm));
			}
			if (PhaseTime >= kPhaseImpact) {
				AtkPortrait->SetRelativeLocation(AtkHomeLoc);
				Phase = EBattlePhase::CtrReturn;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::CtrReturn: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseReturn, 0.f, 1.f);
			const FVector CtrDir = -LungeDir;
			const FVector From = DefHomeLoc + CtrDir * LungeReach;
			FVector Loc = FMath::Lerp(From, DefHomeLoc, Smooth01(Raw));
			Loc.Z += FMath::Sin(PI * Raw) * kLungeHopCm * 0.4f;
			DefPortrait->SetRelativeLocation(Loc);
			if (PhaseTime >= kPhaseReturn) {
				DefPortrait->SetRelativeLocation(DefHomeLoc);
				Phase = EBattlePhase::Settle;
				PhaseTime = 0.f;
			}
			break;
		}

		// ── Defeated combatants topple + darken, then idle ──────────────────────
		case EBattlePhase::Settle: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseSettle, 0.f, 1.f);
			if (bAnimDefenderDies) {
				ApplyDeathFall(DefPortrait, DefPortraitMID, DefHomeLoc, Raw, kBoardDuelHeightCm,
				               EncCamRight, EncCamForward, EncFaceQuat);
			}
			if (bAnimAttackerDies) {
				ApplyDeathFall(AtkPortrait, AtkPortraitMID, AtkHomeLoc, Raw, kBoardDuelHeightCm,
				               EncCamRight, EncCamForward, EncFaceQuat);
			}
			if (PhaseTime >= kPhaseSettle) {
				Phase = EBattlePhase::Idle;
				PhaseTime = 0.f;
			}
			break;
		}

		default:
			break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// TickSide - per-screen single-fighter choreography
// ─────────────────────────────────────────────────────────────────────────────

void ABattleVisualizationActor::TickSide(float DeltaSeconds)
{
	if (Phase == EBattlePhase::Idle) { return; }
	PhaseTime += DeltaSeconds;

	if (MeUnitIdx < 0 || MeUnitIdx >= UnitPortraits.Num()) { return; }
	UStaticMeshComponent* Me = UnitPortraits[MeUnitIdx].Get();
	UMaterialInstanceDynamic* MeMID =
		(MeUnitIdx < UnitMIDs.Num()) ? UnitMIDs[MeUnitIdx].Get() : nullptr;
	if (!Me) { return; }

	// Toward-the-opponent (lunge) and away-from-the-opponent (recoil) directions.
	const FVector LungeDirV  = MeLungeDir;
	const FVector RecoilDirV = -MeLungeDir;
	const FVector LungeTarget = MeHomeLoc + LungeDirV * MeLungeReach;

	// This fighter lunges in its own offensive phases; recoils when struck.
	const bool bILungeOnAttack  = bIsAttackerSide;    // attacker strikes first
	const bool bILungeOnCounter = !bIsAttackerSide;   // defender counters

	UTextRenderComponent* MyDmgLabel = bIsAttackerSide ? AtkDamageLabel : DefDamageLabel;
	const bool bIDie = bIsAttackerSide ? bAnimAttackerDies : bAnimDefenderDies;

	auto LungeOut = [&](float Raw)
	{
		FVector Loc = FMath::Lerp(MeHomeLoc, LungeTarget, Smooth01(Raw));
		Loc.Z += FMath::Sin(PI * Raw) * kLungeHopCm;
		Me->SetRelativeLocation(Loc);
	};
	auto LungeBack = [&](float Raw)
	{
		FVector Loc = FMath::Lerp(LungeTarget, MeHomeLoc, Smooth01(Raw));
		Loc.Z += FMath::Sin(PI * Raw) * kLungeHopCm * 0.4f;
		Me->SetRelativeLocation(Loc);
	};
	auto Recoil = [&](float Raw)
	{
		const float Back = (1.f - Smooth01(Raw)) * kRecoilCm;
		Me->SetRelativeLocation(MeHomeLoc + RecoilDirV * Back);
		if (MyDmgLabel && MyDmgLabel->IsVisible()) {
			MyDmgLabel->SetRelativeLocation(FVector(
				MeHomeLoc.X, MeHomeLoc.Y, kDamageLabelZ + Smooth01(Raw) * kDamageRiseCm));
		}
	};
	auto ShowMyDamage = [&](const int32 Damage, const bool bCrit)
	{
		ShowFloatingDamage(MyDmgLabel, Damage, bCrit);
	};

	switch (Phase) {
		case EBattlePhase::Intro: {
			if (PhaseTime >= kPhaseIntro) { Phase = EBattlePhase::AtkLungeOut; PhaseTime = 0.f; }
			break;
		}

		// ── Attacker strikes ────────────────────────────────────────────────────
		case EBattlePhase::AtkLungeOut: {
			if (bILungeOnAttack) {
				LungeOut(FMath::Clamp(PhaseTime / kPhaseLungeOut, 0.f, 1.f));
			}
			if (PhaseTime >= kPhaseLungeOut) {
				if (bILungeOnAttack) { Me->SetRelativeLocation(LungeTarget); }
				else { ShowMyDamage(AnimAttackDamage, bAnimAttackWasCrit); }  // defender is hit now
				Phase = EBattlePhase::AtkImpact;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::AtkImpact: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseImpact, 0.f, 1.f);
			if (bILungeOnAttack) { Me->SetRelativeLocation(LungeTarget); }  // hold lunge
			else { Recoil(Raw); }                                          // defender recoils
			if (PhaseTime >= kPhaseImpact) {
				if (!bILungeOnAttack) { Me->SetRelativeLocation(MeHomeLoc); }
				Phase = EBattlePhase::AtkReturn;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::AtkReturn: {
			if (bILungeOnAttack) {
				LungeBack(FMath::Clamp(PhaseTime / kPhaseReturn, 0.f, 1.f));
			}
			if (PhaseTime >= kPhaseReturn) {
				if (bILungeOnAttack) { Me->SetRelativeLocation(MeHomeLoc); }
				const bool bNeedsPause =
					AnimCounterDamage > 0 || bAnimDefenderDies || bAnimAttackerDies;
				Phase = bNeedsPause ? EBattlePhase::Gap : EBattlePhase::Settle;
				PhaseTime = 0.f;
			}
			break;
		}

		case EBattlePhase::Gap: {
			if (PhaseTime >= kPhaseGap) {
				Phase = (AnimCounterDamage > 0) ? EBattlePhase::CtrLungeOut : EBattlePhase::Settle;
				PhaseTime = 0.f;
			}
			break;
		}

		// ── Defender counter-strikes ────────────────────────────────────────────
		case EBattlePhase::CtrLungeOut: {
			if (bILungeOnCounter) {
				LungeOut(FMath::Clamp(PhaseTime / kPhaseLungeOut, 0.f, 1.f));
			}
			if (PhaseTime >= kPhaseLungeOut) {
				if (bILungeOnCounter) { Me->SetRelativeLocation(LungeTarget); }
				else { ShowMyDamage(AnimCounterDamage, bAnimCounterWasCrit); }  // attacker is hit now
				Phase = EBattlePhase::CtrImpact;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::CtrImpact: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseImpact, 0.f, 1.f);
			if (bILungeOnCounter) { Me->SetRelativeLocation(LungeTarget); }  // hold lunge
			else { Recoil(Raw); }                                           // attacker recoils
			if (PhaseTime >= kPhaseImpact) {
				if (!bILungeOnCounter) { Me->SetRelativeLocation(MeHomeLoc); }
				Phase = EBattlePhase::CtrReturn;
				PhaseTime = 0.f;
			}
			break;
		}
		case EBattlePhase::CtrReturn: {
			if (bILungeOnCounter) {
				LungeBack(FMath::Clamp(PhaseTime / kPhaseReturn, 0.f, 1.f));
			}
			if (PhaseTime >= kPhaseReturn) {
				if (bILungeOnCounter) { Me->SetRelativeLocation(MeHomeLoc); }
				Phase = EBattlePhase::Settle;
				PhaseTime = 0.f;
			}
			break;
		}

		// ── This fighter topples if it died ─────────────────────────────────────
		case EBattlePhase::Settle: {
			const float Raw = FMath::Clamp(PhaseTime / kPhaseSettle, 0.f, 1.f);
			if (bIDie) {
				// Side path uses the fixed yaw-90 camera: right = -X, away from camera = +Y, base = kPortraitRot.
				ApplyDeathFall(Me, MeMID, MeHomeLoc, Raw, kBoardDuelHeightCm,
				               FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), kPortraitRot.Quaternion());
			}
			if (PhaseTime >= kPhaseSettle) {
				Phase = EBattlePhase::Idle;
				PhaseTime = 0.f;
			}
			break;
		}

		default:
			break;
	}
}
