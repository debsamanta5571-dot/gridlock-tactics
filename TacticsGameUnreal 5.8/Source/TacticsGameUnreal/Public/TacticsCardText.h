#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "TacticsGlossaryMarkup.h"
#include "Widgets/IToolTip.h"
#include "Widgets/SWidget.h"

class ITextDecorator;

/**
 * Helpers for rendering card / ability rules text with inline icons.
 *
 * Generic brace-token icon system - a token `{X}` becomes an inline icon. Resolution order:
 *   1. Short alias (case-insensitive):
 *        {O} Orange  {G} Green  {T} Turquoise  {R} Red  {P} Purple  {M} Omni
 *        {N} Neutral (grey circle; consecutive {N} collapse into one circle showing the count)
 *        {CHANNELED} {REFLEX} {BLAZING} speed icons
 *        {PASSIVE} passive icon (electric-blue "Passive" / "Aura" label beside icon)
 *        {ATTACK_READY} {ATTACK_USED} {REACTION_READY} {REACTION_USED} action-availability chips
 *   2. Subpath form `{category/name}` -> art at ui/category/name.png
 *   3. Generic identifier `{name}` (>= 2 chars) -> art at ui/icons/name.png
 *   4. Anything else (e.g. single unknown {X}) stays as literal text.
 *
 * To add a new icon, just drop a PNG under Content/TacticsData/card_art/ui/... and reference it
 * with the matching token - no code change required. A missing art file falls back to the literal
 * token text. (Optionally add a short alias in IconArtIdForToken for frequently-used icons.)
 */
class UFont;

namespace TacticsCardText
{
/** Converts brace tokens into `<icon .../>` rich-text markup and escapes `&<>"` in the rest. */
FString MarkupEnergyTokens(const FString& In);

/** Description pipeline: `{KW:…}` / `{FX:…}` markers, energy/stat icons. Run `FormatDescriptionProse` first. */
FString MarkupDescriptionText(const FString& In, const TArray<FTacticsGlossaryNameBody>& GlossaryEntries,
	bool bAdvancedGlossary = false);

/**
 * Inserts aesthetic line breaks into a rules string so it reads as a list of clauses rather than a
 * dense block: each sentence (`.`/`!`/`?`) and clause (`;`) starts on its own line. Author-supplied
 * newlines are preserved. Run this before MarkupEnergyTokens.
 */
FString PrettyRulesText(const FString& In);

/** `PrettyRulesText` plus sentence-case at each line start (ability effect lines, clause breaks, etc.). */
FString FormatDescriptionProse(const FString& In);

/**
 * Converts authored speed annotations in rules text into speed-icon tokens: parenthetical
 * "(reflex)" / "(channeled)" / "(blazing)" and a leading "Reflex." / "Channeled." / "Blazing." Run before
 * MarkupEnergyTokens. Ability names ("Overcharge Burst") and words ("energy burst") are untouched.
 */
FString ConvertSpeedAnnotations(const FString& In);

/**
 * Option A ability button strip: "{SPEED}, {RANGE}n, {cost}, Name" as brace tokens for MarkupEnergyTokens.
 * Speed tokens also render a colored label after the icon (Channeled purple, Reflex gold, Blazing dark orange).
 * SpeedTag is "reflex" / "channeled" / "blazing"; CostBraceTokens is already brace-formatted ({O}, {N}, …).
 */
/** RangeToken is empty, `{ADJACENT}`, `{ADJACENT_SELF}`, `{RANGE}n`, or `{RANGE_SELF}n` (see AbilityCatalogRangeToken). */
FString BuildAbilityButtonStrip(const FString& SpeedTag, const FString& CostBraceTokens, const FString& RangeToken,
	const FString& AbilityName);

/** Live/static ability pill metadata: `{SPEED}[, {RANGE}n][, {cost}{ATTACK}]` (no ability name). */
FString BuildAbilityMetadataStrip(const FString& SpeedTag, const FString& RangeToken, const FString& CostBraceTokens);

/** Rich-text markup for the passive type chip (`bAura` → "Aura", else "Passive"). */
FString PassiveTypeMarkup(bool bAura);

/** One activated ability block parsed from Option A catalog strips (speed/range/cost, name, effect). */
struct FCardRulesAbilityBlock
{
	FString MetadataStrip;
	FString Name;
	FString Effect;
	/** True when MetadataStrip is a `{PASSIVE}` catalog strip (not an activated ability). */
	bool bIsPassive{false};
};

/** Preamble prose plus zero or more ability blocks for Option C card-detail layout. */
struct FCardRulesLayout
{
	FString Preamble;
	TArray<FCardRulesAbilityBlock> Abilities;
};

/**
 * Splits converted rules text (after ConvertSpeedAnnotations / ConvertStatWords) into preamble prose and
 * ability blocks. Ability strips are detected when `{CHANNELED}` / `{REFLEX}` / `{BLAZING}` is followed by `,`
 * (spell speed prefixes like `{REFLEX} Destroy` stay in the preamble).
 */
FCardRulesLayout ParseCardRulesLayout(const FString& In);

/** Builds Option A rules text from a parsed layout (preamble + ability/passive blocks). */
FString FormatCardRulesLayout(const FCardRulesLayout& Layout);

/** Moves all `{PASSIVE}` blocks before activated ability blocks (stable within each group). */
void SortCardRulesLayoutPassivesBeforeAbilities(FCardRulesLayout& Layout);

/** Option C metadata pill: comma-separated strip tokens become middle-dot-separated markup input. */
FString FormatMetadataPillText(const FString& MetadataStrip);

/** Option C metadata chip: horizontally laid-out icon groups with vertically centered dot separators. */
TSharedRef<SWidget> BuildMetadataPillWidget(const FString& MetadataStrip, int32 FontSize, const FLinearColor& TextColor);

/** Removes a leading "Reflex:" / "Channeled:" / "Blazing:" prefix from catalog description prose (icons show on the button). */
FString StripLeadingSpeedWordPrefix(const FString& In);

/**
 * Converts unit stat words in rules text into stat-icon tokens: "Melee 3-5" / "Ranged 2-3" /
 * "range 3" / "movement 3" -> the matching icon + number, and a stat-line "N HP" -> {LIFE}N.
 * Prose **adjacent** and **surrounding** are left as plain words (not icon tokens). Heal/buff amounts like
 * "for 4 HP" and "+1 max HP" are left as text. Run before ExpandTargetingTokensToWords / MarkupEnergyTokens.
 */
FString ConvertStatWords(const FString& In);

/** Replaces `{ADJACENT}` with "adjacent" in rules prose (not ability metadata pill headers). */
FString ExpandTargetingTokensToWords(const FString& In);

/** Ability pill body (name + effect): targeting as words, never `{ADJACENT}` / surrounding icons. */
FString ExpandTargetingTokensInAbilityProse(const FString& In);

/** Full description pipeline: speed/stat conversion then targeting words for prose-only surfaces. */
FString PrepareCardRulesTextForDisplay(const FString& In);

/**
 * Removes a leading run of unit stat tokens / words ({LIFE}/{MELEE}/{RANGED}/{RANGE}/{MOVE}/{ARMOR},
 * "Armor N", "2x2", "Large Unit") and separators from rules text, so the stats aren't duplicated
 * when they're shown in a dedicated stat row. Stops at the first non-stat element.
 */
FString StripLeadingStatTokens(const FString& In);

/** Rich-text markup for a single icon token (e.g. "reflex", "O", "status/poison"), escaped literal if unknown. */
FString IconMarkup(const FString& Token, bool bCompactIcon = false);

/**
 * When rules text begins with a spell speed token ({REFLEX}/{CHANNELED}/{BLAZING}), removes it (and any following
 * "." / space) into OutMetadataStrip for a metadata pill; returns true when stripped.
 */
bool TryStripLeadingSpellSpeedMetadata(FString& InOutRules, FString& OutMetadataStrip);

/** Spell vs ability wording for speed hover tooltips. */
enum class ESpeedTooltipSubject : uint8
{
	Spell,
	Ability,
	Attack,
};

/** Decorator array for an SRichTextBlock that renders `<icon .../>` runs as inline icons. */
TArray<TSharedRef<ITextDecorator>> MakeEnergyDecorators(int32 FontSize,
	ESpeedTooltipSubject SpeedSubject = ESpeedTooltipSubject::Spell);

/** Black panel + white Libre Baskerville tooltip for glossary keyword/status hover. Returns null when Tip is empty. */
TSharedPtr<IToolTip> MakeGlossaryHoverToolTip(const FString& Tip);

/**
 * Slate popup fill pattern used by the failure-alert toast: WhiteBrush border image + opaque black
 * BorderBackgroundColor. Pair with `.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())` and
 * `.BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())`.
 */
const FSlateBrush* SolidPopupPanelBorderImage();
FLinearColor SolidPopupPanelFillColor(float Alpha = 1.f);

/**
 * The default UI font set (Calibri, from the system fonts folder). Use this for all standard
 * chrome - buttons, labels, counters, hand/reserves cards, etc. Weight is one of
 * "Regular"/"Bold"/"Italic"/"BoldItalic"/"Light"; engine fallback if the file is absent.
 */
FSlateFontInfo DefaultFont(const FString& Weight, int32 Size);

/**
 * Body / description font (Libre Baskerville). Use for card rules text and ability descriptions.
 * Weight: "Regular"/"Medium"/"Bold"/"Italic"/"BoldItalic"/"Light"; engine fallback if file missing.
 */
FSlateFontInfo DesignFont(const FString& Weight, int32 Size);

/** Offline distance-field UFont for world-space damage popups (TextRender). */
::UFont* AbilityDamagePopupWorldFont();

/** @deprecated Prefer AbilityDamagePopupWorldFont for TextRender popups. */
::UFont* WorldDesignFont();

/** Debug/diagnostic readout font - the default set (Calibri) at Regular weight. */
FSlateFontInfo DebugFont(int32 Size);

/**
 * A Calibri (DefaultFont) text-block style for use as `SButton::TextStyle`, so button labels created
 * via `SButton.Text(...)` render in Calibri. Returns a reference into a process-lifetime cache.
 */
const FTextBlockStyle& DefaultTextStyle(int32 FontSize, const FLinearColor& Color, const FString& Weight = TEXT("Regular"));

/** Bespoke Serif (DesignFont) text-block style for card text. Process-lifetime cached reference. */
const FTextBlockStyle& DesignTextStyle(int32 FontSize, const FLinearColor& Color, const FString& Weight = TEXT("Regular"));

/**
 * Stable default text style (Regular font at FontSize, given colour) for the rich text block.
 * Returns a reference into a process-lifetime cache so the pointer stays valid past Construct.
 */
const FTextBlockStyle& EnergyTextStyle(int32 FontSize, const FLinearColor& Color);
}  // namespace TacticsCardText