#pragma once

#include "CoreMinimal.h"

/** Shared targeting UX bucket for activated abilities (one highlight + prompt style per group). */
enum class ETacticsAbilityVisualGroup : uint8
{
	Instant,
	StackTarget,
	BoardEntityTarget,
	EmptyCellTarget,
	DirectionalAim,
	LobbedAoeCenter,
	PushDirection,
};
