#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticsBoardUnitActor.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;
class UMaterialInstanceDynamic;

/** One tactics unit on the map (skeletal mesh fallback or flat card-art plane). */
UCLASS()
class TACTICSGAMEUNREAL_API ATacticsBoardUnitActor : public AActor
{
	GENERATED_BODY()

public:
	ATacticsBoardUnitActor();

	/** Glides the token toward its target cell each frame. */
	virtual void Tick(float DeltaSeconds) override;

	/** Sets the target board pose. The first call after spawn snaps (a unit appearing/deploying
	 *  should not glide in from the origin); later calls glide the actor to the new cell so a bot
	 *  move reads as a move instead of a teleport. Large jumps (resync/reslot) snap. */
	void ApplyWorldPose(const FVector& WorldLocation, float UniformScale, float GridCenterX, float GridCenterY);
	/** Switches this actor to a base slab (or back to a unit token). */
	void ConfigureAsBase(bool bAsBase, int32 FootprintSpanX, int32 FootprintSpanY);
	/** Scales card-art plane (and mannequin fallback) to occupy FootprintSpanX × FootprintSpanY cells.
	 *  FloorMeshScale is the 3D board floor-tile XY scale (matches grout/cell spacing). */
	void ConfigureFootprintVisual(int32 FootprintSpanX, int32 FootprintSpanY, float FloorMeshScale = 1.f);
	/** Loads this unit's card art PNG and draws it on the portrait plane. */
	void SetCardArt(const FString& ArtId);
	/** Shows the turn-order number (and a taunt mark) above the token. */
	void SetTurnOrderBadge(int32 Rank, bool bIsBase, bool bHasTaunt = false);
	/** Board cell this actor currently represents (pose target). */
	bool TryGetBoardGridCell(int& OutWx, int& OutWy) const;

	/** Face card art toward the battle-stage camera instead of top-down. */
	void SetBattleBackgroundPresentation(bool bEnable);
	/** Tints the portrait while an ability cast flash is playing. */
	void ApplyAbilityCastFlash(bool bSuccess, float Alpha);

protected:
	/** Shows card art on the tile, or the mannequin if the art failed to load. */
	void UpdateUnitVisuals();
	/** Sizes the portrait plane to the unit's footprint and the board's tile scale. */
	void ApplyFootprintVisualScale();

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<USkeletalMeshComponent> UnitMesh;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	/** Lies in XY with normal +Z so top-down camera sees the portrait. */
	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UStaticMeshComponent> ArtPlane;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UTextRenderComponent> TurnOrderLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LoadedArtTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ArtMaterial;

	UPROPERTY(Transient)
	FString LoadedArtId;

	bool bBattleBackgroundPresentation{false};
	int32 CachedFootprintSpanX{1};
	int32 CachedFootprintSpanY{1};
	float CachedFloorMeshScale{1.f};
	float AbilityCastFlashAlpha{0.f};
	bool bAbilityCastFlashSuccess{false};

	/** Smooth-move state: world target, authoritative grid target, and last settled grid cell. */
	FVector PoseTargetLocation{FVector::ZeroVector};
	float PoseTargetGridX{0.f};
	float PoseTargetGridY{0.f};
	float PoseSyncedGridX{0.f};
	float PoseSyncedGridY{0.f};
	bool bPosePlaced{false};
	float PoseUniformScale{1.f};
};
