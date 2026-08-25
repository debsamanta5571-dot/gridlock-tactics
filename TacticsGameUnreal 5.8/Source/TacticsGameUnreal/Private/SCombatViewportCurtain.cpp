#include "SCombatViewportCurtain.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

void SCombatViewportCurtain::Construct(const FArguments& InArgs)
{
	static const FLinearColor kOpaqueBlack(0.f, 0.f, 0.f, 1.f);

	ChildSlot
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SBorder)
				.BorderBackgroundColor(kOpaqueBlack)
				.Padding(0.f)
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				InArgs._Content.Widget
			]
		];
}