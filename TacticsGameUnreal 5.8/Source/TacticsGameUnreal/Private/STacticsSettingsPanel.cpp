#include "STacticsSettingsPanel.h"

#include "TacticsCardText.h"
#include "TacticsGameInstance.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Layout/Visibility.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr FLinearColor kSettingsPanelBg(0.028f, 0.026f, 0.024f, 0.96f);
constexpr FLinearColor kDim(0.f, 0.f, 0.f, 0.62f);
constexpr FLinearColor kTitle(0.95f, 0.95f, 0.95f, 1.f);
constexpr FLinearColor kAccent(0.78f, 0.76f, 0.72f, 1.f);
constexpr FLinearColor kBtnIdle(0.08f, 0.075f, 0.07f, 0.96f);
constexpr FLinearColor kBtnPrimary(0.26f, 0.07f, 0.05f, 1.f);

TSharedRef<SButton> SettingsBtn(const FText& Label, FOnClicked OnClicked, bool bPrimary = false)
{
	return SNew(SButton)
		.OnClicked(OnClicked)
		.ButtonColorAndOpacity(bPrimary ? kBtnPrimary : kBtnIdle)
		[
			SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FLinearColor::White)
				.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 12))
		];
}

const TCHAR* GraphicsLabel(const int32 Level)
{
	switch (Level)
	{
	case 0: return TEXT("Low");
	case 1: return TEXT("Medium");
	case 2: return TEXT("High");
	case 3: return TEXT("Epic");
	default: return TEXT("Custom");
	}
}
}  // namespace

void STacticsSettingsPanel::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;
	OnBack = InArgs._OnBack;
	bInMatch = InArgs._bInMatch;

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(kDim)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(24.f)
				[
					SNew(SBox)
						.WidthOverride(460.f)
						[
							SNew(SBorder)
								.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
								.BorderBackgroundColor(kSettingsPanelBg)
								.Padding(28.f)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Options")))
												.ColorAndOpacity(kTitle)
												.Font(TacticsCardText::DesignFont(TEXT("Bold"), 28))
										]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
										[
											SNew(STextBlock)
												.Text(FText::FromString(TEXT("Graphics")))
												.ColorAndOpacity(kAccent)
												.Font(TacticsCardText::DesignFont(TEXT("Bold"), 14))
										]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
										[BuildGraphicsRow()]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
										[
											SNew(SBox)
												.Visibility(bInMatch ? EVisibility::Visible : EVisibility::Collapsed)
												[
													SettingsBtn(FText::FromString(TEXT("Main menu")),
														FOnClicked::CreateSP(this, &STacticsSettingsPanel::ReturnToMainMenu), true)
												]
										]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
										[
											SNew(SBox)
												.Visibility(bInMatch ? EVisibility::Visible : EVisibility::Collapsed)
												[
													SettingsBtn(FText::FromString(TEXT("Quit")),
														FOnClicked::CreateSP(this, &STacticsSettingsPanel::QuitGame))
												]
										]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
										[
											SettingsBtn(FText::FromString(TEXT("Close")),
												FOnClicked::CreateSP(this, &STacticsSettingsPanel::CloseOverlay))
										]
								]
						]
				]
		];
}

TSharedRef<SWidget> STacticsSettingsPanel::BuildGraphicsRow()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 Level = 0; Level <= 3; ++Level)
	{
		const int32 Captured = Level;
		Row->AddSlot().FillWidth(1.f).Padding(Level == 0 ? 0.f : 6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
					.OnClicked(FOnClicked::CreateSP(this, &STacticsSettingsPanel::SetGraphicsLevel, Captured))
					.ButtonColorAndOpacity_Lambda([this, Captured]() {
						return FSlateColor(CurrentGraphicsLevel() == Captured ? kBtnPrimary : kBtnIdle);
					})
					[
						SNew(STextBlock)
							.Text(FText::FromString(GraphicsLabel(Captured)))
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(FLinearColor::White)
							.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 11))
					]
			];
	}
	return Row;
}

int32 STacticsSettingsPanel::CurrentGraphicsLevel() const
{
	const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return Settings ? Settings->GetOverallScalabilityLevel() : 0;
}

FReply STacticsSettingsPanel::SetGraphicsLevel(const int32 Level)
{
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		return FReply::Handled();
	}
	Settings->SetOverallScalabilityLevel(FMath::Clamp(Level, 0, 3));
	Settings->ApplySettings(true);
	Invalidate(EInvalidateWidget::Paint);
	return FReply::Handled();
}

FReply STacticsSettingsPanel::CloseOverlay()
{
	if (OnBack.IsBound())
	{
		OnBack.Execute();
	}
	return FReply::Handled();
}

FReply STacticsSettingsPanel::ReturnToMainMenu()
{
	if (GameInstance.IsValid())
	{
		GameInstance->ReturnToMainMenuFromMatch();
	}
	return FReply::Handled();
}

FReply STacticsSettingsPanel::QuitGame()
{
	if (GameInstance.IsValid())
	{
		GameInstance->RequestQuitGame();
	}
	return FReply::Handled();
}

FReply STacticsSettingsPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		return CloseOverlay();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}
