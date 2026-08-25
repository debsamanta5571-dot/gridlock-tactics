#include "TacticsGlossaryMarkup.h"

#include "tactics/attributes/attributes.hpp"
#include "tactics/content/glossary_copy.hpp"
#include "tactics/effects/status_effect_catalog.hpp"

namespace
{
void AppendEscapedAttr(FString& Out, TCHAR Ch)
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
		AppendEscapedAttr(Out, Ch);
	}
	return Out;
}

std::string KeywordTooltipBodyUtf8(const std::string& key, bool advanced_glossary)
{
	const std::string body = tactics::keyword_glossary_body(key, advanced_glossary);
	if (!body.empty()) {
		return body;
	}
	const std::string full = tactics::attribute_rules_text(key);
	if (full.empty()) {
		return {};
	}
	if (advanced_glossary) {
		return full;
	}
	// First sentence: split on the first terminator (. ! ?) followed by whitespace/end so that
	// embedded decimals ("Deals 2.5 damage.") don't truncate the simple-mode blurb early.
	for (size_t i = 0; i < full.size(); ++i) {
		const char c = full[i];
		const bool boundary = (i + 1 >= full.size())
			|| full[i + 1] == ' ' || full[i + 1] == '\n' || full[i + 1] == '\t' || full[i + 1] == '\r';
		if ((c == '.' || c == '!' || c == '?') && boundary) {
			return full.substr(0, i + 1);
		}
	}
	return full;
}

bool TryParseBraceToken(const FString& In, int32 Pos, FString& OutToken, int32& OutNext)
{
	const int32 N = In.Len();
	if (Pos >= N || In[Pos] != TEXT('{')) {
		return false;
	}
	int32 Close = INDEX_NONE;
	for (int32 j = Pos + 1; j < N && j <= Pos + 80; ++j) {
		if (In[j] == TEXT('}')) {
			Close = j;
			break;
		}
		if (In[j] == TEXT('{')) {
			break;
		}
	}
	if (Close == INDEX_NONE || Close == Pos + 1) {
		return false;
	}
	OutToken = In.Mid(Pos + 1, Close - Pos - 1);
	OutNext = Close + 1;
	return true;
}

bool TryParseGlossaryMarkerToken(const FString& Token, FString& OutKind, FString& OutSlug, FString& OutDisplay)
{
	const int32 Colon = Token.Find(TEXT(":"));
	if (Colon == INDEX_NONE) {
		return false;
	}
	const FString Prefix = Token.Left(Colon);
	if (!Prefix.Equals(TEXT("KW"), ESearchCase::IgnoreCase) && !Prefix.Equals(TEXT("GL"), ESearchCase::IgnoreCase)
		&& !Prefix.Equals(TEXT("FX"), ESearchCase::IgnoreCase)) {
		return false;
	}
	OutKind = Prefix.ToLower();
	FString Rest = Token.Mid(Colon + 1);
	const int32 Pipe = Rest.Find(TEXT("|"));
	if (Pipe != INDEX_NONE) {
		OutSlug = Rest.Left(Pipe);
		OutDisplay = Rest.Mid(Pipe + 1);
	} else {
		OutSlug = Rest;
		OutDisplay.Reset();
	}
	OutSlug.TrimStartAndEndInline();
	OutDisplay.TrimStartAndEndInline();
	return !OutSlug.IsEmpty();
}

std::string StatusTooltipUtf8(const std::string& key, bool advanced_glossary)
{
	tactics::StatusEffectSpec spec;
	if (!tactics::try_get_status_effect_spec(key, spec)) {
		return {};
	}
	return tactics::status_glossary_body(spec, advanced_glossary);
}

FString ResolveMarkerTooltip(const FString& Kind, const FString& Slug, const FTacticsGlossaryMarkerLookup& Lookup,
	const bool bAdvancedGlossary)
{
	const FString DedupeKey = Kind + TEXT(":") + Slug.ToLower();
	if (const FString* Found = Lookup.TooltipByKey.Find(DedupeKey)) {
		return *Found;
	}
	if (Kind.Equals(TEXT("kw"), ESearchCase::IgnoreCase)) {
		return KeywordGlossaryTooltip(Slug, bAdvancedGlossary);
	}
	if (Kind.Equals(TEXT("gl"), ESearchCase::IgnoreCase)) {
		return TermGlossaryTooltip(Slug, bAdvancedGlossary);
	}
	const std::string Body = StatusTooltipUtf8(TCHAR_TO_UTF8(*Slug), bAdvancedGlossary);
	return Body.empty() ? FString() : UTF8_TO_TCHAR(Body.c_str());
}

FString ResolveMarkerDisplay(const FString& Kind, const FString& Slug, const FString& DisplayOverride)
{
	if (!DisplayOverride.IsEmpty()) {
		return DisplayOverride;
	}
	if (Kind.Equals(TEXT("kw"), ESearchCase::IgnoreCase)) {
		const std::string Label = tactics::attribute_display_name(TCHAR_TO_UTF8(*Slug));
		return Label.empty() ? Slug : UTF8_TO_TCHAR(Label.c_str());
	}
	if (Kind.Equals(TEXT("gl"), ESearchCase::IgnoreCase)) {
		FString Label = Slug;
		if (Slug.Equals(TEXT("flux_energy"), ESearchCase::IgnoreCase)) {
			return TEXT("Flux Energy");
		}
		if (Slug.Equals(TEXT("armor"), ESearchCase::IgnoreCase)) {
			return TEXT("Armor");
		}
		if (Slug.Equals(TEXT("survives"), ESearchCase::IgnoreCase)
			|| Slug.Equals(TEXT("survives_exchange"), ESearchCase::IgnoreCase)) {
			return TEXT("Survives");
		}
		Label.ReplaceInline(TEXT("_"), TEXT(" "));
		return Label;
	}
	tactics::StatusEffectSpec spec;
	if (tactics::try_get_status_effect_spec(TCHAR_TO_UTF8(*Slug), spec)) {
		const std::string Label = spec.display_name.empty() ? std::string(TCHAR_TO_UTF8(*Slug)) : spec.display_name;
		return UTF8_TO_TCHAR(Label.c_str());
	}
	return Slug;
}

FString SanitizeTooltipForAttr(FString Tooltip)
{
	Tooltip.ReplaceInline(TEXT("\r\n"), TEXT(" "));
	Tooltip.ReplaceInline(TEXT("\n"), TEXT(" "));
	Tooltip.ReplaceInline(TEXT("\r"), TEXT(" "));
	Tooltip.TrimStartAndEndInline();
	return Tooltip;
}

struct FStockpileCounterSuffix
{
	bool bValid{false};
	int32 Remaining{0};
	int32 Max{0};
	bool bHasSlash{false};
	bool bUsed{false};
	int32 ConsumedLen{0};
};

bool TryParseStockpileCounterSuffix(const FString& In, const int32 Start, FStockpileCounterSuffix& Out)
{
	Out = {};
	int32 i = Start;
	while (i < In.Len() && In[i] == TEXT(' ')) {
		++i;
	}
	if (i >= In.Len() || !FChar::IsDigit(In[i])) {
		return false;
	}
	int32 Remaining = 0;
	while (i < In.Len() && FChar::IsDigit(In[i])) {
		Remaining = Remaining * 10 + (In[i] - TEXT('0'));
		++i;
	}
	int32 Max = Remaining;
	bool bHasSlash = false;
	if (i < In.Len() && In[i] == TEXT('/')) {
		bHasSlash = true;
		++i;
		Max = 0;
		while (i < In.Len() && FChar::IsDigit(In[i])) {
			Max = Max * 10 + (In[i] - TEXT('0'));
			++i;
		}
	}
	bool bUsed = false;
	while (i < In.Len() && In[i] == TEXT(' ')) {
		++i;
	}
	if (i + 4 <= In.Len() && In.Mid(i, 4).Equals(TEXT("used"), ESearchCase::IgnoreCase)) {
		bUsed = true;
		i += 4;
	}
	Out.bValid = true;
	Out.Remaining = Remaining;
	Out.Max = Max;
	Out.bHasSlash = bHasSlash;
	Out.bUsed = bUsed;
	Out.ConsumedLen = i - Start;
	return true;
}

bool TryParseStockpileDisplayOverride(const FString& DisplayOverride, FStockpileCounterSuffix& Out)
{
	FString Rest = DisplayOverride;
	Rest.TrimStartAndEndInline();
	if (!Rest.RemoveFromStart(TEXT("Stockpile"), ESearchCase::IgnoreCase)) {
		return false;
	}
	return TryParseStockpileCounterSuffix(Rest, 0, Out);
}

FString EmitStockpileRun(const FStockpileCounterSuffix& Counter, const FString& Tooltip)
{
	const FString SafeTip = SanitizeTooltipForAttr(Tooltip);
	if (SafeTip.IsEmpty()) {
		return TEXT("Stockpile");
	}
	return FString::Printf(TEXT("<stockpile rem=\"%d\" max=\"%d\" slash=\"%d\" used=\"%d\" tip=\"%s\"/>"),
		Counter.Remaining, Counter.Max, Counter.bHasSlash ? 1 : 0, Counter.bUsed ? 1 : 0, *EscapeAttr(SafeTip));
}

FString EmitMarkerRun(const FString& Kind, const FString& Slug, const FString& Display, const FString& Tooltip)
{
	const FString SafeTip = SanitizeTooltipForAttr(Tooltip);
	if (SafeTip.IsEmpty()) {
		return Display;
	}
	// Tag is chosen from the shared highlight registry (kw:boost → "boost", else default "glossary").
	const FString Tag = TacticsHighlightTagForMarker(Kind, Slug);
	return FString::Printf(TEXT("<%s w=\"%s\" tip=\"%s\"/>"), *Tag, *EscapeAttr(Display), *EscapeAttr(SafeTip));
}
}  // namespace

const TArray<FTacticsHighlightStyle>& TacticsHighlightStyles()
{
	static const TArray<FTacticsHighlightStyle> Styles = {
		// Default first (empty TriggerKey): any marker no other entry claims renders gold + hover.
		{ TEXT("glossary"), FLinearColor(0.95f, 0.86f, 0.55f, 1.f), TEXT("?"),      FString()        },
		// kw:boost renders as the light-blue Boost verb.
		{ TEXT("boost"),    FLinearColor(0.55f, 0.85f, 1.0f, 1.f),  TEXT("Boosts"), TEXT("kw:boost") },
		// gl:survives / gl:survives_exchange - phase vs exchange survival vocabulary.
		{ TEXT("survive"),  FLinearColor(0.50f, 0.92f, 0.72f, 1.f), TEXT("survives"), TEXT("gl:survives") },
		{ TEXT("survive"),  FLinearColor(0.50f, 0.92f, 0.72f, 1.f), TEXT("survives"), TEXT("gl:survives_exchange") },
	};
	return Styles;
}

FString TacticsHighlightTagForMarker(const FString& Kind, const FString& Slug)
{
	const FString Key = (Kind + TEXT(":") + Slug).ToLower();
	FString DefaultTag = TEXT("glossary");
	for (const FTacticsHighlightStyle& Style : TacticsHighlightStyles()) {
		if (Style.TriggerKey.IsEmpty()) {
			DefaultTag = Style.Tag;
		} else if (Style.TriggerKey.Equals(Key, ESearchCase::IgnoreCase)) {
			return Style.Tag;
		}
	}
	return DefaultTag;
}

FString TacticsTooltipSubjectWord(ETacticsTooltipSubject Subject)
{
	switch (Subject) {
		case ETacticsTooltipSubject::Ability: return TEXT("ability");
		case ETacticsTooltipSubject::Attack:  return TEXT("attack");
		default:                              return TEXT("spell");
	}
}

FTacticsGlossaryMarkerLookup BuildGlossaryMarkerLookup(const TArray<FTacticsGlossaryNameBody>& Entries)
{
	FTacticsGlossaryMarkerLookup Out;
	auto Upsert = [&](const FString& Key, const FString& Body) {
		if (Key.IsEmpty() || Body.IsEmpty()) {
			return;
		}
		const FString Normalized = Key.ToLower();
		if (!Out.TooltipByKey.Contains(Normalized) || Out.TooltipByKey[Normalized].Len() < Body.Len()) {
			Out.TooltipByKey.Add(Normalized, Body);
		}
	};
	for (const FTacticsGlossaryNameBody& Entry : Entries) {
		if (Entry.Key.IsEmpty() || Entry.Body.IsEmpty()) {
			continue;
		}
		Upsert(Entry.Key, Entry.Body);
		if (Entry.Key.StartsWith(TEXT("live:"), ESearchCase::IgnoreCase)) {
			const FString Rest = Entry.Key.Mid(5);
			if (!Rest.StartsWith(TEXT("temp:"), ESearchCase::IgnoreCase)) {
				Upsert(TEXT("fx:") + Rest.ToLower(), Entry.Body);
			}
		}
	}
	return Out;
}

bool IsGlossaryMarkerBraceToken(const FString& Token)
{
	FString Kind;
	FString Slug;
	FString DisplayOverride;
	return TryParseGlossaryMarkerToken(Token, Kind, Slug, DisplayOverride);
}

FString MarkupKeywordMarkers(const FString& In, const FTacticsGlossaryMarkerLookup& Lookup, const bool bAdvancedGlossary)
{
	FString Out;
	Out.Reserve(In.Len() + 32);
	const int32 N = In.Len();
	int32 i = 0;
	while (i < N) {
		FString Token;
		int32 Next = 0;
		if (TryParseBraceToken(In, i, Token, Next)) {
			FString Kind;
			FString Slug;
			FString DisplayOverride;
			if (TryParseGlossaryMarkerToken(Token, Kind, Slug, DisplayOverride)) {
				const FString Tooltip = ResolveMarkerTooltip(Kind, Slug, Lookup, bAdvancedGlossary);
				if (Kind.Equals(TEXT("kw"), ESearchCase::IgnoreCase) && Slug.Equals(TEXT("stockpile"), ESearchCase::IgnoreCase)) {
					FStockpileCounterSuffix Counter;
					if (TryParseStockpileCounterSuffix(In, Next, Counter)) {
						Out += EmitStockpileRun(Counter, Tooltip);
						i = Next + Counter.ConsumedLen;
						continue;
					}
					if (!DisplayOverride.IsEmpty() && TryParseStockpileDisplayOverride(DisplayOverride, Counter)) {
						Out += EmitStockpileRun(Counter, Tooltip);
						i = Next;
						continue;
					}
					Out += EmitMarkerRun(Kind, Slug, ResolveMarkerDisplay(Kind, Slug, FString()), Tooltip);
					i = Next;
					continue;
				}
				const FString Display = ResolveMarkerDisplay(Kind, Slug, DisplayOverride);
				Out += EmitMarkerRun(Kind, Slug, Display, Tooltip);
				i = Next;
				continue;
			}
		}
		Out.AppendChar(In[i]);
		++i;
	}
	return Out;
}

FString KeywordGlossaryTooltip(const FString& KeywordKey, const bool bAdvancedGlossary)
{
	if (KeywordKey.IsEmpty()) {
		return FString();
	}
	const std::string Body = KeywordTooltipBodyUtf8(TCHAR_TO_UTF8(*KeywordKey), bAdvancedGlossary);
	return Body.empty() ? FString() : UTF8_TO_TCHAR(Body.c_str());
}

FString TermGlossaryTooltip(const FString& TermKey, const bool bAdvancedGlossary)
{
	if (TermKey.IsEmpty()) {
		return FString();
	}
	const std::string Body = tactics::term_glossary_body(TCHAR_TO_UTF8(*TermKey), bAdvancedGlossary);
	return Body.empty() ? FString() : UTF8_TO_TCHAR(Body.c_str());
}

FString SpeedGlossaryTooltip(const FString& SpeedKey, const FString& SubjectSlug, const bool bAdvancedGlossary)
{
	if (SpeedKey.IsEmpty()) {
		return FString();
	}
	const FString Slug = SubjectSlug.Equals(TEXT("ability"), ESearchCase::IgnoreCase) ? TEXT("ability") : TEXT("spell");
	const FString LookupKey = FString::Printf(TEXT("%s_%s"), *SpeedKey.ToLower(), *Slug);
	const std::string Body = tactics::speed_glossary_body(TCHAR_TO_UTF8(*LookupKey), bAdvancedGlossary);
	return Body.empty() ? FString() : UTF8_TO_TCHAR(Body.c_str());
}

FString IconGlossaryKeyFromArtId(const FString& ArtId)
{
	if (ArtId.IsEmpty()) {
		return FString();
	}
	FString Key = ArtId;
	Key.RemoveFromStart(TEXT("ui/"));
	return Key.Replace(TEXT("/"), TEXT("_"));
}

FString IconGlossaryTooltip(const FString& IconKey, const bool bAdvancedGlossary)
{
	if (IconKey.IsEmpty()) {
		return FString();
	}
	const std::string Body = tactics::icon_glossary_body(TCHAR_TO_UTF8(*IconKey.ToLower()), bAdvancedGlossary);
	return Body.empty() ? FString() : UTF8_TO_TCHAR(Body.c_str());
}

// Single resolution point for icon hover copy. Most icons just look up their glossary blurb by
// art-id; a few need a computed suffix (energy X-cost / pip count, passive aura vs self, range value).
// Those bespoke cases live here, below - add new dynamic-suffix icons in this function, not scattered.
FString IconGlossaryTooltipForArtId(const FString& ArtId, const FString& TokenFallback, const int32 RangeValue,
	const bool bNeutralXCost, const int32 NeutralPipCount, const bool bAdvancedGlossary, const ETacticsTooltipSubject RangeSubject)
{
	if (ArtId.IsEmpty()) {
		return FString();
	}
	if (ArtId.StartsWith(TEXT("ui/passive/"))) {
		const FString Key = TokenFallback.Equals(TEXT("aura"), ESearchCase::IgnoreCase) ? TEXT("passive_aura")
			: TEXT("passive_passive");
		return IconGlossaryTooltip(Key, bAdvancedGlossary);
	}
	if (ArtId == TEXT("ui/energy/neutral")) {
		if (bNeutralXCost) {
			return IconGlossaryTooltip(TEXT("energy_neutral_x"), bAdvancedGlossary);
		}
		FString Tip = IconGlossaryTooltip(TEXT("energy_neutral"), bAdvancedGlossary);
		if (NeutralPipCount > 0) {
			Tip += FString::Printf(TEXT(" Cost: %d."), NeutralPipCount);
		}
		return Tip;
	}
	if (RangeValue >= 0 && (ArtId == TEXT("ui/stats/range") || ArtId == TEXT("ui/stats/range_self"))) {
		const FString Tiles = (RangeValue == 1) ? TEXT("1 tile") : FString::Printf(TEXT("%d tiles"), RangeValue);
		FString Tip = FString::Printf(TEXT("The range of this %s is %s."),
			*TacticsTooltipSubjectWord(RangeSubject), *Tiles);
		if (ArtId == TEXT("ui/stats/range_self")) {
			Tip += TEXT(" You may also target the caster.");
		}
		return Tip;
	}
	if (ArtId == TEXT("ui/stats/adjacent") || ArtId == TEXT("ui/stats/adjacent_self")) {
		FString Tip = FString::Printf(TEXT("This %s can only target adjacent tiles."),
			*TacticsTooltipSubjectWord(RangeSubject));
		if (ArtId == TEXT("ui/stats/adjacent_self")) {
			Tip += TEXT(" You may also target the caster.");
		}
		return Tip;
	}
	return IconGlossaryTooltip(IconGlossaryKeyFromArtId(ArtId), bAdvancedGlossary);
}