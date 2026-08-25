#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TacticsBoardVisualSettingsSubsystem.generated.h"

/**
 * Saved 3D board layout (main menu → Settings).
 * Cell spacing and grout/floor mesh scale move together; unit card art size is fixed in world units.
 */
UCLASS()
class TACTICSGAMEUNREAL_API UTacticsBoardVisualSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Unscaled grid step (engine cube is 100 uu per 1.0 mesh scale). */
	static constexpr float BaseCellWorldSize = 100.f;
	static constexpr float DefaultBoardCellSpacingScale = 1.45f;
	static constexpr float MinBoardCellSpacingScale = 1.f;
	static constexpr float MaxBoardCellSpacingScale = 1.75f;
	/** Brown grout ring inside each cell (fraction of cell width per side). */
	static constexpr float GroutSeamFraction = 0.055f;

	/** Loads saved board spacing from GameUserSettings.ini. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	float GetBoardCellSpacingScale() const { return BoardCellSpacingScale; }
	float GetVisualCellWorldSize() const { return BaseCellWorldSize * BoardCellSpacingScale; }
	/** Sets how far apart board cells sit (clamped). */
	void SetBoardCellSpacingScale(float InScale);
	/** Restores the default cell spacing. */
	void ResetToDefaults();
	/** Reads BoardCellSpacingScale from config. */
	void LoadFromConfig();
	/** Writes BoardCellSpacingScale to GameUserSettings.ini. */
	void SaveToConfig() const;

	/** Mesh XY scale so grout fills one visual cell slot. */
	float GetGroutFootprintScale() const;
	/** Mesh XY scale for floor tiles (inset inside grout). */
	float GetFloorFootprintScale() const;

private:
	UPROPERTY()
	float BoardCellSpacingScale{DefaultBoardCellSpacingScale};
};
