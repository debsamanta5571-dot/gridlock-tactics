#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TacticsDeckLibrarySubsystem.generated.h"

/** One card line in a deck list (key plus copy count). */
USTRUCT()
struct FTacticsDeckCardEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString CardKey;

	UPROPERTY()
	int32 Copies{0};
};

/** One energy-zone land line in a deck list. */
USTRUCT()
struct FTacticsDeckZoneEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString ZoneId;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString EnergyKey = TEXT("neutral");

	UPROPERTY()
	int32 EnergyAmount{1};

	UPROPERTY()
	int32 Copies{1};
};

/** A saved deck: main, reserves, and energy zones. */
USTRUCT()
struct FTacticsDeckListData
{
	GENERATED_BODY()

	UPROPERTY()
	FString Key = TEXT("custom");

	UPROPERTY()
	TArray<FTacticsDeckCardEntry> MainDeck;

	UPROPERTY()
	TArray<FTacticsDeckCardEntry> Reserves;

	UPROPERTY()
	TArray<FTacticsDeckZoneEntry> Zones;
};

USTRUCT()
struct FTacticsCardSearchFacetValue
{
	GENERATED_BODY()

	UPROPERTY()
	FString Value;

	UPROPERTY()
	int32 Count{0};
};

USTRUCT()
struct FTacticsCardSearchFacetGroup
{
	GENERATED_BODY()

	UPROPERTY()
	FString FacetId;

	UPROPERTY()
	TArray<FTacticsCardSearchFacetValue> Values;
};

/** Loads catalogs, lists/saves decks under Saved/Decks, and applies the active deck before a match. */
UCLASS()
class TACTICSCORE_API UTacticsDeckLibrarySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Called by the engine when the subsystem is created; pre-loads all gameplay catalogs. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void EnsureGameplayCatalogsLoaded() const;

	// Populate a UTacticsLocSubsystem with English source strings from the card catalog.
	void PopulateLocSubsystem(class UTacticsLocSubsystem* LocSys) const;

	TArray<FString> ListCardKeysSorted() const;

	/** Arena-style search: free text plus facet tokens like `type:unit keyword:haste cost:<=2 red`. */
	TArray<FString> SearchCardKeys(const FString& FilterString) const;

	TArray<FTacticsCardSearchFacetGroup> ListCardSearchFacets() const;

	TArray<FString> ListSavedDeckKeys() const;

	/**
	 * Decks legal for solo / host / lobby play (exactly 40 main + 5 reserves + 20 zones).
	 * Content starter decks are listed first; incomplete Saved drafts are omitted.
	 */
	TArray<FString> ListPlayableDeckKeys() const;

	/** Human-readable label for the deck picker (e.g. Asterian Starforged Ascension). */
	FString GetDeckDisplayName(const FString& DeckKey) const;

	bool TryGetCardDisplayName(const FString& CardKey, FString& OutName) const;

	/** Localized card text via UTacticsLocSubsystem. Prefer these over TryGetCardDisplayName for UI. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cards")
	FText GetCardNameText(const FString& CardKey) const;

	UFUNCTION(BlueprintCallable, Category = "Tactics|Cards")
	FText GetCardRulesText(const FString& CardKey) const;

	/** Normal: normal_rules_text (fallback rules_text). Advanced: normal + rules_text when rules_text adds detail. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Cards")
	FText GetCardRulesTextForDisplay(const FString& CardKey, bool bPreferNormal) const;

	UFUNCTION(BlueprintCallable, Category = "Tactics|Cards")
	FText GetCardFlavorText(const FString& CardKey) const;

	bool LoadDeckByKey(const FString& DeckKey, FTacticsDeckListData& OutDeck, FString& OutError) const;
	bool SaveDeck(const FTacticsDeckListData& Deck, FString& OutError);
	bool ValidateDeck(const FTacticsDeckListData& Deck, FString& OutError) const;

	FTacticsDeckListData MakeDefaultBuilderDeck() const;

	FString GetActiveDeckKey() const { return ActiveDeckKey; }
	void SetActiveDeckKey(const FString& DeckKey) { ActiveDeckKey = DeckKey; }

	bool ApplyActiveDeckForMatch(FString& OutError);

	/** Export a saved/content deck as JSON for lobby / network (validates; preserves territory fields). */
	bool ExportDeckJsonByKey(const FString& DeckKey, FString& OutJson, FString& OutError) const;

	/** Load raw deck file JSON by key (Content then Saved). Does not validate. */
	bool TryReadDeckFileJson(const FString& DeckKey, FString& OutJson, FString& OutError) const;

	/** Parse + tournament-validate a deck JSON string (preserves territory fields in-engine). */
	bool ValidateDeckJson(const FString& DeckJson, FString& OutError) const;

private:
	FString SavedDecksDirectory() const;
	FString ContentDecksDirectory() const;
	/** Resolve Content/TacticsData/decks path for a deck key (empty if not a content deck). */
	FString ResolveContentDeckPath(const FString& DeckKey) const;
	void CollectContentDeckKeys(TArray<FString>& InOutKeys) const;

	UPROPERTY()
	FString ActiveDeckKey = TEXT("militia_starforged");
};
