#include "TacticsAbilityVisualFeedback.h"

FLinearColor TacticsAbilityVisual::FrameColor(const ETacticsAbilityVisualGroup Group, const bool bArmed)
{
	if (bArmed) {
		return FLinearColor(0.78f, 0.22f, 0.62f, 1.f);
	}
	switch (Group) {
	case ETacticsAbilityVisualGroup::Instant:
		return FLinearColor(0.18f, 0.32f, 0.18f, 1.f);
	case ETacticsAbilityVisualGroup::StackTarget:
		return FLinearColor(0.32f, 0.18f, 0.48f, 1.f);
	case ETacticsAbilityVisualGroup::BoardEntityTarget:
		return FLinearColor(0.42f, 0.14f, 0.58f, 1.f);
	case ETacticsAbilityVisualGroup::EmptyCellTarget:
		return FLinearColor(0.28f, 0.16f, 0.46f, 1.f);
	case ETacticsAbilityVisualGroup::DirectionalAim:
		return FLinearColor(0.36f, 0.12f, 0.52f, 1.f);
	case ETacticsAbilityVisualGroup::LobbedAoeCenter:
		return FLinearColor(0.38f, 0.14f, 0.50f, 1.f);
	case ETacticsAbilityVisualGroup::PushDirection:
		return FLinearColor(0.30f, 0.20f, 0.44f, 1.f);
	default:
		return FLinearColor(0.22f, 0.28f, 0.38f, 1.f);
	}
}

FLinearColor TacticsAbilityVisual::SpeedTagColor(const FString& SpeedTag)
{
	if (SpeedTag.Equals(TEXT("reflex"), ESearchCase::IgnoreCase)) {
		return FLinearColor(0.45f, 0.82f, 1.f, 1.f);
	}
	if (SpeedTag.Equals(TEXT("blazing"), ESearchCase::IgnoreCase)) {
		return FLinearColor(1.f, 0.62f, 0.22f, 1.f);
	}
	return FLinearColor(0.82f, 0.78f, 0.55f, 1.f);
}

FString TacticsAbilityVisual::GroupPrompt(const ETacticsAbilityVisualGroup Group)
{
	switch (Group) {
	case ETacticsAbilityVisualGroup::Instant:
		return TEXT("Instant - fires immediately");
	case ETacticsAbilityVisualGroup::StackTarget:
		return TEXT("Click a highlighted batch queue entry");
	case ETacticsAbilityVisualGroup::BoardEntityTarget:
		return TEXT("Click a highlighted unit or structure");
	case ETacticsAbilityVisualGroup::EmptyCellTarget:
		return TEXT("Click a highlighted empty cell");
	case ETacticsAbilityVisualGroup::DirectionalAim:
		return TEXT("Click a direction cell - orange shows the blast");
	case ETacticsAbilityVisualGroup::LobbedAoeCenter:
		return TEXT("Click a center cell - orange shows the blast radius");
	case ETacticsAbilityVisualGroup::PushDirection:
		return TEXT("Pick a unit, then a push direction");
	default:
		return TEXT("Click a highlighted cell");
	}
}

FString TacticsAbilityVisual::GroupShortLabel(const ETacticsAbilityVisualGroup Group)
{
	switch (Group) {
	case ETacticsAbilityVisualGroup::Instant:
		return TEXT("Instant");
	case ETacticsAbilityVisualGroup::StackTarget:
		return TEXT("Stack");
	case ETacticsAbilityVisualGroup::BoardEntityTarget:
		return TEXT("Target");
	case ETacticsAbilityVisualGroup::EmptyCellTarget:
		return TEXT("Tile");
	case ETacticsAbilityVisualGroup::DirectionalAim:
		return TEXT("Direction");
	case ETacticsAbilityVisualGroup::LobbedAoeCenter:
		return TEXT("AoE");
	case ETacticsAbilityVisualGroup::PushDirection:
		return TEXT("Push");
	default:
		return TEXT("Ability");
	}
}


