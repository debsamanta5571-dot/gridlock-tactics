#include "Tactics3DWorldBoardActor.h"

#include "BattleVisualizationActor.h"
#include "TacticsBoardUnitActor.h"
#include "TacticsBoardVisualSettingsSubsystem.h"

#include "Tactics3DBoardGameMode.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TacticsMatchSubsystem.h"
#include "TacticsAbilityResolveFlash.h"
#include "TacticsCardText.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr FLinearColor k3DBoard_GroutBrown(0.46f, 0.28f, 0.12f, 1.f);

/** First LoadObject success among Paths. */
UMaterial* Board3D_LoadMaterialFromCandidates(const TCHAR* const* Paths, const int32 Count)
{
	for (int32 I = 0; I < Count; ++I) {
		if (UMaterial* M = LoadObject<UMaterial>(nullptr, Paths[I])) {
			return M;
		}
	}
	return nullptr;
}

/** Unlit flat color for tiles and grout; rooted so it survives GC. */
UMaterial* LoadBoardFlatMaterial()
{
	static UMaterial* Cached = nullptr;
	static bool bRooted = false;
	if (Cached) {
		return Cached;
	}
	static const TCHAR* Candidates[] = {
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"),
		TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"),
		TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"),
	};
	Cached = Board3D_LoadMaterialFromCandidates(Candidates, UE_ARRAY_COUNT(Candidates));
	if (Cached && !bRooted) {
		Cached->AddToRoot();
		bRooted = true;
	}
	return Cached;
}

/** Sets Color, BaseColor, and EmissiveColor on a flat MID. */
void Board3D_TintFlatMaterial(UMaterialInstanceDynamic* MID, const FLinearColor& Color)
{
	if (!MID) {
		return;
	}
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
	MID->SetVectorParameterValue(TEXT("EmissiveColor"), Color);
}
}

ATactics3DWorldBoardActor::ATactics3DWorldBoardActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(Root);

	BoardUnderlay = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardUnderlay"));
	BoardUnderlay->SetupAttachment(RootComponent);
	BoardUnderlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardUnderlay->SetCastShadow(false);
	BoardUnderlay->SetHiddenInGame(false);

	GridGroutMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GridGroutMeshes"));
	GridGroutMeshes->SetupAttachment(RootComponent);
	GridGroutMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GridGroutMeshes->SetCastShadow(false);

	CellMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CellMeshes"));
	CellMeshes->SetupAttachment(RootComponent);
	CellMeshes->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CellMeshes->SetCollisionProfileName(TEXT("BlockAll"));
	CellMeshes->SetGenerateOverlapEvents(false);
	CellMeshes->SetCastShadow(false);
	CellMeshes->SetReceivesDecals(false);

	MoveReachHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MoveReachHighlightMeshes"));
	MoveReachHighlightMeshes->SetupAttachment(RootComponent);
	MoveReachHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoveReachHighlightMeshes->SetCastShadow(false);

	PendingMoveDestinationHighlightMeshes =
		CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PendingMoveDestinationHighlightMeshes"));
	PendingMoveDestinationHighlightMeshes->SetupAttachment(RootComponent);
	PendingMoveDestinationHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PendingMoveDestinationHighlightMeshes->SetCastShadow(false);

	PendingMoveOriginHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PendingMoveOriginHighlightMeshes"));
	PendingMoveOriginHighlightMeshes->SetupAttachment(RootComponent);
	PendingMoveOriginHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PendingMoveOriginHighlightMeshes->SetCastShadow(false);

	AttackTargetHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("AttackTargetHighlightMeshes"));
	AttackTargetHighlightMeshes->SetupAttachment(RootComponent);
	AttackTargetHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttackTargetHighlightMeshes->SetCastShadow(false);

	BoardTargetOtherHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoardTargetOtherHighlightMeshes"));
	BoardTargetOtherHighlightMeshes->SetupAttachment(RootComponent);
	BoardTargetOtherHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardTargetOtherHighlightMeshes->SetCastShadow(false);

	BoardTargetAoEHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoardTargetAoEHighlightMeshes"));
	BoardTargetAoEHighlightMeshes->SetupAttachment(RootComponent);
	BoardTargetAoEHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardTargetAoEHighlightMeshes->SetCastShadow(false);

	AbilityBoardTargetOtherHighlightMeshes =
		CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("AbilityBoardTargetOtherHighlightMeshes"));
	AbilityBoardTargetOtherHighlightMeshes->SetupAttachment(RootComponent);
	AbilityBoardTargetOtherHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AbilityBoardTargetOtherHighlightMeshes->SetCastShadow(false);

	AbilityBoardTargetEnemyHighlightMeshes =
		CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("AbilityBoardTargetEnemyHighlightMeshes"));
	AbilityBoardTargetEnemyHighlightMeshes->SetupAttachment(RootComponent);
	AbilityBoardTargetEnemyHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AbilityBoardTargetEnemyHighlightMeshes->SetCastShadow(false);

	ResolveFlashHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ResolveFlashHighlightMeshes"));
	ResolveFlashHighlightMeshes->SetupAttachment(RootComponent);
	ResolveFlashHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ResolveFlashHighlightMeshes->SetCastShadow(false);

	AbilityTargetLinkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AbilityTargetLinkMesh"));
	AbilityTargetLinkMesh->SetupAttachment(RootComponent);
	AbilityTargetLinkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AbilityTargetLinkMesh->SetCastShadow(false);
	AbilityTargetLinkMesh->SetVisibility(false);

	DeployHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DeployHighlightMeshes"));
	DeployHighlightMeshes->SetupAttachment(RootComponent);
	DeployHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeployHighlightMeshes->SetCastShadow(false);

	SelectionHighlightMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SelectionHighlightMeshes"));
	SelectionHighlightMeshes->SetupAttachment(RootComponent);
	SelectionHighlightMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectionHighlightMeshes->SetCastShadow(false);

	ObstacleMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ObstacleMeshes"));
	ObstacleMeshes->SetupAttachment(RootComponent);
	ObstacleMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ObstacleMeshes->SetCastShadow(true);

	RoadMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RoadMeshes"));
	RoadMeshes->SetupAttachment(RootComponent);
	RoadMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoadMeshes->SetCastShadow(false);

	RoughTerrainMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RoughTerrainMeshes"));
	RoughTerrainMeshes->SetupAttachment(RootComponent);
	RoughTerrainMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoughTerrainMeshes->SetCastShadow(false);

	DamagingTerrainMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DamagingTerrainMeshes"));
	DamagingTerrainMeshes->SetupAttachment(RootComponent);
	DamagingTerrainMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DamagingTerrainMeshes->SetCastShadow(false);

	VoidTerrainMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VoidTerrainMeshes"));
	VoidTerrainMeshes->SetupAttachment(RootComponent);
	VoidTerrainMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VoidTerrainMeshes->SetCastShadow(false);

	AetherTerrainMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("AetherTerrainMeshes"));
	AetherTerrainMeshes->SetupAttachment(RootComponent);
	AetherTerrainMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AetherTerrainMeshes->SetCastShadow(false);

	ScannerTerrainMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ScannerTerrainMeshes"));
	ScannerTerrainMeshes->SetupAttachment(RootComponent);
	ScannerTerrainMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ScannerTerrainMeshes->SetCastShadow(false);

	OmniEnergyTerrainMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("OmniEnergyTerrainMeshes"));
	OmniEnergyTerrainMeshes->SetupAttachment(RootComponent);
	OmniEnergyTerrainMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OmniEnergyTerrainMeshes->SetCastShadow(false);

	const auto SetOverlaySortPriority = [](UInstancedStaticMeshComponent* Comp, const int32 Priority) {
		if (Comp) {
			Comp->SetTranslucentSortPriority(Priority);
		}
	};
	SetOverlaySortPriority(RoadMeshes, TerrainOverlaySortPriority);
	SetOverlaySortPriority(RoughTerrainMeshes, TerrainOverlaySortPriority);
	SetOverlaySortPriority(DamagingTerrainMeshes, TerrainOverlaySortPriority);
	SetOverlaySortPriority(VoidTerrainMeshes, TerrainOverlaySortPriority);
	SetOverlaySortPriority(AetherTerrainMeshes, ObjectiveOverlaySortPriority);
	SetOverlaySortPriority(ScannerTerrainMeshes, ObjectiveOverlaySortPriority);
	SetOverlaySortPriority(OmniEnergyTerrainMeshes, ObjectiveOverlaySortPriority);
	SetOverlaySortPriority(MoveReachHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(PendingMoveDestinationHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(PendingMoveOriginHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(AttackTargetHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(BoardTargetOtherHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(BoardTargetAoEHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(AbilityBoardTargetOtherHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(AbilityBoardTargetEnemyHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(ResolveFlashHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(DeployHighlightMeshes, HighlightOverlaySortPriority);
	SetOverlaySortPriority(SelectionHighlightMeshes, HighlightOverlaySortPriority);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded()) {
		TileMesh = CubeFinder.Object;
		BoardUnderlay->SetStaticMesh(TileMesh);
		GridGroutMeshes->SetStaticMesh(TileMesh);
		CellMeshes->SetStaticMesh(TileMesh);
		MoveReachHighlightMeshes->SetStaticMesh(TileMesh);
		PendingMoveDestinationHighlightMeshes->SetStaticMesh(TileMesh);
		PendingMoveOriginHighlightMeshes->SetStaticMesh(TileMesh);
		AttackTargetHighlightMeshes->SetStaticMesh(TileMesh);
		BoardTargetOtherHighlightMeshes->SetStaticMesh(TileMesh);
		BoardTargetAoEHighlightMeshes->SetStaticMesh(TileMesh);
		AbilityBoardTargetOtherHighlightMeshes->SetStaticMesh(TileMesh);
		AbilityBoardTargetEnemyHighlightMeshes->SetStaticMesh(TileMesh);
		ResolveFlashHighlightMeshes->SetStaticMesh(TileMesh);
		DeployHighlightMeshes->SetStaticMesh(TileMesh);
		SelectionHighlightMeshes->SetStaticMesh(TileMesh);
		ObstacleMeshes->SetStaticMesh(TileMesh);
		RoadMeshes->SetStaticMesh(TileMesh);
		RoughTerrainMeshes->SetStaticMesh(TileMesh);
		DamagingTerrainMeshes->SetStaticMesh(TileMesh);
		VoidTerrainMeshes->SetStaticMesh(TileMesh);
		AetherTerrainMeshes->SetStaticMesh(TileMesh);
		ScannerTerrainMeshes->SetStaticMesh(TileMesh);
		OmniEnergyTerrainMeshes->SetStaticMesh(TileMesh);
	}

	static ConstructorHelpers::FObjectFinder<UMaterial> MatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatColFinder(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GridMatFinder(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpriteMatFinder(
		TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"));
	static ConstructorHelpers::FObjectFinder<UFont> DesignFontFinder(
		TEXT("/Game/TacticsData/fonts/LibreBaskervilleBoldDistanceField.LibreBaskervilleBoldDistanceField"));
	(void)FlatColFinder;
	(void)GridMatFinder;
	(void)SpriteMatFinder;
	(void)DesignFontFinder;
	if (MatFinder.Succeeded()) {
		if (UMaterialInstanceDynamic* UnderlayMid = UMaterialInstanceDynamic::Create(MatFinder.Object, this)) {
			UnderlayMid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.07f, 0.07f, 0.09f, 1.f));
			UnderlayMid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.07f, 0.07f, 0.09f, 1.f));
			BoardUnderlay->SetMaterial(0, UnderlayMid);
		}
		MoveReachHighlightMaterial = MatFinder.Object;
		AttackTargetHighlightMaterial = MatFinder.Object;
		BoardTargetOtherHighlightMaterial = MatFinder.Object;
		BoardTargetAoEHighlightMaterial = MatFinder.Object;
		DeployHighlightMaterial = MatFinder.Object;
		if (AbilityTargetLinkMesh) {
			AbilityTargetLinkMesh->SetStaticMesh(TileMesh);
		}
		ObstacleMaterial = MatFinder.Object;
		RoadMaterial = MatFinder.Object;
		RoughTerrainMaterial = MatFinder.Object;
		DamagingTerrainMaterial = MatFinder.Object;
		VoidTerrainMaterial = MatFinder.Object;
		AetherTerrainMaterial = MatFinder.Object;
		ScannerTerrainMaterial = MatFinder.Object;
		OmniEnergyTerrainMaterial = MatFinder.Object;
	}
}

void ATactics3DWorldBoardActor::BeginPlay()
{
	Super::BeginPlay();

	if (UGameInstance* GI = GetGameInstance()) {
		MatchSubsystem = GI->GetSubsystem<UTacticsMatchSubsystem>();
	}

	// Battle-stage backdrop: owned by ABattleVisualizationActor, no live board hooks.
	if (GetOwner() && GetOwner()->IsA(ABattleVisualizationActor::StaticClass())) {
		bBattleBackgroundMode = true;
		RefreshBattleBackgroundSnapshot();
		return;
	}

	ApplyGridGroutMaterial();

	if (MatchSubsystem.IsValid()) {
		BoardChangedHandle = MatchSubsystem->OnBoardChanged.AddUObject(this, &ATactics3DWorldBoardActor::OnMatchBoardChanged);
		TargetPreviewChangedHandle =
			MatchSubsystem->OnTargetPreviewChanged.AddUObject(this, &ATactics3DWorldBoardActor::OnTargetPreviewChanged);
		AbilityDamagePopupsChangedHandle = MatchSubsystem->OnAbilityDamagePopupsChanged.AddUObject(
			this, &ATactics3DWorldBoardActor::OnAbilityDamagePopupsChanged);
		MatchSubsystem->SetUses3DBoardTiles(true);
	}
}

void ATactics3DWorldBoardActor::ApplyGridGroutMaterial()
{
	if (!GridGroutMeshes) {
		return;
	}
	if (!GridGroutMaterialInstance) {
		if (UMaterial* FlatMat = LoadBoardFlatMaterial()) {
			GridGroutMaterialInstance = UMaterialInstanceDynamic::Create(FlatMat, this);
			Board3D_TintFlatMaterial(GridGroutMaterialInstance, k3DBoard_GroutBrown);
		} else if (UMaterial* FallbackMat = LoadObject<UMaterial>(
					   nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"))) {
			GridGroutMaterialInstance = UMaterialInstanceDynamic::Create(FallbackMat, this);
			GridGroutMaterialInstance->SetVectorParameterValue(TEXT("Color"), k3DBoard_GroutBrown);
			GridGroutMaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), k3DBoard_GroutBrown);
		}
	}
	if (GridGroutMaterialInstance) {
		GridGroutMeshes->SetMaterial(0, GridGroutMaterialInstance);
	}
}

void ATactics3DWorldBoardActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearUnitActors();
	UnregisterBoardDelegate();
	if (!bBattleBackgroundMode && MatchSubsystem.IsValid()) {
		MatchSubsystem->SetUses3DBoardTiles(false);
	}
	Super::EndPlay(EndPlayReason);
}

void ATactics3DWorldBoardActor::RefreshBattleBackgroundSnapshot()
{
	RebuildVisuals();
}

void ATactics3DWorldBoardActor::UnregisterBoardDelegate()
{
	if (MatchSubsystem.IsValid()) {
		if (BoardChangedHandle.IsValid()) {
			MatchSubsystem->OnBoardChanged.Remove(BoardChangedHandle);
			BoardChangedHandle.Reset();
		}
		if (TargetPreviewChangedHandle.IsValid()) {
			MatchSubsystem->OnTargetPreviewChanged.Remove(TargetPreviewChangedHandle);
			TargetPreviewChangedHandle.Reset();
		}
		if (AbilityDamagePopupsChangedHandle.IsValid()) {
			MatchSubsystem->OnAbilityDamagePopupsChanged.Remove(AbilityDamagePopupsChangedHandle);
			AbilityDamagePopupsChangedHandle.Reset();
		}
	}
}

FVector ATactics3DWorldBoardActor::GetBoardWorldCenter() const
{
	return GetActorTransform().TransformPosition(ViewMapping.BoundsCenterLocal(SpanW, SpanH));
}

float ATactics3DWorldBoardActor::GetSuggestedTopDownArmLength() const
{
	if (SpanW < 1 || SpanH < 1) {
		return 2200.f;
	}
	const float w = static_cast<float>(SpanW) * GetVisualCellWorldSize();
	const float h = static_cast<float>(SpanH) * GetVisualCellWorldSize();
	return FMath::Sqrt(w * w + h * h) * 0.72f + 450.f;
}

bool ATactics3DWorldBoardActor::ResolveWorldCellFromHit(const FHitResult& Hit, int& OutWx, int& OutWy) const
{
	if (const ATacticsBoardUnitActor* Unit = Cast<ATacticsBoardUnitActor>(Hit.GetActor())) {
		return Unit->TryGetBoardGridCell(OutWx, OutWy);
	}
	UPrimitiveComponent* const HitComp = Hit.GetComponent();
	if (HitComp != CellMeshes || Hit.Item == INDEX_NONE) {
		return false;
	}
	const int32 I = Hit.Item;
	if (!HitWx.IsValidIndex(I)) {
		return false;
	}
	OutWx = HitWx[I];
	OutWy = HitWy[I];
	return true;
}

void ATactics3DWorldBoardActor::OnMatchBoardChanged()
{
	if (!MatchSubsystem.IsValid()) {
		RebuildVisuals();
		return;
	}
	RefreshVisualsIncremental(MatchSubsystem->ConsumeBoardVisualDirtyMask());
}

void ATactics3DWorldBoardActor::UpdateResolveFlashHighlights()
{
	if (!ResolveFlashHighlightMeshes) {
		return;
	}
	ResolveFlashHighlightMeshes->ClearInstances();
	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady() || !Sub->HasActiveAbilityResolvePresentation()) {
		return;
	}
	if (!ResolveFlashHighlightMeshes->GetStaticMesh()) {
		return;
	}

	FLinearColor FlashColor(0.95f, 0.06f, 0.04f, 1.f);
	float FlashAlpha = 0.f;
	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		float CellAlpha = 0.f;
		float CellScale = 0.f;
		FLinearColor CellColor;
		if (!Sub->TryGetAbilityResolvePresentationAtWorld(Wx, Wy, CellAlpha, CellScale, CellColor)) {
			continue;
		}
		if (CellAlpha > FlashAlpha) {
			FlashAlpha = CellAlpha;
			FlashColor = CellColor;
		}
		if (CellScale <= 0.01f) {
			continue;
		}
		const float PulseScale = FMath::Clamp(CellScale, 0.f, 1.5f);
		const float ZThickness = 0.09f * PulseScale;
		ResolveFlashHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, ZThickness, 0.f, PulseScale));
	}

	if (ResolveFlashHighlightMeshes->GetInstanceCount() > 0) {
		if (UMaterialInterface* BaseMat = BoardTargetAoEHighlightMaterial) {
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
			if (Mid) {
				const FLinearColor Tint(FlashColor.R, FlashColor.G, FlashColor.B, FMath::Clamp(FlashAlpha, 0.15f, 1.f));
				Mid->SetVectorParameterValue(TEXT("Color"), Tint);
				Mid->SetVectorParameterValue(TEXT("BaseColor"), Tint);
				ResolveFlashHighlightMeshes->SetMaterial(0, Mid);
			}
		}
	}
}
void ATactics3DWorldBoardActor::PruneInvalidAbilityDamagePopupLabels()
{
	AbilityDamagePopupLabels.RemoveAll([](const TObjectPtr<UTextRenderComponent>& LabelPtr) {
		return !IsValid(LabelPtr.Get());
	});
}

void ATactics3DWorldBoardActor::DetachAbilityPopupsFromUnitActor(ATacticsBoardUnitActor* UnitActor)
{
	if (!IsValid(UnitActor)) {
		return;
	}
	USceneComponent* UnitRoot = UnitActor->GetRootComponent();
	if (!UnitRoot) {
		return;
	}
	for (TObjectPtr<UTextRenderComponent>& LabelPtr : AbilityDamagePopupLabels) {
		UTextRenderComponent* Label = LabelPtr.Get();
		if (!IsValid(Label) || Label->GetAttachParent() != UnitRoot) {
			continue;
		}
		Label->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Label->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
	}
}

void ATactics3DWorldBoardActor::UpdateAbilityDamagePopups()
{
	PruneInvalidAbilityDamagePopupLabels();
	for (TObjectPtr<UTextRenderComponent>& LabelPtr : AbilityDamagePopupLabels) {
		UTextRenderComponent* Label = LabelPtr.Get();
		if (IsValid(Label)) {
			Label->SetVisibility(false);
			Label->SetHiddenInGame(true);
		}
	}
	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		return;
	}
	const TArray<FTacticsAbilityDamagePopup>& Popups = Sub->GetActiveAbilityDamagePopups();
	if (Popups.IsEmpty()) {
		return;
	}
	UFont* PopupFont = TacticsCardText::AbilityDamagePopupWorldFont();
	const double Now = FPlatformTime::Seconds();
	TArray<FTacticsBoardUnitPose> UnitPoses;
	Sub->GatherBoardUnitPoses(UnitPoses);
	TSet<FString> LiveEntityIds;
	LiveEntityIds.Reserve(UnitPoses.Num());
	for (const FTacticsBoardUnitPose& Pose : UnitPoses) {
		if (!Pose.EntityId.IsEmpty()) {
			LiveEntityIds.Add(Pose.EntityId);
		}
	}
	for (int32 Index = 0; Index < Popups.Num(); ++Index) {
		const FTacticsAbilityDamagePopup& Popup = Popups[Index];
		float Alpha = 0.f;
		float Scale = 0.f;
		float OffsetY = 0.f;
		Popup.GetVisual(Now, Alpha, Scale, OffsetY);
		if (Alpha <= 0.01f) {
			continue;
		}
		while (AbilityDamagePopupLabels.Num() <= Index) {
			UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
			Label->SetupAttachment(RootComponent);
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetVerticalAlignment(EVRTA_TextCenter);
			Label->SetTranslucentSortPriority(1200);
			Label->SetCastShadow(false);
			if (PopupFont) {
				Label->SetFont(PopupFont);
			}
			Label->RegisterComponent();
			AbilityDamagePopupLabels.Add(Label);
		}
		UTextRenderComponent* Label = AbilityDamagePopupLabels[Index].Get();
		if (!IsValid(Label)) {
			AbilityDamagePopupLabels[Index] = nullptr;
			continue;
		}
		if (PopupFont) {
			Label->SetFont(PopupFont);
		}
		const FString Text = Popup.GetDisplayText();
		Label->SetText(FText::FromString(Text));
		Label->SetTextRenderColor(Popup.GetDisplayColor(Alpha).ToFColor(true));
		const float BaseWorldSize = Popup.IsLabelOnly() ? 56.f : 72.f;
		const float LengthScale = FMath::Clamp(1.f - static_cast<float>(FMath::Max(0, Text.Len() - 8)) * 0.04f, 0.72f, 1.f);
		Label->SetWorldSize(FMath::Max(40.f, BaseWorldSize * Scale * LengthScale));
		float GridCenterX = static_cast<float>(Popup.Cell.X);
		float GridCenterY = static_cast<float>(Popup.Cell.Y);
		int32 FootprintSpan = 1;
		if (!Popup.EntityId.IsEmpty()) {
			for (const FTacticsBoardUnitPose& Pose : UnitPoses) {
				if (Pose.EntityId == Popup.EntityId) {
					GridCenterX = Pose.GridCenterX;
					GridCenterY = Pose.GridCenterY;
					FootprintSpan = FMath::Max(Pose.FootprintSpanX, Pose.FootprintSpanY);
					break;
				}
			}
		}
		const float LabelZ = GetTileFloorTopZ() + 260.f * static_cast<float>(FMath::Max(1, FootprintSpan)) - OffsetY * 0.6f;
		ATacticsBoardUnitActor* UnitActor = nullptr;
		const bool bEntityOnBoard = Popup.EntityId.IsEmpty() || LiveEntityIds.Contains(Popup.EntityId);
		if (bEntityOnBoard && !Popup.EntityId.IsEmpty()) {
			if (const TObjectPtr<ATacticsBoardUnitActor>* UnitPtr = UnitActors.Find(Popup.EntityId)) {
				UnitActor = UnitPtr->Get();
			}
		}
		if (IsValid(UnitActor) && bEntityOnBoard) {
			if (Label->GetAttachParent() != UnitActor->GetRootComponent()) {
				Label->AttachToComponent(UnitActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			}
			const float UnitLabelZ = 260.f * static_cast<float>(FMath::Max(1, FootprintSpan));
			Label->SetRelativeLocation(FVector(-OffsetY * 0.6f, 0.f, UnitLabelZ));
		} else {
			if (Label->GetAttachParent() != RootComponent) {
				Label->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
			}
			const FVector Loc = ViewMapping.GridCentroidToLocal(GridCenterX, GridCenterY, LabelZ);
			Label->SetRelativeLocation(Loc);
		}
		Label->SetRelativeRotation(FRotator(90.f, 180.f, 0.f));
		Label->SetHiddenInGame(false);
		Label->SetVisibility(true);
		Label->MarkRenderStateDirty();
	}
}

void ATactics3DWorldBoardActor::OnAbilityDamagePopupsChanged()
{
	UpdateAbilityDamagePopups();
}

void ATactics3DWorldBoardActor::RefreshVisualsIncremental(const uint32 DirtyMask)
{
	using namespace tactics;
	const uint32 kUnits = 1u << 0;
	const uint32 kHighlights = 1u << 1;
	const uint32 kTerrain = 1u << 2;
	const uint32 kLayout = 1u << 3;
	const bool bWantFlip = MatchSubsystem.IsValid() && MatchSubsystem->ShouldFlipBoardPresentation();
	uint32 Mask = DirtyMask;
	if (bWantFlip != ViewMapping.bFlipBoard) {
		Mask |= kLayout;
	}
	if (Mask & kLayout) {
		RebuildVisuals();
		return;
	}
	if (!MatchSubsystem.IsValid() || !MatchSubsystem->IsMatchReady()) {
		ClearUnitActors();
		return;
	}
	if (DirtyMask & kTerrain) {
		SyncTerrainMeshes();
		SyncObstacleMeshes();
	}
	if (DirtyMask & kUnits) {
		SyncUnitActors();
	}
	if (DirtyMask & kHighlights) {
		RefreshTargetingHighlightsOnly();
	}
}


void ATactics3DWorldBoardActor::OnTargetPreviewChanged()
{
	RefreshTargetingHighlightsOnly();
}

void ATactics3DWorldBoardActor::RefreshTargetingHighlightsOnly()
{
	if (!MatchSubsystem.IsValid() || !MatchSubsystem->IsMatchReady()) {
		return;
	}
	UpdateMoveReachHighlights();
	UpdatePendingMoveHighlights();
	UpdateAttackTargetHighlights();
	UpdateBoardTargetHighlights();
	UpdateBoardTargetAoEHighlights();
	UpdateAbilityBoardTargetHighlights();
	UpdateResolveFlashHighlights();
	UpdateAbilityDamagePopups();
	UpdateAbilityTargetLink();
	UpdateDeployHighlights();
	UpdateSelectionHighlights();
}

FVector ATactics3DWorldBoardActor::GridCentroidToWorld(float GridCenterX, float GridCenterY) const
{
	return GetActorTransform().TransformPosition(ViewMapping.GridCentroidToLocal(GridCenterX, GridCenterY, 0.f));
}

FVector ATactics3DWorldBoardActor::GetUnitHoverAnchorWorldPosition(const float GridCenterX, const float GridCenterY,
	const int32 FootprintSpanMax) const
{
	const float Z = GetUnitArtFloorZ();
	return GetActorTransform().TransformPosition(ViewMapping.GridCentroidToLocal(GridCenterX, GridCenterY, Z));
}

float ATactics3DWorldBoardActor::GetUnitArtExtentWorldZ(const int32 FootprintSpanMax) const
{
	return GetVisualCellWorldSize() * static_cast<float>(FMath::Max(1, FootprintSpanMax)) * 0.98f;
}

void ATactics3DWorldBoardActor::ClearUnitActors()
{
	for (auto It = UnitActors.CreateIterator(); It; ++It) {
		if (ATacticsBoardUnitActor* a = It.Value().Get()) {
			DetachAbilityPopupsFromUnitActor(a);
			a->Destroy();
		}
		It.RemoveCurrent();
	}
	PruneInvalidAbilityDamagePopupLabels();
}

/** Spawns, moves, or destroys token actors so they match the rules board. */
void ATactics3DWorldBoardActor::SyncUnitActors()
{
	if (!MatchSubsystem.IsValid() || !MatchSubsystem->IsMatchReady()) {
		ClearUnitActors();
		return;
	}

	TArray<FTacticsBoardUnitPose> poses;
	MatchSubsystem->GatherBoardUnitPoses(poses);

	TSet<FString> keep;
	keep.Reserve(poses.Num());
	for (const FTacticsBoardUnitPose& p : poses) {
		keep.Add(p.EntityId);
	}

	TArray<FString> removeIds;
	for (const auto& kv : UnitActors) {
		if (!keep.Contains(kv.Key)) {
			removeIds.Add(kv.Key);
		}
	}
	for (const FString& id : removeIds) {
		if (ATacticsBoardUnitActor* u = UnitActors.FindRef(id)) {
			DetachAbilityPopupsFromUnitActor(u);
			u->Destroy();
		}
		UnitActors.Remove(id);
	}
	PruneInvalidAbilityDamagePopupLabels();

	UWorld* W = GetWorld();
	if (!W) {
		return;
	}

	const float zFoot = GetUnitArtFloorZ();

	for (const FTacticsBoardUnitPose& p : poses) {
		if (p.EntityId.IsEmpty()) {
			continue;
		}
		if (p.ArtId.IsEmpty() && !p.bIsBase) {
			UE_LOG(LogTemp, Warning, TEXT("Tactics3D board unit '%s' has no card art id"), *p.EntityId);
		}
		const FVector loc = GridCentroidToWorld(p.GridCenterX, p.GridCenterY) + FVector(0.f, 0.f, zFoot);
		if (TObjectPtr<ATacticsBoardUnitActor>* existing = UnitActors.Find(p.EntityId)) {
			ATacticsBoardUnitActor* actor = existing->Get();
			if (actor) {
				actor->ApplyWorldPose(loc, 1.f, p.GridCenterX, p.GridCenterY);
				actor->SetCardArt(p.ArtId);
				actor->ConfigureAsBase(p.bIsBase, p.FootprintSpanX, p.FootprintSpanY);
				actor->ConfigureFootprintVisual(p.FootprintSpanX, p.FootprintSpanY, GetFloorFootprintScale());
				actor->SetBattleBackgroundPresentation(bBattleBackgroundMode);
				const int32 badgeRank = bBattleBackgroundMode
					? 0
					: (MatchSubsystem->IsTurnOrderViewEnabled() ? p.TurnOrderRank : 0);
				actor->SetTurnOrderBadge(badgeRank, p.bIsBase, p.bHasTaunt);
				actor->ApplyAbilityCastFlash(p.bAbilityCastFlashSuccess, p.AbilityCastFlashAlpha);
				continue;
			}
			UnitActors.Remove(p.EntityId);
		}
		FActorSpawnParameters sp;
		sp.Owner = this;
		sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATacticsBoardUnitActor* spawned =
			W->SpawnActor<ATacticsBoardUnitActor>(ATacticsBoardUnitActor::StaticClass(), loc, FRotator::ZeroRotator, sp);
		if (spawned) {
			spawned->ApplyWorldPose(loc, 1.f, p.GridCenterX, p.GridCenterY);
			spawned->SetCardArt(p.ArtId);
			spawned->ConfigureAsBase(p.bIsBase, p.FootprintSpanX, p.FootprintSpanY);
			spawned->ConfigureFootprintVisual(p.FootprintSpanX, p.FootprintSpanY, GetFloorFootprintScale());
			spawned->SetBattleBackgroundPresentation(bBattleBackgroundMode);
			const int32 badgeRank = bBattleBackgroundMode
				? 0
				: (MatchSubsystem->IsTurnOrderViewEnabled() ? p.TurnOrderRank : 0);
			spawned->SetTurnOrderBadge(badgeRank, p.bIsBase, p.bHasTaunt);
			spawned->ApplyAbilityCastFlash(p.bAbilityCastFlashSuccess, p.AbilityCastFlashAlpha);
			UnitActors.Add(p.EntityId, spawned);
		}
	}
}

float ATactics3DWorldBoardActor::GetTileGroutTopZ() const
{
	return TileGroutCenterZ + 50.f * TileGroutThicknessScale;
}

float ATactics3DWorldBoardActor::GetTileFloorCenterZ() const
{
	return GetTileGroutTopZ() + 50.f * TileFloorThicknessScale;
}

float ATactics3DWorldBoardActor::GetTileFloorTopZ() const
{
	return GetTileGroutTopZ() + 100.f * TileFloorThicknessScale;
}

float ATactics3DWorldBoardActor::GetUnitArtFloorZ() const
{
	return GetTileFloorTopZ() + UnitArtFloorLiftZ;
}

float ATactics3DWorldBoardActor::GetTerrainOverlayCenterZ(const float ZThicknessScale) const
{
	const float HalfThickness = 50.f * ZThicknessScale;
	// Sit on the tile surface; unit card art is lifted by UnitArtFloorLiftZ above this.
	constexpr float kTerrainOverlayTopAboveFloor = 0.02f;
	const float TopZ = GetTileFloorTopZ() + kTerrainOverlayTopAboveFloor;
	return TopZ - HalfThickness;
}

float ATactics3DWorldBoardActor::GetObjectiveTerrainOverlayCenterZ(const float ZThicknessScale) const
{
	const float HalfThickness = 50.f * ZThicknessScale;
	// Above move-reach slab tops, but stay below the unit card-art floor plane.
	constexpr float kHighlightTopAboveFloor = UnitArtFloorLiftZ - HighlightUnderArtGapZ;
	constexpr float kObjectiveGapAboveHighlights = 0.02f;
	constexpr float kObjectiveGapBelowUnitArt = 0.02f;
	const float TopZ = GetTileFloorTopZ() + FMath::Min(
		UnitArtFloorLiftZ - kObjectiveGapBelowUnitArt,
		kHighlightTopAboveFloor + kObjectiveGapAboveHighlights);
	return TopZ - HalfThickness;
}

float ATactics3DWorldBoardActor::GetHighlightSlabCenterZ(const float ZThicknessScale, const float LayerBias) const
{
	const float HalfThickness = 50.f * ZThicknessScale;
	const float TopZ = GetTileFloorTopZ() + UnitArtFloorLiftZ - HighlightUnderArtGapZ - LayerBias;
	return TopZ - HalfThickness;
}

const UTacticsBoardVisualSettingsSubsystem* ATactics3DWorldBoardActor::GetBoardVisualSettings() const
{
	if (const UWorld* World = GetWorld()) {
		if (const UGameInstance* GI = World->GetGameInstance()) {
			return GI->GetSubsystem<UTacticsBoardVisualSettingsSubsystem>();
		}
	}
	return nullptr;
}

float ATactics3DWorldBoardActor::GetVisualCellWorldSize() const
{
	if (const UTacticsBoardVisualSettingsSubsystem* Settings = GetBoardVisualSettings()) {
		return Settings->GetVisualCellWorldSize();
	}
	return UTacticsBoardVisualSettingsSubsystem::BaseCellWorldSize
		* UTacticsBoardVisualSettingsSubsystem::DefaultBoardCellSpacingScale;
}

float ATactics3DWorldBoardActor::GetGroutFootprintScale() const
{
	if (const UTacticsBoardVisualSettingsSubsystem* Settings = GetBoardVisualSettings()) {
		return Settings->GetGroutFootprintScale();
	}
	return UTacticsBoardVisualSettingsSubsystem::DefaultBoardCellSpacingScale;
}

float ATactics3DWorldBoardActor::GetFloorFootprintScale() const
{
	if (const UTacticsBoardVisualSettingsSubsystem* Settings = GetBoardVisualSettings()) {
		return Settings->GetFloorFootprintScale();
	}
	return UTacticsBoardVisualSettingsSubsystem::DefaultBoardCellSpacingScale
		* FMath::Max(0.55f, 1.f - 2.f * UTacticsBoardVisualSettingsSubsystem::GroutSeamFraction);
}

FVector ATactics3DWorldBoardActor::GetDamagePopupLocalPosition(
	const float GridCenterX, const float GridCenterY, const float AnimatedOffsetY) const
{
	FVector Loc = ViewMapping.GridCentroidToLocal(GridCenterX, GridCenterY, GetUnitArtFloorZ() + DamagePopupArtLiftZ);
	Loc.X += -AnimatedOffsetY * DamagePopupFloatWorldScale;
	return Loc;
}

FTransform ATactics3DWorldBoardActor::MakeBoardHighlightTransform(
	const int Wx, const int Wy, const float ZThicknessScale, const float LayerBias, const float FloorFootprintMul) const
{
	const FVector Loc = ViewMapping.GridCellCenterToLocal(Wx, Wy, GetHighlightSlabCenterZ(ZThicknessScale, LayerBias));
	const float Foot = GetFloorFootprintScale() * FloorFootprintMul;
	return FTransform(FRotator::ZeroRotator, Loc, FVector(Foot, Foot, ZThicknessScale));
}

void ATactics3DWorldBoardActor::SyncBoardUnderlay()
{
	if (!BoardUnderlay || !BoardUnderlay->GetStaticMesh()) {
		return;
	}
	if (SpanW < 1 || SpanH < 1) {
		BoardUnderlay->SetVisibility(false);
		return;
	}
	const float S = GetVisualCellWorldSize();
	const FVector Center = ViewMapping.BoundsCenterLocal(SpanW, SpanH);
	// Deep void slab only - visible grout is per-cell GridGroutMeshes above this.
	BoardUnderlay->SetRelativeLocation(FVector(Center.X, Center.Y, -40.f));
	const float Pad = 1.04f;
	const float ScaleX = (static_cast<float>(SpanH) * S * Pad) / 100.f;
	const float ScaleY = (static_cast<float>(SpanW) * S * Pad) / 100.f;
	BoardUnderlay->SetRelativeScale3D(FVector(ScaleX, ScaleY, 0.05f));
	BoardUnderlay->SetVisibility(true);
}

void ATactics3DWorldBoardActor::SyncObstacleMeshes()
{
	if (!ObstacleMeshes) {
		return;
	}
	ObstacleMeshes->ClearInstances();

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady() || !ObstacleMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = ObstacleMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.42f, 0.38f, 0.34f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.42f, 0.38f, 0.34f, 1.f));
			ObstacleMeshes->SetMaterial(0, Mid);
		}
	}

	TArray<FIntPoint> cells;
	Sub->GatherBoardObstacleCells(cells);
	const float S = GetVisualCellWorldSize();
	const float zCenter = GetTileFloorTopZ() + S * 0.11f;
	const float zScale = 0.52f;
	for (const FIntPoint& c : cells) {
		const FVector Loc = ViewMapping.GridCellCenterToLocal(c.X, c.Y, zCenter);
		const FTransform Xf(FRotator::ZeroRotator, Loc, FVector(0.88f, 0.88f, zScale));
		ObstacleMeshes->AddInstance(Xf);
	}
}

void ATactics3DWorldBoardActor::SyncTerrainMeshes()
{
	if (RoadMeshes) {
		RoadMeshes->ClearInstances();
	}
	if (RoughTerrainMeshes) {
		RoughTerrainMeshes->ClearInstances();
	}
	if (DamagingTerrainMeshes) {
		DamagingTerrainMeshes->ClearInstances();
	}
	if (VoidTerrainMeshes) {
		VoidTerrainMeshes->ClearInstances();
	}
	if (AetherTerrainMeshes) {
		AetherTerrainMeshes->ClearInstances();
	}
	if (ScannerTerrainMeshes) {
		ScannerTerrainMeshes->ClearInstances();
	}
	if (OmniEnergyTerrainMeshes) {
		OmniEnergyTerrainMeshes->ClearInstances();
	}

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		return;
	}
	if (!RoadMeshes || !RoughTerrainMeshes || !DamagingTerrainMeshes || !VoidTerrainMeshes || !AetherTerrainMeshes
		|| !ScannerTerrainMeshes || !OmniEnergyTerrainMeshes
		|| !RoadMeshes->GetStaticMesh() || !RoughTerrainMeshes->GetStaticMesh() || !DamagingTerrainMeshes->GetStaticMesh()
		|| !VoidTerrainMeshes->GetStaticMesh() || !AetherTerrainMeshes->GetStaticMesh()
		|| !ScannerTerrainMeshes->GetStaticMesh() || !OmniEnergyTerrainMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = RoadMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.58f, 0.42f, 0.22f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.58f, 0.42f, 0.22f, 1.f));
			RoadMeshes->SetMaterial(0, Mid);
		}
	}
	if (UMaterialInterface* BaseMat = RoughTerrainMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.18f, 0.36f, 0.16f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.18f, 0.36f, 0.16f, 1.f));
			RoughTerrainMeshes->SetMaterial(0, Mid);
		}
	}
	if (UMaterialInterface* BaseMat = DamagingTerrainMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.72f, 0.12f, 0.04f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.72f, 0.12f, 0.04f, 1.f));
			DamagingTerrainMeshes->SetMaterial(0, Mid);
		}
	}
	if (UMaterialInterface* BaseMat = VoidTerrainMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.01f, 0.01f, 0.04f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.01f, 0.01f, 0.04f, 1.f));
			VoidTerrainMeshes->SetMaterial(0, Mid);
		}
	}
	if (UMaterialInterface* BaseMat = AetherTerrainMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.42f, 0.22f, 0.92f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.42f, 0.22f, 0.92f, 1.f));
			AetherTerrainMeshes->SetMaterial(0, Mid);
		}
	}
	if (UMaterialInterface* BaseMat = ScannerTerrainMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.72f, 0.88f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.12f, 0.72f, 0.88f, 1.f));
			ScannerTerrainMeshes->SetMaterial(0, Mid);
		}
	}
	if (UMaterialInterface* BaseMat = OmniEnergyTerrainMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.92f, 0.78f, 0.18f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.92f, 0.78f, 0.18f, 1.f));
			OmniEnergyTerrainMeshes->SetMaterial(0, Mid);
		}
	}

	TArray<FTacticsBoardTerrainCell> cells;
	Sub->GatherBoardTerrainCells(cells);
	constexpr float TerrainOverlayZScale = 0.035f;
	const float TerrainOverlayCenterZ = GetTerrainOverlayCenterZ(TerrainOverlayZScale);
	constexpr float ObjectiveOverlayZScale = 0.04f;
	const float ObjectiveOverlayCenterZ = GetObjectiveTerrainOverlayCenterZ(ObjectiveOverlayZScale);
	for (const FTacticsBoardTerrainCell& c : cells) {
		if (c.TerrainName.Equals(TEXT("aether"), ESearchCase::IgnoreCase)) {
			const FVector Loc = ViewMapping.GridCellCenterToLocal(c.Cell.X, c.Cell.Y, ObjectiveOverlayCenterZ);
			const FTransform Xf(FRotator::ZeroRotator, Loc, FVector(0.96f, 0.96f, ObjectiveOverlayZScale));
			AetherTerrainMeshes->AddInstance(Xf);
		} else if (c.TerrainName.Equals(TEXT("scanner"), ESearchCase::IgnoreCase)) {
			const FVector Loc = ViewMapping.GridCellCenterToLocal(c.Cell.X, c.Cell.Y, ObjectiveOverlayCenterZ);
			const FTransform Xf(FRotator::ZeroRotator, Loc, FVector(0.96f, 0.96f, ObjectiveOverlayZScale));
			ScannerTerrainMeshes->AddInstance(Xf);
		} else if (c.TerrainName.Equals(TEXT("omni_energy"), ESearchCase::IgnoreCase)) {
			const FVector Loc = ViewMapping.GridCellCenterToLocal(c.Cell.X, c.Cell.Y, ObjectiveOverlayCenterZ);
			const FTransform Xf(FRotator::ZeroRotator, Loc, FVector(0.96f, 0.96f, ObjectiveOverlayZScale));
			OmniEnergyTerrainMeshes->AddInstance(Xf);
		} else {
			const FVector Loc = ViewMapping.GridCellCenterToLocal(c.Cell.X, c.Cell.Y, TerrainOverlayCenterZ);
			const FTransform Xf(FRotator::ZeroRotator, Loc, FVector(0.96f, 0.96f, TerrainOverlayZScale));
			if (c.TerrainName.Equals(TEXT("road"), ESearchCase::IgnoreCase)) {
				RoadMeshes->AddInstance(Xf);
			} else if (c.TerrainName.Equals(TEXT("rough"), ESearchCase::IgnoreCase)) {
				RoughTerrainMeshes->AddInstance(Xf);
			} else if (c.bIsVoid) {
				VoidTerrainMeshes->AddInstance(Xf);
			} else if (c.DamageOnEnter > 0) {
				DamagingTerrainMeshes->AddInstance(Xf);
			}
		}
	}
}

void ATactics3DWorldBoardActor::UpdateMoveReachHighlights()
{
	if (!MoveReachHighlightMeshes) {
		return;
	}
	MoveReachHighlightMeshes->ClearInstances();

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		return;
	}
	if (!MoveReachHighlightMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = MoveReachHighlightMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.15f, 0.88f, 0.42f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.15f, 0.88f, 0.42f, 1.f));
			MoveReachHighlightMeshes->SetMaterial(0, Mid);
		}
	}

	const float S = GetVisualCellWorldSize();
	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		if (!Sub->IsReachableMoveCellAtWorld(Wx, Wy)) {
			continue;
		}
		MoveReachHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.045f));
	}
}

void ATactics3DWorldBoardActor::UpdatePendingMoveHighlights()
{
	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	auto FillLayer = [this, Sub](UInstancedStaticMeshComponent* Meshes, const FLinearColor& Color,
						  TFunctionRef<bool(int, int)> IncludeCell) {
		if (!Meshes) {
			return;
		}
		Meshes->ClearInstances();
		if (!Sub || !Sub->IsMatchReady() || !Meshes->GetStaticMesh()) {
			return;
		}
		if (UMaterialInterface* BaseMat = MoveReachHighlightMaterial) {
			if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this)) {
				Mid->SetVectorParameterValue(TEXT("Color"), Color);
				Mid->SetVectorParameterValue(TEXT("BaseColor"), Color);
				Meshes->SetMaterial(0, Mid);
			}
		}
		const int32 N = HitWx.Num();
		for (int32 i = 0; i < N; ++i) {
			const int Wx = HitWx[i];
			const int Wy = HitWy[i];
			if (!IncludeCell(Wx, Wy)) {
				continue;
			}
			Meshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.05f, 0.f, 0.94f));
		}
	};

	FillLayer(PendingMoveDestinationHighlightMeshes, FLinearColor(0.15f, 0.88f, 0.95f, 1.f),
		[Sub](int Wx, int Wy) { return Sub && Sub->IsPendingMoveDestinationCellAtWorld(Wx, Wy); });
	FillLayer(PendingMoveOriginHighlightMeshes, FLinearColor(0.95f, 0.62f, 0.12f, 1.f),
		[Sub](int Wx, int Wy) { return Sub && Sub->IsPendingMoveOriginCellAtWorld(Wx, Wy); });
}

void ATactics3DWorldBoardActor::UpdateAttackTargetHighlights()
{
	if (!AttackTargetHighlightMeshes) {
		return;
	}
	AttackTargetHighlightMeshes->ClearInstances();

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		return;
	}
	if (!AttackTargetHighlightMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = AttackTargetHighlightMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.96f, 0.08f, 0.04f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.96f, 0.08f, 0.04f, 1.f));
			AttackTargetHighlightMeshes->SetMaterial(0, Mid);
		}
	}

	const float S = GetVisualCellWorldSize();
	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		if (Sub->IsAbilityBoardTargetPreviewActive()) {
			if (!Sub->IsAttackTargetCellAtWorld(Wx, Wy)) {
				continue;
			}
		} else if (!Sub->IsAttackTargetCellAtWorld(Wx, Wy) && !Sub->IsBoardTargetEnemyHighlightAtWorld(Wx, Wy)
			&& !Sub->IsActionQueueHoverTargetAtWorld(Wx, Wy)) {
			continue;
		}
		AttackTargetHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.07f));
	}
}

void ATactics3DWorldBoardActor::UpdateBoardTargetHighlights()
{
	if (!BoardTargetOtherHighlightMeshes) {
		return;
	}
	BoardTargetOtherHighlightMeshes->ClearInstances();

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		return;
	}
	if (!BoardTargetOtherHighlightMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = BoardTargetOtherHighlightMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.42f, 0.95f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.12f, 0.42f, 0.95f, 1.f));
			BoardTargetOtherHighlightMeshes->SetMaterial(0, Mid);
		}
	}

	const float S = GetVisualCellWorldSize();
	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		if (Sub->IsAbilityBoardTargetPreviewActive()) {
			continue;
		}
		if (!Sub->IsBoardTargetOtherHighlightAtWorld(Wx, Wy)) {
			continue;
		}
		BoardTargetOtherHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.075f, 0.f, 0.94f));
	}
}

void ATactics3DWorldBoardActor::UpdateAbilityBoardTargetHighlights()
{
	if (AbilityBoardTargetOtherHighlightMeshes) {
		AbilityBoardTargetOtherHighlightMeshes->ClearInstances();
	}
	if (AbilityBoardTargetEnemyHighlightMeshes) {
		AbilityBoardTargetEnemyHighlightMeshes->ClearInstances();
	}

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady() || !Sub->IsAbilityBoardTargetPreviewActive()) {
		return;
	}
	if (!AbilityBoardTargetOtherHighlightMeshes || !AbilityBoardTargetEnemyHighlightMeshes
		|| !AbilityBoardTargetOtherHighlightMeshes->GetStaticMesh() || !AbilityBoardTargetEnemyHighlightMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = BoardTargetOtherHighlightMaterial) {
		if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this)) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.52f, 0.18f, 0.78f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.52f, 0.18f, 0.78f, 1.f));
			AbilityBoardTargetOtherHighlightMeshes->SetMaterial(0, Mid);
		}
	}
	if (UMaterialInterface* BaseMat = AttackTargetHighlightMaterial) {
		if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this)) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.88f, 0.16f, 0.62f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.88f, 0.16f, 0.62f, 1.f));
			AbilityBoardTargetEnemyHighlightMeshes->SetMaterial(0, Mid);
		}
	}

	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		if (Sub->IsBoardTargetEnemyHighlightAtWorld(Wx, Wy)) {
			AbilityBoardTargetEnemyHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.08f));
		} else if (Sub->IsBoardTargetOtherHighlightAtWorld(Wx, Wy)) {
			AbilityBoardTargetOtherHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.078f, 0.01f, 0.94f));
		}
	}
}

void ATactics3DWorldBoardActor::UpdateAbilityTargetLink()
{
	if (!AbilityTargetLinkMesh) {
		return;
	}
	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady() || !Sub->IsAbilityBoardTargetPreviewActive() || !Sub->HasUnitSelected()) {
		AbilityTargetLinkMesh->SetVisibility(false);
		return;
	}

	int HoverX = 0;
	int HoverY = 0;
	if (!Sub->TryGetPendingCliWorldCell(HoverX, HoverY)) {
		AbilityTargetLinkMesh->SetVisibility(false);
		return;
	}
	if (!Sub->IsBoardTargetOtherHighlightAtWorld(HoverX, HoverY) && !Sub->IsBoardTargetEnemyHighlightAtWorld(HoverX, HoverY)
		&& !Sub->IsBoardTargetAoEHighlightAtWorld(HoverX, HoverY)) {
		AbilityTargetLinkMesh->SetVisibility(false);
		return;
	}

	TArray<FTacticsBoardUnitPose> Poses;
	Sub->GatherBoardUnitPoses(Poses);
	FVector CasterGrid(0.f, 0.f, 0.f);
	bool bFoundCaster = false;
	for (const FTacticsBoardUnitPose& Pose : Poses) {
		if (Sub->IsSelectedAtWorld(FMath::RoundToInt(Pose.GridCenterX), FMath::RoundToInt(Pose.GridCenterY))) {
			CasterGrid = FVector(Pose.GridCenterX, Pose.GridCenterY, 0.f);
			bFoundCaster = true;
			break;
		}
	}
	if (!bFoundCaster) {
		AbilityTargetLinkMesh->SetVisibility(false);
		return;
	}

	const FVector Start = ViewMapping.GridCellCenterToLocal(FMath::RoundToInt(CasterGrid.X), FMath::RoundToInt(CasterGrid.Y),
		GetTileFloorTopZ() + 18.f);
	const FVector End = ViewMapping.GridCellCenterToLocal(HoverX, HoverY, GetTileFloorTopZ() + 18.f);
	const FLinearColor LinkColor{0.72f, 0.28f, 0.88f, 0.75f};
	const FVector Delta = End - Start;
	const float Length = Delta.Size2D();
	if (Length < 1.f) {
		AbilityTargetLinkMesh->SetVisibility(false);
		return;
	}
	const FVector Mid = (Start + End) * 0.5f;
	const FRotator Rot(0.f, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0.f);
	AbilityTargetLinkMesh->SetRelativeLocation(Mid);
	AbilityTargetLinkMesh->SetRelativeRotation(Rot);
	AbilityTargetLinkMesh->SetRelativeScale3D(FVector(Length / 100.f, 0.04f, 0.04f));
	if (UMaterialInterface* BaseMat = BoardTargetOtherHighlightMaterial) {
		if (UMaterialInstanceDynamic* MidMat = UMaterialInstanceDynamic::Create(BaseMat, this)) {
			MidMat->SetVectorParameterValue(TEXT("Color"), LinkColor);
			MidMat->SetVectorParameterValue(TEXT("BaseColor"), LinkColor);
			AbilityTargetLinkMesh->SetMaterial(0, MidMat);
		}
	}
	AbilityTargetLinkMesh->SetVisibility(true);
}

void ATactics3DWorldBoardActor::UpdateBoardTargetAoEHighlights()
{
	if (!BoardTargetAoEHighlightMeshes) {
		return;
	}
	BoardTargetAoEHighlightMeshes->ClearInstances();

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		return;
	}
	if (!BoardTargetAoEHighlightMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = BoardTargetAoEHighlightMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.98f, 0.52f, 0.1f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.98f, 0.52f, 0.1f, 1.f));
			BoardTargetAoEHighlightMeshes->SetMaterial(0, Mid);
		}
	}

	const float S = GetVisualCellWorldSize();
	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		if (!Sub->IsBoardTargetAoEHighlightAtWorld(Wx, Wy) && !Sub->IsActionQueueHoverAoEAtWorld(Wx, Wy)) {
			continue;
		}
		BoardTargetAoEHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.085f));
	}
}

void ATactics3DWorldBoardActor::UpdateDeployHighlights()
{
	if (!DeployHighlightMeshes) {
		return;
	}
	DeployHighlightMeshes->ClearInstances();

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady() || !Sub->IsDeployPreviewArmed()) {
		return;
	}
	if (!DeployHighlightMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = DeployHighlightMaterial) {
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (Mid) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.95f, 0.72f, 0.18f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.95f, 0.72f, 0.18f, 1.f));
			DeployHighlightMeshes->SetMaterial(0, Mid);
		}
	}

	const float S = GetVisualCellWorldSize();
	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		if (!Sub->IsDeployValidCellAtWorld(Wx, Wy)) {
			continue;
		}
		DeployHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.05f));
	}
}

void ATactics3DWorldBoardActor::UpdateSelectionHighlights()
{
	if (!SelectionHighlightMeshes) {
		return;
	}
	SelectionHighlightMeshes->ClearInstances();

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		return;
	}
	const bool bHasSelection = Sub->HasUnitSelected();
	const bool bHasQueueHoverSource = Sub->IsActionQueueHoverActive();
	if (!bHasSelection && !bHasQueueHoverSource) {
		return;
	}
	if (!SelectionHighlightMeshes->GetStaticMesh()) {
		return;
	}

	if (UMaterialInterface* BaseMat = MoveReachHighlightMaterial) {
		if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this)) {
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.95f, 0.75f, 0.15f, 1.f));
			Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.95f, 0.75f, 0.15f, 1.f));
			SelectionHighlightMeshes->SetMaterial(0, Mid);
		}
	}

	const int32 N = HitWx.Num();
	for (int32 i = 0; i < N; ++i) {
		const int Wx = HitWx[i];
		const int Wy = HitWy[i];
		if (!Sub->IsSelectedAtWorld(Wx, Wy) && !Sub->IsActionQueueHoverSourceAtWorld(Wx, Wy)) {
			continue;
		}
		SelectionHighlightMeshes->AddInstance(MakeBoardHighlightTransform(Wx, Wy, 0.055f, 0.02f, 0.98f));
	}
}

void ATactics3DWorldBoardActor::RebuildVisuals()
{
	HitWx.Reset();
	HitWy.Reset();
	if (GridGroutMeshes) {
		GridGroutMeshes->ClearInstances();
	}
	if (CellMeshes) {
		CellMeshes->ClearInstances();
	}
	if (MoveReachHighlightMeshes) {
		MoveReachHighlightMeshes->ClearInstances();
	}
	if (PendingMoveDestinationHighlightMeshes) {
		PendingMoveDestinationHighlightMeshes->ClearInstances();
	}
	if (PendingMoveOriginHighlightMeshes) {
		PendingMoveOriginHighlightMeshes->ClearInstances();
	}
	if (AttackTargetHighlightMeshes) {
		AttackTargetHighlightMeshes->ClearInstances();
	}
	if (BoardTargetOtherHighlightMeshes) {
		BoardTargetOtherHighlightMeshes->ClearInstances();
	}
	if (BoardTargetAoEHighlightMeshes) {
		BoardTargetAoEHighlightMeshes->ClearInstances();
	}
	if (AbilityBoardTargetOtherHighlightMeshes) {
		AbilityBoardTargetOtherHighlightMeshes->ClearInstances();
	}
	if (AbilityBoardTargetEnemyHighlightMeshes) {
		AbilityBoardTargetEnemyHighlightMeshes->ClearInstances();
	}
	if (ResolveFlashHighlightMeshes) {
		ResolveFlashHighlightMeshes->ClearInstances();
	}
	if (AbilityTargetLinkMesh) {
		AbilityTargetLinkMesh->SetVisibility(false);
	}
	if (DeployHighlightMeshes) {
		DeployHighlightMeshes->ClearInstances();
	}
	if (SelectionHighlightMeshes) {
		SelectionHighlightMeshes->ClearInstances();
	}
	if (ObstacleMeshes) {
		ObstacleMeshes->ClearInstances();
	}
	if (RoadMeshes) {
		RoadMeshes->ClearInstances();
	}
	if (RoughTerrainMeshes) {
		RoughTerrainMeshes->ClearInstances();
	}

	UTacticsMatchSubsystem* Sub = MatchSubsystem.Get();
	if (!Sub || !Sub->IsMatchReady()) {
		ClearUnitActors();
		if (BoardUnderlay) {
			BoardUnderlay->SetVisibility(false);
		}
		return;
	}

	int MinX = 0, MinY = 0, W = 0, H = 0;
	if (!Sub->GetMergedBounds(MinX, MinY, W, H) || W < 1 || H < 1) {
		ClearUnitActors();
		if (BoardUnderlay) {
			BoardUnderlay->SetVisibility(false);
		}
		return;
	}

	RefMinX = MinX;
	RefMinY = MinY;
	SpanW = W;
	SpanH = H;
	const bool bWantFlip = Sub->ShouldFlipBoardPresentation();
	if (bWantFlip != ViewMapping.bFlipBoard) {
		ClearUnitActors();
	}
	ViewMapping.CellWorldSize = GetVisualCellWorldSize();
	ViewMapping.MinGridX = MinX;
	ViewMapping.MinGridY = MinY;
	ViewMapping.SpanGridX = W;
	ViewMapping.SpanGridY = H;
	ViewMapping.bFlipBoard = bWantFlip;

	if (!CellMeshes || !CellMeshes->GetStaticMesh()) {
		ClearUnitActors();
		return;
	}

	const int MaxY = MinY + H - 1;
	const FVector GroutScale(GetGroutFootprintScale(), GetGroutFootprintScale(), TileGroutThicknessScale);
	const FVector FloorScale(GetFloorFootprintScale(), GetFloorFootprintScale(), TileFloorThicknessScale);
	const float FloorCenterZ = GetTileFloorCenterZ();

	ApplyGridGroutMaterial();
	SyncBoardUnderlay();

	for (int Cy = 0; Cy < H; ++Cy) {
		for (int Cx = 0; Cx < W; ++Cx) {
			const int WorldX = MinX + Cx;
			const int WorldY = MaxY - Cy;
			if (!Sub->HasBoardCellAtWorld(WorldX, WorldY)) {
				continue;
			}
			if (GridGroutMeshes) {
				const FVector GroutLoc = ViewMapping.GridCellCenterToLocal(WorldX, WorldY, TileGroutCenterZ);
				GridGroutMeshes->AddInstance(FTransform(FRotator::ZeroRotator, GroutLoc, GroutScale));
			}
			const FVector FloorLoc = ViewMapping.GridCellCenterToLocal(WorldX, WorldY, FloorCenterZ);
			const FTransform Xf(FRotator::ZeroRotator, FloorLoc, FloorScale);
			CellMeshes->AddInstance(Xf);
			HitWx.Add(WorldX);
			HitWy.Add(WorldY);
		}
	}

	const int32 TileInstances = CellMeshes ? CellMeshes->GetInstanceCount() : 0;
	UE_LOG(LogTemp, Log, TEXT("Tactics3D board visuals: %d tile instances (expect 80 on duel map)"), TileInstances);

	SyncUnitActors();
	SyncTerrainMeshes();
	SyncObstacleMeshes();
	if (!bBattleBackgroundMode) {
		UpdateMoveReachHighlights();
		UpdatePendingMoveHighlights();
		UpdateAttackTargetHighlights();
		UpdateBoardTargetHighlights();
		UpdateBoardTargetAoEHighlights();
		UpdateAbilityBoardTargetHighlights();
	UpdateResolveFlashHighlights();
	UpdateAbilityDamagePopups();
	UpdateAbilityTargetLink();
		UpdateDeployHighlights();
		UpdateSelectionHighlights();

		if (TileInstances > 0 && HasVisualGridBounds()) {
			if (UWorld* World = GetWorld()) {
				if (ATactics3DBoardGameMode* GM = Cast<ATactics3DBoardGameMode>(World->GetAuthGameMode())) {
					GM->AlignPlayerViewToBoard();
				}
			}
		}
	}
}
