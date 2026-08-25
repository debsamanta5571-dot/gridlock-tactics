#pragma once

#include "CoreMinimal.h"

// ─────────────────────────────────────────────────────────────────────────────
// Inline text-highlight registry (shared by markup-emit and render).
//
// A "highlight" is a colored, hoverable word rendered from a self-closing tag
//   <Tag w="DisplayWord" tip="Hover text"/>
// This registry is the SINGLE source of truth for both halves of the pipeline:
//   • EmitMarkerRun (TacticsGlossaryMarkup.cpp) picks the tag for a marker.
//   • MakeEnergyDecorators (TacticsCardText.cpp) builds one decorator per entry.
//
// TO ADD A NEW HIGHLIGHT: add one entry to TacticsHighlightStyles() - nothing else.
// The first entry with an empty TriggerKey is the DEFAULT tag (used by any marker
// that no other entry claims). TriggerKey is "<kind>:<slug>" (e.g. "kw:boost").
// ─────────────────────────────────────────────────────────────────────────────
struct FTacticsHighlightStyle
{
	FString Tag;          // rich-text tag name, e.g. "boost" / "glossary"
	FLinearColor Color;   // run text color
	FString FallbackWord; // display word when the `w` attribute is missing/empty
	FString TriggerKey;   // "<kind>:<slug>" that selects this tag; empty = default tag
};

/** Canonical inline-highlight registry (see header note). First empty-TriggerKey entry is the default. */
TACTICSCORE_API const TArray<FTacticsHighlightStyle>& TacticsHighlightStyles();

/** Rich-text tag a glossary marker should emit (e.g. kw:boost → "boost", else default "glossary"). */
TACTICSCORE_API FString TacticsHighlightTagForMarker(const FString& Kind, const FString& Slug);

/** Subject a tooltip is describing - picks the noun in dynamic copy ("the range of this {word} …"). */
enum class ETacticsTooltipSubject : uint8
{
	Spell,
	Ability,
	Attack,
};

/** Noun word for a tooltip subject: "spell" / "ability" / "attack". */
TACTICSCORE_API FString TacticsTooltipSubjectWord(ETacticsTooltipSubject Subject);

/** Glossary row used to resolve `{KW:…}` / `{GL:…}` / `{FX:…}` marker tooltips (sidebar + live effects). */
struct FTacticsGlossaryNameBody
{
	/** Dedupe slug from `card_glossary` (`kw:boost`, `fx:poison`, `live:poison`, …). */
	FString Key;
	FString Name;
	FString Body;
};

/** Tooltip lookup for explicit glossary markers in rules prose. */
struct FTacticsGlossaryMarkerLookup
{
	TMap<FString, FString> TooltipByKey;
};

TACTICSCORE_API FTacticsGlossaryMarkerLookup BuildGlossaryMarkerLookup(const TArray<FTacticsGlossaryNameBody>& Entries);

/** True when `Token` is the inner text of a `{KW:…}` / `{GL:…}` / `{FX:…}` brace marker. */
TACTICSCORE_API bool IsGlossaryMarkerBraceToken(const FString& Token);

/**
 * Replaces `{KW:slug}` / `{GL:slug}` / `{FX:slug}` and optional display overrides with rich-text
 * glossary/boost runs. Unmarked prose is never auto-matched. Run after `MarkupEnergyTokens`
 * (see `TacticsCardText::MarkupDescriptionText`).
 */
TACTICSCORE_API FString MarkupKeywordMarkers(const FString& In, const FTacticsGlossaryMarkerLookup& Lookup,
	bool bAdvancedGlossary = false);

/** Sidebar/tooltip copy for a keyword slug (e.g. `boost`). Empty when unknown. */
TACTICSCORE_API FString KeywordGlossaryTooltip(const FString& KeywordKey, bool bAdvancedGlossary = false);

/** Sidebar/tooltip copy for a glossary term slug (e.g. `flux_energy`). Empty when unknown. */
TACTICSCORE_API FString TermGlossaryTooltip(const FString& TermKey, bool bAdvancedGlossary = false);

/** Hover tooltip for spell/ability speed (`channeled` / `reflex` / `blazing`). SubjectSlug is `spell` or `ability`. */
TACTICSCORE_API FString SpeedGlossaryTooltip(const FString& SpeedKey, const FString& SubjectSlug,
	bool bAdvancedGlossary = false);

/** Hover tooltip for an inline icon glossary key (e.g. energy_orange, stats_life). */
TACTICSCORE_API FString IconGlossaryTooltip(const FString& IconKey, bool bAdvancedGlossary = false);

/**
 * Resolves `ui/...` art id (+ optional token/range hints) to icon hover copy.
 * Dynamic hovers (subject from RangeSubject - stat row = Attack, spell text = Spell, ability strip = Ability):
 * - `ui/stats/range` / `range_self` + RangeValue: "The range of this <subject> is N tile(s)." (+ self note for range_self)
 * - `ui/stats/adjacent` / `adjacent_self`: "This <subject> can only target adjacent tiles." (+ self note for adjacent_self)
 */
TACTICSCORE_API FString IconGlossaryTooltipForArtId(const FString& ArtId, const FString& TokenFallback = FString(),
	int32 RangeValue = -1, bool bNeutralXCost = false, int32 NeutralPipCount = 0, bool bAdvancedGlossary = false,
	ETacticsTooltipSubject RangeSubject = ETacticsTooltipSubject::Attack);