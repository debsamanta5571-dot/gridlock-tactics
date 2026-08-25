#include "TacticsDeckLibrarySubsystem.h"
#include "TacticsLocSubsystem.h"
#include "TacticsProjectContentReader.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "tactics/common/types.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/cards/card_search_index.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/content/project_content.hpp"
#include "tactics/effects/status_effect_catalog.hpp"

namespace
{
FString EnergyTypeToKey(tactics::EnergyType Et)
{
	return UTF8_TO_TCHAR(tactics::to_string(Et).c_str());
}

std::optional<tactics::EnergyType> KeyToEnergyType(const FString& Key)
{
	return tactics::energy_type_from_string(TCHAR_TO_UTF8(*Key));
}

bool DeckDataToCpp(const FTacticsDeckListData& In, tactics::DeckListDefinition& Out, FString& OutError)
{
	Out.key = TCHAR_TO_UTF8(*In.Key);
	Out.entries.clear();
	Out.reserves.clear();
	Out.zones.clear();
	for (const FTacticsDeckCardEntry& E : In.MainDeck) {
		if (E.Copies > 0) {
			Out.entries.push_back({TCHAR_TO_UTF8(*E.CardKey), E.Copies});
		}
	}
	for (const FTacticsDeckCardEntry& E : In.Reserves) {
		if (E.Copies > 0) {
			Out.reserves.push_back({TCHAR_TO_UTF8(*E.CardKey), E.Copies});
		}
	}
	for (const FTacticsDeckZoneEntry& Z : In.Zones) {
		if (Z.Copies <= 0) {
			continue;
		}
		const auto Et = KeyToEnergyType(Z.EnergyKey);
		if (!Et) {
			OutError = FString::Printf(TEXT("Unknown energy type \"%s\" on zone %s"), *Z.EnergyKey, *Z.ZoneId);
			return false;
		}
		tactics::ZoneListEntry Ze;
		Ze.zone_id = TCHAR_TO_UTF8(*Z.ZoneId);
		Ze.name = TCHAR_TO_UTF8(*Z.Name);
		Ze.copies = Z.Copies;
		Ze.energy_produced[*Et] = Z.EnergyAmount;
		Out.zones.push_back(std::move(Ze));
	}
	return true;
}

bool DeckDataFromCpp(const tactics::DeckListDefinition& In, FTacticsDeckListData& Out)
{
	Out.Key = UTF8_TO_TCHAR(In.key.c_str());
	Out.MainDeck.Reset();
	Out.Reserves.Reset();
	Out.Zones.Reset();
	for (const auto& E : In.entries) {
		FTacticsDeckCardEntry Row;
		Row.CardKey = UTF8_TO_TCHAR(E.card_key.c_str());
		Row.Copies = E.copies;
		Out.MainDeck.Add(Row);
	}
	for (const auto& E : In.reserves) {
		FTacticsDeckCardEntry Row;
		Row.CardKey = UTF8_TO_TCHAR(E.card_key.c_str());
		Row.Copies = E.copies;
		Out.Reserves.Add(Row);
	}
	for (const auto& Z : In.zones) {
		FTacticsDeckZoneEntry Row;
		Row.ZoneId = UTF8_TO_TCHAR(Z.zone_id.c_str());
		Row.Name = UTF8_TO_TCHAR(Z.name.c_str());
		Row.Copies = Z.copies;
		if (!Z.energy_produced.empty()) {
			const auto It = Z.energy_produced.begin();
			Row.EnergyKey = EnergyTypeToKey(It->first);
			Row.EnergyAmount = It->second;
		}
		Out.Zones.Add(Row);
	}
	return true;
}
}  // namespace

void UTacticsDeckLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Pre-load all gameplay catalogs at startup so the deck builder and any other early
	// UI that calls SearchCardKeys always sees a populated card catalog immediately.
	EnsureGameplayCatalogsLoaded();
}

void UTacticsDeckLibrarySubsystem::EnsureGameplayCatalogsLoaded() const
{
	const auto ReadProjectFile = [](const std::string& RelPath, std::string& OutUtf8, std::string& Err) -> bool {
		return TacticsProjectContentReader::ReadUtf8File(RelPath, OutUtf8, Err);
	};
	std::string Err;
	if (!tactics::load_all_project_content(ReadProjectFile, Err)) {
		UE_LOG(LogTemp, Warning, TEXT("TacticsData project content load error: %s"), UTF8_TO_TCHAR(Err.c_str()));
	} else {
		UE_LOG(LogTemp, Log, TEXT("TacticsData: loaded OK (%d cards in catalog)"),
			static_cast<int32>(tactics::list_card_catalog_keys_sorted().size()));
	}
}

TArray<FString> UTacticsDeckLibrarySubsystem::ListCardKeysSorted() const
{
	TArray<FString> Keys;
	EnsureGameplayCatalogsLoaded();
	for (const std::string& Key : tactics::list_card_catalog_keys_sorted()) {
		Keys.Add(UTF8_TO_TCHAR(Key.c_str()));
	}
	return Keys;
}

TArray<FString> UTacticsDeckLibrarySubsystem::SearchCardKeys(const FString& FilterString) const
{
	EnsureGameplayCatalogsLoaded();
	tactics::CardSearchQuery Query;
	std::string Err;
	const FTCHARToUTF8 FilterUtf8(*FilterString);
	if (!tactics::try_parse_card_search_filter_string(std::string(FilterUtf8.Get(), FilterUtf8.Length()), Query, Err)) {
		Query = tactics::CardSearchQuery{};
		Query.text.text = std::string(FilterUtf8.Get(), FilterUtf8.Length());
		Query.text.match_all_tokens = true;
		Query.text.fields = tactics::CardSearchTextField::All;
	}
	TArray<FString> Keys;
	for (const std::string& Key : tactics::search_card_keys(Query)) {
		Keys.Add(UTF8_TO_TCHAR(Key.c_str()));
	}
	return Keys;
}

TArray<FTacticsCardSearchFacetGroup> UTacticsDeckLibrarySubsystem::ListCardSearchFacets() const
{
	EnsureGameplayCatalogsLoaded();
	TArray<FTacticsCardSearchFacetGroup> Out;
	for (const tactics::CardSearchFacetGroup& Group : tactics::list_card_search_facet_groups()) {
		FTacticsCardSearchFacetGroup Row;
		Row.FacetId = UTF8_TO_TCHAR(Group.facet_id.c_str());
		for (const tactics::CardSearchFacetValueCount& Value : Group.values) {
			FTacticsCardSearchFacetValue Entry;
			Entry.Value = UTF8_TO_TCHAR(Value.value.c_str());
			Entry.Count = Value.count;
			Row.Values.Add(Entry);
		}
		Out.Add(Row);
	}
	return Out;
}

FString UTacticsDeckLibrarySubsystem::SavedDecksDirectory() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Decks"));
}

FString UTacticsDeckLibrarySubsystem::ContentDecksDirectory() const
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("TacticsData/decks"));
}

FString UTacticsDeckLibrarySubsystem::ResolveContentDeckPath(const FString& DeckKey) const
{
	if (DeckKey.IsEmpty()) {
		return FString();
	}
	const FString Dir = ContentDecksDirectory();
	if (DeckKey == TEXT("starter")) {
		return FPaths::Combine(Dir, TEXT("starter_deck.json"));
	}
	if (DeckKey == TEXT("test")) {
		return FPaths::Combine(Dir, TEXT("test_deck.json"));
	}
	if (DeckKey == TEXT("example_tournament")) {
		return FPaths::Combine(Dir, TEXT("example_tournament_deck.json"));
	}
	const FString Direct = FPaths::Combine(Dir, DeckKey + TEXT(".json"));
	if (FPaths::FileExists(Direct)) {
		return Direct;
	}
	const FString WithSuffix = FPaths::Combine(Dir, DeckKey + TEXT("_deck.json"));
	if (FPaths::FileExists(WithSuffix)) {
		return WithSuffix;
	}
	return FString();
}

void UTacticsDeckLibrarySubsystem::CollectContentDeckKeys(TArray<FString>& InOutKeys) const
{
	const FString Dir = ContentDecksDirectory();
	IPlatformFile& Pf = FPlatformFileManager::Get().GetPlatformFile();
	if (!Pf.DirectoryExists(*Dir)) {
		InOutKeys.AddUnique(TEXT("starter"));
		return;
	}
	Pf.IterateDirectory(*Dir, [&InOutKeys](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool {
		if (bIsDirectory) {
			return true;
		}
		const FString Path(FilenameOrDirectory);
		if (!Path.EndsWith(TEXT(".json"))) {
			return true;
		}
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path)) {
			return true;
		}
		FTCHARToUTF8 Utf8(*Json);
		tactics::DeckListDefinition Cpp;
		std::string Err;
		if (!tactics::load_deck_list_from_json_utf8(std::string(Utf8.Get(), Utf8.Length()), Cpp, Err)) {
			// Still list by filename key so authors can open broken drafts in the builder.
			const FString Base = FPaths::GetBaseFilename(Path);
			if (Base.EndsWith(TEXT("_deck"))) {
				InOutKeys.AddUnique(Base.LeftChop(5));
			} else {
				InOutKeys.AddUnique(Base);
			}
			return true;
		}
		if (!Cpp.key.empty()) {
			InOutKeys.AddUnique(UTF8_TO_TCHAR(Cpp.key.c_str()));
		}
		return true;
	});
}

TArray<FString> UTacticsDeckLibrarySubsystem::ListSavedDeckKeys() const
{
	EnsureGameplayCatalogsLoaded();
	TArray<FString> Keys;
	CollectContentDeckKeys(Keys);

	const FString Dir = SavedDecksDirectory();
	IPlatformFile& Pf = FPlatformFileManager::Get().GetPlatformFile();
	if (Pf.DirectoryExists(*Dir)) {
		Pf.IterateDirectory(*Dir, [&Keys](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool {
			if (bIsDirectory) {
				return true;
			}
			const FString Path(FilenameOrDirectory);
			if (!Path.EndsWith(TEXT(".json"))) {
				return true;
			}
			FString Base = FPaths::GetBaseFilename(Path);
			if (!Base.IsEmpty()) {
				Keys.AddUnique(Base);
			}
			return true;
		});
	}
	Keys.Sort();
	return Keys;
}

TArray<FString> UTacticsDeckLibrarySubsystem::ListPlayableDeckKeys() const
{
	EnsureGameplayCatalogsLoaded();
	TArray<FString> Playable;
	for (const FString& Key : ListSavedDeckKeys()) {
		FString Json, Err;
		if (!TryReadDeckFileJson(Key, Json, Err)) {
			continue;
		}
		tactics::DeckListDefinition Cpp;
		std::string StdErr;
		if (!tactics::load_deck_list_from_json_utf8(TCHAR_TO_UTF8(*Json), Cpp, StdErr)) {
			continue;
		}
		if (!tactics::validate_deck_list(Cpp, StdErr)) {
			continue;
		}
		Playable.Add(Key);
	}
	// Prefer Asterian / starter first for easy discovery.
	Playable.Sort([](const FString& A, const FString& B) {
		auto Rank = [](const FString& K) -> int32 {
			if (K == TEXT("militia_starforged")) {
				return 0;
			}
			if (K == TEXT("militia_shock_and_awe")) {
				return 1;
			}
			if (K == TEXT("militia_cyberunners")) {
				return 2;
			}
			if (K == TEXT("dieselheart_endless_assault")) {
				return 3;
			}
			if (K == TEXT("dieselheart_covering_fire")) {
				return 4;
			}
			if (K == TEXT("dieselheart_horrors_of_war")) {
				return 5;
			}
			if (K == TEXT("starter")) {
				return 20;
			}
			if (K == TEXT("asterian_starforged_ascension")) {
				return 21;
			}
			return 100;
		};
		const int32 Ra = Rank(A);
		const int32 Rb = Rank(B);
		if (Ra != Rb) {
			return Ra < Rb;
		}
		return A < B;
	});
	return Playable;
}

FString UTacticsDeckLibrarySubsystem::GetDeckDisplayName(const FString& DeckKey) const
{
	if (DeckKey == TEXT("militia_starforged")) {
		return TEXT("Militia - Starforged");
	}
	if (DeckKey == TEXT("militia_shock_and_awe")) {
		return TEXT("Militia - Shock and Awe");
	}
	if (DeckKey == TEXT("militia_cyberunners")) {
		return TEXT("Militia - Cyberunners");
	}
	if (DeckKey == TEXT("dieselheart_endless_assault")) {
		return TEXT("Dieselheart - Endless Assault");
	}
	if (DeckKey == TEXT("dieselheart_covering_fire")) {
		return TEXT("Dieselheart - Covering Fire");
	}
	if (DeckKey == TEXT("dieselheart_horrors_of_war")) {
		return TEXT("Dieselheart - Horrors of War");
	}
	if (DeckKey == TEXT("starter")) {
		return TEXT("Starforged Ascension (Asterian starter)");
	}
	if (DeckKey == TEXT("asterian_starforged_ascension")) {
		return TEXT("Asterian Starforged Ascension");
	}
	if (DeckKey == TEXT("example_tournament")) {
		return TEXT("Example Tournament (legacy)");
	}
	if (DeckKey == TEXT("test")) {
		return TEXT("Test deck (dev)");
	}
	FString Nice = DeckKey;
	Nice.ReplaceInline(TEXT("_"), TEXT(" "));
	return Nice;
}

bool UTacticsDeckLibrarySubsystem::TryGetCardDisplayName(const FString& CardKey, FString& OutName) const
{
	tactics::CardDefinition Def;
	if (!tactics::try_get_card_definition(TCHAR_TO_UTF8(*CardKey), Def)) {
		return false;
	}
	OutName = UTF8_TO_TCHAR(Def.name.c_str());
	return true;
}


void UTacticsDeckLibrarySubsystem::PopulateLocSubsystem(UTacticsLocSubsystem* LocSys) const
{
	if (!LocSys) return;
	EnsureGameplayCatalogsLoaded();

	for (const FString& CardKey : ListCardKeysSorted())
	{
		tactics::CardDefinition Def;
		if (!tactics::try_get_card_definition(TCHAR_TO_UTF8(*CardKey), Def)) continue;

		auto Reg = [&](const FString& Field, const std::string& Value)
		{
			if (!Value.empty())
			{
				LocSys->RegisterSourceString(TEXT("card"), CardKey, Field,
					UTF8_TO_TCHAR(Value.c_str()));
			}
		};

		Reg(TEXT("name"),              Def.name);
		Reg(TEXT("rules_text"),        Def.rules_text);
		Reg(TEXT("normal_rules_text"), Def.normal_rules_text);
		Reg(TEXT("flavor_text"),       Def.flavor_text);
	}
}

FText UTacticsDeckLibrarySubsystem::GetCardNameText(const FString& CardKey) const
{
	FString Name; TryGetCardDisplayName(CardKey, Name);
	return FText::FromString(Name);
}

FText UTacticsDeckLibrarySubsystem::GetCardRulesText(const FString& CardKey) const
{
	tactics::CardDefinition Def;
	if (tactics::try_get_card_definition(TCHAR_TO_UTF8(*CardKey), Def))
		return FText::FromString(UTF8_TO_TCHAR(Def.rules_text.c_str()));
	return FText::GetEmpty();
}

namespace
{
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
}  // namespace

FText UTacticsDeckLibrarySubsystem::GetCardRulesTextForDisplay(const FString& CardKey, bool bPreferNormal) const
{
	tactics::CardDefinition Def;
	if (tactics::try_get_card_definition(TCHAR_TO_UTF8(*CardKey), Def))
	{
		const std::string Src = CardTextForDisplay(Def.normal_rules_text, Def.rules_text, !bPreferNormal);
		return FText::FromString(UTF8_TO_TCHAR(Src.c_str()));
	}
	return FText::GetEmpty();
}

FText UTacticsDeckLibrarySubsystem::GetCardFlavorText(const FString& CardKey) const
{
	tactics::CardDefinition Def;
	if (tactics::try_get_card_definition(TCHAR_TO_UTF8(*CardKey), Def))
		return FText::FromString(UTF8_TO_TCHAR(Def.flavor_text.c_str()));
	return FText::GetEmpty();
}

bool UTacticsDeckLibrarySubsystem::LoadDeckByKey(const FString& DeckKey, FTacticsDeckListData& OutDeck,
	FString& OutError) const
{
	EnsureGameplayCatalogsLoaded();
	OutError.Empty();

	auto TryLoadFile = [&](const FString& Path, bool bRequired) -> bool {
		if (!FPaths::FileExists(Path)) {
			if (bRequired) {
				OutError = FString::Printf(TEXT("Deck file missing: %s"), *Path);
			}
			return false;
		}
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path)) {
			OutError = FString::Printf(TEXT("Failed to read deck \"%s\"."), *DeckKey);
			return false;
		}
		FTCHARToUTF8 Utf8(*Json);
		tactics::DeckListDefinition Cpp;
		std::string Err;
		if (!tactics::load_deck_list_from_json_utf8(std::string(Utf8.Get(), Utf8.Length()), Cpp, Err)) {
			OutError = UTF8_TO_TCHAR(Err.c_str());
			return false;
		}
		return DeckDataFromCpp(Cpp, OutDeck);
	};

	const FString ContentPath = ResolveContentDeckPath(DeckKey);
	if (!ContentPath.IsEmpty() && FPaths::FileExists(ContentPath)) {
		return TryLoadFile(ContentPath, true);
	}

	const FString SavedPath = FPaths::Combine(SavedDecksDirectory(), DeckKey + TEXT(".json"));
	if (FPaths::FileExists(SavedPath)) {
		return TryLoadFile(SavedPath, true);
	}

	if (DeckKey == TEXT("starter")) {
		OutDeck = const_cast<UTacticsDeckLibrarySubsystem*>(this)->MakeDefaultBuilderDeck();
		return true;
	}
	OutError = FString::Printf(TEXT("Deck \"%s\" not found."), *DeckKey);
	return false;
}

bool UTacticsDeckLibrarySubsystem::ValidateDeck(const FTacticsDeckListData& Deck, FString& OutError) const
{
	tactics::DeckListDefinition Cpp;
	if (!DeckDataToCpp(Deck, Cpp, OutError)) {
		return false;
	}
	std::string Err;
	if (!tactics::validate_deck_list(Cpp, Err)) {
		OutError = UTF8_TO_TCHAR(Err.c_str());
		return false;
	}
	return true;
}

bool UTacticsDeckLibrarySubsystem::SaveDeck(const FTacticsDeckListData& Deck, FString& OutError)
{
	EnsureGameplayCatalogsLoaded();
	if (!ValidateDeck(Deck, OutError)) {
		return false;
	}
	tactics::DeckListDefinition Cpp;
	if (!DeckDataToCpp(Deck, Cpp, OutError)) {
		return false;
	}
	std::string Json;
	std::string Err;
	if (!tactics::save_deck_list_to_json_utf8(Cpp, Json, Err)) {
		OutError = UTF8_TO_TCHAR(Err.c_str());
		return false;
	}
	const FString Dir = SavedDecksDirectory();
	IPlatformFile& Pf = FPlatformFileManager::Get().GetPlatformFile();
	Pf.CreateDirectoryTree(*Dir);
	const FString Path = FPaths::Combine(Dir, Deck.Key + TEXT(".json"));
	if (!FFileHelper::SaveStringToFile(UTF8_TO_TCHAR(Json.c_str()), *Path)) {
		OutError = TEXT("Failed to write deck file.");
		return false;
	}
	ActiveDeckKey = Deck.Key;
	return true;
}

FTacticsDeckListData UTacticsDeckLibrarySubsystem::MakeDefaultBuilderDeck() const
{
	FTacticsDeckListData Deck;
	Deck.Key = TEXT("my_deck");
	Deck.MainDeck = {
		{TEXT("basic_infantry"), 3},
		{TEXT("taunt_guard"), 3},
		{TEXT("focus_bolt"), 3},
		{TEXT("kill_spell"), 3},
		{TEXT("poison_cloud"), 3},
		{TEXT("ignite"), 3},
		{TEXT("deep_cut"), 3},
		{TEXT("silence"), 3},
		{TEXT("cleanse_silence"), 3},
		{TEXT("sky_banner"), 3},
	};
	Deck.Reserves = {
		{TEXT("basic_infantry"), 2},
		{TEXT("kill_spell"), 2},
		{TEXT("focus_bolt"), 1},
	};
	FTacticsDeckZoneEntry Z;
	Z.ZoneId = TEXT("asteria");
	Z.Name = TEXT("Asteria");
	Z.EnergyKey = TEXT("ingenuity");
	Z.EnergyAmount = 1;
	Z.Copies = tactics::kMaxZoneDeckSize;
	Deck.Zones.Add(Z);
	return Deck;
}

bool UTacticsDeckLibrarySubsystem::ApplyActiveDeckForMatch(FString& OutError)
{
	EnsureGameplayCatalogsLoaded();
	FString Json;
	if (!TryReadDeckFileJson(ActiveDeckKey, Json, OutError)) {
		tactics::clear_active_match_deck_list();
		return false;
	}
	tactics::DeckListDefinition Cpp;
	std::string Err;
	if (!tactics::load_deck_list_from_json_utf8(TCHAR_TO_UTF8(*Json), Cpp, Err)) {
		OutError = UTF8_TO_TCHAR(Err.c_str());
		tactics::clear_active_match_deck_list();
		return false;
	}
	if (!tactics::validate_deck_list(Cpp, Err)) {
		OutError = FString::Printf(
			TEXT("Active deck \"%s\" is not tournament-legal (%s). Pick a legal deck (40/5/20) before starting."),
			*ActiveDeckKey, UTF8_TO_TCHAR(Err.c_str()));
		tactics::clear_active_match_deck_list();
		return false;
	}
	tactics::set_active_match_deck_list(std::move(Cpp));
	UE_LOG(LogTemp, Log, TEXT("Active match deck set to '%s'"), *ActiveDeckKey);
	return true;
}

bool UTacticsDeckLibrarySubsystem::TryReadDeckFileJson(const FString& DeckKey, FString& OutJson, FString& OutError) const
{
	OutJson.Empty();
	OutError.Empty();
	const FString ContentPath = ResolveContentDeckPath(DeckKey);
	if (!ContentPath.IsEmpty() && FPaths::FileExists(ContentPath)) {
		if (FFileHelper::LoadFileToString(OutJson, *ContentPath) && !OutJson.IsEmpty()) {
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to read content deck \"%s\"."), *DeckKey);
		return false;
	}
	const FString SavedPath = FPaths::Combine(SavedDecksDirectory(), DeckKey + TEXT(".json"));
	if (FPaths::FileExists(SavedPath)) {
		if (FFileHelper::LoadFileToString(OutJson, *SavedPath) && !OutJson.IsEmpty()) {
			return true;
		}
		OutError = FString::Printf(TEXT("Failed to read saved deck \"%s\"."), *DeckKey);
		return false;
	}
	OutError = FString::Printf(TEXT("Deck \"%s\" not found."), *DeckKey);
	return false;
}

bool UTacticsDeckLibrarySubsystem::ExportDeckJsonByKey(const FString& DeckKey, FString& OutJson, FString& OutError) const
{
	EnsureGameplayCatalogsLoaded();
	if (!TryReadDeckFileJson(DeckKey, OutJson, OutError)) {
		return false;
	}
	if (!ValidateDeckJson(OutJson, OutError)) {
		OutJson.Empty();
		return false;
	}
	// Return the original file JSON so Conquering Territories fields survive lobby/network.
	return true;
}

bool UTacticsDeckLibrarySubsystem::ValidateDeckJson(const FString& DeckJson, FString& OutError) const
{
	EnsureGameplayCatalogsLoaded();
	if (DeckJson.IsEmpty()) {
		OutError = TEXT("Empty deck JSON.");
		return false;
	}
	tactics::DeckListDefinition Cpp;
	std::string Err;
	if (!tactics::load_deck_list_from_json_utf8(TCHAR_TO_UTF8(*DeckJson), Cpp, Err)) {
		OutError = UTF8_TO_TCHAR(Err.c_str());
		return false;
	}
	if (!tactics::validate_deck_list(Cpp, Err)) {
		OutError = UTF8_TO_TCHAR(Err.c_str());
		return false;
	}
	return true;
}
