#include "TacticsBoardViewMapping.h"

void FTacticsBoardViewMapping::PresentGrid(const float GridX, const float GridY, float& OutPresX, float& OutPresY) const
{
	if (!bFlipBoard || SpanGridX < 1 || SpanGridY < 1) {
		OutPresX = GridX;
		OutPresY = GridY;
		return;
	}
	OutPresX = static_cast<float>(MinGridX + SpanGridX - 1) - (GridX - static_cast<float>(MinGridX));
	OutPresY = static_cast<float>(MinGridY + SpanGridY - 1) - (GridY - static_cast<float>(MinGridY));
}

void FTacticsBoardViewMapping::UnpresentGrid(const float PresX, const float PresY, float& OutGridX, float& OutGridY) const
{
	PresentGrid(PresX, PresY, OutGridX, OutGridY);
}

FVector FTacticsBoardViewMapping::GridCellCenterToLocal(int32 GridX, int32 GridY, float Z) const
{
	float PresX = 0.f;
	float PresY = 0.f;
	PresentGrid(static_cast<float>(GridX), static_cast<float>(GridY), PresX, PresY);
	const float S = CellWorldSize;
	const float lx = (PresY - static_cast<float>(MinGridY) + 0.5f) * S;
	const float ly = (PresX - static_cast<float>(MinGridX) + 0.5f) * S;
	return FVector(lx, ly, Z);
}

FVector FTacticsBoardViewMapping::GridCentroidToLocal(float GridX, float GridY, float Z) const
{
	float PresX = 0.f;
	float PresY = 0.f;
	PresentGrid(GridX, GridY, PresX, PresY);
	const float S = CellWorldSize;
	const float lx = (PresY - static_cast<float>(MinGridY) + 0.5f) * S;
	const float ly = (PresX - static_cast<float>(MinGridX) + 0.5f) * S;
	return FVector(lx, ly, Z);
}

bool FTacticsBoardViewMapping::TryLocalToGridCell(const FVector& Local, int32& OutGridX, int32& OutGridY) const
{
	if (CellWorldSize <= KINDA_SMALL_NUMBER) {
		return false;
	}
	const float S = CellWorldSize;
	const float PresY = Local.X / S - 0.5f + static_cast<float>(MinGridY);
	const float PresX = Local.Y / S - 0.5f + static_cast<float>(MinGridX);
	float GridX = 0.f;
	float GridY = 0.f;
	UnpresentGrid(PresX, PresY, GridX, GridY);
	OutGridY = FMath::FloorToInt(GridY + 0.5f);
	OutGridX = FMath::FloorToInt(GridX + 0.5f);
	return true;
}

FVector FTacticsBoardViewMapping::BoundsCenterLocal(int32 InSpanX, int32 InSpanY) const
{
	return FVector(
		static_cast<float>(InSpanY) * CellWorldSize * 0.5f,
		static_cast<float>(InSpanX) * CellWorldSize * 0.5f,
		0.f);
}
