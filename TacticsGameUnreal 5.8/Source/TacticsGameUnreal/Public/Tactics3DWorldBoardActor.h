#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticsBoardUnitActor.h"
#include "TacticsBoardViewMapping.h"
#include "Tactics3DWorldBoardActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UTacticsMatchSubsystem;

/** World-space grid (instanced floor tiles) and spawned unit actors - not Slate. */
UCLASS()
class TACTICSGAMEUNREAL_API ATactics3DWorldBoardActor : public AActor
{
	GENERATED_BODY()

public:
	ATactics3DWorldBoardActor();

	/** Hooks match-board delegates and builds the first grid. */
	virtual void BeginPlay() override;
	/** Destroys unit actors and unregisters match delegates. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** World-space center of the current visual grid. */
	FVector GetBoardWorldCenter() const;
	/** Spring-arm length that frames the whole board from above. */
	float GetSuggestedTopDownArmLength() const;
	bool HasVisualGridBounds() const { return SpanW >= 1 && SpanH >= 1; }

	/** World-space anchor below a unit footprint centroid (for Slate hover projection). */
	FVector GetUnitHoverAnchorWorldPosition(float GridCenterX, float GridCenterY, int32 FootprintSpanMax) const;
	/** Vertical extent of card-art plane above the unit floor anchor (world units). */
	float GetUnitArtExtentWorldZ(int32 FootprintSpanMax) const;

	/** Uses `Hit.Item` as the instance index when `Hit.Component` is the cell ISMC. */
	bool ResolveWorldCellFromHit(const FHitResult& Hit, int& OutWx, int& OutWy) const;

	/**
	 * Frozen tactics-board snapshot for the battle visualization backdrop.
	 * Reads the live match subsystem once (no delegates); safe to call again to
	 * refresh after combat resolution updates entity state.
	 */
	void RefreshBattleBackgroundSnapshot();

protected:
	/** Rebuilds tiles and units when the rules board changes. */
	void OnMatchBoardChanged();
	/** Refreshes move, attack, and ability targeting overlays. */
	void OnTargetPreviewChanged();
	/** Full rebuild of tiles, terrain, highlights, and unit actors. */
	void RebuildVisuals();
	/** Applies only the dirty visual layers in DirtyMask. */
	void RefreshVisualsIncremental(uint32 DirtyMask);
	/** Rebuilds targeting highlight instances without touching units. */
	void RefreshTargetingHighlightsOnly();
	/** Syncs road, rough, hazard, void, and objective terrain meshes. */
	void SyncTerrainMeshes();
	void UpdateMoveReachHighlights();
	void UpdatePendingMoveHighlights();
	void UpdateAttackTargetHighlights();
	void UpdateBoardTargetHighlights();
	void UpdateBoardTargetAoEHighlights();
	void UpdateAbilityBoardTargetHighlights();
	void UpdateResolveFlashHighlights();
	/** Places world-space damage/heal numbers above units. */
	void UpdateAbilityDamagePopups();
	void OnAbilityDamagePopupsChanged();
	void DetachAbilityPopupsFromUnitActor(ATacticsBoardUnitActor* UnitActor);
	void PruneInvalidAbilityDamagePopupLabels();
	void UpdateAbilityTargetLink();
	void UpdateDeployHighlights();
	void UpdateSelectionHighlights();
	void SyncObstacleMeshes();
	void SyncBoardUnderlay();
	void ApplyGridGroutMaterial();
	void UnregisterBoardDelegate();
	/** Spawns, moves, and destroys unit token actors to match the rules board. */
	void SyncUnitActors();
	void ClearUnitActors();
	FVector GridCentroidToWorld(float GridCenterX, float GridCenterY) const;
	float GetTileGroutTopZ() const;
	float GetTileFloorCenterZ() const;
	float GetTileFloorTopZ() const;
	/** World Z of unit card art, just above the tile surface. */
	float GetUnitArtFloorZ() const;
	/** Thin terrain decal (road, rough, hazard, etc.) - flush on the floor, below unit card art. */
	float GetTerrainOverlayCenterZ(float ZThicknessScale) const;
	/** Objective markers (aether / scanner / omni) sit above move-reach highlights so they stay visible. */
	float GetObjectiveTerrainOverlayCenterZ(float ZThicknessScale) const;
	float GetHighlightSlabCenterZ(float ZThicknessScale, float LayerBias) const;
	FVector GetDamagePopupLocalPosition(float GridCenterX, float GridCenterY, float AnimatedOffsetY) const;
	/** Grout slab XY scale - always one full cell slot (never overlaps neighbors). */
	float GetGroutFootprintScale() const;
	/** Floor tile XY scale - grows with cell spacing but stays inside grout seams. */
	float GetFloorFootprintScale() const;
	FTransform MakeBoardHighlightTransform(int Wx, int Wy, float ZThicknessScale, float LayerBias = 0.f,
		float FloorFootprintMul = 0.96f) const;
	float GetVisualCellWorldSize() const;
	const class UTacticsBoardVisualSettingsSubsystem* GetBoardVisualSettings() const;

	/** Single slab under the grid so jigsaw gaps and tile seams do not show the void. */
	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UStaticMeshComponent> BoardUnderlay;

	/** Full-footprint brown grout slab per cell; sits below floor tiles. */
	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> GridGroutMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> CellMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GridGroutMaterialInstance;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> MoveReachHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> PendingMoveDestinationHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> PendingMoveOriginHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> AttackTargetHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> BoardTargetOtherHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> BoardTargetAoEHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> AbilityBoardTargetOtherHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> AbilityBoardTargetEnemyHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> ResolveFlashHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UStaticMeshComponent> AbilityTargetLinkMesh;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> DeployHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> SelectionHighlightMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> ObstacleMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> RoadMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> RoughTerrainMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> DamagingTerrainMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> VoidTerrainMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> AetherTerrainMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> ScannerTerrainMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UInstancedStaticMeshComponent> OmniEnergyTerrainMeshes;

	/** Shared mesh for all board tiles (defaults to engine Cube; override in Blueprint CDO). */
	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Visuals")
	TObjectPtr<UStaticMesh> TileMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> MoveReachHighlightMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> AttackTargetHighlightMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> BoardTargetOtherHighlightMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> BoardTargetAoEHighlightMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> DeployHighlightMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> ObstacleMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> RoadMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> RoughTerrainMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> DamagingTerrainMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> VoidTerrainMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> AetherTerrainMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> ScannerTerrainMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tactics3D|Materials")
	TObjectPtr<UMaterialInterface> OmniEnergyTerrainMaterial;

	TWeakObjectPtr<UTacticsMatchSubsystem> MatchSubsystem;
	FDelegateHandle BoardChangedHandle;
	FDelegateHandle TargetPreviewChangedHandle;
	FDelegateHandle AbilityDamagePopupsChangedHandle;

	TArray<int32> HitWx;
	TArray<int32> HitWy;

	UPROPERTY()
	TMap<FString, TObjectPtr<ATacticsBoardUnitActor>> UnitActors;

	float TileGroutCenterZ{3.f};
	float TileGroutThicknessScale{0.04f};
	float TileFloorThicknessScale{0.12f};
	/** World Z of unit card-art plane above floor top (highlights sit below this). */
	static constexpr float UnitArtFloorLiftZ = 0.5f;
	static constexpr float HighlightUnderArtGapZ = 0.04f;
	/** Translucent sort: terrain < objectives < highlights < unit art. */
	static constexpr int32 TerrainOverlaySortPriority = 80;
	static constexpr int32 ObjectiveOverlaySortPriority = 120;
	static constexpr int32 HighlightOverlaySortPriority = 200;
	/** Z lift above card-art plane; popups float along +local X (screen up). */
	static constexpr float DamagePopupArtLiftZ = 48.f;
	static constexpr float DamagePopupFloatWorldScale = 0.5f;
	int32 RefMinX{0};
	int32 RefMinY{0};
	int32 SpanW{0};
	int32 SpanH{0};

	/** Grid ↔ local XY for the fixed top-down camera; see TacticsBoardViewMapping.h. */
	FTacticsBoardViewMapping ViewMapping;

	/** Owned by ABattleVisualizationActor - one-shot snapshot, no live board hooks. */
	bool bBattleBackgroundMode{false};

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UTextRenderComponent>> AbilityDamagePopupLabels;
};
