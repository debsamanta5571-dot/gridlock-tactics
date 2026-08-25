#include "TacticsBoardVisualSettingsSubsystem.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
constexpr TCHAR kBoardVisualPrefsSection[] = TEXT("TacticsBoardVisual");
}

void UTacticsBoardVisualSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadFromConfig();
}

void UTacticsBoardVisualSettingsSubsystem::SetBoardCellSpacingScale(const float InScale)
{
	BoardCellSpacingScale =
		FMath::Clamp(InScale, MinBoardCellSpacingScale, MaxBoardCellSpacingScale);
}

void UTacticsBoardVisualSettingsSubsystem::ResetToDefaults()
{
	BoardCellSpacingScale = DefaultBoardCellSpacingScale;
}

void UTacticsBoardVisualSettingsSubsystem::LoadFromConfig()
{
	float Loaded = DefaultBoardCellSpacingScale;
	if (!GConfig->GetFloat(kBoardVisualPrefsSection, TEXT("BoardCellSpacingScale"), Loaded, GGameUserSettingsIni)) {
		float LegacyTileScale = DefaultBoardCellSpacingScale;
		GConfig->GetFloat(kBoardVisualPrefsSection, TEXT("BoardTileVisualScale"), LegacyTileScale, GGameUserSettingsIni);
		Loaded = LegacyTileScale;
	}
	SetBoardCellSpacingScale(Loaded);
}

void UTacticsBoardVisualSettingsSubsystem::SaveToConfig() const
{
	GConfig->SetFloat(
		kBoardVisualPrefsSection, TEXT("BoardCellSpacingScale"), BoardCellSpacingScale, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

float UTacticsBoardVisualSettingsSubsystem::GetGroutFootprintScale() const
{
	return BoardCellSpacingScale;
}

float UTacticsBoardVisualSettingsSubsystem::GetFloorFootprintScale() const
{
	return BoardCellSpacingScale * FMath::Max(0.55f, 1.f - 2.f * GroutSeamFraction);
}
