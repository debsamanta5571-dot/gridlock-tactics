#include "TacticsBoardUnitActor.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TacticsCardArtUi.h"

#include "UObject/ConstructorHelpers.h"

/** Builds the mannequin fallback, clickable art plane, and turn-order label. */
ATacticsBoardUnitActor::ATacticsBoardUnitActor()
{
	// Tick drives the smooth glide between board cells (see ApplyWorldPose / Tick).
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Line traces from the cursor hit these; they do not block movement or overlap.
	auto EnableBoardClickQuery = [](UPrimitiveComponent* Comp) {
		if (!Comp) {
			return;
		}
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Comp->SetCollisionObjectType(ECC_WorldDynamic);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Comp->SetGenerateOverlapEvents(false);
	};

	UnitMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("UnitMesh"));
	UnitMesh->SetupAttachment(RootComponent);
	EnableBoardClickQuery(UnitMesh);
	UnitMesh->SetCastShadow(true);
	UnitMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(RootComponent);
	EnableBoardClickQuery(BaseMesh);
	BaseMesh->SetCastShadow(true);
	BaseMesh->SetVisibility(false);

	ArtPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArtPlane"));
	ArtPlane->SetupAttachment(RootComponent);
	EnableBoardClickQuery(ArtPlane);
	ArtPlane->SetCastShadow(false);
	ArtPlane->SetReceivesDecals(false);
	ArtPlane->bUseAsOccluder = false;
	ArtPlane->SetAffectDistanceFieldLighting(false);
	ArtPlane->SetVisibility(false);
	ArtPlane->SetHiddenInGame(false);
	ArtPlane->SetTranslucentSortPriority(700);
	ArtPlane->SetRelativeLocation(FVector::ZeroVector);
	ArtPlane->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	ArtPlane->SetRelativeScale3D(FVector(0.9f, 0.9f, 1.f));

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyFinder(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (MannyFinder.Succeeded()) {
		UnitMesh->SetSkeletalMesh(MannyFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded()) {
		BaseMesh->SetStaticMesh(CubeFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded()) {
		ArtPlane->SetStaticMesh(PlaneFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpriteMatFinder(
		TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"));
	if (SpriteMatFinder.Succeeded()) {
		ArtPlane->SetMaterial(0, SpriteMatFinder.Object);
	}
	// Cook-force board tile / portrait assets that runtime LoadObject otherwise misses.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatColFinder(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GridMatFinder(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	(void)FlatColFinder;
	(void)GridMatFinder;

	TurnOrderLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TurnOrderLabel"));
	TurnOrderLabel->SetupAttachment(RootComponent);
	TurnOrderLabel->SetHorizontalAlignment(EHTA_Center);
	TurnOrderLabel->SetVerticalAlignment(EVRTA_TextCenter);
	TurnOrderLabel->SetTextRenderColor(FColor(64, 140, 255));
	TurnOrderLabel->SetWorldSize(72.f);
	TurnOrderLabel->SetRelativeRotation(FRotator(90.f, 180.f, 0.f));
	TurnOrderLabel->SetXScale(1.f);
	TurnOrderLabel->SetYScale(1.f);
	TurnOrderLabel->SetRelativeLocation(FVector(0.f, 0.f, 260.f));
	TurnOrderLabel->SetVisibility(false);
	TurnOrderLabel->SetHiddenInGame(true);
	TurnOrderLabel->SetCastShadow(false);
	TurnOrderLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurnOrderLabel->SetTranslucentSortPriority(1000);

	static ConstructorHelpers::FObjectFinder<UFont> TurnOrderFontFinder(
		TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
	if (TurnOrderFontFinder.Succeeded()) {
		TurnOrderLabel->SetFont(TurnOrderFontFinder.Object);
	}
}

void ATacticsBoardUnitActor::ApplyWorldPose(const FVector& WorldLocation, const float UniformScale,
	const float GridCenterX, const float GridCenterY)
{
	PoseTargetLocation = WorldLocation;
	PoseTargetGridX = GridCenterX;
	PoseTargetGridY = GridCenterY;
	PoseUniformScale = UniformScale;
	SetActorScale3D(FVector(UniformScale));

	// First placement (spawn/deploy) snaps - a unit appearing on the board should not slide in from the origin.
	if (!bPosePlaced) {
		SetActorLocation(WorldLocation);
		bPosePlaced = true;
		PoseSyncedGridX = GridCenterX;
		PoseSyncedGridY = GridCenterY;
		return;
	}

	// Decide snap vs glide from authoritative grid delta (immune to board layout / spacing reprojection).
	const float Dx = GridCenterX - PoseSyncedGridX;
	const float Dy = GridCenterY - PoseSyncedGridY;
	const float GridDistSq = Dx * Dx + Dy * Dy;
	// Cross-board resync / re-slot (~12+ cells): snap so units do not streak across the whole map.
	constexpr float kResyncGridCells = 12.f;
	if (GridDistSq > kResyncGridCells * kResyncGridCells) {
		SetActorLocation(WorldLocation);
		PoseSyncedGridX = GridCenterX;
		PoseSyncedGridY = GridCenterY;
		return;
	}

	// Normal move: keep current world position; Tick glides toward PoseTargetLocation.
}

bool ATacticsBoardUnitActor::TryGetBoardGridCell(int& OutWx, int& OutWy) const
{
	if (!bPosePlaced) {
		return false;
	}
	OutWx = FMath::RoundToInt(PoseTargetGridX);
	OutWy = FMath::RoundToInt(PoseTargetGridY);
	return true;
}

void ATacticsBoardUnitActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FVector Current = GetActorLocation();
	if (Current.Equals(PoseTargetLocation, 0.5f)) {
		PoseSyncedGridX = PoseTargetGridX;
		PoseSyncedGridY = PoseTargetGridY;
		return;
	}
	// Ease toward the target cell; the interp speed completes a normal move well within the bot's
	// per-move pacing so the glide reads as a deliberate move, not a teleport.
	const FVector Next = FMath::VInterpTo(Current, PoseTargetLocation, DeltaSeconds, 9.f);
	SetActorLocation(Next);
}

void ATacticsBoardUnitActor::ConfigureFootprintVisual(const int32 FootprintSpanX, const int32 FootprintSpanY,
	const float FloorMeshScale)
{
	CachedFootprintSpanX = FMath::Max(1, FootprintSpanX);
	CachedFootprintSpanY = FMath::Max(1, FootprintSpanY);
	CachedFloorMeshScale = FMath::Max(0.1f, FloorMeshScale);
	ApplyFootprintVisualScale();
}

void ATacticsBoardUnitActor::ApplyFootprintVisualScale()
{
	const float Fit = CachedFloorMeshScale;
	const float WidthScale = static_cast<float>(CachedFootprintSpanX) * 0.98f * Fit;
	const float DepthScale = static_cast<float>(CachedFootprintSpanY) * 0.98f * Fit;
	const float MannequinScale = static_cast<float>(FMath::Max(CachedFootprintSpanX, CachedFootprintSpanY)) * 0.98f * Fit;

	if (ArtPlane) {
		if (bBattleBackgroundPresentation) {
			ArtPlane->SetRelativeScale3D(FVector(DepthScale * 0.55f, WidthScale * 0.55f, 1.f));
		} else {
			ArtPlane->SetRelativeScale3D(FVector(DepthScale, WidthScale, 1.f));
		}
	}
	if (UnitMesh) {
		UnitMesh->SetRelativeScale3D(FVector(MannequinScale));
	}
	if (TurnOrderLabel) {
		const float LabelZ = 260.f * static_cast<float>(FMath::Max(CachedFootprintSpanX, CachedFootprintSpanY));
		TurnOrderLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelZ));
	}
}

void ATacticsBoardUnitActor::ConfigureAsBase(bool bAsBase, int32 FootprintSpanX, int32 FootprintSpanY)
{
	if (!BaseMesh) {
		return;
	}
	BaseMesh->SetVisibility(bAsBase);
	BaseMesh->SetCastShadow(false);

	if (bAsBase) {
		if (UnitMesh) {
			UnitMesh->SetVisibility(false);
		}
		if (ArtPlane) {
			ArtPlane->SetVisibility(false);
		}
		if (TurnOrderLabel) {
			TurnOrderLabel->SetVisibility(false);
			TurnOrderLabel->SetHiddenInGame(true);
		}
	} else {
		UpdateUnitVisuals();
	}

	if (!bAsBase) {
		return;
	}
	const float WidthScale = FMath::Max(1.f, static_cast<float>(FootprintSpanX)) * 0.98f;
	const float DepthScale = FMath::Max(1.f, static_cast<float>(FootprintSpanY)) * 0.58f;
	const FVector BaseScale(DepthScale, WidthScale, 0.42f);
	BaseMesh->SetRelativeScale3D(BaseScale);
	BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, BaseScale.Z * 50.f));
}

void ATacticsBoardUnitActor::UpdateUnitVisuals()
{
	// Bases use the slab mesh only; units use art if loaded, otherwise the mannequin.
	const bool bIsBase = BaseMesh && BaseMesh->IsVisible();
	if (bIsBase) {
		return;
	}
	const bool bShowArt = IsValid(LoadedArtTexture) && ArtPlane != nullptr;
	if (UnitMesh) {
		UnitMesh->SetVisibility(!bShowArt);
	}
	if (ArtPlane) {
		ArtPlane->SetVisibility(bShowArt);
		ArtPlane->SetHiddenInGame(!bShowArt);
		if (bShowArt) {
			ArtPlane->MarkRenderStateDirty();
		}
	}
}

void ATacticsBoardUnitActor::SetCardArt(const FString& ArtId)
{
	if (ArtId.IsEmpty()) {
		if (LoadedArtTexture) {
			UpdateUnitVisuals();
			return;
		}
		LoadedArtId.Reset();
		LoadedArtTexture = nullptr;
		UpdateUnitVisuals();
		return;
	}

	if (LoadedArtId == ArtId && IsValid(LoadedArtTexture)) {
		UpdateUnitVisuals();
		return;
	}

	const FString ArtPath = TacticsCardArtUi::CardArtFilePathFromId(ArtId);
	UTexture2D* Texture = TacticsCardArtUi::LoadCardArtTexture(ArtId);
	if (!Texture) {
		UE_LOG(LogTemp, Warning, TEXT("TacticsBoardUnitActor: no texture for art_id '%s' (path: %s)"), *ArtId, *ArtPath);
		if (!LoadedArtTexture) {
			UpdateUnitVisuals();
		}
		return;
	}

	if (!ArtPlane) {
		UE_LOG(LogTemp, Warning, TEXT("TacticsBoardUnitActor: failed to apply art to plane for '%s'"), *ArtId);
		if (!LoadedArtTexture) {
			UpdateUnitVisuals();
		}
		return;
	}
	ArtMaterial = TacticsCardArtUi::ApplyCardArtToMesh(ArtPlane, Texture, ArtMaterial.Get(), this);
	if (!ArtMaterial) {
		UE_LOG(LogTemp, Warning, TEXT("TacticsBoardUnitActor: failed to apply art to plane for '%s'"), *ArtId);
		if (!LoadedArtTexture) {
			UpdateUnitVisuals();
		}
		return;
	}
	ArtPlane->SetTranslucentSortPriority(700);

	LoadedArtId = ArtId;
	LoadedArtTexture = Texture;
	UpdateUnitVisuals();
}

void ATacticsBoardUnitActor::ApplyAbilityCastFlash(const bool bSuccess, const float Alpha)
{
	AbilityCastFlashAlpha = Alpha;
	bAbilityCastFlashSuccess = bSuccess;
	if (Alpha <= 0.f) {
		if (ArtMaterial) {
			ArtMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor::Black);
		}
		return;
	}
	const FLinearColor Flash = bSuccess ? FLinearColor(0.15f, 0.92f, 0.38f, Alpha * 0.85f)
										: FLinearColor(0.95f, 0.12f, 0.08f, Alpha * 0.85f);
	if (ArtMaterial) {
		ArtMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), Flash);
	}
}

void ATacticsBoardUnitActor::SetTurnOrderBadge(const int32 Rank, const bool bIsBase, const bool bHasTaunt)
{
	if (!TurnOrderLabel) {
		return;
	}
	if (bIsBase || (Rank <= 0 && !bHasTaunt)) {
		TurnOrderLabel->SetVisibility(false);
		TurnOrderLabel->SetHiddenInGame(true);
		return;
	}
	FString Label;
	if (Rank > 0 && bHasTaunt) {
		Label = FString::Printf(TEXT("%d·T"), Rank);
	} else if (Rank > 0) {
		Label = FString::FromInt(Rank);
	} else {
		Label = TEXT("T");
	}
	TurnOrderLabel->SetText(FText::FromString(Label));
	TurnOrderLabel->SetTextRenderColor(bHasTaunt ? FColor(255, 180, 40) : FColor(64, 140, 255));
	TurnOrderLabel->SetRelativeRotation(FRotator(90.f, 180.f, 0.f));
	TurnOrderLabel->SetXScale(1.f);
	TurnOrderLabel->SetYScale(1.f);
	TurnOrderLabel->SetRelativeLocation(FVector(0.f, 0.f, 260.f));
	TurnOrderLabel->SetHiddenInGame(false);
	TurnOrderLabel->SetVisibility(true);
	TurnOrderLabel->MarkRenderStateDirty();
}

void ATacticsBoardUnitActor::SetBattleBackgroundPresentation(const bool bEnable)
{
	bBattleBackgroundPresentation = bEnable;
	if (ArtPlane) {
		if (bEnable) {
			// Match ABattleVisualizationActor portrait facing (-Y camera).
			ArtPlane->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
			ArtPlane->SetRelativeLocation(FVector(0.f, 0.f, 18.f));
		} else {
			ArtPlane->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
			ArtPlane->SetRelativeLocation(FVector::ZeroVector);
		}
	}
	ApplyFootprintVisualScale();
}