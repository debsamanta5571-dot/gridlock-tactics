#include "TacticsCardText.h"

#include "TacticsCardArtUi.h"
#include "TacticsGlossaryMarkup.h"

#include "Brushes/SlateNoResource.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/ISlateRun.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/SlateWidgetRun.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/CompositeFont.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

namespace TacticsCardText
{
namespace
{
// Art id (relative to Content/TacticsData/card_art/, no extension) that supports an inline count
// number drawn over the icon, like generic mana. Consecutive identical tokens collapse onto it.
const TCHAR* kNeutralArtId = TEXT("ui/energy/neutral");

bool IsTokenIdentifier(const FString& Token)
{
	for (const TCHAR Ch : Token) {
		const bool bOk = FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-');
		if (!bOk) {
			return false;
		}
	}
	return !Token.IsEmpty();
}

// Resolves a brace token to a card-art id (relative path under card_art/, no extension), or empty
// if the token is not an icon and should stay as literal text.
//
// To add a new icon you do NOT need to touch this code:
//   * drop  ui/icons/<name>.png            and write  {name}          (generic bucket)
//   * drop  ui/<category>/<name>.png        and write  {category/name} (any subfolder)
//   * (optional) add a short alias below for a frequently-used icon.
FString IconArtIdForToken(const FString& Token)
{
	// Short aliases for the most common icons. Keyed upper-case (lookup is case-insensitive).
	static const TMap<FString, FString> Aliases = {
		{TEXT("O"), TEXT("ui/energy/orange")},
		{TEXT("N"), TEXT("ui/energy/neutral")},
		{TEXT("G"), TEXT("ui/energy/green")},
		{TEXT("T"), TEXT("ui/energy/turquoise")},
		{TEXT("R"), TEXT("ui/energy/red")},
		{TEXT("P"), TEXT("ui/energy/purple")},
		{TEXT("M"), TEXT("ui/energy/omni")},
		{TEXT("CHANNELED"), TEXT("ui/speed/channeled")},
		{TEXT("REFLEX"),    TEXT("ui/speed/reflex")},
		{TEXT("BLAZING"),   TEXT("ui/speed/blazing")},
		{TEXT("PASSIVE"), TEXT("ui/passive/passive")},
		// Unit stats.
		{TEXT("LIFE"),     TEXT("ui/stats/life")},
		{TEXT("HP"),       TEXT("ui/stats/life")},
		{TEXT("MELEE"),    TEXT("ui/stats/melee")},
		{TEXT("MOVE"),     TEXT("ui/stats/movement")},
		{TEXT("MOVEMENT"), TEXT("ui/stats/movement")},
		// NB: "ranged attack" and "range" are distinct icons:
		//   {RANGED} marks that a unit *has* a ranged attack (its ranged damage),
		//   {RANGE}  is the reach distance of an attack or ability.
		{TEXT("RANGED"), TEXT("ui/stats/ranged")},
		{TEXT("ARMOR"),  TEXT("ui/stats/armor")},
		{TEXT("RANGE"),  TEXT("ui/stats/range")},
		{TEXT("RANGE_SELF"), TEXT("ui/stats/range_self")},
		{TEXT("ADJACENT"), TEXT("ui/stats/adjacent")},
		{TEXT("ADJACENT_SELF"), TEXT("ui/stats/adjacent_self")},
		// Live unit action availability (board detail stat row).
		{TEXT("MOVE_READY"), TEXT("ui/actions/movement_ready")},
		{TEXT("MOVE_USED"), TEXT("ui/actions/movement_used")},
		{TEXT("MOVEMENT_READY"), TEXT("ui/actions/movement_ready")},
		{TEXT("MOVEMENT_USED"), TEXT("ui/actions/movement_used")},
		{TEXT("ATTACK"), TEXT("ui/actions/attack_ready")},
		{TEXT("ATTACK_READY"), TEXT("ui/actions/attack_ready")},
		{TEXT("ATTACK_USED"), TEXT("ui/actions/attack_used")},
		{TEXT("REACTION_READY"), TEXT("ui/actions/reaction_ready")},
		{TEXT("REACTION_USED"), TEXT("ui/actions/reaction_used")},
		{TEXT("ABILITY_READY"), TEXT("ui/actions/ability_ready")},
		{TEXT("ABILITY_USED"), TEXT("ui/actions/ability_used")},
	};

	if (Token.IsEmpty()) {
		return FString();
	}
	if (const FString* Found = Aliases.Find(Token.ToUpper())) {
		return *Found;
	}
	// Explicit subpath: {category/name} -> ui/category/name (drop art in any ui subfolder).
	if (Token.Contains(TEXT("/"))) {
		return FString::Printf(TEXT("ui/%s"), *Token);
	}
	// Variable X-cost: silver neutral disc with an X (see MarkupEnergyTokens).
	if (Token.ToUpper() == TEXT("X")) {
		return kNeutralArtId;
	}
	// Generic bucket: any multi-letter identifier -> ui/icons/<lower>. Other single unknown letters stay as text.
	if (Token.Len() >= 2 && IsTokenIdentifier(Token)) {
		return FString::Printf(TEXT("ui/icons/%s"), *Token.ToLower());
	}
	return FString();
}

void AppendEscaped(FString& Out, TCHAR Ch)
{
	switch (Ch) {
		case '&': Out += TEXT("&amp;"); break;
		case '<': Out += TEXT("&lt;"); break;
		case '>': Out += TEXT("&gt;"); break;
		case '"': Out += TEXT("&quot;"); break;
		default: Out.AppendChar(Ch); break;
	}
}

FString EscapeAttr(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());
	for (const TCHAR Ch : In) {
		AppendEscaped(Out, Ch);
	}
	return Out;
}

// Parses a brace token starting at Pos. On success fills OutToken (text between braces) and
// OutNext (index just past the closing brace) and returns true.
bool TryParseBraceToken(const FString& In, int32 Pos, FString& OutToken, int32& OutNext)
{
	const int32 N = In.Len();
	if (Pos >= N || In[Pos] != TEXT('{')) {
		return false;
	}
	int32 Close = INDEX_NONE;
	for (int32 j = Pos + 1; j < N && j <= Pos + 40; ++j) {
		if (In[j] == TEXT('}')) { Close = j; break; }
		if (In[j] == TEXT('{')) { break; }
	}
	if (Close == INDEX_NONE || Close == Pos + 1) {
		return false;
	}
	OutToken = In.Mid(Pos + 1, Close - Pos - 1);
	OutNext = Close + 1;
	return true;
}

FLinearColor SpeedLabelColor(const FString& TypeLower)
{
	if (TypeLower.Equals(TEXT("reflex"), ESearchCase::IgnoreCase)) {
		return FLinearColor(0.968627f, 0.776471f, 0.164706f, 1.f);  // #F7C62A
	}
	if (TypeLower.Equals(TEXT("blazing"), ESearchCase::IgnoreCase)) {
		return FLinearColor(0.839216f, 0.090196f, 0.168627f, 1.f);  // #D6172B
	}
	return FLinearColor(0.78f, 0.55f, 1.0f, 1.f);  // channeled - purple
}

FLinearColor PassiveLabelColor()
{
	return FLinearColor(0.211678f, 0.598928f, 0.887207f, 1.f);  // #3598E2 - sampled from passive.png glow
}

FString SpeedLabelTextForToken(const FString& Token)
{
	const FString Upper = Token.ToUpper();
	if (Upper == TEXT("REFLEX")) {
		return TEXT("Reflex");
	}
	if (Upper == TEXT("BLAZING")) {
		return TEXT("Blazing");
	}
	return TEXT("Channeled");
}

FString SpeedTooltipSubjectSlug(const ESpeedTooltipSubject Subject)
{
	switch (Subject) {
		case ESpeedTooltipSubject::Ability: return TEXT("ability");
		case ESpeedTooltipSubject::Attack:  return TEXT("attack");
		default:                            return TEXT("spell");
	}
}

FString SpeedTooltipTextForToken(const FString& Token, const ESpeedTooltipSubject Subject)
{
	return SpeedGlossaryTooltip(Token, SpeedTooltipSubjectSlug(Subject));
}

FString PassiveLabelTextForToken(const FString& Token)
{
	if (Token.Equals(TEXT("aura"), ESearchCase::IgnoreCase)) {
		return TEXT("Aura");
	}
	return TEXT("Passive");
}

TSharedRef<SWidget> WrapWidgetToolTip(TSharedRef<SWidget> Inner, const FString& Tip);
ETacticsTooltipSubject TooltipSubjectFromSpeed(ESpeedTooltipSubject Subject);

// Generic inline-icon decorator. Reads <icon i="art/id" count="N" f="fallback"/> runs:
//   i = card-art id to display
//   count = optional pip count drawn over neutral energy (legacy attr: n)
//   f = literal text to show if the art is missing
// Implements ITextDecorator directly (rather than UMG's FRichTextDecorator, whose Create() is
// `final` and dereferences a URichTextBlock owner we don't have when used with a raw SRichTextBlock).
class FInlineIconDecorator : public ITextDecorator
{
public:
	explicit FInlineIconDecorator(int32 InIconSize, const FTextBlockStyle& InDefaultStyle,
		ESpeedTooltipSubject InSpeedSubject = ESpeedTooltipSubject::Spell)
		: IconSize(InIconSize)
		, DefaultStyle(InDefaultStyle)
		, SpeedSubject(InSpeedSubject)
	{
	}

	static constexpr float kRangeBrushScale = 1.22f;
	/** Attack-action cost chip beside energy pips in ability metadata strips. */
	static constexpr float kAttackCostIconScale = 0.62f;
	// movement.png ink sits ~26px above canvas center; nudge down in-slot (art unchanged).
	static constexpr float kMovementArtYOffsetRatio = 26.f / 256.f;

	float IconPadTopForArt(const FString& ArtId, float SlotDim) const
	{
		if (ArtId == TEXT("ui/stats/movement")) {
			return SlotDim * kMovementArtYOffsetRatio;
		}
		return 0.f;
	}

	TSharedRef<SWidget> MakeCenteredIconBox(const FSlateBrush* Brush, float SlotDim, const FString& ArtId) const
	{
		const float PadTop = IconPadTopForArt(ArtId, SlotDim);
		return SNew(SBox)
			.WidthOverride(SlotDim)
			.HeightOverride(SlotDim)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(0.f, PadTop, 0.f, 0.f)
			[
				SNew(SImage)
					.Image(Brush)
					.ColorAndOpacity(ArtId == TEXT("ui/stats/armor")
						? FLinearColor(1.12f, 1.08f, 1.02f, 1.f)
						: FLinearColor::White)
			];
	}

	TSharedRef<SWidget> MakeIconWithSideLabel(const FSlateBrush* Brush, float SlotDim, const FText& Label,
		const FSlateFontInfo& Font, const FLinearColor& Color, const FString& ArtId, float LabelPadLeft = 2.f) const
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeCenteredIconBox(Brush, SlotDim, ArtId)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(LabelPadLeft, 0.f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
						.HeightOverride(SlotDim)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(Label)
								.Font(Font)
								.ColorAndOpacity(Color)
						]
				];
	}

	float BrushScaleForArt(const FString& ArtId) const
	{
		return ArtId == TEXT("ui/stats/range") ? kRangeBrushScale : 1.f;
	}

	TSharedRef<SWidget> MakeNeutralPipWithLabel(const FSlateBrush* Brush, float SlotDim, const FString& Label) const
	{
		const bool bSingleChar = Label.Len() <= 1;
		FSlateFontInfo LabelFont = DesignFont(
			TEXT("Bold"),
			FMath::Clamp(FMath::RoundToInt(SlotDim * (bSingleChar ? 0.56f : 0.46f)), 10, 18));
		LabelFont.OutlineSettings.OutlineSize = 1;
		LabelFont.OutlineSettings.OutlineColor = FLinearColor(0.92f, 0.94f, 0.96f, 0.55f);

		const FLinearColor DigitColor(0.07f, 0.09f, 0.13f, 1.f);

		return SNew(SBox)
			.WidthOverride(SlotDim)
			.HeightOverride(SlotDim)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage).Image(Brush)
					]
				+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Text(FText::FromString(Label))
							.Font(LabelFont)
							.ColorAndOpacity(DigitColor)
							.Justification(ETextJustify::Center)
							.LineHeightPercentage(1.f)
					]
			];
	}

	TSharedRef<SWidget> MakeNeutralPipWithCount(const FSlateBrush* Brush, float SlotDim, int32 Count) const
	{
		return MakeNeutralPipWithLabel(Brush, SlotDim, FString::FromInt(Count));
	}

	static int32 ParseNeutralPipCount(const FString& CountStr)
	{
		FString S = CountStr;
		S.TrimStartAndEndInline();
		if (S.IsEmpty()) {
			return 0;
		}
		int32 Count = 0;
		if (LexTryParseString(Count, *S) && Count > 0) {
			return Count;
		}
		return FCString::Atoi(*S);
	}

	static int32 NeutralCountFromMetadata(const TMap<FString, FString>& Meta)
	{
		if (const FString* CountStr = Meta.Find(TEXT("count"))) {
			return ParseNeutralPipCount(*CountStr);
		}
		// Legacy markup used the single-letter attr name "n".
		if (const FString* Legacy = Meta.Find(TEXT("n"))) {
			return ParseNeutralPipCount(*Legacy);
		}
		return 0;
	}

	virtual bool Supports(const FTextRunParseResults& RunParseResult, const FString& /*Text*/) const override
	{
		return RunParseResult.Name == TEXT("icon");
	}

	// Mirrors FRichTextDecorator::Create but sources the default text style from a stored copy
	// instead of an owning URichTextBlock.
	virtual TSharedRef<ISlateRun> Create(const TSharedRef<FTextLayout>& TextLayout, const FTextRunParseResults& RunParseResult, const FString& OriginalText, const TSharedRef<FString>& InOutModelText, const ISlateStyle* /*Style*/) override
	{
		FTextRange ModelRange;
		ModelRange.BeginIndex = InOutModelText->Len();

		FTextRunInfo RunInfo(RunParseResult.Name, FText::FromString(OriginalText.Mid(RunParseResult.ContentRange.BeginIndex, RunParseResult.ContentRange.EndIndex - RunParseResult.ContentRange.BeginIndex)));
		for (const TPair<FString, FTextRange>& Pair : RunParseResult.MetaData) {
			RunInfo.MetaData.Add(Pair.Key, OriginalText.Mid(Pair.Value.BeginIndex, Pair.Value.EndIndex - Pair.Value.BeginIndex));
		}

		TSharedPtr<SWidget> DecoratorWidget = CreateDecoratorWidget(RunInfo, DefaultStyle);

		*InOutModelText += TEXT('​');  // Zero-Width Breaking Space
		ModelRange.EndIndex = InOutModelText->Len();

		const FSlateFontInfo Font = DefaultStyle.Font;
		const float ShadowOffsetY = FMath::Min(0.0f, DefaultStyle.ShadowOffset.Y);
		TAttribute<int16> GetBaseline = TAttribute<int16>::CreateLambda([Font, ShadowOffsetY]() {
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return static_cast<int16>(FontMeasure->GetBaseline(Font) - ShadowOffsetY);
		});

		FSlateWidgetRun::FWidgetRunInfo WidgetRunInfo(DecoratorWidget.ToSharedRef(), GetBaseline);
		return FSlateWidgetRun::Create(TextLayout, RunInfo, InOutModelText, WidgetRunInfo, ModelRange);
	}

	FString ResolveInlineIconTooltip(const FTextRunInfo& RunInfo, const FString& ArtId, const FString& TokenFallback,
		const int32 RangeValue = -1, const bool bNeutralXCost = false, const int32 NeutralPipCount = 0) const
	{
		if (ArtId.StartsWith(TEXT("ui/speed/"))) {
			const FString TokenKey = TokenFallback.IsEmpty() ? TEXT("channeled") : TokenFallback;
			return SpeedTooltipTextForToken(TokenKey, SpeedSubject);
		}
		if (ArtId == TEXT("ui/actions/attack_ready") && RunInfo.MetaData.Contains(TEXT("sm"))) {
			return TEXT("Costs attack action");
		}
		// All icon hover text (static blurb + dynamic range sentence) resolves in one place;
		// we pass the render context so the resolver can pick the right subject noun.
		return IconGlossaryTooltipForArtId(ArtId, TokenFallback, RangeValue, bNeutralXCost, NeutralPipCount,
			/*bAdvancedGlossary*/ false, TooltipSubjectFromSpeed(SpeedSubject));
	}

	TSharedRef<SWidget> WrapInlineIconTooltip(TSharedRef<SWidget> Inner, const FString& Tip) const
	{
		return WrapWidgetToolTip(Inner, Tip);
	}

protected:
	TSharedPtr<SWidget> CreateDecoratorWidget(const FTextRunInfo& RunInfo, const FTextBlockStyle& /*TextStyle*/) const
	{
		FString ArtId;
		if (const FString* Found = RunInfo.MetaData.Find(TEXT("i"))) {
			ArtId = *Found;
		}
		FString TokenFallback;
		if (const FString* F = RunInfo.MetaData.Find(TEXT("f"))) {
			TokenFallback = *F;
		}
		FString Fallback = TokenFallback.IsEmpty() ? TEXT("?") : FString::Printf(TEXT("{%s}"), *TokenFallback);

		float IconDim = static_cast<float>(IconSize);
		if (RunInfo.MetaData.Contains(TEXT("sm"))) {
			IconDim *= kAttackCostIconScale;
		}
		const float BrushScale = BrushScaleForArt(ArtId);
		const FVector2D BrushSize(IconDim * BrushScale, IconDim * BrushScale);
		const FSlateBrush* Brush = ArtId.IsEmpty() ? nullptr : TacticsCardArtUi::GetCardArtBrush(ArtId, BrushSize);
		const bool bNeutralXCost = ArtId == kNeutralArtId && RunInfo.MetaData.Contains(TEXT("xcost"));
		const int32 NeutralPipCount = ArtId == kNeutralArtId ? NeutralCountFromMetadata(RunInfo.MetaData) : 0;
		int32 RangeValue = -1;
		if (const FString* ValueStr = RunInfo.MetaData.Find(TEXT("v"))) {
			RangeValue = FCString::Atoi(**ValueStr);
		}
		const FString IconTip = ResolveInlineIconTooltip(RunInfo, ArtId, TokenFallback, RangeValue, bNeutralXCost,
			NeutralPipCount);

		if (!Brush) {
			return WrapInlineIconTooltip(SNew(STextBlock).Text(FText::FromString(Fallback)), IconTip);
		}

		const FLinearColor TextColor = DefaultStyle.ColorAndOpacity.GetSpecifiedColor();

		// Range reach: icon + number in one inline row (matches speed-label layout; avoids low plain-text digits).
		if (RangeValue >= 0) {
			return WrapInlineIconTooltip(
				MakeIconWithSideLabel(Brush, IconDim, FText::FromString(FString::FromInt(RangeValue)),
					DefaultStyle.Font, TextColor, ArtId),
				IconTip);
		}

		// Neutral energy: variable X-cost or numbered pip count on the silver disc.
		if (ArtId == kNeutralArtId) {
			if (bNeutralXCost) {
				return WrapInlineIconTooltip(MakeNeutralPipWithLabel(Brush, IconDim, TEXT("X")), IconTip);
			}
			if (NeutralPipCount >= 1) {
				return WrapInlineIconTooltip(MakeNeutralPipWithCount(Brush, IconDim, NeutralPipCount), IconTip);
			}
		}

		// Speed icons: icon + colored speed word in one inline widget (SRichTextBlock only parses self-closing tags).
		if (ArtId.StartsWith(TEXT("ui/speed/"))) {
			const FString TokenKey = TokenFallback.IsEmpty() ? TEXT("channeled") : TokenFallback;
			if (RunInfo.MetaData.Contains(TEXT("c"))) {
				return WrapInlineIconTooltip(MakeCenteredIconBox(Brush, IconDim, ArtId), IconTip);
			}
			const FString Label = SpeedLabelTextForToken(TokenKey);
			return WrapInlineIconTooltip(
				MakeIconWithSideLabel(Brush, IconDim, FText::FromString(Label),
					DefaultStyle.Font, SpeedLabelColor(TokenKey), ArtId),
				IconTip);
		}

		// Passive icon: hourglass + electric-blue "Passive" / "Aura" label (same layout as speed).
		if (ArtId.StartsWith(TEXT("ui/passive/"))) {
			if (RunInfo.MetaData.Contains(TEXT("c"))) {
				return WrapInlineIconTooltip(MakeCenteredIconBox(Brush, IconDim, ArtId), IconTip);
			}
			const FString TokenKey = TokenFallback.IsEmpty() ? TEXT("passive") : TokenFallback;
			const FString Label = PassiveLabelTextForToken(TokenKey);
			return WrapInlineIconTooltip(
				MakeIconWithSideLabel(Brush, IconDim, FText::FromString(Label),
					DefaultStyle.Font, PassiveLabelColor(), ArtId),
				IconTip);
		}

		return WrapInlineIconTooltip(MakeCenteredIconBox(Brush, IconDim, ArtId), IconTip);
	}

private:
	int32 IconSize;
	FTextBlockStyle DefaultStyle;
	ESpeedTooltipSubject SpeedSubject;
};

constexpr int32 kGlossaryTooltipFontSize = 17;
constexpr float kGlossaryTooltipWrapWidth = 420.f;

const FSlateBrush* GlossaryTooltipBackgroundBrush()
{
	static FSlateColorBrush Brush(FLinearColor(0.f, 0.f, 0.f, 0.96f));
	return &Brush;
}

TSharedPtr<IToolTip> MakeGlossaryHoverToolTipImpl(const FString& Tip)
{
	if (Tip.IsEmpty()) {
		return nullptr;
	}
	// Custom content on SToolTip (not ToolTipText) - black panel, white Libre Baskerville body text.
	return SNew(SToolTip)
		.BorderImage(GlossaryTooltipBackgroundBrush())
		.TextMargin(FMargin(16.f, 14.f))
		.Content()
		[
			SNew(STextBlock)
				.Text(FText::FromString(Tip))
				.Font(DesignFont(TEXT("Regular"), kGlossaryTooltipFontSize))
				.ColorAndOpacity(FLinearColor::White)
				.WrapTextAt(kGlossaryTooltipWrapWidth)
				.AutoWrapText(true)
				.LineHeightPercentage(1.28f)
		];
}

TSharedRef<SWidget> WrapWidgetToolTip(TSharedRef<SWidget> Inner, const FString& Tip)
{
	const TSharedPtr<IToolTip> HoverTip = MakeGlossaryHoverToolTipImpl(Tip);
	if (!HoverTip.IsValid()) {
		return Inner;
	}
	return SNew(SBox)
		.ToolTip(HoverTip)
		[
			Inner
		];
}

FHyperlinkStyle MakeGlossaryHyperlinkStyle(const FTextBlockStyle& TextStyle)
{
	const FButtonStyle NoUnderline = FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetDisabled(FSlateNoResource());

	return FHyperlinkStyle()
		.SetUnderlineStyle(NoUnderline)
		.SetTextStyle(TextStyle)
		.SetPadding(FMargin(0.f));
}

FSlateHyperlinkRun::FOnGenerateTooltip MakeGlossaryTooltipDelegate(const FString& Tip)
{
	FSlateHyperlinkRun::FOnGenerateTooltip Delegate;
	if (!Tip.IsEmpty()) {
		const FString CapturedTip = Tip;
		Delegate.BindLambda([CapturedTip](const TMap<FString, FString>&) -> TSharedRef<IToolTip> {
			return MakeGlossaryHoverToolTipImpl(CapturedTip).ToSharedRef();
		});
	}
	return Delegate;
}

TSharedRef<ISlateRun> CreateGlossaryHyperlinkRun(const FTextRunParseResults& RunParseResult, const TSharedRef<FString>& InOutModelText,
	const FString& Word, const FString& Tip, const FTextBlockStyle& TextStyle)
{
	FTextRange ModelRange;
	ModelRange.BeginIndex = InOutModelText->Len();
	*InOutModelText += Word;
	ModelRange.EndIndex = InOutModelText->Len();

	const FTextRunInfo RunInfo(RunParseResult.Name, FText::FromString(Word));
	return FSlateHyperlinkRun::Create(
		RunInfo,
		InOutModelText,
		MakeGlossaryHyperlinkStyle(TextStyle),
		FSlateHyperlinkRun::FOnClick(),
		MakeGlossaryTooltipDelegate(Tip),
		FSlateHyperlinkRun::FOnGetTooltipText(),
		ModelRange);
}

static FLinearColor GlossaryTermColor()
{
	return FLinearColor(0.95f, 0.86f, 0.55f, 1.f);
}

// Maps the UI-facing speed subject to the neutral TacticsCore tooltip subject (used for the
// dynamic "the range of this <attack/spell/ability> is N tiles" sentence in icon hovers).
ETacticsTooltipSubject TooltipSubjectFromSpeed(const ESpeedTooltipSubject Subject)
{
	switch (Subject) {
		case ESpeedTooltipSubject::Ability: return ETacticsTooltipSubject::Ability;
		case ESpeedTooltipSubject::Attack:  return ETacticsTooltipSubject::Attack;
		default:                            return ETacticsTooltipSubject::Spell;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Inline text-highlight decorator.
//
// A "highlight" is a colored, hoverable word in rules text, rendered from a
// self-closing tag  <TAG w="DisplayWord" tip="Hover text"/>.  The set of styles is
// the SHARED registry TacticsHighlightStyles() in TacticsGlossaryMarkup.cpp
// (TacticsCore), so markup-emit and render never disagree.
//
// TO ADD A NEW HIGHLIGHT: add one entry to TacticsHighlightStyles() - not here.
// MakeEnergyDecorators() builds one FHighlightRunDecorator per registry entry.
// ─────────────────────────────────────────────────────────────────────────────
class FHighlightRunDecorator : public ITextDecorator
{
public:
	FHighlightRunDecorator(const FTacticsHighlightStyle& InStyle, const FTextBlockStyle& InDefaultStyle)
		: Style(InStyle)
		, RunStyle(InDefaultStyle)
	{
		RunStyle.SetColorAndOpacity(Style.Color);
	}

	virtual bool Supports(const FTextRunParseResults& RunParseResult, const FString& /*Text*/) const override
	{
		return RunParseResult.Name.Equals(Style.Tag, ESearchCase::IgnoreCase);
	}

	virtual TSharedRef<ISlateRun> Create(const TSharedRef<FTextLayout>& /*TextLayout*/, const FTextRunParseResults& RunParseResult,
		const FString& OriginalText, const TSharedRef<FString>& InOutModelText, const ISlateStyle* /*Style*/) override
	{
		FString Word = Style.FallbackWord;
		FString Tip;
		for (const TPair<FString, FTextRange>& Pair : RunParseResult.MetaData) {
			const FString Value = OriginalText.Mid(Pair.Value.BeginIndex, Pair.Value.EndIndex - Pair.Value.BeginIndex);
			if (Pair.Key == TEXT("w")) {
				Word = Value;
			} else if (Pair.Key == TEXT("tip")) {
				Tip = Value;
			}
		}
		if (Word.IsEmpty()) {
			Word = Style.FallbackWord;
		}
		return CreateGlossaryHyperlinkRun(RunParseResult, InOutModelText, Word, Tip, RunStyle);
	}

private:
	FTacticsHighlightStyle Style;
	FTextBlockStyle RunStyle;
};

FString FormatStockpileLabel(const int32 Remaining, const int32 Max, const bool bHasSlash, const bool bUsed)
{
	FString Label = bHasSlash
		? FString::Printf(TEXT("Stockpile %d/%d"), Remaining, Max)
		: FString::Printf(TEXT("Stockpile %d"), Remaining);
	if (bUsed) {
		Label += TEXT(" used");
	}
	return Label;
}

// `<stockpile rem="2" max="3" slash="1" used="0" tip="..."/>` - counter stays out of `w` (SRichTextBlock breaks on spaces).
class FStockpileDecorator : public ITextDecorator
{
public:
	explicit FStockpileDecorator(const FTextBlockStyle& InDefaultStyle)
		: TermStyle(InDefaultStyle)
	{
		TermStyle.SetColorAndOpacity(GlossaryTermColor());
	}

	virtual bool Supports(const FTextRunParseResults& RunParseResult, const FString& /*Text*/) const override
	{
		return RunParseResult.Name.Equals(TEXT("stockpile"), ESearchCase::IgnoreCase);
	}

	virtual TSharedRef<ISlateRun> Create(const TSharedRef<FTextLayout>& TextLayout, const FTextRunParseResults& RunParseResult,
		const FString& OriginalText, const TSharedRef<FString>& InOutModelText, const ISlateStyle* /*Style*/) override
	{
		int32 Remaining = 0;
		int32 Max = 0;
		bool bHasSlash = false;
		bool bUsed = false;
		FString Tip;
		for (const TPair<FString, FTextRange>& Pair : RunParseResult.MetaData) {
			const FString Value = OriginalText.Mid(Pair.Value.BeginIndex, Pair.Value.EndIndex - Pair.Value.BeginIndex);
			if (Pair.Key == TEXT("rem")) {
				Remaining = FCString::Atoi(*Value);
			} else if (Pair.Key == TEXT("max")) {
				Max = FCString::Atoi(*Value);
			} else if (Pair.Key == TEXT("slash")) {
				bHasSlash = Value != TEXT("0");
			} else if (Pair.Key == TEXT("used")) {
				bUsed = Value != TEXT("0");
			} else if (Pair.Key == TEXT("tip")) {
				Tip = Value;
			}
		}
		const FString Word = FormatStockpileLabel(Remaining, Max, bHasSlash, bUsed);
		return CreateGlossaryHyperlinkRun(RunParseResult, InOutModelText, Word, Tip, TermStyle);
	}

private:
	FTextBlockStyle TermStyle;
};

bool IsBoostVerbWordBoundary(const FString& S, int32 Index, int32 WordLen)
{
	if (Index > 0) {
		const TCHAR Prev = S[Index - 1];
		if (FChar::IsAlnum(Prev) || Prev == TEXT('_')) {
			return false;
		}
	}
	const int32 After = Index + WordLen;
	if (After < S.Len()) {
		const TCHAR Next = S[After];
		if (FChar::IsAlnum(Next) || Next == TEXT('_')) {
			return false;
		}
	}
	return true;
}

bool MatchesBoostVerbAt(const FString& S, int32 Index)
{
	static constexpr int32 kLen = 6;
	if (Index + kLen > S.Len()) {
		return false;
	}
	if (FChar::ToLower(S[Index]) != TEXT('b')
		|| FChar::ToLower(S[Index + 1]) != TEXT('o')
		|| FChar::ToLower(S[Index + 2]) != TEXT('o')
		|| FChar::ToLower(S[Index + 3]) != TEXT('s')
		|| FChar::ToLower(S[Index + 4]) != TEXT('t')
		|| FChar::ToLower(S[Index + 5]) != TEXT('s')) {
		return false;
	}
	return IsBoostVerbWordBoundary(S, Index, kLen);
}

bool IsBoostVerbInsideRichTag(const FString& S, int32 Index)
{
	int32 Open = INDEX_NONE;
	for (int32 j = Index - 1; j >= 0; --j) {
		if (S[j] == TEXT('>')) {
			return false;
		}
		if (S[j] == TEXT('<')) {
			Open = j;
			break;
		}
	}
	if (Open == INDEX_NONE) {
		return false;
	}
	const FString Tag = S.Mid(Open + 1, Index - Open - 1);
	return Tag.StartsWith(TEXT("/")) || Tag.StartsWith(TEXT("icon")) || Tag.StartsWith(TEXT("boost"));
}

bool IsSpeedBraceToken(const FString& Token)
{
	const FString Upper = Token.ToUpper();
	return Upper == TEXT("CHANNELED") || Upper == TEXT("REFLEX") || Upper == TEXT("BLAZING");
}

bool IsPassiveBraceToken(const FString& Token)
{
	return Token.Equals(TEXT("PASSIVE"), ESearchCase::IgnoreCase);
}

bool IsMetadataBraceToken(const FString& Token)
{
	const FString Upper = Token.ToUpper();
	if (IsSpeedBraceToken(Token) || IsPassiveBraceToken(Token)) {
		return true;
	}
	if (Upper == TEXT("ADJACENT") || Upper == TEXT("ADJACENT_SELF")
		|| Upper == TEXT("RANGE") || Upper == TEXT("RANGE_SELF")) {
		return true;
	}
	return Upper == TEXT("O") || Upper == TEXT("G") || Upper == TEXT("T") || Upper == TEXT("R")
		|| Upper == TEXT("P") || Upper == TEXT("M") || Upper == TEXT("N") || Upper == TEXT("X")
		|| Upper == TEXT("ATTACK");
}

bool StripAttackCostFromQualifierList(FString& QualifierList)
{
	TArray<FString> Parts;
	QualifierList.ParseIntoArray(Parts, TEXT(","), true);
	bool bRemoved = false;
	TArray<FString> Kept;
	for (FString Part : Parts) {
		Part.TrimStartAndEndInline();
		const FString Lower = Part.ToLower();
		if (Lower == TEXT("attack") || Lower.Contains(TEXT("attack action"))) {
			bRemoved = true;
			continue;
		}
		if (!Part.IsEmpty()) {
			Kept.Add(Part);
		}
	}
	if (!bRemoved) {
		return false;
	}
	QualifierList = FString::Join(Kept, TEXT(", "));
	return true;
}

bool IsMetadataPart(const FString& Part)
{
	FString S = Part;
	S.TrimStartAndEndInline();
	if (S.IsEmpty()) {
		return false;
	}
	const FString Lower = S.ToLower();
	if (Lower == TEXT("adjacent") || Lower == TEXT("surrounding")) {
		return true;
	}
	int32 i = 0;
	while (i < S.Len()) {
		while (i < S.Len() && S[i] == TEXT(' ')) {
			++i;
		}
		FString Token;
		int32 Next = 0;
		if (!TryParseBraceToken(S, i, Token, Next)) {
			return false;
		}
		if (!IsMetadataBraceToken(Token)) {
			return false;
		}
		i = Next;
		if (const FString TokUpper = Token.ToUpper(); TokUpper == TEXT("RANGE") || TokUpper == TEXT("RANGE_SELF")) {
			while (i < S.Len() && S[i] == TEXT(' ')) {
				++i;
			}
			while (i < S.Len() && FChar::IsDigit(S[i])) {
				++i;
			}
		}
	}
	return true;
}

int32 FindAbilityStripStart(const FString& S, int32 From)
{
	static const TCHAR* Needles[] = {
		TEXT("{CHANNELED}, "), TEXT("{REFLEX}, "), TEXT("{BLAZING}, "), TEXT("{PASSIVE}, ")};
	int32 Best = INDEX_NONE;
	for (const TCHAR* Needle : Needles) {
		int32 SearchFrom = From;
		while (SearchFrom < S.Len()) {
			const int32 Idx = S.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Idx == INDEX_NONE) {
				break;
			}
			const bool bAtStart = (Idx == 0);
			const bool bAfterBreak = Idx > 0 && (S[Idx - 1] == TEXT('\n')
				|| (S[Idx - 1] == TEXT(' ') && Idx >= 2 && S[Idx - 2] == TEXT('.')));
			const bool bAfterBraceToken = Idx > 0 && S[Idx - 1] == TEXT(' ')
				&& Idx >= 2 && S[Idx - 2] == TEXT('}');
			if (bAtStart || bAfterBreak || (Idx > 0 && S[Idx - 1] == TEXT('.')) || bAfterBraceToken) {
				if (Best == INDEX_NONE || Idx < Best) {
					Best = Idx;
				}
				break;
			}
			SearchFrom = Idx + 1;
		}
	}
	return Best;
}

int32 FindNameEffectColon(const FString& S, int32 From)
{
	int32 Depth = 0;
	for (int32 i = From; i < S.Len(); ++i) {
		const TCHAR Ch = S[i];
		if (Ch == TEXT('{')) {
			++Depth;
		} else if (Ch == TEXT('}')) {
			Depth = FMath::Max(0, Depth - 1);
		} else if (Ch == TEXT(':') && Depth == 0) {
			return i;
		}
	}
	return INDEX_NONE;
}

void NormalizeMetadataStripPart(FString& Part)
{
	Part.TrimStartAndEndInline();
	int32 RangeIdx = Part.Find(TEXT("{RANGE_SELF}"), ESearchCase::IgnoreCase);
	int32 PrefixLen = 12;
	if (RangeIdx == INDEX_NONE) {
		RangeIdx = Part.Find(TEXT("{RANGE}"), ESearchCase::IgnoreCase);
		PrefixLen = 7;
	}
	if (RangeIdx != INDEX_NONE) {
		int32 After = RangeIdx + PrefixLen;
		while (After < Part.Len() && Part[After] == TEXT(' ')) {
			Part.RemoveAt(After, 1);
		}
	}
}

TArray<FString> SplitMetadataStripPartsInternal(const FString& MetadataStrip)
{
	TArray<FString> Parts;
	MetadataStrip.ParseIntoArray(Parts, TEXT(", "), true);
	TArray<FString> Out;
	for (FString Part : Parts) {
		NormalizeMetadataStripPart(Part);
		if (!Part.IsEmpty()) {
			Out.Add(Part);
		}
	}
	return Out;
}

FString FormatMetadataPillTextInternal(const FString& MetadataStrip)
{
	FString Out;
	for (const FString& Part : SplitMetadataStripPartsInternal(MetadataStrip)) {
		if (!Out.IsEmpty()) {
			Out += TEXT(" \u00b7 ");
		}
		Out += Part;
	}
	return Out;
}

FCardRulesLayout ParseCardRulesLayoutInternal(const FString& In)
{
	FCardRulesLayout Layout;
	FString S = In;
	S.TrimStartAndEndInline();
	if (S.IsEmpty()) {
		return Layout;
	}

	int32 StripStart = FindAbilityStripStart(S, 0);
	if (StripStart == INDEX_NONE) {
		Layout.Preamble = S;
		return Layout;
	}

	Layout.Preamble = S.Left(StripStart);
	Layout.Preamble.TrimEndInline();

	while (StripStart != INDEX_NONE) {
		const int32 Colon = FindNameEffectColon(S, StripStart);
		if (Colon == INDEX_NONE) {
			break;
		}

		FCardRulesAbilityBlock Block;
		FString Header = S.Mid(StripStart, Colon - StripStart);
		Header.TrimStartAndEndInline();
		TArray<FString> HeaderParts;
		Header.ParseIntoArray(HeaderParts, TEXT(", "), true);
		if (HeaderParts.Num() >= 1) {
			Block.MetadataStrip = HeaderParts[0];
			int32 NameStartIdx = 1;
			while (NameStartIdx < HeaderParts.Num() - 1 && IsMetadataPart(HeaderParts[NameStartIdx])) {
				Block.MetadataStrip += TEXT(", ") + HeaderParts[NameStartIdx];
				++NameStartIdx;
			}
			for (const FString& Part : SplitMetadataStripPartsInternal(Block.MetadataStrip)) {
				FString Token;
				int32 Next = 0;
				if (TryParseBraceToken(Part, 0, Token, Next) && IsPassiveBraceToken(Token)) {
					Block.bIsPassive = true;
					break;
				}
			}
			for (int32 pi = NameStartIdx; pi < HeaderParts.Num(); ++pi) {
				if (!Block.Name.IsEmpty()) {
					Block.Name += TEXT(", ");
				}
				Block.Name += HeaderParts[pi];
			}
		}

		int32 EffectEnd = S.Len();
		const int32 NextStrip = FindAbilityStripStart(S, Colon + 1);
		if (NextStrip != INDEX_NONE) {
			EffectEnd = NextStrip;
		}
		Block.Effect = S.Mid(Colon + 1, EffectEnd - Colon - 1);
		Block.Effect.TrimStartAndEndInline();
		Block.Name.TrimStartAndEndInline();
		if (!Block.Name.IsEmpty()) {
			Layout.Abilities.Add(Block);
		}

		StripStart = NextStrip;
	}

	if (Layout.Abilities.IsEmpty()) {
		Layout.Preamble = S;
	}
	return Layout;
}

}  // namespace

const FSlateBrush* SolidPopupPanelBorderImage()
{
	return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
}

FLinearColor SolidPopupPanelFillColor(const float Alpha)
{
	return FLinearColor(0.f, 0.f, 0.f, Alpha);
}

TSharedPtr<IToolTip> MakeGlossaryHoverToolTip(const FString& Tip)
{
	return MakeGlossaryHoverToolTipImpl(Tip);
}

FString PassiveTypeMarkup(bool bAura)
{
	const TCHAR* LabelKey = bAura ? TEXT("aura") : TEXT("passive");
	return FString::Printf(TEXT("<icon i=\"ui/passive/passive\" f=\"%s\"/>"), LabelKey);
}

FString IconMarkup(const FString& Token, const bool bCompactIcon)
{
	const FString ArtId = IconArtIdForToken(Token);
	if (ArtId.IsEmpty()) {
		// Not a known icon - emit the literal token, escaped.
		FString Out;
		AppendEscaped(Out, TEXT('{'));
		Out += EscapeAttr(Token);
		AppendEscaped(Out, TEXT('}'));
		return Out;
	}
	if (bCompactIcon) {
		return FString::Printf(TEXT("<icon i=\"%s\" f=\"%s\" c=\"1\"/>"), *EscapeAttr(ArtId), *EscapeAttr(Token));
	}
	return FString::Printf(TEXT("<icon i=\"%s\" f=\"%s\"/>"), *EscapeAttr(ArtId), *EscapeAttr(Token));
}

bool TryStripLeadingSpellSpeedMetadata(FString& InOutRules, FString& OutMetadataStrip)
{
	OutMetadataStrip.Reset();
	FString S = InOutRules;
	S.TrimStartAndEndInline();
	if (S.IsEmpty() || S[0] != TEXT('{')) {
		return false;
	}
	FString Token;
	int32 Next = 0;
	if (!TryParseBraceToken(S, 0, Token, Next) || !IsSpeedBraceToken(Token)) {
		return false;
	}
	OutMetadataStrip = FString::Printf(TEXT("{%s}"), *Token.ToUpper());
	int32 After = Next;
	while (After < S.Len()) {
		const TCHAR Ch = S[After];
		if (Ch == TEXT(' ') || Ch == TEXT('.') || Ch == TEXT(',')) {
			++After;
			continue;
		}
		break;
	}
	InOutRules = S.Mid(After);
	InOutRules.TrimStartInline();
	return true;
}

FString ConvertSpeedAnnotations(const FString& In)
{
	FString S = In;

	// Leading spell speed: "Reflex. ..." / "Channeled. ..." / "Blazing. ..." -> icon at the very front.
	if (S.StartsWith(TEXT("Reflex."), ESearchCase::IgnoreCase)) {
		S = FString(TEXT("{REFLEX}")) + S.Mid(7);
	} else if (S.StartsWith(TEXT("Channeled."), ESearchCase::IgnoreCase)) {
		S = FString(TEXT("{CHANNELED}")) + S.Mid(10);
	} else if (S.StartsWith(TEXT("Blazing."), ESearchCase::IgnoreCase)) {
		S = FString(TEXT("{BLAZING}")) + S.Mid(8);
	}

	// Parenthetical ability speed annotations: "Name {cost} (slow): ..." / "(fast, attack)" become a
	// speed icon moved to the START of the ability (no parentheses), keeping any extra qualifier.
	// Only matches a speed word right after "(", so ability names ("Overcharge Burst") and phrases
	// ("energy burst") are untouched.
	while (true) {
		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		const TCHAR* Token = nullptr;
		FString Extra;
		for (int32 i = 0; i < S.Len(); ++i) {
			if (S[i] != TEXT('(')) {
				continue;
			}
			const int32 C = S.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i + 1);
			if (C == INDEX_NONE) {
				break;
			}
			const FString Inside = S.Mid(i + 1, C - i - 1).TrimStartAndEnd();
			const FString Lower = Inside.ToLower();
			int32 SpeedLen = 0;
			if (Lower.StartsWith(TEXT("reflex")))      { Token = TEXT("{REFLEX}");    SpeedLen = 6; }
			else if (Lower.StartsWith(TEXT("channeled"))) { Token = TEXT("{CHANNELED}"); SpeedLen = 9; }
			else if (Lower.StartsWith(TEXT("blazing"))) { Token = TEXT("{BLAZING}");  SpeedLen = 7; }
			if (Token) {
				Open = i;
				Close = C;
				FString Rest = Inside.Mid(SpeedLen).TrimStartAndEnd();
				Rest.RemoveFromStart(TEXT(","));
				Extra = Rest.TrimStartAndEnd();
				break;
			}
		}
		if (Open == INDEX_NONE) {
			break;
		}
		// Ability start = just after the previous sentence boundary ". " before the annotation.
		int32 AbilityStart = 0;
		const FString Prefix = S.Left(Open);
		const int32 Dot = Prefix.Find(TEXT(". "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Dot != INDEX_NONE) {
			AbilityStart = Dot + 2;
		}
		int32 RemoveStart = Open;
		if (RemoveStart > 0 && S[RemoveStart - 1] == TEXT(' ')) {
			--RemoveStart;  // also drop the space before "("
		}

		// Pull a "range N" qualifier out of the extra into a {RANGE}N icon for the strip.
		FString RangeTok;
		FString OtherExtra = Extra;
		{
			const int32 Ri = OtherExtra.ToLower().Find(TEXT("range"));
			if (Ri != INDEX_NONE) {
				int32 j = Ri + 5;
				while (j < OtherExtra.Len() && !FChar::IsDigit(OtherExtra[j])) { ++j; }
				int32 k = j;
				while (k < OtherExtra.Len() && FChar::IsDigit(OtherExtra[k])) { ++k; }
				if (k > j) {
					RangeTok = FString(TEXT("{RANGE}")) + OtherExtra.Mid(j, k - j);
					OtherExtra = OtherExtra.Left(Ri) + OtherExtra.Mid(k);
				}
			}
			OtherExtra.TrimStartAndEndInline();
			OtherExtra.RemoveFromStart(TEXT(","));
			OtherExtra.RemoveFromEnd(TEXT(","));
			OtherExtra.TrimStartAndEndInline();
		}

		// Split "Name {cost}" -> move the trailing energy pips in front of the name.
		FString NamePart = S.Mid(AbilityStart, RemoveStart - AbilityStart);
		NamePart.TrimEndInline();
		FString CostStr;
		while (NamePart.EndsWith(TEXT("}"))) {
			const int32 OpenBrace = NamePart.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (OpenBrace == INDEX_NONE) {
				break;
			}
			CostStr = NamePart.Mid(OpenBrace) + CostStr;
			NamePart = NamePart.Left(OpenBrace);
			NamePart.TrimEndInline();
		}

		if (StripAttackCostFromQualifierList(OtherExtra)) {
			CostStr += TEXT("{ATTACK}");
		}

		// Strip order (Option A): speed, range, cost, then the name - comma-separated.
		FString Strip = Token;
		if (!RangeTok.IsEmpty()) { Strip += TEXT(", ") + RangeTok; }
		if (!CostStr.IsEmpty())  { Strip += TEXT(", ") + CostStr; }

		const FString Before = S.Left(AbilityStart);
		const FString After = S.Mid(Close + 1);
		const FString ExtraStr = OtherExtra.IsEmpty() ? FString() : (FString(TEXT(" (")) + OtherExtra + TEXT(")"));
		S = Before + Strip + TEXT(", ") + NamePart + ExtraStr + After;
	}
	return S;
}

FString FormatMetadataPillText(const FString& MetadataStrip)
{
	return FormatMetadataPillTextInternal(MetadataStrip);
}

constexpr float kAttackCostChipScale = 0.72f;
constexpr float kAttackCostChipMinSize = 16.f;

void SplitAbilityCostTokens(FString CostTokens, FString& OutEnergyTokens, bool& bOutAttackCost)
{
	OutEnergyTokens = MoveTemp(CostTokens);
	bOutAttackCost = false;
	int32 Pos = 0;
	while (Pos < OutEnergyTokens.Len()) {
		FString Token;
		int32 Next = 0;
		if (!TryParseBraceToken(OutEnergyTokens, Pos, Token, Next)) {
			break;
		}
		if (Token.ToUpper() == TEXT("ATTACK")) {
			bOutAttackCost = true;
			OutEnergyTokens = OutEnergyTokens.Left(Pos) + OutEnergyTokens.Mid(Next);
			OutEnergyTokens.TrimStartAndEndInline();
			Pos = 0;
			continue;
		}
		Pos = Next;
	}
}

TSharedRef<SWidget> BuildAbilityCostRowWidget(const FString& CostTokens, const int32 FontSize, const FLinearColor& TextColor)
{
	FString EnergyTokens;
	bool bAttackCost = false;
	SplitAbilityCostTokens(CostTokens, EnergyTokens, bAttackCost);

	const int32 EnergyIconSize = FMath::Max(21, FontSize + 14);
	const float AttackIconDim = FMath::Max(kAttackCostChipMinSize, static_cast<float>(EnergyIconSize) * kAttackCostChipScale);

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (!EnergyTokens.IsEmpty()) {
		Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SRichTextBlock)
					.TextStyle(&EnergyTextStyle(FontSize, TextColor))
					.DecoratorStyleSet(&FCoreStyle::Get())
					.Decorators(MakeEnergyDecorators(FontSize, ESpeedTooltipSubject::Ability))
					.Text(FText::FromString(MarkupEnergyTokens(EnergyTokens)))
			];
	}
	if (bAttackCost) {
		if (!EnergyTokens.IsEmpty()) {
			const FLinearColor SeparatorColor(0.55f, 0.58f, 0.62f, 1.f);
			Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(5.f, 0.f)
				[
					SNew(SBox)
						.HeightOverride(static_cast<float>(EnergyIconSize))
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("\u00b7")))
								.Font(DefaultFont(TEXT("Bold"), FontSize))
								.ColorAndOpacity(SeparatorColor)
								.Justification(ETextJustify::Center)
						]
				];
		}
		const FVector2D AttackBrushSize(AttackIconDim, AttackIconDim);
		const FSlateBrush* AttackBrush = TacticsCardArtUi::GetCardArtBrush(TEXT("ui/actions/attack_ready"), AttackBrushSize);
		TSharedRef<SWidget> AttackWidget = AttackBrush
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(AttackBrush))
			: StaticCastSharedRef<SWidget>(SNew(STextBlock)
					.Text(FText::FromString(TEXT("{ATTACK}")))
					.Font(EnergyTextStyle(FontSize, TextColor).Font));
		Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
					.WidthOverride(AttackIconDim)
					.HeightOverride(static_cast<float>(EnergyIconSize))
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.ToolTipText(FText::FromString(TEXT("Costs attack action")))
					[
						AttackWidget
					]
			];
	}
	return Row;
}

bool MetadataStripPartIsAbilityCost(const FString& Part)
{
	FString S = Part;
	S.TrimStartAndEndInline();
	int32 i = 0;
	bool bHasEnergyOrAttack = false;
	while (i < S.Len()) {
		FString Token;
		int32 Next = 0;
		if (!TryParseBraceToken(S, i, Token, Next)) {
			return false;
		}
		const FString Upper = Token.ToUpper();
		if (Upper == TEXT("ATTACK")
			|| Upper == TEXT("O") || Upper == TEXT("G") || Upper == TEXT("T") || Upper == TEXT("R")
			|| Upper == TEXT("P") || Upper == TEXT("M") || Upper == TEXT("N") || Upper == TEXT("X")) {
			bHasEnergyOrAttack = true;
		} else {
			return false;
		}
		i = Next;
	}
	return bHasEnergyOrAttack;
}

TSharedRef<SWidget> BuildMetadataPillWidget(const FString& MetadataStrip, int32 FontSize, const FLinearColor& TextColor)
{
	const TArray<FString> Parts = SplitMetadataStripPartsInternal(MetadataStrip);
	const int32 IconSize = FMath::Max(21, FontSize + 14);
	const FLinearColor SeparatorColor(0.55f, 0.58f, 0.62f, 1.f);

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	bool bFirst = true;
	for (const FString& Part : Parts) {
		if (!bFirst) {
			Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(5.f, 0.f)
				[
					SNew(SBox)
						.HeightOverride(static_cast<float>(IconSize))
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("\u00b7")))
								.Font(DefaultFont(TEXT("Bold"), FontSize))
								.ColorAndOpacity(SeparatorColor)
								.Justification(ETextJustify::Center)
						]
				];
		}
		bFirst = false;
		const bool bCostPart = MetadataStripPartIsAbilityCost(Part);
		Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				bCostPart
					? StaticCastSharedRef<SWidget>(BuildAbilityCostRowWidget(Part, FontSize, TextColor))
					: StaticCastSharedRef<SWidget>(SNew(SBox)
						.HeightOverride(static_cast<float>(IconSize))
						.VAlign(VAlign_Center)
						[
							SNew(SRichTextBlock)
								.TextStyle(&EnergyTextStyle(FontSize, TextColor))
								.DecoratorStyleSet(&FCoreStyle::Get())
								.Decorators(MakeEnergyDecorators(FontSize, ESpeedTooltipSubject::Ability))
								.Text(FText::FromString(MarkupEnergyTokens(Part)))
						])
			];
	}
	return Row;
}

FCardRulesLayout ParseCardRulesLayout(const FString& In)
{
	return ParseCardRulesLayoutInternal(In);
}

void SortCardRulesLayoutPassivesBeforeAbilities(FCardRulesLayout& Layout)
{
	TArray<FCardRulesAbilityBlock> Passives;
	TArray<FCardRulesAbilityBlock> Actives;
	Passives.Reserve(Layout.Abilities.Num());
	Actives.Reserve(Layout.Abilities.Num());
	for (const FCardRulesAbilityBlock& Block : Layout.Abilities) {
		if (Block.bIsPassive) {
			Passives.Add(Block);
		} else {
			Actives.Add(Block);
		}
	}
	Layout.Abilities.Reset();
	Layout.Abilities.Append(Passives);
	Layout.Abilities.Append(Actives);
}

FString FormatCardRulesLayout(const FCardRulesLayout& Layout)
{
	FString Out = Layout.Preamble;
	for (const FCardRulesAbilityBlock& Block : Layout.Abilities) {
		if (Block.Name.IsEmpty()) {
			continue;
		}
		if (!Out.IsEmpty()) {
			Out += TEXT(". ");
		}
		if (!Block.MetadataStrip.IsEmpty()) {
			Out += Block.MetadataStrip + TEXT(", ");
		}
		Out += Block.Name;
		if (!Block.Effect.IsEmpty()) {
			Out += TEXT(": ") + Block.Effect;
		}
	}
	return Out;
}

FString BuildAbilityMetadataStrip(const FString& SpeedTag, const FString& RangeToken, const FString& CostBraceTokens)
{
	FString Strip;
	if (!SpeedTag.IsEmpty()) {
		Strip = FString::Printf(TEXT("{%s}"), *SpeedTag.ToUpper());
	}
	if (!RangeToken.IsEmpty()) {
		if (!Strip.IsEmpty()) {
			Strip += TEXT(", ");
		}
		Strip += RangeToken;
	}
	if (!CostBraceTokens.IsEmpty() && !CostBraceTokens.Equals(TEXT("0"))) {
		if (!Strip.IsEmpty()) {
			Strip += TEXT(", ");
		}
		Strip += CostBraceTokens;
	}
	return Strip;
}

FString BuildAbilityButtonStrip(const FString& SpeedTag, const FString& CostBraceTokens, const FString& RangeToken,
	const FString& AbilityName)
{
	FString Strip = FString::Printf(TEXT("{%s}"), *SpeedTag.ToUpper());
	if (!RangeToken.IsEmpty()) {
		Strip += TEXT(", ") + RangeToken;
	}
	if (!CostBraceTokens.IsEmpty() && !CostBraceTokens.Equals(TEXT("0"))) {
		Strip += TEXT(", ") + CostBraceTokens;
	}
	Strip += TEXT(", ") + AbilityName;
	return Strip;
}

FString StripLeadingSpeedWordPrefix(const FString& In)
{
	FString S = In.TrimStart();
	static const TCHAR* Prefixes[] = {TEXT("Reflex:"), TEXT("Channeled:"), TEXT("Blazing:")};
	for (const TCHAR* Prefix : Prefixes) {
		if (S.StartsWith(Prefix, ESearchCase::IgnoreCase)) {
			S = S.Mid(FCString::Strlen(Prefix)).TrimStart();
			break;
		}
	}
	return S;
}

FString ConvertStatWords(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len() + 16);
	const int32 N = In.Len();
	auto ParseNum = [&](int32 i) -> int32 {
		int32 j = i;
		while (j < N && FChar::IsDigit(In[j])) { ++j; }
		if (j > i && j < N && (In[j] == TEXT('-') || In[j] == TCHAR(0x2013) || In[j] == TCHAR(0x2014))) {
			int32 k = j + 1;
			while (k < N && FChar::IsDigit(In[k])) { ++k; }
			if (k > j + 1) { j = k; }
		}
		return j;
	};
	struct FStatKw { const TCHAR* Word; int32 Len; const TCHAR* Token; };
	static const FStatKw Kws[] = {
		{ TEXT("ranged"),   6, TEXT("{RANGED}") },   // before "range"
		{ TEXT("melee"),    5, TEXT("{MELEE}") },
		{ TEXT("movement"), 8, TEXT("{MOVE}") },
		{ TEXT("range"),    5, TEXT("{RANGE}") },
	};
	int32 i = 0;
	while (i < N) {
		// Leave existing brace tokens untouched (e.g. {ADJACENT} must not match the "adjacent" keyword inside).
		FString ExistingToken;
		int32 TokenEnd = 0;
		if (TryParseBraceToken(In, i, ExistingToken, TokenEnd)) {
			Out += In.Mid(i, TokenEnd - i);
			i = TokenEnd;
			continue;
		}
		const TCHAR Prev = (i > 0) ? In[i - 1] : TEXT(' ');
		bool bMatched = false;
		if (!FChar::IsAlpha(Prev)) {
			// "Melee 3-5" / "Ranged 2-3" / "movement 3" / "range 3" -> icon + the number.
			for (const FStatKw& Kw : Kws) {
				if (i + Kw.Len > N) { continue; }
				bool bEq = true;
				for (int32 t = 0; t < Kw.Len; ++t) {
					if (FChar::ToLower(In[i + t]) != Kw.Word[t]) { bEq = false; break; }
				}
				if (!bEq || (i + Kw.Len < N && FChar::IsAlpha(In[i + Kw.Len]))) { continue; }
				int32 NumStart = i + Kw.Len;
				while (NumStart < N && In[NumStart] == TEXT(' ')) { ++NumStart; }
				const int32 NumEnd = ParseNum(NumStart);
				if (NumEnd > NumStart) {
					Out += Kw.Token;
					Out += TEXT(" ");
					Out += In.Mid(NumStart, NumEnd - NumStart);
					i = NumEnd;
					bMatched = true;
					break;
				}
			}
			// "N HP" -> life icon, but only as a stat-line entry (preceded by , . or start) so heal
			// amounts like "for 4 HP" and "+1 max HP" are left as text.
			if (!bMatched && FChar::IsDigit(In[i])) {
				const int32 NumEnd = ParseNum(i);
				int32 p = NumEnd;
				while (p < N && In[p] == TEXT(' ')) { ++p; }
				int32 b = i - 1;
				while (b >= 0 && In[b] == TEXT(' ')) { --b; }
				const bool bStatCtx = (b < 0) || In[b] == TEXT(',') || In[b] == TEXT('.');
				if (bStatCtx && p + 1 < N
					&& (In[p] == TEXT('H') || In[p] == TEXT('h'))
					&& (In[p + 1] == TEXT('P') || In[p + 1] == TEXT('p'))
					&& (p + 2 >= N || !FChar::IsAlpha(In[p + 2]))) {
					Out += TEXT("{LIFE}");
					Out += In.Mid(i, NumEnd - i);
					i = p + 2;
					bMatched = true;
				}
			}
		}
		if (bMatched) { continue; }
		Out.AppendChar(In[i]);
		++i;
	}
	return Out;
}

FString StripLeadingStatTokens(const FString& In)
{
	const int32 N = In.Len();
	auto IsSep = [](TCHAR C) { return C == TEXT(' ') || C == TEXT(',') || C == TEXT('.') || C == TEXT('-') || C == TCHAR(0x2013) || C == TCHAR(0x2014); };
	auto IsStatBraceName = [](const FString& Name) {
		return Name == TEXT("LIFE") || Name == TEXT("MELEE") || Name == TEXT("RANGED")
			|| Name == TEXT("RANGE") || Name == TEXT("MOVE") || Name == TEXT("MOVEMENT")
			|| Name == TEXT("ARMOR");
	};
	// Returns the index past a stat element starting at j, or -1 if j doesn't start one.
	auto MatchStatElement = [&](int32 j) -> int32 {
		if (j >= N) { return -1; }
		auto SkipNum = [&](int32 k) -> int32 {
			while (k < N && In[k] == TEXT(' ')) { ++k; }
			int32 s = k;
			while (k < N && (FChar::IsDigit(In[k]) || In[k] == TEXT('-') || In[k] == TCHAR(0x2013) || In[k] == TCHAR(0x2014))) { ++k; }
			return k > s ? k : s;
		};
		if (In[j] == TEXT('{')) {
			const int32 C = In.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, j + 1);
			if (C == INDEX_NONE) { return -1; }
			const FString Name = In.Mid(j + 1, C - j - 1).ToUpper();
			if (IsStatBraceName(Name)) {
				return SkipNum(C + 1);
			}
			return -1;  // speed or other token -> not a stat
		}
		if (In.Mid(j, 5).Equals(TEXT("Armor"), ESearchCase::IgnoreCase)) {
			int32 k = j + 5;
			while (k < N && In[k] == TEXT(' ')) { ++k; }
			if (k < N && FChar::IsDigit(In[k])) { return SkipNum(j + 5); }
		}
		if (In.Mid(j, 3).Equals(TEXT("2x2"), ESearchCase::IgnoreCase) || In.Mid(j, 3) == TEXT("2×2")) {
			return j + 3;
		}
		if (In.Mid(j, 10).Equals(TEXT("Large Unit"), ESearchCase::IgnoreCase)) {
			return j + 10;
		}
		return -1;
	};

	int32 StatStart = INDEX_NONE;
	for (int32 Scan = 0; Scan < N; ++Scan) {
		if (In[Scan] != TEXT('{')) {
			continue;
		}
		const int32 C = In.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Scan + 1);
		if (C == INDEX_NONE) {
			break;
		}
		if (IsStatBraceName(In.Mid(Scan + 1, C - Scan - 1).ToUpper())) {
			StatStart = Scan;
			break;
		}
	}
	if (StatStart == INDEX_NONE) {
		return In;
	}

	FString Prefix = In.Left(StatStart);
	Prefix.TrimEndInline();

	int32 i = StatStart;
	while (true) {
		int32 j = i;
		while (j < N && IsSep(In[j])) { ++j; }
		const int32 End = MatchStatElement(j);
		if (End < 0) { break; }
		i = End;
	}
	FString Rest = In.Mid(i);
	Rest.TrimStartInline();
	int32 LeadSkip = 0;
	while (LeadSkip < Rest.Len() && (Rest[LeadSkip] == TEXT('.') || Rest[LeadSkip] == TEXT(',') || Rest[LeadSkip] == TEXT(';'))) {
		++LeadSkip;
	}
	if (LeadSkip > 0) {
		Rest = Rest.Mid(LeadSkip);
		Rest.TrimStartInline();
	}

	if (Prefix.IsEmpty()) {
		return Rest;
	}
	if (Rest.IsEmpty()) {
		return Prefix;
	}
	FString Out = Prefix;
	if (!Out.EndsWith(TEXT(".")) && !Out.EndsWith(TEXT(":"))) {
		Out += TEXT(". ");
	} else if (!Out.EndsWith(TEXT(" "))) {
		Out += TEXT(" ");
	}
	Out += Rest;
	return Out;
}

FString ExpandTargetingTokensToWords(const FString& In)
{
	FString S = In;
	S.ReplaceInline(TEXT("{ADJACENT_SELF}"), TEXT("adjacent"));
	S.ReplaceInline(TEXT("{ADJACENT}"), TEXT("adjacent"));
	return S;
}

FString ExpandTargetingTokensInAbilityProse(const FString& In)
{
	FString S = ExpandTargetingTokensToWords(In);
	// Legacy data may use {RANGE}1 for 8-way surrounding; header already shows reach icons.
	int32 Pos = 0;
	while (Pos < S.Len()) {
		if (S[Pos] != TEXT('{') || !S.Mid(Pos, 7).Equals(TEXT("{RANGE}"), ESearchCase::CaseSensitive)) {
			++Pos;
			continue;
		}
		int32 After = Pos + 7;
		while (After < S.Len() && S[After] == TEXT(' ')) {
			++After;
		}
		int32 NumEnd = After;
		while (NumEnd < S.Len() && FChar::IsDigit(S[NumEnd])) {
			++NumEnd;
		}
		if (NumEnd > After) {
			const FString Num = S.Mid(After, NumEnd - After);
			const FString Word = (Num == TEXT("1")) ? TEXT("surrounding") : FString::Printf(TEXT("within %s tiles"), *Num);
			S = S.Left(Pos) + Word + S.Mid(NumEnd);
			Pos += Word.Len();
		} else {
			Pos = After;
		}
	}
	return S;
}

FString PrepareCardRulesTextForDisplay(const FString& In)
{
	FString S = ConvertSpeedAnnotations(In);
	S = ConvertStatWords(S);
	S = ExpandTargetingTokensToWords(S);
	return S;
}

FString CapitalizeDescriptionLineStarts(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());
	bool bLineStart = true;
	for (const TCHAR Ch : In) {
		TCHAR OutCh = Ch;
		if (bLineStart && FChar::IsLower(OutCh)) {
			OutCh = FChar::ToUpper(OutCh);
		}
		Out.AppendChar(OutCh);
		if (Ch == TEXT('\n')) {
			bLineStart = true;
		} else if (!FChar::IsWhitespace(Ch)) {
			bLineStart = false;
		}
	}
	return Out;
}

FString PrettyRulesText(const FString& In)
{
	FString Trimmed = In;
	Trimmed.TrimStartAndEndInline();
	FString Out;
	Out.Reserve(Trimmed.Len() + 16);
	const int32 N = Trimmed.Len();
	for (int32 i = 0; i < N; ++i) {
		const TCHAR Ch = Trimmed[i];

		// Break after a sentence end ('.' '!' '?') or clause separator (';') followed by a space.
		// When breaking on '.', drop the period - the line break is the separator visually.
		// '!' and '?' are kept. Decimals like "2.5" are safe (not followed by whitespace).
		const bool bPeriod = (Ch == TEXT('.'));
		const bool bBreakable = bPeriod || Ch == TEXT('!') || Ch == TEXT('?') || Ch == TEXT(';');
		if (bBreakable && i + 1 < N && (Trimmed[i + 1] == TEXT(' ') || Trimmed[i + 1] == TEXT('\t'))) {
			int32 j = i + 1;
			while (j < N && (Trimmed[j] == TEXT(' ') || Trimmed[j] == TEXT('\t'))) {
				++j;
			}
			// Don't double up if the author already put a newline next, and don't end on a trailing break.
			if (j < N && Trimmed[j] != TEXT('\n')) {
				if (!bPeriod) { Out.AppendChar(Ch); }  // keep '!' '?' ';' but not '.'
				Out.AppendChar(TEXT('\n'));
			} else {
				if (!bPeriod) { Out.AppendChar(Ch); }
			}
			i = j - 1;
		} else {
			// Not a line-break trigger - keep the trailing period only when it ends the string.
			if (bPeriod && i + 1 >= N) {
				// trailing period at end: drop it
			} else {
				Out.AppendChar(Ch);
			}
		}
	}
	return Out;
}

FString FormatDescriptionProse(const FString& In)
{
	return CapitalizeDescriptionLineStarts(PrettyRulesText(In));
}

FString MarkupEnergyTokens(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len() + 16);
	const int32 N = In.Len();
	int32 i = 0;
	while (i < N) {
		FString Token;
		int32 Next = 0;
		if (TryParseBraceToken(In, i, Token, Next)) {
			// Glossary markers and other unknown brace tokens stay intact for MarkupKeywordMarkers.
			if (IsGlossaryMarkerBraceToken(Token)) {
				Out += In.Mid(i, Next - i);
				i = Next;
				continue;
			}
			const FString ArtId = IconArtIdForToken(Token);
			if (!ArtId.IsEmpty()) {
				if (Token.ToUpper() == TEXT("X")) {
					Out += FString::Printf(TEXT("<icon i=\"%s\" xcost=\"1\" f=\"X\"/>"), kNeutralArtId);
					i = Next;
					continue;
				}
				if (Token.ToUpper() == TEXT("ATTACK")) {
					Out += TEXT("<icon i=\"ui/actions/attack_ready\" f=\"ATTACK\" sm=\"1\"/>");
					i = Next;
					continue;
				}
				if (ArtId == kNeutralArtId && Token.ToUpper() == TEXT("N")) {
					// Collapse a run of consecutive {N} (optional whitespace between) into one numbered icon.
					int32 Count = 0;
					int32 Cur = i;
					FString T2;
					int32 Nx = 0;
					while (TryParseBraceToken(In, Cur, T2, Nx) && T2.ToUpper() == TEXT("N")) {
						++Count;
						Cur = Nx;
						while (Cur < N && FChar::IsWhitespace(In[Cur])) {
							++Cur;
						}
					}
					Out += FString::Printf(TEXT("<icon i=\"%s\" count=\"%d\" f=\"N\"/>"), kNeutralArtId, Count);
					i = Cur;
				} else if (const FString TokUpper = Token.ToUpper();
					TokUpper == TEXT("RANGE") || TokUpper == TEXT("RANGE_SELF")) {
					int32 NumStart = Next;
					while (NumStart < N && In[NumStart] == TEXT(' ')) {
						++NumStart;
					}
					int32 NumEnd = NumStart;
					while (NumEnd < N && FChar::IsDigit(In[NumEnd])) {
						++NumEnd;
					}
					if (NumEnd > NumStart) {
						const int32 RangeValue = FCString::Atoi(*In.Mid(NumStart, NumEnd - NumStart));
						Out += FString::Printf(TEXT("<icon i=\"%s\" v=\"%d\" f=\"%s\"/>"),
							*EscapeAttr(ArtId), RangeValue, *EscapeAttr(Token));
						i = NumEnd;
					} else {
						Out += FString::Printf(TEXT("<icon i=\"%s\" f=\"%s\"/>"), *EscapeAttr(ArtId), *EscapeAttr(Token));
						i = Next;
					}
				} else {
					Out += FString::Printf(TEXT("<icon i=\"%s\" f=\"%s\"/>"), *EscapeAttr(ArtId), *EscapeAttr(Token));
					i = Next;
				}
				continue;
			}
			Out += In.Mid(i, Next - i);
			i = Next;
			continue;
		}
		AppendEscaped(Out, In[i]);
		++i;
	}
	return Out;
}

FString MarkupDescriptionText(const FString& In, const TArray<FTacticsGlossaryNameBody>& GlossaryEntries,
	const bool bAdvancedGlossary)
{
	const FTacticsGlossaryMarkerLookup Lookup = BuildGlossaryMarkerLookup(GlossaryEntries);
	// Energy/icon tokens first; glossary markers last so <boost>/<glossary> tags are not escaped by AppendEscaped.
	return MarkupKeywordMarkers(MarkupEnergyTokens(In), Lookup, bAdvancedGlossary);
}

TArray<TSharedRef<ITextDecorator>> MakeEnergyDecorators(int32 FontSize, const ESpeedTooltipSubject SpeedSubject)
{
	const int32 IconSize = FMath::Max(21, FontSize + 14);
	const FTextBlockStyle DefaultStyle = EnergyTextStyle(FontSize, FLinearColor::White);
	TArray<TSharedRef<ITextDecorator>> Decorators;
	Decorators.Add(MakeShared<FInlineIconDecorator>(IconSize, DefaultStyle, SpeedSubject));
	// One decorator per registered highlight style (boost, glossary, …). To add a new
	// highlight, add an entry to TacticsHighlightStyles() in TacticsGlossaryMarkup.cpp.
	for (const FTacticsHighlightStyle& Style : TacticsHighlightStyles()) {
		Decorators.Add(MakeShared<FHighlightRunDecorator>(Style, DefaultStyle));
	}
	Decorators.Add(MakeShared<FStockpileDecorator>(DefaultStyle));
	return Decorators;
}

namespace
{
// Wraps a font file at an absolute path in a composite font, cached per path. Returns null if the
// file is missing.
TSharedPtr<const FCompositeFont> CompositeFontFromFile(const FString& Path)
{
	static TMap<FString, TSharedPtr<const FCompositeFont>> Cache;
	if (TSharedPtr<const FCompositeFont>* Found = Cache.Find(Path)) {
		return *Found;
	}
	TSharedPtr<const FCompositeFont> Composite;
	if (IFileManager::Get().FileExists(*Path)) {
		Composite = MakeShared<FStandaloneCompositeFont>(
			NAME_None, Path, EFontHinting::Auto, EFontLoadingPolicy::LazyLoad);
	}
	Cache.Add(Path, Composite);
	return Composite;
}
}  // namespace

FSlateFontInfo DefaultFont(const FString& Weight, int32 Size)
{
	// Maps a weight name to the Calibri file in the Windows system fonts folder.
	auto CalibriFileForWeight = [](const FString& W) -> const TCHAR* {
		if (W.Equals(TEXT("Bold"), ESearchCase::IgnoreCase))       { return TEXT("calibrib.ttf"); }
		if (W.Equals(TEXT("Italic"), ESearchCase::IgnoreCase))     { return TEXT("calibrii.ttf"); }
		if (W.Equals(TEXT("BoldItalic"), ESearchCase::IgnoreCase)) { return TEXT("calibriz.ttf"); }
		if (W.Equals(TEXT("Light"), ESearchCase::IgnoreCase))      { return TEXT("calibril.ttf"); }
		return TEXT("calibri.ttf");
	};
	const FString WinDir = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
	const FString Path = (WinDir.IsEmpty() ? FString(TEXT("C:/Windows")) : WinDir) / TEXT("Fonts") / CalibriFileForWeight(Weight);
	if (TSharedPtr<const FCompositeFont> Font = CompositeFontFromFile(Path)) {
		return FSlateFontInfo(Font, Size);
	}
	return FCoreStyle::GetDefaultFontStyle(*Weight, Size);
}

FSlateFontInfo DebugFont(int32 Size)
{
	return DefaultFont(TEXT("Regular"), Size);
}

FSlateFontInfo DesignFont(const FString& Weight, int32 Size)
{
	// Card text font. Swap the lambda below to trial a different typeface.
	// "-Lining" variants have digits 3/4/5/7/9 shifted to sit on the baseline (lining figures).
	auto FileForWeight = [](const FString& W) -> const TCHAR* {
		if (W.Equals(TEXT("Bold"), ESearchCase::IgnoreCase))        { return TEXT("LibreBaskerville-Bold.ttf"); }
		if (W.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))      { return TEXT("LibreBaskerville-Medium.ttf"); }
		if (W.Equals(TEXT("Light"), ESearchCase::IgnoreCase))       { return TEXT("LibreBaskerville-Regular.ttf"); }
		if (W.Equals(TEXT("Italic"), ESearchCase::IgnoreCase))      { return TEXT("LibreBaskerville-Italic.ttf"); }
		if (W.Equals(TEXT("BoldItalic"), ESearchCase::IgnoreCase))  { return TEXT("LibreBaskerville-BoldItalic.ttf"); }
		return TEXT("LibreBaskerville-Regular.ttf");
	};

	// A missing file falls back to the engine default font.
	const FString Path = FPaths::ProjectContentDir() / TEXT("TacticsData/fonts") / FileForWeight(Weight);
	if (TSharedPtr<const FCompositeFont> Font = CompositeFontFromFile(Path)) {
		return FSlateFontInfo(Font, Size);
	}
	return FCoreStyle::GetDefaultFontStyle(*Weight, Size);
}

::UFont* AbilityDamagePopupWorldFont()
{
	static TWeakObjectPtr<UFont> Cache;
	if (Cache.IsValid()) {
		return Cache.Get();
	}
	if (UFont* ContentFont = LoadObject<UFont>(
			nullptr, TEXT("/Game/TacticsData/fonts/LibreBaskervilleBoldDistanceField.LibreBaskervilleBoldDistanceField"))) {
		Cache = ContentFont;
		return ContentFont;
	}
	if (UFont* Fallback = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"))) {
		Cache = Fallback;
		return Fallback;
	}
	return nullptr;
}

::UFont* WorldDesignFont()
{
	return AbilityDamagePopupWorldFont();
}


const FTextBlockStyle& DefaultTextStyle(int32 FontSize, const FLinearColor& Color, const FString& Weight)
{
	// Calibri text-block style for SButton::TextStyle / SRichTextBlock (needs a process-lifetime pointer).
	static TMap<FString, FTextBlockStyle> Cache;
	const FString Key = FString::Printf(TEXT("%s_%d_%08x"), *Weight, FontSize, Color.ToFColor(true).ToPackedARGB());
	if (FTextBlockStyle* Found = Cache.Find(Key)) {
		return *Found;
	}
	FTextBlockStyle Style = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));
	Style.SetFont(DefaultFont(Weight, FontSize));
	Style.SetColorAndOpacity(Color);
	return Cache.Add(Key, Style);
}

const FTextBlockStyle& DesignTextStyle(int32 FontSize, const FLinearColor& Color, const FString& Weight)
{
	// Bespoke Serif text-block style (card text) for SRichTextBlock / SButton::TextStyle.
	static TMap<FString, FTextBlockStyle> Cache;
	const FString Key = FString::Printf(TEXT("%s_%d_%08x"), *Weight, FontSize, Color.ToFColor(true).ToPackedARGB());
	if (FTextBlockStyle* Found = Cache.Find(Key)) {
		return *Found;
	}
	FTextBlockStyle Style = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));
	Style.SetFont(DesignFont(Weight, FontSize));
	Style.SetColorAndOpacity(Color);
	return Cache.Add(Key, Style);
}

const FTextBlockStyle& EnergyTextStyle(int32 FontSize, const FLinearColor& Color)
{
	static TMap<FString, FTextBlockStyle> Cache;
	const FString Key = FString::Printf(TEXT("%d_%08x"), FontSize, Color.ToFColor(true).ToPackedARGB());
	if (FTextBlockStyle* Found = Cache.Find(Key)) {
		return *Found;
	}
	FTextBlockStyle Style = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));
	Style.SetFont(DesignFont(TEXT("Regular"), FontSize));
	Style.SetColorAndOpacity(Color);
	return Cache.Add(Key, Style);
}

}  // namespace TacticsCardText
