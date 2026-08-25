#pragma once

namespace
{
tactics::BoardCellBounds MergedBounds(const tactics::GameState& G)
{
	return tactics::cell_bounds_or_main_module(G.board_cell_bounds(), G.board_width(), G.board_height());
}

FString StdToF(const std::string& Utf8)
{
	return FString(UTF8_TO_TCHAR(Utf8.c_str()));
}

// Single brace-token letter for an energy colour (matches TacticsCardText / the authoring
// convention). The Slate layer turns these into inline energy icons; neutral collapses to a
// numbered grey circle.
TCHAR EnergyBraceLetter(tactics::EnergyType Type)
{
	switch (Type) {
		case tactics::EnergyType::Neutral:   return TEXT('N');
		case tactics::EnergyType::Omni:      return TEXT('M');
		case tactics::EnergyType::Red:       return TEXT('R');
		case tactics::EnergyType::Turquoise: return TEXT('T');
		case tactics::EnergyType::Orange:    return TEXT('O');
		case tactics::EnergyType::Purple:    return TEXT('P');
		case tactics::EnergyType::Green:     return TEXT('G');
		default:                             return TEXT('N');
	}
}

// Renders an energy cost map as brace tokens, e.g. {N}{N}{O} (one token per pip; neutral pips
// repeat so the icon decorator can show the count). Returns "{N}0"-free empty -> "0" caller-side.
FString EnergyCostBraceTokens(const std::map<tactics::EnergyType, int>& Cost)
{
	FString Out;
	for (const auto& Pr : Cost) {
		const int Count = Pr.second;
		if (Count <= 0) {
			continue;
		}
		const TCHAR Letter = EnergyBraceLetter(Pr.first);
		for (int i = 0; i < Count; ++i) {
			Out += TEXT('{');
			Out.AppendChar(Letter);
			Out += TEXT('}');
		}
	}
	return Out;
}

void AppendXCostBraceToken(FString& CostLine)
{
	CostLine += TEXT("{X}");
}

FString AbilityCostBraceTokens(const tactics::AbilitySpec& A)
{
	FString Out = EnergyCostBraceTokens(A.energy_cost);
	if (A.x_cost_energy_type.has_value()) {
		AppendXCostBraceToken(Out);
	}
	if (tactics::ability_consumes_attack_action(A)) {
		Out += TEXT("{ATTACK}");
	}
	return Out;
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

bool StripAttackCostQualifierFromParensSuffix(FString& Suffix)
{
	if (Suffix.IsEmpty() || !Suffix.StartsWith(TEXT(" (")) || !Suffix.EndsWith(TEXT(")"))) {
		return false;
	}
	FString Inner = Suffix.Mid(2, Suffix.Len() - 3).TrimStartAndEnd();
	if (!StripAttackCostFromQualifierList(Inner)) {
		return false;
	}
	Suffix = Inner.IsEmpty() ? FString() : FString(TEXT(" (")) + Inner + TEXT(")");
	return true;
}

FString SpeedBraceFromAbilitySpec(const tactics::AbilitySpec& A)
{
	switch (A.speed) {
		case tactics::EffectSpeed::Reflex:
			return TEXT("{REFLEX}");
		case tactics::EffectSpeed::Blazing:
			return TEXT("{BLAZING}");
		case tactics::EffectSpeed::Channeled:
		default:
			return TEXT("{CHANNELED}");
	}
}

int32 AbilityCatalogDisplayRange(const tactics::AbilitySpec& A)
{
	if (A.range_max > 0) {
		return A.range_max;
	}
	const auto find_key = [&](const char* Key) -> int32 {
		const auto It = A.effect_payload.find(Key);
		return It != A.effect_payload.end() ? static_cast<int32>(It->second) : 0;
	};
	if (const int32 R = find_key("range")) {
		return R;
	}
	return find_key("max_range");
}

bool AbilityUsesAdjacentIcon(const tactics::AbilitySpec& A)
{
	if (A.effect_key.find("_adjacent") != std::string::npos) {
		return true;
	}
	return A.effect_key == "terra_cone_strike";
}

/** True when the caster's own tile or entity is a legal target (in addition to adjacent/range targets). */
bool AbilityAllowsSelfOrOwnTileTarget(const tactics::AbilitySpec& A)
{
	if (!tactics::ability_requires_board_target(A) && !tactics::effect_key_targets_empty_cell(A.effect_key)) {
		return false;
	}
	const tactics::BoardTargetKind kind = tactics::ability_board_target_kind(A);
	if (kind == tactics::BoardTargetKind::NonSelf || kind == tactics::BoardTargetKind::Enemy) {
		return false;
	}
	if (kind == tactics::BoardTargetKind::Own || kind == tactics::BoardTargetKind::Any) {
		return true;
	}
	if (A.effect_key.find("_adjacent") != std::string::npos) {
		return false;
	}
	return kind == tactics::BoardTargetKind::Ally;
}

FString AbilityCatalogRangeToken(const tactics::AbilitySpec& A, tactics::GameState* Game = nullptr,
	const std::shared_ptr<tactics::Unit>& Actor = nullptr, const int PlayerId = 0)
{
	const bool bAllowsSelf = (Game && Actor && PlayerId > 0)
		? tactics::ability_board_target_includes_caster_self(*Game, Actor, PlayerId, A.key)
		: AbilityAllowsSelfOrOwnTileTarget(A);
	if (AbilityUsesAdjacentIcon(A)) {
		return bAllowsSelf ? TEXT("{ADJACENT_SELF}") : TEXT("{ADJACENT}");
	}
	if (const int32 Range = AbilityCatalogDisplayRange(A); Range > 0) {
		if (bAllowsSelf) {
			return FString::Printf(TEXT("{RANGE_SELF} %d"), Range);
		}
		return FString::Printf(TEXT("{RANGE} %d"), Range);
	}
	return FString();
}

FString AbilityMetadataStripFromSpec(const tactics::AbilitySpec& A)
{
	FString Strip = SpeedBraceFromAbilitySpec(A);
	if (const FString RangeTok = AbilityCatalogRangeToken(A); !RangeTok.IsEmpty()) {
		Strip += TEXT(", ") + RangeTok;
	}
	if (const FString Cost = AbilityCostBraceTokens(A); !Cost.IsEmpty()) {
		Strip += TEXT(", ") + Cost;
	}
	return Strip;
}

void RemoveAllInsensitive(FString& S, const FString& Pattern)
{
	int32 Idx = INDEX_NONE;
	while ((Idx = S.Find(Pattern, ESearchCase::IgnoreCase)) != INDEX_NONE) {
		S.RemoveAt(Idx, Pattern.Len());
	}
}

/** Normal view: normal if present, else advanced. Advanced view: normal + advanced (when advanced adds new text). */
std::string CardTextForDisplay(const std::string& Normal, const std::string& Advanced, bool bShowAdvanced)
{
	if (!bShowAdvanced) {
		return !Normal.empty() ? Normal : Advanced;
	}
	if (!Normal.empty()) {
		if (!Advanced.empty() && Advanced != Normal) {
			return Normal + "\n\n" + Advanced;
		}
		return Normal;
	}
	return Advanced;
}

FString StripRedundantTargetingFromText(FString S, const FString& RangeToken)
{
	if (RangeToken.IsEmpty()) {
		return S;
	}
	// "{RANGE} 4" / "{RANGE_SELF} 4" -> collapse space before the digit.
	for (int32 Pos = 0; Pos < S.Len(); ++Pos) {
		if (S[Pos] != TEXT('{')) {
			continue;
		}
		int32 PrefixLen = 0;
		if (S.Mid(Pos, 12).StartsWith(TEXT("{RANGE_SELF}"), ESearchCase::CaseSensitive)) {
			PrefixLen = 12;
		} else if (S.Mid(Pos, 7).StartsWith(TEXT("{RANGE}"), ESearchCase::CaseSensitive)) {
			PrefixLen = 7;
		} else {
			continue;
		}
		int32 After = Pos + PrefixLen;
		while (After < S.Len() && S[After] == TEXT(' ')) {
			S.RemoveAt(After, 1);
		}
	}

	if (RangeToken == TEXT("{ADJACENT}") || RangeToken == TEXT("{ADJACENT_SELF}")) {
		struct FAdjRepl { const TCHAR* From; const TCHAR* To; };
		static const FAdjRepl Replacements[] = {
			{TEXT("on a random unoccupied {ADJACENT} "), TEXT("on a random unoccupied ")},
			{TEXT("on a random unoccupied adjacent "), TEXT("on a random unoccupied ")},
			{TEXT("on an {ADJACENT} "), TEXT("on an ")},
			{TEXT("on an adjacent "), TEXT("on an ")},
			{TEXT("to an {ADJACENT} "), TEXT("to an ")},
			{TEXT("to an adjacent "), TEXT("to an ")},
			{TEXT("from an {ADJACENT} "), TEXT("from an ")},
			{TEXT("from an adjacent "), TEXT("from an ")},
			{TEXT("an {ADJACENT} "), TEXT("an ")},
			{TEXT("an adjacent "), TEXT("an ")},
			{TEXT("{ADJACENT} "), TEXT("")},
		};
		for (const FAdjRepl& R : Replacements) {
			S.ReplaceInline(R.From, R.To, ESearchCase::IgnoreCase);
		}
	} else if (RangeToken.StartsWith(TEXT("{RANGE_SELF}"))) {
		const FString Num = RangeToken.Mid(12).TrimStart();
		S.ReplaceInline(*FString::Printf(TEXT("(channeled, {RANGE_SELF}%s)"), *Num), TEXT("(channeled)"), ESearchCase::IgnoreCase);
		S.ReplaceInline(*FString::Printf(TEXT("(reflex, {RANGE_SELF}%s)"), *Num), TEXT("(reflex)"), ESearchCase::IgnoreCase);
		S.ReplaceInline(*FString::Printf(TEXT("(blazing, {RANGE_SELF}%s)"), *Num), TEXT("(blazing)"), ESearchCase::IgnoreCase);
		RemoveAllInsensitive(S, FString::Printf(TEXT("within cardinal {RANGE_SELF}%s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within {RANGE_SELF}%s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within {RANGE_SELF} %s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within range %s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within %s tiles"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT(", {RANGE_SELF}%s)"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT(", {RANGE_SELF} %s)"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("({RANGE_SELF}%s)"), *Num));
	} else if (RangeToken.StartsWith(TEXT("{RANGE}"))) {
		const FString Num = RangeToken.Mid(7).TrimStart();
		S.ReplaceInline(*FString::Printf(TEXT("(channeled, {RANGE}%s)"), *Num), TEXT("(channeled)"), ESearchCase::IgnoreCase);
		S.ReplaceInline(*FString::Printf(TEXT("(reflex, {RANGE}%s)"), *Num), TEXT("(reflex)"), ESearchCase::IgnoreCase);
		S.ReplaceInline(*FString::Printf(TEXT("(blazing, {RANGE}%s)"), *Num), TEXT("(blazing)"), ESearchCase::IgnoreCase);
		RemoveAllInsensitive(S, FString::Printf(TEXT("within cardinal {RANGE}%s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within {RANGE}%s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within {RANGE} %s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within range %s"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("within %s tiles"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT(", {RANGE}%s)"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT(", {RANGE} %s)"), *Num));
		RemoveAllInsensitive(S, FString::Printf(TEXT("({RANGE}%s)"), *Num));
	}

	while (S.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {
	}
	return S.TrimStartAndEnd();
}

FString ExtraQualifierFromSpeedParens(const FString& Inside)
{
	FString Rest = Inside.TrimStartAndEnd();
	const FString Lower = Rest.ToLower();
	if (Lower.StartsWith(TEXT("reflex"))) {
		Rest = Rest.Mid(6);
	} else if (Lower.StartsWith(TEXT("channeled"))) {
		Rest = Rest.Mid(9);
	} else if (Lower.StartsWith(TEXT("blazing"))) {
		Rest = Rest.Mid(7);
	}
	Rest.TrimStartAndEndInline();
	if (Rest.StartsWith(TEXT(","))) {
		Rest = Rest.Mid(1).TrimStart();
	}
	Rest.TrimStartAndEndInline();
	return Rest.IsEmpty() ? FString() : FString(TEXT(" (")) + Rest + TEXT(")");
}

bool TryReplaceAbilityRulesPrefix(FString& Rules, const FString& Name, const tactics::AbilitySpec& A)
{
	int32 SearchFrom = 0;
	while (SearchFrom < Rules.Len()) {
		const int32 Pos = Rules.Find(Name, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (Pos == INDEX_NONE) {
			return false;
		}
		if (Pos > 0) {
			const TCHAR Prev = Rules[Pos - 1];
			if (Prev != TEXT(' ') && Prev != TEXT('.') && Prev != TEXT('\n')) {
				SearchFrom = Pos + 1;
				continue;
			}
		}

		int32 i = Pos + Name.Len();
		while (i < Rules.Len() && Rules[i] == TEXT(' ')) {
			++i;
		}
		while (i < Rules.Len() && Rules[i] == TEXT('{')) {
			const int32 Close = Rules.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i);
			if (Close == INDEX_NONE) {
				break;
			}
			i = Close + 1;
			while (i < Rules.Len() && Rules[i] == TEXT(' ')) {
				++i;
			}
		}

		FString ExtraSuffix;
		if (i < Rules.Len() && Rules[i] == TEXT('(')) {
			const int32 Close = Rules.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i + 1);
			if (Close != INDEX_NONE) {
				ExtraSuffix = ExtraQualifierFromSpeedParens(Rules.Mid(i + 1, Close - i - 1));
				i = Close + 1;
				while (i < Rules.Len() && Rules[i] == TEXT(' ')) {
					++i;
				}
			}
		}

		if (i >= Rules.Len() || Rules[i] != TEXT(':')) {
			SearchFrom = Pos + 1;
			continue;
		}

		FString Strip = SpeedBraceFromAbilitySpec(A);
		if (const FString RangeTok = AbilityCatalogRangeToken(A); !RangeTok.IsEmpty()) {
			Strip += TEXT(", ") + RangeTok;
		}
		const FString Cost = AbilityCostBraceTokens(A);
		if (!Cost.IsEmpty()) {
			Strip += TEXT(", ") + Cost;
		}
		Strip += TEXT(", ") + Name;
		StripAttackCostQualifierFromParensSuffix(ExtraSuffix);
		Strip += ExtraSuffix;

		FString Tail = Rules.Mid(i);
		if (Tail.StartsWith(TEXT(":"))) {
			FString Body = Tail.Mid(1).TrimStart();
			Body = StripRedundantTargetingFromText(Body, AbilityCatalogRangeToken(A));
			Tail = FString(TEXT(": ")) + Body;
		}
		Rules = Rules.Left(Pos) + Strip + Tail;
		return true;
	}
	return false;
}

/** Catalog ids from the card plus inline `unit.abilities` keys (inline-only cards were missing strips). */
std::vector<std::string> CollectAbilityIdsForCatalogStrips(const tactics::CardDefinition& def)
{
	std::vector<std::string> ids = def.abilities;
	auto append_unique = [&](const std::string& key) {
		if (key.empty()) {
			return;
		}
		if (std::find(ids.begin(), ids.end(), key) == ids.end()) {
			ids.push_back(key);
		}
	};
	if (def.unit) {
		for (const tactics::AbilitySpec& spec : def.unit->activated_abilities) {
			append_unique(spec.key);
		}
	}
	return ids;
}

std::vector<tactics::AbilitySpec> BuildOrderedCardAbilitySpecs(const tactics::CardDefinition& def)
{
	std::vector<tactics::AbilitySpec> specs;
	const std::vector<std::string> ids = CollectAbilityIdsForCatalogStrips(def);
	for (const std::string& id : ids) {
		tactics::AbilitySpec spec;
		if (tactics::try_get_ability_from_catalog(id, spec)) {
			specs.push_back(std::move(spec));
			continue;
		}
		if (def.unit) {
			for (const tactics::AbilitySpec& inline_spec : def.unit->activated_abilities) {
				if (inline_spec.key == id) {
					specs.push_back(inline_spec);
					break;
				}
			}
		}
	}
	if (def.unit) {
		for (const tactics::AbilitySpec& inline_spec : def.unit->activated_abilities) {
			if (std::find(ids.begin(), ids.end(), inline_spec.key) == ids.end()) {
				specs.push_back(inline_spec);
			}
		}
	}
	return specs;
}

bool AbilityBlockLabelMatches(const FString& block_name, const tactics::AbilitySpec& spec)
{
	const FString key = UTF8_TO_TCHAR(spec.key.c_str());
	const FString name = UTF8_TO_TCHAR(spec.name.empty() ? spec.key.c_str() : spec.name.c_str());
	auto names_match = [](const FString& a, const FString& b) {
		return a.Equals(b, ESearchCase::IgnoreCase)
			|| a.Contains(b, ESearchCase::IgnoreCase)
			|| b.Contains(a, ESearchCase::IgnoreCase);
	};
	return names_match(name, block_name) || names_match(key, block_name);
}

/** Match catalog ability by block name first; `activated_ability_index` is 0-based among activated abilities only (not layout index). */
const tactics::AbilitySpec* ResolveCardAbilitySpecForDetailBlock(const std::vector<tactics::AbilitySpec>& specs,
	const FString& AbilityBlockName, int32 ActivatedAbilityIndex)
{
	if (!AbilityBlockName.IsEmpty()) {
		for (const tactics::AbilitySpec& spec : specs) {
			if (AbilityBlockLabelMatches(AbilityBlockName, spec)) {
				return &spec;
			}
		}
	}
	if (ActivatedAbilityIndex >= 0 && ActivatedAbilityIndex < static_cast<int32>(specs.size())) {
		return &specs[static_cast<size_t>(ActivatedAbilityIndex)];
	}
	return nullptr;
}

bool TryReplacePassiveRulesPrefix(FString& Rules, const FString& Name)
{
	int32 SearchFrom = 0;
	while (SearchFrom < Rules.Len()) {
		const int32 Pos = Rules.Find(Name, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (Pos == INDEX_NONE) {
			return false;
		}
		if (Pos > 0) {
			const TCHAR Prev = Rules[Pos - 1];
			if (Prev != TEXT(' ') && Prev != TEXT('.') && Prev != TEXT('\n')) {
				SearchFrom = Pos + 1;
				continue;
			}
		}
		int32 i = Pos + Name.Len();
		while (i < Rules.Len() && Rules[i] == TEXT(' ')) {
			++i;
		}
		if (i >= Rules.Len() || Rules[i] != TEXT(':')) {
			SearchFrom = Pos + 1;
			continue;
		}
		FString Tail = Rules.Mid(i);
		if (Tail.StartsWith(TEXT(":"))) {
			FString Body = Tail.Mid(1).TrimStart();
			Tail = FString(TEXT(": ")) + Body;
		}
		const FString Strip = FString(TEXT("{PASSIVE}, ")) + Name;
		Rules = Rules.Left(Pos) + Strip + Tail;
		return true;
	}
	return false;
}

std::vector<std::string> CollectPassiveIdsForCatalogStrips(const tactics::CardDefinition& def)
{
	std::vector<std::string> ids;
	auto append_unique = [&](const std::string& key) {
		if (key.empty()) {
			return;
		}
		if (std::find(ids.begin(), ids.end(), key) == ids.end()) {
			ids.push_back(key);
		}
	};
	if (def.unit) {
		for (const std::string& id : def.unit->passive_ability_ids) {
			append_unique(id);
		}
		for (const tactics::PassiveAbilitySpec& spec : def.unit->passive_abilities) {
			append_unique(spec.key);
		}
	}
	return ids;
}

void ApplyPassiveCatalogStrips(FString& Rules, const std::vector<std::string>& PassiveIds)
{
	struct FPassiveStripEntry {
		FString Name;
		int32 NameLen{0};
	};
	TArray<FPassiveStripEntry> Entries;
	Entries.Reserve(static_cast<int32>(PassiveIds.size()));
	for (const std::string& Id : PassiveIds) {
		tactics::PassiveAbilitySpec Spec;
		if (!tactics::try_get_passive_from_catalog(Id, Spec)) {
			continue;
		}
		FPassiveStripEntry Entry;
		Entry.Name = UTF8_TO_TCHAR(Spec.name.empty() ? Spec.key.c_str() : Spec.name.c_str());
		Entry.NameLen = Entry.Name.Len();
		Entries.Add(Entry);
	}
	Entries.Sort([](const FPassiveStripEntry& A, const FPassiveStripEntry& B) {
		return A.NameLen > B.NameLen;
	});
	for (const FPassiveStripEntry& Entry : Entries) {
		TryReplacePassiveRulesPrefix(Rules, Entry.Name);
	}
	// Inline "Passive: …" prose (no catalog name) → passive metadata strip.
	int32 SearchFrom = 0;
	while (SearchFrom < Rules.Len()) {
		const int32 Pos = Rules.Find(TEXT("Passive:"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
		if (Pos == INDEX_NONE) {
			break;
		}
		if (Pos > 0) {
			const TCHAR Prev = Rules[Pos - 1];
			if (Prev != TEXT(' ') && Prev != TEXT('.') && Prev != TEXT('\n')) {
				SearchFrom = Pos + 8;
				continue;
			}
		}
		Rules = Rules.Left(Pos) + TEXT("{PASSIVE}, ") + Rules.Mid(Pos + 8).TrimStart();
		SearchFrom = Pos + 11;
	}
}

void ApplyAbilityCatalogStrips(FString& Rules, const std::vector<std::string>& AbilityIds)
{
	struct FAbilityStripEntry {
		FString Name;
		tactics::AbilitySpec Spec;
		int32 NameLen{0};
	};
	TArray<FAbilityStripEntry> Entries;
	Entries.Reserve(static_cast<int32>(AbilityIds.size()));
	for (const std::string& Id : AbilityIds) {
		tactics::AbilitySpec Spec;
		if (!tactics::try_get_ability_from_catalog(Id, Spec)) {
			continue;
		}
		FAbilityStripEntry Entry;
		Entry.Name = UTF8_TO_TCHAR(Spec.name.empty() ? Spec.key.c_str() : Spec.name.c_str());
		Entry.Spec = Spec;
		Entry.NameLen = Entry.Name.Len();
		Entries.Add(Entry);
	}
	Entries.Sort([](const FAbilityStripEntry& A, const FAbilityStripEntry& B) {
		return A.NameLen > B.NameLen;
	});
	for (const FAbilityStripEntry& Entry : Entries) {
		TryReplaceAbilityRulesPrefix(Rules, Entry.Name, Entry.Spec);
	}
}

/** First `{CHANNELED}`, `{REFLEX}`, or `{BLAZING}` strip - granted passives insert before this. */
int32 FindFirstActiveAbilityStripStart(const FString& S)
{
	static const TCHAR* Needles[] = {
		TEXT("{CHANNELED}, "), TEXT("{REFLEX}, "), TEXT("{BLAZING}, ")};
	int32 Best = INDEX_NONE;
	for (const TCHAR* Needle : Needles) {
		int32 SearchFrom = 0;
		while (SearchFrom < S.Len()) {
			const int32 Idx = S.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Idx == INDEX_NONE) {
				break;
			}
			const bool bAtStart = (Idx == 0);
			const bool bAfterBreak = Idx > 0 && (S[Idx - 1] == TEXT('\n')
				|| (S[Idx - 1] == TEXT(' ') && Idx >= 2 && S[Idx - 2] == TEXT('.')));
			if (bAtStart || bAfterBreak || (Idx > 0 && S[Idx - 1] == TEXT('.'))) {
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

FString FormatGrantedPassiveRulesStrip(const tactics::PassiveAbilitySpec& Passive, const bool bAdvanced)
{
	const FString Name = UTF8_TO_TCHAR(Passive.name.empty() ? Passive.key.c_str() : Passive.name.c_str());
	const std::string ChosenRules = CardTextForDisplay(Passive.normal_rules_text, Passive.rules_text, bAdvanced);
	if (ChosenRules.empty()) {
		return FString::Printf(TEXT("{PASSIVE}, %s"), *Name);
	}
	return FString::Printf(TEXT("{PASSIVE}, %s: %s"), *Name, UTF8_TO_TCHAR(ChosenRules.c_str()));
}

bool PassiveRulesStripAlreadyPresent(const FString& Rules, const FString& PassiveName)
{
	FString Probe = Rules;
	return TryReplacePassiveRulesPrefix(Probe, PassiveName);
}

/** Appends runtime-granted passives (not on the card definition) before active ability strips. */
void ApplyEntityGrantedPassiveStrips(FString& Rules, const tactics::Entity& Entity,
	const std::vector<std::string>& CardPassiveIds, const bool bAdvanced)
{
	TArray<FString> GrantedStrips;
	GrantedStrips.Reserve(static_cast<int32>(Entity.passive_abilities.size()));
	for (const tactics::PassiveAbilitySpec& Passive : Entity.passive_abilities) {
		if (Passive.key.empty()) {
			continue;
		}
		if (std::find(CardPassiveIds.begin(), CardPassiveIds.end(), Passive.key) != CardPassiveIds.end()) {
			continue;
		}
		const FString Name = UTF8_TO_TCHAR(Passive.name.empty() ? Passive.key.c_str() : Passive.name.c_str());
		if (PassiveRulesStripAlreadyPresent(Rules, Name)) {
			continue;
		}
		GrantedStrips.Add(FormatGrantedPassiveRulesStrip(Passive, bAdvanced));
	}
	if (GrantedStrips.IsEmpty()) {
		return;
	}
	const FString InsertBlock = FString::Join(GrantedStrips, TEXT(" "));
	const int32 ActiveStart = FindFirstActiveAbilityStripStart(Rules);
	if (ActiveStart == INDEX_NONE) {
		if (Rules.IsEmpty()) {
			Rules = InsertBlock;
		} else {
			Rules += TEXT(" ") + InsertBlock;
		}
		return;
	}
	FString Before = Rules.Left(ActiveStart);
	FString After = Rules.Mid(ActiveStart);
	Before.TrimEndInline();
	if (Before.IsEmpty()) {
		Rules = InsertBlock + TEXT(" ") + After;
	} else {
		Rules = Before + TEXT(" ") + InsertBlock + TEXT(" ") + After;
	}
}

const tactics::CardInstance* FindCardInstanceForEntity(const tactics::GameState& game, const tactics::Entity& entity)
{
	if (entity.source_card_id.empty()) {
		return nullptr;
	}
	for (const auto& [SeatId, Deck] : game.players_decks) {
		static_cast<void>(SeatId);
		const tactics::CardInstanceId CardId = Deck.find_card_by_public_id(entity.source_card_id);
		if (!CardId.is_valid()) {
			continue;
		}
		if (const tactics::CardInstance* Instance = Deck.pool.try_get(CardId)) {
			return Instance;
		}
	}
	return nullptr;
}

FString ResolveArtIdForEntity(const tactics::GameState& game, const tactics::Entity& entity)
{

	auto try_art_for_key = [](const std::string& Key) -> FString {
		if (Key.empty()) {
			return {};
		}
		if (const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(Key)) {
			if (!Def->art_id.empty()) {
				return UTF8_TO_TCHAR(Def->art_id.c_str());
			}
		}
		return {};
	};

	if (!entity.source_card_id.empty()) {
		for (const auto& [SeatId, Deck] : game.players_decks) {
			static_cast<void>(SeatId);
			const tactics::CardInstanceId CardId = Deck.find_card_by_public_id(entity.source_card_id);
			if (!CardId.is_valid()) {
				continue;
			}
			const tactics::CardInstance* Instance = Deck.pool.try_get(CardId);
			if (!Instance) {
				continue;
			}
			if (const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(Instance->definition_id)) {
				if (!Def->art_id.empty()) {
					return UTF8_TO_TCHAR(Def->art_id.c_str());
				}
			}
		}
		const std::string& SourceId = entity.source_card_id;
		if (FString Art = try_art_for_key(SourceId); !Art.IsEmpty()) {
			return Art;
		}
		if (SourceId.rfind("sandbox_", 0) == 0) {
			if (FString Art = try_art_for_key(SourceId.substr(8)); !Art.IsEmpty()) {
				return Art;
			}
		}
		const std::size_t Underscore = SourceId.rfind('_');
		if (Underscore != std::string::npos && Underscore > 0) {
			if (FString Art = try_art_for_key(SourceId.substr(0, Underscore)); !Art.IsEmpty()) {
				return Art;
			}
		}
	}

	// sandbox_p1_grease_monkeys -> grease_monkeys
	const std::string& Eid = entity.entity_id;
	if (Eid.rfind("sandbox_p", 0) == 0) {
		const std::size_t Prefix = Eid.find('_', 8);
		if (Prefix != std::string::npos && Prefix + 1 < Eid.size()) {
			if (FString Art = try_art_for_key(Eid.substr(Prefix + 1)); !Art.IsEmpty()) {
				return Art;
			}
		}
	}

	// Spawned tokens: "<token_type>_<source_entity_id>_<spawn_sequence>".
	static const std::pair<const char*, const char*> SpawnedTokenArtKeys[] = {
		{"flame_trooper_", "flame_trooper"},
	};
	for (const auto& [Prefix, CardKey] : SpawnedTokenArtKeys) {
		if (Eid.rfind(Prefix, 0) == 0) {
			if (FString Art = try_art_for_key(CardKey); !Art.IsEmpty()) {
				return Art;
			}
		}
	}

	return TEXT("token/token");
}

const tactics::CardDefinition* MatchCardDef(const tactics::GameState& game, int pid, tactics::CardInstanceId id)
{
	const auto deck_it = game.players_decks.find(pid);
	if (deck_it == game.players_decks.end()) {
		return nullptr;
	}
	const tactics::CardInstance* inst = deck_it->second.pool.try_get(id);
	if (!inst) {
		return nullptr;
	}
	return tactics::try_get_card_definition_ptr(inst->definition_id);
}

const tactics::CardInstance* MatchCardInst(const tactics::GameState& game, int pid, tactics::CardInstanceId id)
{
	const auto deck_it = game.players_decks.find(pid);
	if (deck_it == game.players_decks.end()) {
		return nullptr;
	}
	return deck_it->second.pool.try_get(id);
}

bool InBoundsWorld(const tactics::BoardCellBounds& B, int Wx, int Wy)
{
	if (B.empty()) {
		return false;
	}
	return Wx >= B.min_x && Wx <= B.max_x && Wy >= B.min_y && Wy <= B.max_y;
}

const TCHAR* AttackTypeToUi(tactics::AttackType T)
{
	switch (T) {
	case tactics::AttackType::Melee:
		return TEXT("Melee");
	case tactics::AttackType::Ranged:
		return TEXT("Ranged");
	case tactics::AttackType::Hybrid:
		return TEXT("Hybrid");
	case tactics::AttackType::Utility:
		return TEXT("Utility");
	default:
		return TEXT("?");
	}
}

std::optional<tactics::PendingMoveSelection> PendingMoveForEntity(const tactics::GameState& Game, const tactics::Unit& Unit)
{
	if (!Unit.owner) {
		return std::nullopt;
	}
	std::optional<tactics::PendingMoveSelection> Pending = Game.get_pending_move_for(*Unit.owner);
	if (!Pending || Pending->unit_entity_id != Unit.entity_id) {
		return std::nullopt;
	}
	return Pending;
}

bool PendingMoveFootprintContains(const tactics::Unit& Unit, const tactics::PendingMoveSelection& Pending, int WorldX, int WorldY)
{
	auto Shape = tactics::entity_shape_offsets(Unit);
	if (Pending.quarter_turns_cw != 0) {
		tactics::rotate_shape_offsets_n_quarters_cw(Shape, Pending.quarter_turns_cw);
	}
	for (const auto& [dx, dy] : Shape) {
		if (Pending.resolved_ax + dx == WorldX && Pending.resolved_ay + dy == WorldY) {
			return true;
		}
	}
	return false;
}

std::shared_ptr<tactics::Unit> UnitAtPendingMovePose(const tactics::Unit& Unit, const tactics::PendingMoveSelection& Pending)
{
	auto Preview = std::make_shared<tactics::Unit>(Unit);
	Preview->position = {Pending.resolved_ax, Pending.resolved_ay};
	Preview->shape = tactics::entity_shape_offsets(*Preview);
	if (Pending.quarter_turns_cw != 0) {
		tactics::rotate_shape_offsets_n_quarters_cw(Preview->shape, Pending.quarter_turns_cw);
	}
	Preview->occupied_positions.clear();
	for (const auto& [dx, dy] : Preview->shape) {
		Preview->occupied_positions.push_back({Pending.resolved_ax + dx, Pending.resolved_ay + dy});
	}
	return Preview;
}

std::shared_ptr<tactics::Unit> DisplayUnitAtWorld(const tactics::GameState& Game, int WorldX, int WorldY)
{
	for (const std::shared_ptr<tactics::Entity>& EntPtr : Game.board.all_entities()) {
		const auto U = std::dynamic_pointer_cast<tactics::Unit>(EntPtr);
		if (!U) {
			continue;
		}
		const auto Pending = PendingMoveForEntity(Game, *U);
		if (Pending && PendingMoveFootprintContains(*U, *Pending, WorldX, WorldY)) {
			return U;
		}
	}
	const auto E = Game.board.entity_at(WorldX, WorldY);
	const auto U = std::dynamic_pointer_cast<tactics::Unit>(E);
	if (U && PendingMoveForEntity(Game, *U)) {
		return nullptr;
	}
	return U;
}
std::map<std::string, std::string> spell_string_payload_from_shape(const FString& ShapeKey)
{
	std::map<std::string, std::string> out;
	if (!ShapeKey.IsEmpty()) {
		out.emplace("shape", TCHAR_TO_UTF8(*ShapeKey));
	}
	return out;
}


void apply_spell_preview_fields_from_def(const tactics::CardDefinition& Def,
    FString& OutEffectKey, FString& OutShapeKey, std::map<std::string, int>& OutPayload, int32& OutMaxRange)
{
	OutEffectKey.Reset();
	OutShapeKey = TEXT("rectangle");
	OutPayload.clear();
	OutMaxRange = 4;
	if (!tactics::definition_is_spell(Def)) {
		return;
	}
	const tactics::SpellCardDefinition& Spell = tactics::definition_spell(Def);
	OutEffectKey = StdToF(Spell.effect_key);
	OutPayload = Spell.effect_payload;
	if (const auto It = Spell.effect_string_payload.find("shape"); It != Spell.effect_string_payload.end()) {
		OutShapeKey = StdToF(It->second);
	}
	if (const auto It = Spell.effect_payload.find("max_range"); It != Spell.effect_payload.end()) {
		OutMaxRange = It->second;
	}
}

void apply_spell_preview_fields_from_mode(const tactics::CardDefinition& Def, const int32 ModeIndex0,
	FString& OutEffectKey, FString& OutShapeKey, std::map<std::string, int>& OutPayload, int32& OutMaxRange)
{
	apply_spell_preview_fields_from_def(Def, OutEffectKey, OutShapeKey, OutPayload, OutMaxRange);
	if (const tactics::SpellMode* Mode = tactics::try_definition_spell_mode(Def, ModeIndex0)) {
		OutEffectKey = StdToF(Mode->effect_key);
		OutPayload = Mode->effect_payload;
		OutShapeKey = TEXT("rectangle");
		if (const auto It = Mode->effect_string_payload.find("shape"); It != Mode->effect_string_payload.end()) {
			OutShapeKey = StdToF(It->second);
		}
		if (const auto It = Mode->effect_payload.find("max_range"); It != Mode->effect_payload.end()) {
			OutMaxRange = It->second;
		}
	}
}

bool try_get_selected_ability_spec(const tactics::Unit& Unit, const std::string& KeyUtf8, tactics::AbilitySpec& OutAbility)
{
	for (const tactics::AbilitySpec& Ability : Unit.activated_abilities) {
		if (Ability.key == KeyUtf8) {
			OutAbility = Ability;
			return true;
		}
	}
	return tactics::try_get_ability_from_catalog(KeyUtf8, OutAbility);
}

void AppendActiveEffectRowsFromEntity(const tactics::Entity& Entity, const bool bAdvancedGlossary,
	TArray<FTacticsActiveEffectEntry>& OutEffects)
{
	std::vector<tactics::CardGlossaryEntry> Collected;
	tactics::collect_entity_active_glossary_entries(Entity, Collected, bAdvancedGlossary);
	for (const tactics::CardGlossaryEntry& Entry : Collected) {
		if (Entry.name.empty() || Entry.body.empty()) {
			continue;
		}
		FTacticsActiveEffectEntry Row;
		Row.Key = UTF8_TO_TCHAR(Entry.dedupe_key.c_str());
		Row.Name = UTF8_TO_TCHAR(Entry.name.c_str());
		Row.Body = UTF8_TO_TCHAR(Entry.body.c_str());
		Row.bNegative = Entry.is_negative;
		Row.bPositive = Entry.is_positive;
		OutEffects.Add(std::move(Row));
	}
}


const tactics::Entity* FindEntityById(const tactics::GameState& Game, const std::string& EntityId)
{
	if (EntityId.empty()) {
		return nullptr;
	}
	const auto It = Game.board.all_entities_map.find(EntityId);
	if (It == Game.board.all_entities_map.end() || !It->second) {
		return nullptr;
	}
	return It->second.get();
}

FString DisplayNameForEntity(const tactics::GameState& Game, const tactics::Entity& Entity)
{
	if (!Entity.source_card_id.empty()) {
		if (const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(Entity.source_card_id)) {
			if (!Def->name.empty()) {
				return UTF8_TO_TCHAR(Def->name.c_str());
			}
		}
		for (const auto& [SeatId, Deck] : Game.players_decks) {
			static_cast<void>(SeatId);
			const tactics::CardInstanceId CardId = Deck.find_card_by_public_id(Entity.source_card_id);
			if (!CardId.is_valid()) {
				continue;
			}
			const tactics::CardInstance* Instance = Deck.pool.try_get(CardId);
			if (!Instance) {
				continue;
			}
			if (const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(Instance->definition_id)) {
				if (!Def->name.empty()) {
					return UTF8_TO_TCHAR(Def->name.c_str());
				}
			}
		}
	}
	if (const auto* AsUnit = dynamic_cast<const tactics::Unit*>(&Entity)) {
		if (!AsUnit->unit_type.empty()) {
			return UTF8_TO_TCHAR(AsUnit->unit_type.c_str());
		}
	}
	return UTF8_TO_TCHAR(Entity.entity_id.c_str());
}

FString SourceTypeLineForStackItem(const tactics::StackItem& Item)
{
	if (Item.source_type == "focus_spell") {
		return TEXT("FOCUS SPELL");
	}
	if (Item.source_type == "ability") {
		return TEXT("ABILITY");
	}
	if (Item.source_type == "spell") {
		return TEXT("SPELL");
	}
	return FString(UTF8_TO_TCHAR(Item.source_type.c_str())).ToUpper();
}

const tactics::CardDefinition* ResolveCardDefinitionForSpellStackItem(const tactics::StackItem& Item)
{
	const std::string& EffectKey = Item.effect_key;
	const std::string& SourceName = Item.source_name;
	const tactics::CardDefinition* EffectMatch = nullptr;
	for (const std::string& Key : tactics::list_card_catalog_keys_sorted()) {
		const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(Key);
		if (!Def || !Def->spell.has_value() || Def->spell->effect_key != EffectKey) {
			continue;
		}
		if (!SourceName.empty() && Def->name == SourceName) {
			return Def;
		}
		if (!EffectMatch) {
			EffectMatch = Def;
		}
	}
	return EffectMatch;
}

FString ResolveArtIdForSpellStackItem(const tactics::StackItem& Item)
{
	if (const tactics::CardDefinition* Def = ResolveCardDefinitionForSpellStackItem(Item)) {
		if (!Def->art_id.empty()) {
			return UTF8_TO_TCHAR(Def->art_id.c_str());
		}
	}
	return {};
}

FString ResolveStackItemSourceDescription(const tactics::StackItem& Item, const bool bAdvancedCardText)
{
	if (Item.source_type == "spell" || Item.source_type == "focus_spell") {
		if (const tactics::CardDefinition* Def = ResolveCardDefinitionForSpellStackItem(Item)) {
			const std::string ChosenRules = CardTextForDisplay(Def->normal_rules_text, Def->rules_text, bAdvancedCardText);
			if (!ChosenRules.empty()) {
				return UTF8_TO_TCHAR(ChosenRules.c_str());
			}
		}
	} else if (Item.source_type == "ability") {
		tactics::AbilitySpec Spec;
		if (!Item.source_ability_key.empty() && tactics::try_get_ability_from_catalog(Item.source_ability_key, Spec)) {
			const std::string ChosenDesc = CardTextForDisplay(Spec.normal_description, Spec.description, bAdvancedCardText);
			if (!ChosenDesc.empty()) {
				return UTF8_TO_TCHAR(ChosenDesc.c_str());
			}
		}
	}
	tactics::EffectDefinition EffectDef;
	if (tactics::try_get_effect_definition(Item.effect_key, EffectDef) && !EffectDef.rules_text.empty()) {
		return UTF8_TO_TCHAR(EffectDef.rules_text.c_str());
	}
	return {};
}

void AppendEntityFootprintCells(const tactics::Entity& Entity, TSet<FIntPoint>& Out)
{
	if (!Entity.occupied_positions.empty()) {
		for (const auto& [Wx, Wy] : Entity.occupied_positions) {
			Out.Add(FIntPoint(Wx, Wy));
		}
		return;
	}
	if (!Entity.position) {
		return;
	}
	const int Ax = Entity.position->first;
	const int Ay = Entity.position->second;
	for (const auto& [Dx, Dy] : tactics::entity_shape_offsets(Entity)) {
		Out.Add(FIntPoint(Ax + Dx, Ay + Dy));
	}
}

void AppendEntityFootprintById(const tactics::GameState& Game, const std::string& EntityId, TSet<FIntPoint>& Out)
{
	if (EntityId.empty()) {
		return;
	}
	if (const tactics::Entity* Entity = FindEntityById(Game, EntityId)) {
		AppendEntityFootprintCells(*Entity, Out);
	}
}

void AppendStackItemTargetFootprints(const tactics::GameState& Game, const tactics::StackItem& Item, TSet<FIntPoint>& Out)
{
	if (!Item.target_entity_id.empty()) {
		AppendEntityFootprintById(Game, Item.target_entity_id, Out);
		return;
	}
	const auto XIt = Item.targets.find(tactics::effect_keys::kCellX);
	const auto YIt = Item.targets.find(tactics::effect_keys::kCellY);
	if (XIt == Item.targets.end() || YIt == Item.targets.end()) {
		return;
	}
	if (const auto Target = Game.board.entity_at(XIt->second, YIt->second)) {
		AppendEntityFootprintCells(*Target, Out);
	}
}

void FillEntityHoverCard(const tactics::GameState& Game, const tactics::Entity& Entity, FTacticsActionQueueHoverCardUi& Out)
{
	Out.bVisible = true;
	Out.Label = DisplayNameForEntity(Game, Entity);
	Out.TypeLine = TEXT("UNIT");
	Out.ArtId = ResolveArtIdForEntity(Game, Entity);
}

FString FormatDamageRangeForUi(const tactics::DamageRange& Range)
{
	if (Range.min <= 0 && Range.max <= 0) {
		return TEXT("0");
	}
	if (Range.max <= 0 || Range.min == Range.max) {
		const int V = Range.max > 0 ? Range.max : Range.min;
		return FString::FromInt(V);
	}
	return FString::Printf(TEXT("%d-%d"), Range.min, Range.max);
}

FString BuildCombatStatMarkup(const int Hp, const int MaxHp, const FString& AttackToken, const FString& DamageText,
	const int Armor)
{
	TArray<FString> Parts;
	Parts.Add(FString::Printf(TEXT("{LIFE} %d/%d"), Hp, MaxHp));
	if (!AttackToken.IsEmpty() && !DamageText.IsEmpty()) {
		Parts.Add(FString::Printf(TEXT("%s %s"), *AttackToken, *DamageText));
	}
	if (Armor > 0) {
		Parts.Add(FString::Printf(TEXT("{ARMOR} %d"), Armor));
	}
	return FString::Join(Parts, TEXT(" \u00b7 "));
}

struct FAttackCompareDamagePreview
{
	bool bValid{false};
	bool bRanged{false};
	bool bReturnFire{false};
	tactics::DamageRange Range{};
};

FAttackCompareDamagePreview PreviewCounterattackDamage(const tactics::GameState& Game, const tactics::Unit& Defender,
	const tactics::Unit& Attacker)
{
	FAttackCompareDamagePreview Out;
	if (Defender.entity_type != "unit" || !Defender.position || !Attacker.position) {
		return Out;
	}
	if (tactics::core_cracker_shutdown_blocks_actions(Defender)) {
		return Out;
	}
	if (Defender.reactions_remaining_this_turn <= 0 && !tactics::has_vigilance(Defender)) {
		return Out;
	}
	const int Dist = tactics::min_chebyshev_entity_to_cell(Defender, Attacker.position->first, Attacker.position->second);
	if ((Defender.attack_type == tactics::AttackType::Melee || Defender.attack_type == tactics::AttackType::Hybrid)
		&& Dist <= Defender.melee_range) {
		Out.bValid = true;
		Out.bRanged = false;
		Out.Range = tactics::unit_effective_melee_damage_range(Defender);
		return Out;
	}
	if (tactics::has_return_fire(Defender)) {
		const tactics::AttackProfile Ranged = tactics::attack_profile_for_unit(Defender, true);
		const bool bInMeleeRange = Dist <= Defender.melee_range;
		const bool bInRangedBand = Ranged.use_ranged && Dist >= Ranged.range_min && Dist <= Ranged.range_max;
		const bool bDeadzoneBlocksMeleeReturnFire = Ranged.range_min > 0 && bInMeleeRange;
		if (Ranged.use_ranged && bInRangedBand && !bDeadzoneBlocksMeleeReturnFire
			&& (!Ranged.requires_line_of_sight || tactics::ignores_attack_line_of_sight(Defender)
				|| tactics::entity_has_line_of_sight_to_cell(Game, Defender, *Attacker.position))) {
			Out.bValid = true;
			Out.bRanged = true;
			Out.bReturnFire = true;
			Out.Range = Ranged.damage_range;
		}
	}
	return Out;
}

void FillAttackCompareUi(const tactics::GameState& Game, const tactics::GameState::AttackDeclaration& Decl,
	FTacticsActionQueueAttackCompareUi& Out)
{
	Out = {};
	const tactics::Entity* AttackerEntity = FindEntityById(Game, Decl.attacker_id);
	const std::shared_ptr<tactics::Entity> TargetEntity = Game.board.entity_at(Decl.target_x, Decl.target_y);
	if (!AttackerEntity || !TargetEntity) {
		return;
	}
	const auto* AttackerUnit = dynamic_cast<const tactics::Unit*>(AttackerEntity);
	const std::shared_ptr<tactics::Unit> TargetUnit = std::dynamic_pointer_cast<tactics::Unit>(TargetEntity);
	if (!AttackerUnit || !TargetUnit) {
		return;
	}
	const tactics::AttackProfile AttackProfile = tactics::attack_profile_for_unit(*AttackerUnit, Decl.ranged);
	const FString AttackerAttackToken = Decl.ranged ? TEXT("{RANGED}") : TEXT("{MELEE}");
	Out.AttackerStatsMarkup = BuildCombatStatMarkup(
		AttackerUnit->current_health,
		tactics::entity_effective_base_health(*AttackerUnit),
		AttackerAttackToken,
		FormatDamageRangeForUi(AttackProfile.damage_range),
		tactics::armor_value(*AttackerUnit));

	const FAttackCompareDamagePreview Counter = PreviewCounterattackDamage(Game, *TargetUnit, *AttackerUnit);
	FString TargetAttackToken;
	FString TargetDamageText;
	if (Counter.bValid) {
		TargetAttackToken = Counter.bRanged ? TEXT("{RANGED}") : TEXT("{MELEE}");
		TargetDamageText = FormatDamageRangeForUi(Counter.Range);
		Out.TargetCounterLabel = Counter.bReturnFire ? TEXT("Return fire") : TEXT("Counter");
	}
	Out.TargetStatsMarkup = BuildCombatStatMarkup(
		TargetUnit->current_health,
		tactics::entity_effective_base_health(*TargetUnit),
		TargetAttackToken,
		TargetDamageText,
		tactics::armor_value(*TargetUnit));
	Out.bShow = true;
}

void FillStackItemSourceHoverCard(const tactics::GameState& Game, const tactics::StackItem& Item,
	FTacticsActionQueueHoverCardUi& Out, const bool bAdvancedCardText)
{
	Out.bVisible = true;
	Out.Label = Item.source_name.empty() ? UTF8_TO_TCHAR(Item.effect_key.c_str()) : UTF8_TO_TCHAR(Item.source_name.c_str());
	Out.TypeLine = SourceTypeLineForStackItem(Item);
	Out.ArtId = ResolveArtIdForSpellStackItem(Item);
	if (Out.ArtId.IsEmpty() && Item.source_type != "focus_spell" && !Item.source_entity_id.empty()) {
		if (const tactics::Entity* Source = FindEntityById(Game, Item.source_entity_id)) {
			Out.ArtId = ResolveArtIdForEntity(Game, *Source);
		}
	}
	if (Item.source_type == "spell" || Item.source_type == "focus_spell" || Item.source_type == "ability") {
		Out.Description = ResolveStackItemSourceDescription(Item, bAdvancedCardText);
	}
}

const tactics::StackItem* FindQueuedStackItemById(const std::vector<tactics::GameState::AttackPhaseEntry>& Queue,
	const std::string& ItemId)
{
	if (ItemId.empty()) {
		return nullptr;
	}
	for (const tactics::GameState::AttackPhaseEntry& Entry : Queue) {
		if (!Entry.is_attack && Entry.spell_item.item_id == ItemId) {
			return &Entry.spell_item;
		}
	}
	return nullptr;
}

void FillStackItemTargetHoverCard(const tactics::GameState& Game, const tactics::StackItem& Item,
	const std::vector<tactics::GameState::AttackPhaseEntry>& Queue, FTacticsActionQueueHoverCardUi& Out,
	const bool bAdvancedCardText)
{
	if (!Item.target_stack_item_id.empty()) {
		if (const tactics::StackItem* Targeted = FindQueuedStackItemById(Queue, Item.target_stack_item_id)) {
			FillStackItemSourceHoverCard(Game, *Targeted, Out, bAdvancedCardText);
			return;
		}
	}
	if (!Item.target_entity_id.empty()) {
		if (const tactics::Entity* Target = FindEntityById(Game, Item.target_entity_id)) {
			FillEntityHoverCard(Game, *Target, Out);
			return;
		}
	}
	if (Item.targets.contains(tactics::effect_keys::kCellX) && Item.targets.contains(tactics::effect_keys::kCellY)) {
		const int Cx = Item.targets.at(tactics::effect_keys::kCellX);
		const int Cy = Item.targets.at(tactics::effect_keys::kCellY);
		if (const auto Target = Game.board.entity_at(Cx, Cy)) {
			FillEntityHoverCard(Game, *Target, Out);
			return;
		}
		Out.bVisible = true;
		Out.Label = FString::Printf(TEXT("Cell %d,%d"), Cx, Cy);
		Out.TypeLine = TEXT("CELL");
		Out.ArtId = TEXT("token/token");
	}
}

bool HoverTargetCardKey(const FTacticsActionQueueHoverCardUi& Card)
{
	return Card.bVisible && (!Card.Label.IsEmpty() || !Card.ArtId.IsEmpty());
}

void AppendUniqueHoverTargetCard(TArray<FTacticsActionQueueHoverCardUi>& Targets, FTacticsActionQueueHoverCardUi Card)
{
	if (!HoverTargetCardKey(Card)) {
		return;
	}
	for (const FTacticsActionQueueHoverCardUi& Existing : Targets) {
		if (Existing.Label == Card.Label && Existing.ArtId == Card.ArtId && Existing.TypeLine == Card.TypeLine) {
			return;
		}
	}
	Targets.Add(MoveTemp(Card));
}

void CollectEntityTargetsFromBlastCells(tactics::GameState& Game,
	const std::vector<tactics::AbilityResolveVizBlastCell>& BlastCells, TArray<FTacticsActionQueueHoverCardUi>& OutTargets)
{
	TSet<FString> SeenEntityIds;
	for (const tactics::AbilityResolveVizBlastCell& Blast : BlastCells) {
		const std::shared_ptr<tactics::Entity> EntityPtr = Game.board.entity_at(Blast.grid_x, Blast.grid_y);
		if (!EntityPtr) {
			continue;
		}
		const FString EntityId = UTF8_TO_TCHAR(EntityPtr->entity_id.c_str());
		if (SeenEntityIds.Contains(EntityId)) {
			continue;
		}
		SeenEntityIds.Add(EntityId);
		FTacticsActionQueueHoverCardUi Card;
		FillEntityHoverCard(Game, *EntityPtr, Card);
		AppendUniqueHoverTargetCard(OutTargets, MoveTemp(Card));
	}
}

void FillStackItemTargetHoverCards(tactics::GameState& Game, const tactics::StackItem& Item,
	const std::vector<tactics::GameState::AttackPhaseEntry>& Queue, TArray<FTacticsActionQueueHoverCardUi>& OutTargets,
	const bool bAdvancedCardText)
{
	OutTargets.Reset();
	if (!Item.multicast_cast_id.empty()) {
		for (const tactics::GameState::AttackPhaseEntry& Entry : Queue) {
			if (Entry.is_attack || Entry.spell_item.multicast_cast_id != Item.multicast_cast_id) {
				continue;
			}
			FTacticsActionQueueHoverCardUi Card;
			FillStackItemTargetHoverCard(Game, Entry.spell_item, Queue, Card, bAdvancedCardText);
			AppendUniqueHoverTargetCard(OutTargets, MoveTemp(Card));
		}
		if (!OutTargets.IsEmpty()) {
			return;
		}
	}

	const tactics::AbilityResolveVizPreview Preview = tactics::build_ability_resolve_viz_preview(Game, Item);
	if (!Preview.blast_cells.empty()) {
		CollectEntityTargetsFromBlastCells(Game, Preview.blast_cells, OutTargets);
		if (!OutTargets.IsEmpty()) {
			return;
		}
	}

	FTacticsActionQueueHoverCardUi Card;
	FillStackItemTargetHoverCard(Game, Item, Queue, Card, bAdvancedCardText);
	AppendUniqueHoverTargetCard(OutTargets, MoveTemp(Card));
}

void AppendUniqueGlossaryRows(TArray<FTacticsCardGlossaryEntry>& Out, const std::vector<tactics::CardGlossaryEntry>& In)
{
	TSet<FString> Seen;
	for (const FTacticsCardGlossaryEntry& Row : Out) {
		Seen.Add(Row.Key);
	}
	for (const tactics::CardGlossaryEntry& Entry : In) {
		const FString Key = UTF8_TO_TCHAR(Entry.dedupe_key.c_str());
		if (Key.IsEmpty() || Seen.Contains(Key)) {
			continue;
		}
		Seen.Add(Key);
		FTacticsCardGlossaryEntry Row;
		Row.Key = Key;
		Row.Name = UTF8_TO_TCHAR(Entry.name.c_str());
		Row.Body = UTF8_TO_TCHAR(Entry.body.c_str());
		Out.Add(MoveTemp(Row));
	}
}

void CopyCollectedGlossaryToUe(const std::vector<tactics::CardGlossaryEntry>& Collected, TArray<FTacticsCardGlossaryEntry>& Out)
{
	Out.Reset();
	Out.Reserve(static_cast<int32>(Collected.size()));
	for (const tactics::CardGlossaryEntry& Entry : Collected) {
		FTacticsCardGlossaryEntry Row;
		Row.Key = UTF8_TO_TCHAR(Entry.dedupe_key.c_str());
		Row.Name = UTF8_TO_TCHAR(Entry.name.c_str());
		Row.Body = UTF8_TO_TCHAR(Entry.body.c_str());
		Out.Add(MoveTemp(Row));
	}
}

void FillGlossaryForStackItemSource(const tactics::GameState& Game, const tactics::StackItem& Item,
	TArray<FTacticsCardGlossaryEntry>& OutGlossary, const bool bAdvancedCardText)
{
	OutGlossary.Reset();
	if (Item.source_type == "spell" || Item.source_type == "focus_spell") {
		if (const tactics::CardDefinition* Def = ResolveCardDefinitionForSpellStackItem(Item)) {
			std::vector<tactics::CardGlossaryEntry> Collected;
			tactics::collect_card_glossary_entries(*Def, Collected, bAdvancedCardText);
			CopyCollectedGlossaryToUe(Collected, OutGlossary);
		}
		return;
	}
	if (Item.source_type == "ability") {
		if (!Item.source_entity_id.empty()) {
			if (const tactics::Entity* Source = FindEntityById(Game, Item.source_entity_id)) {
				const tactics::CardInstance* Inst = FindCardInstanceForEntity(Game, *Source);
				const tactics::CardDefinition* Def = nullptr;
				if (Inst) {
					Def = tactics::try_get_card_definition_ptr(Inst->definition_id);
				}
				if (!Def && !Source->source_card_id.empty()) {
					Def = tactics::try_get_card_definition_ptr(Source->source_card_id);
				}
				if (Def) {
					std::vector<tactics::CardGlossaryEntry> Collected;
					tactics::collect_card_glossary_entries(*Def, Collected, bAdvancedCardText);
					CopyCollectedGlossaryToUe(Collected, OutGlossary);
				}
			}
		}
		const FString Desc = ResolveStackItemSourceDescription(Item, bAdvancedCardText);
		if (!Desc.IsEmpty()) {
			std::vector<tactics::CardGlossaryEntry> FromRules;
			tactics::collect_glossary_from_rules_text(TCHAR_TO_UTF8(*Desc), FromRules, bAdvancedCardText);
			AppendUniqueGlossaryRows(OutGlossary, FromRules);
		}
	}
}

FString BuildPhaseQueueEntryFingerprint(const tactics::GameState::AttackPhaseEntry& Entry)
{
	if (Entry.is_attack) {
		return FString::Printf(TEXT("attack:%s:%d:%d:%d"),
			UTF8_TO_TCHAR(Entry.attack.attacker_id.c_str()),
			Entry.attack.target_x,
			Entry.attack.target_y,
			Entry.attack.ranged ? 1 : 0);
	}
	return FString::Printf(TEXT("spell:%s"), UTF8_TO_TCHAR(Entry.spell_item.item_id.c_str()));
}

FString BuildPausedBlazingFingerprint(const tactics::StackItem& Item)
{
	return FString::Printf(TEXT("blazing:%s"), UTF8_TO_TCHAR(Item.item_id.c_str()));
}

bool IsCardPlayAuthorityLine(const FString& Line)
{
	FString Trimmed = Line.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) {
		return false;
	}
	FString Cmd = Trimmed;
	int32 Sp = INDEX_NONE;
	if (Trimmed.FindChar(TEXT(' '), Sp)) {
		Cmd = Trimmed.Left(Sp);
	}
	Cmd.ToLowerInline();
	return Cmd == TEXT("cast") || Cmd == TEXT("cast_reserve") || Cmd == TEXT("ability") || Cmd == TEXT("attack")
		|| Cmd == TEXT("deploy") || Cmd == TEXT("deploy_reserve");
}

int32 ResolveQueueEntryControllerId(const tactics::GameState& Game, const tactics::GameState::AttackPhaseEntry& Entry)
{
	if (Entry.is_attack) {
		if (const tactics::Entity* Attacker = FindEntityById(Game, Entry.attack.attacker_id)) {
			return Attacker->owner.value_or(0);
		}
		return 0;
	}
	return Entry.spell_item.controller_id;
}

void PopulateSpellStackItemHoverOverlay(tactics::GameState& Game, const tactics::StackItem& Item,
	const std::vector<tactics::GameState::AttackPhaseEntry>& Queue, const bool bAdvancedCardText,
	FTacticsActionQueueHoverOverlayUi& OutPreview)
{
	if (Item.source_type == "focus_spell" && !Item.source_entity_id.empty()) {
		if (const tactics::Entity* Caster = FindEntityById(Game, Item.source_entity_id)) {
			FillEntityHoverCard(Game, *Caster, OutPreview.FocusCaster);
		}
	}
	FillStackItemSourceHoverCard(Game, Item, OutPreview.Source, bAdvancedCardText);
	if (Item.source_type == "focus_spell") {
		OutPreview.Source.TypeLine = TEXT("SPELL");
	}
	FillStackItemTargetHoverCards(Game, Item, Queue, OutPreview.Targets, bAdvancedCardText);
	FillGlossaryForStackItemSource(Game, Item, OutPreview.SourceGlossary, bAdvancedCardText);
	OutPreview.bShowPreview = OutPreview.Source.bVisible || OutPreview.FocusCaster.bVisible;
	OutPreview.bShowArrow = (OutPreview.FocusCaster.bVisible || OutPreview.Source.bVisible) && !OutPreview.Targets.IsEmpty();
}

bool BuildHoverOverlayForQueueEntry(tactics::GameState& Game, const tactics::GameState::AttackPhaseEntry& Entry,
	const std::vector<tactics::GameState::AttackPhaseEntry>& Queue, const bool bAdvancedCardText,
	FTacticsActionQueueHoverOverlayUi& OutPreview)
{
	OutPreview = {};
	if (Entry.is_attack) {
		OutPreview.bIsAttack = true;
		if (const tactics::Entity* Attacker = FindEntityById(Game, Entry.attack.attacker_id)) {
			FillEntityHoverCard(Game, *Attacker, OutPreview.Source);
		} else {
			OutPreview.Source.bVisible = true;
			OutPreview.Source.Label = UTF8_TO_TCHAR(Entry.attack.attacker_id.c_str());
			OutPreview.Source.TypeLine = TEXT("ATTACK");
		}
		FTacticsActionQueueHoverCardUi TargetCard;
		if (const auto Target = Game.board.entity_at(Entry.attack.target_x, Entry.attack.target_y)) {
			FillEntityHoverCard(Game, *Target, TargetCard);
		} else {
			TargetCard.bVisible = true;
			TargetCard.Label = FString::Printf(TEXT("Cell %d,%d"), Entry.attack.target_x, Entry.attack.target_y);
			TargetCard.TypeLine = TEXT("CELL");
			TargetCard.ArtId = TEXT("token/token");
		}
		AppendUniqueHoverTargetCard(OutPreview.Targets, MoveTemp(TargetCard));
		FillAttackCompareUi(Game, Entry.attack, OutPreview.AttackCompare);
		OutPreview.bShowPreview = OutPreview.Source.bVisible;
		OutPreview.bShowArrow = OutPreview.Source.bVisible && !OutPreview.Targets.IsEmpty();
	} else {
		PopulateSpellStackItemHoverOverlay(Game, Entry.spell_item, Queue, bAdvancedCardText, OutPreview);
	}
	return OutPreview.bShowPreview;
}

bool BuildHoverOverlayForStackItem(tactics::GameState& Game, const tactics::StackItem& Item,
	const std::vector<tactics::GameState::AttackPhaseEntry>& Queue, const bool bAdvancedCardText,
	FTacticsActionQueueHoverOverlayUi& OutPreview)
{
	OutPreview = {};
	PopulateSpellStackItemHoverOverlay(Game, Item, Queue, bAdvancedCardText, OutPreview);
	return OutPreview.bShowPreview;
}

int card_definition_armor_amount(const tactics::CardDefinition& def)
{
	if (!tactics::definition_is_unit(def)) {
		return 0;
	}
	auto scan_keywords = [](const std::vector<tactics::CardKeywordDefinition>& keywords) -> int {
		int best = 0;
		for (const tactics::CardKeywordDefinition& kw : keywords) {
			if (kw.key != "armor") {
				continue;
			}
			const int amount = kw.amount.has_value() ? std::max(1, *kw.amount) : 1;
			best = std::max(best, amount);
		}
		return best;
	};
	int armor = scan_keywords(def.keywords);
	armor = std::max(armor, scan_keywords(tactics::definition_unit(def).keywords));
	return armor;
}

/** Live board-unit overrides for stat row (-1 = use card definition). */
struct FUnitStatLiveOverrides {
	int32 MeleeMin = -1;
	int32 MeleeMax = -1;
	int32 RangedMin = -1;
	int32 RangedMax = -1;
	int32 Movement = -1;
	int32 RangedRange = -1;
};

// Compact icon-token stat line for a unit/building card: "{LIFE} cur/max", "{MELEE} X-Y", etc.
// Advanced view appends "Crit N%" (live value on board, card def in hand/reserves). Empty for non-units.
// LiveArmorOverride >= 0 uses live board armor (includes defend stance and temporary stacks); -1 uses card def.
FString build_unit_stat_tokens(const tactics::CardDefinition& def, const bool bIncludeCritChance = false,
	const int32 CritChancePercentOverride = -1, const int32 LiveCurrentHealth = -1, const int32 LiveMaxHealth = -1,
	const int32 LiveArmorOverride = -1, const FUnitStatLiveOverrides& Live = FUnitStatLiveOverrides{})
{
	if (!tactics::definition_is_unit(def)) {
		return FString();
	}
	const tactics::UnitCardDefinition& ud = tactics::definition_unit(def);
	const bool bBuilding = ud.entity_type == "building";
	const bool bObstacle = ud.entity_type == "breakable_obstacle";
	auto dmg = [](int lo, int hi, int fallback) -> FString {
		if (lo <= 0 && hi <= 0) {
			return fallback > 0 ? FString::FromInt(fallback) : FString();
		}
		if (hi <= 0) {
			return FString::FromInt(lo > 0 ? lo : hi);
		}
		return FString::Printf(TEXT("%d-%d"), lo, hi);
	};
	// Gate each attack by the unit's attack type so a ranged unit doesn't show a stray melee icon
	// from the default melee_damage; an explicit min/max still shows a real secondary attack.
	const bool bMeleeAtk = ud.attack_type == tactics::AttackType::Melee || ud.attack_type == tactics::AttackType::Hybrid;
	const bool bRangedAtk = ud.attack_type == tactics::AttackType::Ranged || ud.attack_type == tactics::AttackType::Hybrid;
	const bool bShowMelee = bMeleeAtk || ud.melee_damage_min > 0 || ud.melee_damage_max > 0;
	const bool bShowRanged = bRangedAtk || ud.ranged_damage_min > 0 || ud.ranged_damage_max > 0;
	TArray<FString> Parts;
	if (LiveCurrentHealth >= 0 && LiveMaxHealth > 0) {
		Parts.Add(FString::Printf(TEXT("{LIFE} %d/%d"), LiveCurrentHealth, LiveMaxHealth));
	} else {
		Parts.Add(FString::Printf(TEXT("{LIFE} %d/%d"), ud.base_health, ud.base_health));
	}
	if (!bObstacle) {
		const int32 MeleeLo = Live.MeleeMin >= 0 ? Live.MeleeMin : ud.melee_damage_min;
		const int32 MeleeHi = Live.MeleeMax >= 0 ? Live.MeleeMax : ud.melee_damage_max;
		const int32 MeleeFallback = Live.MeleeMin >= 0 ? Live.MeleeMin : ud.melee_damage;
		const FString Melee = bShowMelee ? dmg(MeleeLo, MeleeHi, MeleeFallback) : FString();
		if (!Melee.IsEmpty()) {
			Parts.Add(FString::Printf(TEXT("{MELEE} %s"), *Melee));
		}
		const int32 RangedLo = Live.RangedMin >= 0 ? Live.RangedMin : ud.ranged_damage_min;
		const int32 RangedHi = Live.RangedMax >= 0 ? Live.RangedMax : ud.ranged_damage_max;
		const int32 RangedFallback = Live.RangedMin >= 0 ? Live.RangedMin : ud.ranged_damage;
		const FString Ranged = bShowRanged ? dmg(RangedLo, RangedHi, RangedFallback) : FString();
		if (!Ranged.IsEmpty()) {
			// Ranged attack pairs its damage with its reach in one group (no comma between the icons).
			FString RangedGroup = FString::Printf(TEXT("{RANGED} %s"), *Ranged);
			const int32 RangeDisplay = Live.RangedRange >= 0 ? Live.RangedRange : ud.ranged_range;
			if (RangeDisplay > 0) {
				RangedGroup += FString::Printf(TEXT(" {RANGE} %d"), RangeDisplay);
			}
			Parts.Add(RangedGroup);
		}
		const int32 ArmorAmount = LiveArmorOverride >= 0 ? LiveArmorOverride : card_definition_armor_amount(def);
		if (!bBuilding) {
			const int32 MoveDisplay = Live.Movement >= 0 ? Live.Movement : ud.movement;
			FString MovePart = FString::Printf(TEXT("{MOVE} %d"), MoveDisplay);
			if (ArmorAmount > 0) {
				MovePart += FString::Printf(TEXT(" {ARMOR} %d"), ArmorAmount);
			}
			Parts.Add(MovePart);
		} else if (ArmorAmount > 0) {
			Parts.Add(FString::Printf(TEXT("{ARMOR} %d"), ArmorAmount));
		}
	}
	if (bIncludeCritChance && !bObstacle) {
		const int32 Crit = CritChancePercentOverride >= 0 ? CritChancePercentOverride : ud.crit_chance_percent;
		Parts.Add(FString::Printf(TEXT("Crit %d%%"), Crit));
	}
	return FString::Join(Parts, TEXT(" \u00b7 "));
}

void CopyGlossaryToUe(const std::vector<tactics::CardGlossaryEntry>& Src, TArray<FTacticsCardGlossaryEntry>& Out)
{
	Out.Reset();
	Out.Reserve(static_cast<int32>(Src.size()));
	for (const tactics::CardGlossaryEntry& Entry : Src) {
		FTacticsCardGlossaryEntry Row;
		Row.Key = UTF8_TO_TCHAR(Entry.dedupe_key.c_str());
		Row.Name = UTF8_TO_TCHAR(Entry.name.c_str());
		Row.Body = UTF8_TO_TCHAR(Entry.body.c_str());
		Out.Add(std::move(Row));
	}
}

bool TryFindStockpileClauseStart(const FString& Rules, const int32 SearchFrom, int32& OutFound, int32& OutClauseLen)
{
	const int32 KwFound = Rules.Find(TEXT("{KW:stockpile"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
	const int32 WordFound = Rules.Find(TEXT("Stockpile"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
	int32 Found = INDEX_NONE;
	int32 ClauseLen = 0;
	if (KwFound != INDEX_NONE && (WordFound == INDEX_NONE || KwFound <= WordFound)) {
		const int32 Close = Rules.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KwFound);
		if (Close == INDEX_NONE) {
			return false;
		}
		Found = KwFound;
		ClauseLen = Close - KwFound + 1;
	} else if (WordFound != INDEX_NONE) {
		Found = WordFound;
		ClauseLen = 9;
	} else {
		return false;
	}
	OutFound = Found;
	OutClauseLen = ClauseLen;
	return true;
}

/** Removes Stockpile clauses from rules prose (board unit detail - stockpile stays in hand/reserves only). */
void StripStockpileMentionsFromRulesText(FString& Rules)
{
	if (Rules.IsEmpty()) {
		return;
	}
	for (;;) {
		int32 Found = INDEX_NONE;
		int32 ClauseLen = 0;
		if (!TryFindStockpileClauseStart(Rules, 0, Found, ClauseLen)) {
			break;
		}
		int32 End = Found + ClauseLen;
		while (End < Rules.Len() && Rules[End] == TEXT(' ')) {
			++End;
		}
		while (End < Rules.Len() && (FChar::IsDigit(Rules[End]) || Rules[End] == TEXT('/'))) {
			++End;
		}
		while (End < Rules.Len() && Rules[End] == TEXT(' ')) {
			++End;
		}
		if (End + 4 <= Rules.Len() && Rules.Mid(End, 4).Equals(TEXT("used"), ESearchCase::IgnoreCase)) {
			End += 4;
			while (End < Rules.Len() && Rules[End] == TEXT(' ')) {
				++End;
			}
		}
		while (End < Rules.Len() && (Rules[End] == TEXT('.') || Rules[End] == TEXT('·') || Rules[End] == TEXT(' '))) {
			++End;
		}
		FString Before = Rules.Left(Found);
		FString After = Rules.Mid(End);
		Before.TrimEndInline();
		After.TrimStartInline();
		while (Before.Len() > 0 && (Before[Before.Len() - 1] == TEXT('.') || Before[Before.Len() - 1] == TEXT('·'))) {
			Before.LeftChopInline(1, EAllowShrinking::No);
			Before.TrimEndInline();
		}
		if (Before.IsEmpty()) {
			Rules = After;
		} else if (After.IsEmpty()) {
			Rules = Before;
		} else {
			Rules = Before + TEXT(". ") + After;
		}
	}
	Rules.TrimStartAndEndInline();
}

/** Updates the authored "Stockpile N" / `{KW:stockpile}` clause to the live remaining/max counter. */
void ApplyLiveStockpileCounterToRulesText(FString& Rules, const int32 Remaining, const int32 Max, const bool bUsedThisTurn)
{
	if (Max <= 0) {
		return;
	}
	const FString Counter = FString::Printf(TEXT("{KW:stockpile} %d/%d%s"),
		Remaining, Max, bUsedThisTurn ? TEXT(" used") : TEXT(""));

	int32 SearchFrom = 0;
	while (SearchFrom < Rules.Len()) {
		int32 Found = INDEX_NONE;
		int32 ClauseLen = 0;
		if (!TryFindStockpileClauseStart(Rules, SearchFrom, Found, ClauseLen)) {
			break;
		}
		int32 After = Found + ClauseLen;
		while (After < Rules.Len() && Rules[After] == TEXT(' ')) {
			++After;
		}
		const int32 NumStart = After;
		while (After < Rules.Len() && FChar::IsDigit(Rules[After])) {
			++After;
		}
		if (After > NumStart || ClauseLen > 9) {
			Rules = Rules.Left(Found) + Counter + Rules.Mid(After);
			return;
		}
		SearchFrom = Found + ClauseLen;
	}

	if (Rules.IsEmpty()) {
		Rules = Counter;
	} else {
		Rules = Counter + TEXT(". ") + Rules;
	}
}

bool fill_card_ui_strings(const tactics::CardDefinition& def, const tactics::CardInstance* inst, FString& OutName, FString& OutTypeTag,
	FString& OutCostLine, FString& OutRulesLine, bool bAdvanced = true, bool bIncludeStockpileInRules = true)
{
	OutName = FString(UTF8_TO_TCHAR(def.name.c_str()));
	OutTypeTag = FString(UTF8_TO_TCHAR(def.type.c_str()));
	{
		// Brace tokens so the UI renders the cost as inline energy icons (see TacticsCardText).
		OutCostLine = EnergyCostBraceTokens(def.energy_cost);
		if (tactics::definition_is_spell(def) && tactics::definition_spell_has_x_cost(def)) {
			AppendXCostBraceToken(OutCostLine);
		}
	}
	const int stockpile_amount = inst ? inst->stockpile_amount : tactics::definition_stockpile_amount(def);
	const int stockpile_remaining = inst ? inst->stockpile_remaining : stockpile_amount;
	const bool stockpile_used = inst && inst->stockpile_used_this_turn;
	const std::string chosen_rules = CardTextForDisplay(def.normal_rules_text, def.rules_text, bAdvanced);
	if (!chosen_rules.empty()) {
		OutRulesLine = FString(UTF8_TO_TCHAR(chosen_rules.c_str()));
		if (tactics::definition_is_unit(def)) {
			ApplyAbilityCatalogStrips(OutRulesLine, CollectAbilityIdsForCatalogStrips(def));
			ApplyPassiveCatalogStrips(OutRulesLine, CollectPassiveIdsForCatalogStrips(def));
		}
		if (bIncludeStockpileInRules && stockpile_amount > 0) {
			ApplyLiveStockpileCounterToRulesText(OutRulesLine, stockpile_remaining, stockpile_amount, stockpile_used);
		} else if (!bIncludeStockpileInRules) {
			StripStockpileMentionsFromRulesText(OutRulesLine);
		}
		return true;
	}
	if (tactics::definition_is_unit(def)) {
		const tactics::UnitCardDefinition& ud = tactics::definition_unit(def);
		const bool bBuilding = ud.entity_type == "building";
		const bool bObstacle = ud.entity_type == "breakable_obstacle";
		OutRulesLine = bObstacle
			? FString::Printf(TEXT("Breakable obstacle · %s · {LIFE} %d · blocks LOS"),
				UTF8_TO_TCHAR(ud.unit_type.c_str()), ud.base_health)
			: bBuilding
			? FString::Printf(TEXT("Building · %s · immobile · {MELEE} %d · {RANGED} %d · {RANGE} %d"),
				UTF8_TO_TCHAR(ud.unit_type.c_str()), ud.melee_damage, ud.ranged_damage, ud.ranged_range)
			: FString::Printf(TEXT("%s · {LIFE} %d · {MOVE} %d · {MELEE} %d"), UTF8_TO_TCHAR(ud.unit_type.c_str()), ud.base_health, ud.movement, ud.melee_damage);
		if (!bBuilding && !bObstacle && ud.ranged_damage > 0) {
			OutRulesLine += FString::Printf(TEXT(" · {RANGED} %d · {RANGE} %d"), ud.ranged_damage, ud.ranged_range);
		}
		std::vector<std::string> keyword_keys;
		keyword_keys.reserve(ud.keywords.size());
		for (const auto& kw : ud.keywords) {
			if (kw.key != "stockpile") {
				keyword_keys.push_back(kw.key);
			}
		}
		if (!keyword_keys.empty()) {
			OutRulesLine += FString::Printf(TEXT(" · %s"), UTF8_TO_TCHAR(tactics::format_attribute_names(keyword_keys).c_str()));
		}
	} else if (tactics::definition_is_spell(def)) {
		const tactics::SpellCardDefinition& sp = tactics::definition_spell(def);
		const char* Spd = "channeled";
		switch (sp.speed) {
			case tactics::EffectSpeed::Reflex:
				Spd = "reflex";
				break;
			case tactics::EffectSpeed::Blazing:
				Spd = "blazing";
				break;
			case tactics::EffectSpeed::Channeled:
			default:
				break;
		}
		OutRulesLine = FString::Printf(TEXT("%s · %s"), UTF8_TO_TCHAR(sp.effect_key.c_str()), UTF8_TO_TCHAR(Spd));
	} else {
		OutRulesLine.Reset();
	}
	if (bIncludeStockpileInRules && stockpile_amount > 0) {
		ApplyLiveStockpileCounterToRulesText(OutRulesLine, stockpile_remaining, stockpile_amount, stockpile_used);
	} else if (!bIncludeStockpileInRules) {
		StripStockpileMentionsFromRulesText(OutRulesLine);
	}
	return true;
}

void FillCardGlossaryFromDefinition(const tactics::CardDefinition* Def, TArray<FTacticsCardGlossaryEntry>& OutEntries,
	bool bAdvancedGlossary)
{
	OutEntries.Reset();
	if (!Def) {
		return;
	}
	std::vector<tactics::CardGlossaryEntry> Collected;
	tactics::collect_card_glossary_entries(*Def, Collected, bAdvancedGlossary);
	CopyGlossaryToUe(Collected, OutEntries);
}

const tactics::CardDefinition* SelectedUnitCardDefinition(const tactics::GameState& Game, const tactics::Entity& Entity,
	const tactics::CardInstance*& OutInst)
{
	OutInst = FindCardInstanceForEntity(Game, Entity);
	const tactics::CardDefinition* Def = nullptr;
	if (OutInst) {
		Def = tactics::try_get_card_definition_ptr(OutInst->definition_id);
	}
	if (!Def && !Entity.source_card_id.empty()) {
		Def = tactics::try_get_card_definition_ptr(Entity.source_card_id);
	}
	return Def;
}


FString ResolveDisplayNameForEntity(const tactics::GameState& Game, const tactics::Entity& E)
{
	if (!E.source_card_id.empty()) {
		if (const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(E.source_card_id)) {
			if (!Def->name.empty()) {
				return UTF8_TO_TCHAR(Def->name.c_str());
			}
		}
		for (const auto& [SeatId, Deck] : Game.players_decks) {
			static_cast<void>(SeatId);
			const tactics::CardInstanceId CardId = Deck.find_card_by_public_id(E.source_card_id);
			if (!CardId.is_valid()) {
				continue;
			}
			const tactics::CardInstance* Instance = Deck.pool.try_get(CardId);
			if (!Instance) {
				continue;
			}
			if (const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(Instance->definition_id)) {
				if (!Def->name.empty()) {
					return UTF8_TO_TCHAR(Def->name.c_str());
				}
			}
		}
	}
	if (const auto* AsUnit = dynamic_cast<const tactics::Unit*>(&E)) {
		if (!AsUnit->unit_type.empty()) {
			return UTF8_TO_TCHAR(AsUnit->unit_type.c_str());
		}
	}
	return UTF8_TO_TCHAR(E.entity_id.c_str());
}

FCombatUnitSnapshot SnapshotEntity(const tactics::GameState& Game, const tactics::Entity& E)
{
	FCombatUnitSnapshot S;
	S.EntityId = UTF8_TO_TCHAR(E.entity_id.c_str());
	S.DisplayName = ResolveDisplayNameForEntity(Game, E);
	S.ArtId = ResolveArtIdForEntity(Game, E);
	S.Hp = E.current_health;
	S.MaxHp = E.base_health > 0 ? E.base_health : 1;
	S.bAlive = E.current_health > 0;
	if (E.position) {
		S.GridX = E.position->first;
		S.GridY = E.position->second;
	}
	S.Team = E.team.value_or(-1);
	return S;
}

/** Populate AllBattlefieldUnits / BoardCols / BoardRows on Enc from the live board state. */
static void PopulateBattlefieldUnits(const tactics::GameState& Game, FCombatEncounter& Enc)
{
	Enc.AllBattlefieldUnits.Empty();
	// cell_bounds() gives the axis-aligned union of all board tiles.
	const tactics::BoardCellBounds Bounds = Game.board.cell_bounds();
	Enc.BoardCols = Bounds.empty() ? 8 : Bounds.span_x();
	Enc.BoardRows = Bounds.empty() ? 8 : Bounds.span_y();
	// Expose the board origin so the visualizer can normalize RAW grid coords
	// (from both AllBattlefieldUnits and the Attacker/Defender snapshots, which all
	// store RAW coords) into 0-based board indices uniformly.
	Enc.BoardMinX = Bounds.empty() ? 0 : Bounds.min_x;
	Enc.BoardMinY = Bounds.empty() ? 0 : Bounds.min_y;
	for (const auto& [Id, EntPtr] : Game.board.all_entities_map) {
		if (!EntPtr || !EntPtr->position) {
			continue;
		}
		if (EntPtr->current_health <= 0) {
			continue;  // skip dead / off-board entities
		}
		// Background should show only real combat units - not obstacles, terrain,
		// low-cover, pickups, bases, or buildings that also live in all_entities_map.
		if (tactics::entity_kind(*EntPtr) != tactics::EntityKind::Unit || !EntPtr->owner.has_value()) {
			continue;
		}
		// Keep RAW GridX/GridY; the actor normalizes via BoardMinX/BoardMinY so the
		// duelists (also RAW) and bystanders share one coordinate space.
		Enc.AllBattlefieldUnits.Add(SnapshotEntity(Game, *EntPtr));
	}
}

}  // namespace
