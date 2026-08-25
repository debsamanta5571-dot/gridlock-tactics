#pragma once

#include "CoreMinimal.h"

/**
 * Converts authoritative tactics grid coordinates into actor-local space for the 3D board.
 *
 * Canonical grid (cpp_core, CLI, snapshots, Slate 2D - never swap these):
 *   - X = column, 0 = left, increases right (board width)
 *   - Y = row, 0 = bottom / P1 side, increases toward top / P2 side
 *
 * Presentation uses the standard top-down camera rig (spring arm pitch ~ -90°, yaw 0°).
 * On that rig, actor local +X reads as screen-up and local +Y as screen-right, so we map:
 *   local.X <- grid Y,  local.Y <- grid X
 *
 * Do not rotate the camera to fix board orientation; that couples view to every system and
 * breaks if the rig changes. Always route 3D placement and picking through this mapping.
 *
 * Far-side seats (P2) set bFlipBoard: a 180° remap so that seat's base sits at screen-bottom.
 * Authoritative grid coords are never swapped - only presentation.
 */
struct TACTICSGAMEUNREAL_API FTacticsBoardViewMapping
{
	float CellWorldSize{100.f};
	int32 MinGridX{0};
	int32 MinGridY{0};
	int32 SpanGridX{0};
	int32 SpanGridY{0};
	bool bFlipBoard{false};

	/** Local position of a grid cell center. */
	FVector GridCellCenterToLocal(int32 GridX, int32 GridY, float Z = 0.f) const;
	/** Local position of a fractional grid centroid (multi-cell footprints). */
	FVector GridCentroidToLocal(float GridX, float GridY, float Z = 0.f) const;

	/** Inverse of GridCellCenterToLocal (for future traces / gizmos). */
	bool TryLocalToGridCell(const FVector& Local, int32& OutGridX, int32& OutGridY) const;

	/** Center of an axis-aligned grid span in local space (InSpanX = columns, InSpanY = rows). */
	FVector BoundsCenterLocal(int32 InSpanX, int32 InSpanY) const;

private:
	/** Maps authoritative grid XY into presentation XY (optional 180 flip). */
	void PresentGrid(float GridX, float GridY, float& OutPresX, float& OutPresY) const;
	/** Inverse of PresentGrid. */
	void UnpresentGrid(float PresX, float PresY, float& OutGridX, float& OutGridY) const;
};
