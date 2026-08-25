#include "STacticsSettingsPanel.h"

#include "TacticsBoardVisualSettingsSubsystem.h"
#include "TacticsCardText.h"
#include "TacticsGameInstance.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr FLinearColor kSettingsPanelBg(0.02f, 0.04f, 0.08f, 0.92f);
constexpr FLinearColor kTitle(0.95f, 0.95f, 0.95f, 1.f);
constexpr FLinearColor kBody(0.78f, 0.82f, 0.88f, 1.f);
constexpr FLinearColor kAccent(0.72f, 0.88f, 1.f, 1.f);

TSharedRef<SButton> SettingsBtn(const FText& Label, FOnClicked OnClicked)
{
	return SNew(SButton)
		.OnClicked(OnClicked)
		[
			SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
				.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 12))
		];
}
}  // namespace

void STacticsSettingsPanel::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;
	OnBack = InArgs._OnBack;

	ChildSlot
		[
			SNew(SBorder)
				.BorderBackgroundColor(kSettingsPanelBg)
				.Padding(32.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Settings")))
								.ColorAndOpacity(kTitle)
								.Font(TacticsCardText::DesignFont(TEXT("Bold"), 28))
						]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 20.f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(
									TEXT("Scales cell spacing, grout, floor tiles, and highlights together. "
										 "Unit card art stays the same size in world units so more grout shows around units. "
										 "Applies on the next match.")))
								.ColorAndOpacity(kBody)
								.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 11))
								.AutoWrapText(true)
						]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("Board cell scale")))
								.ColorAndOpacity(kAccent)
								.Font(TacticsCardText::DesignFont(TEXT("Bold"), 14))
						]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 20.f)
						[BuildCellSpacingControl()]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[
							SettingsBtn(FText::FromString(TEXT("Save")),
								FOnClicked::CreateSP(this, &STacticsSettingsPanel::SaveSettings))
						]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[
							SettingsBtn(FText::FromString(TEXT("Reset to defaults")),
								FOnClicked::CreateSP(this, &STacticsSettingsPanel::ResetDefaults))
						]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
						[
							SettingsBtn(FText::FromString(TEXT("Back")),
								FOnClicked::CreateLambda([this]() {
									if (OnBack.IsBound()) {
										OnBack.Execute();
									}
									return FReply::Handled();
								}))
						]
					+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
								.Text_Lambda([this]() { return FText::FromString(StatusMessage); })
								.ColorAndOpacity(FLinearColor(0.75f, 0.9f, 1.f, 1.f))
								.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 11))
								.AutoWrapText(true)
						]
				]
		];

	RefreshStatus();
}

TSharedRef<SWidget> STacticsSettingsPanel::BuildCellSpacingControl()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(TEXT("Larger = wider cells, bigger grout slabs, and more room around unit portraits.")))
					.ColorAndOpacity(kBody)
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 10))
					.AutoWrapText(true)
			]
		+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
					[
						SettingsBtn(FText::FromString(TEXT("−")),
							FOnClicked::CreateSP(this, &STacticsSettingsPanel::AdjustCellSpacing, -0.05f))
					]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Text_Lambda([this]() {
								if (!GameInstance.IsValid()) {
									return FText::FromString(TEXT("-"));
								}
								const UTacticsBoardVisualSettingsSubsystem* Settings =
									GameInstance->GetSubsystem<UTacticsBoardVisualSettingsSubsystem>();
								if (!Settings) {
									return FText::FromString(TEXT("-"));
								}
								const float Spacing = Settings->GetBoardCellSpacingScale();
								const float CellUU = Settings->GetVisualCellWorldSize();
								return FText::FromString(FString::Printf(
									TEXT("Scale %.2f  (cell spacing %.0f uu)"), Spacing, CellUU));
							})
							.ColorAndOpacity(kTitle)
							.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 12))
					]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
					[
						SettingsBtn(FText::FromString(TEXT("+")),
							FOnClicked::CreateSP(this, &STacticsSettingsPanel::AdjustCellSpacing, 0.05f))
					]
			];
}

void STacticsSettingsPanel::RefreshStatus()
{
	StatusMessage = TEXT("Adjust cell scale, then Save. Launch a match to preview.");
}

FReply STacticsSettingsPanel::AdjustCellSpacing(const float Delta)
{
	if (!GameInstance.IsValid()) {
		return FReply::Handled();
	}
	UTacticsBoardVisualSettingsSubsystem* Settings = GameInstance->GetSubsystem<UTacticsBoardVisualSettingsSubsystem>();
	if (!Settings) {
		return FReply::Handled();
	}
	Settings->SetBoardCellSpacingScale(Settings->GetBoardCellSpacingScale() + Delta);
	StatusMessage = FString::Printf(
		TEXT("Cell scale %.2f (%.0f uu spacing, not saved yet)."),
		Settings->GetBoardCellSpacingScale(),
		Settings->GetVisualCellWorldSize());
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled();
}

FReply STacticsSettingsPanel::SaveSettings()
{
	if (!GameInstance.IsValid()) {
		StatusMessage = TEXT("Game instance not ready.");
		return FReply::Handled();
	}
	UTacticsBoardVisualSettingsSubsystem* Settings = GameInstance->GetSubsystem<UTacticsBoardVisualSettingsSubsystem>();
	if (!Settings) {
		StatusMessage = TEXT("Settings subsystem unavailable.");
		return FReply::Handled();
	}
	Settings->SaveToConfig();
	StatusMessage = FString::Printf(
		TEXT("Saved. Cell scale %.2f (%.0f uu) - applies on next match."),
		Settings->GetBoardCellSpacingScale(),
		Settings->GetVisualCellWorldSize());
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled();
}

FReply STacticsSettingsPanel::ResetDefaults()
{
	if (!GameInstance.IsValid()) {
		return FReply::Handled();
	}
	UTacticsBoardVisualSettingsSubsystem* Settings = GameInstance->GetSubsystem<UTacticsBoardVisualSettingsSubsystem>();
	if (!Settings) {
		return FReply::Handled();
	}
	Settings->ResetToDefaults();
	StatusMessage = TEXT("Defaults restored (not saved yet).");
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled();
}
