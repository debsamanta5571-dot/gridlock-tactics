#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Full-viewport opaque black curtain. Sits above the match board UI and the 3D scene.
 * Hosts the combat flash content centered on solid black.
 */
class SCombatViewportCurtain : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCombatViewportCurtain) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	/** Fills the viewport with opaque black and hosts combat content. */
	void Construct(const FArguments& InArgs);
};