#pragma once

#include "CoreMinimal.h"
#include "TacticsAbilityVisualGroup.h"

namespace TacticsAbilityVisual
{
FLinearColor FrameColor(ETacticsAbilityVisualGroup Group, bool bArmed);
FLinearColor SpeedTagColor(const FString& SpeedTag);
FString GroupPrompt(ETacticsAbilityVisualGroup Group);
FString GroupShortLabel(ETacticsAbilityVisualGroup Group);
}
