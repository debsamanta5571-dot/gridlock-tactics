#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TacticsLocSubsystem.generated.h"

/**
 * Localization subsystem for Tactics.
 *
 * Loc key convention:  {domain}.{entity_key}.{field}
 *   e.g.  card.grease_monkeys.name
 *         card.grease_monkeys.rules_text
 *         status.stunned.display_name
 *
 * At startup the subsystem populates source strings from the C++ card catalog (English).
 * To add a language, drop a CSV at:
 *   Content/Localization/{locale}/ST_Cards.csv
 * with columns: Key, SourceString
 * The subsystem loads it automatically and overrides matching keys.
 * No code changes are needed to ship a new language.
 *
 * All card text display should go through this subsystem so the seam is in place.
 */
UCLASS()
class TACTICSCORE_API UTacticsLocSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Loads English catalog strings and optional locale CSV overrides. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ---- Card text ----

	UFUNCTION(BlueprintCallable, Category = "Tactics|Localization")
	/** Localized display name for CardKey. */
	FText GetCardName(const FString& CardKey) const;

	UFUNCTION(BlueprintCallable, Category = "Tactics|Localization")
	/** Full rules text for CardKey. */
	FText GetCardRulesText(const FString& CardKey) const;

	UFUNCTION(BlueprintCallable, Category = "Tactics|Localization")
	/** Shorter rules text used when advanced text is hidden. */
	FText GetCardNormalRulesText(const FString& CardKey) const;

	/** Normal: normal_rules_text (fallback rules_text). Advanced: normal + rules_text when rules_text adds detail. */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Localization")
	FText GetCardRulesTextForDisplay(const FString& CardKey, bool bPreferNormal) const;

	UFUNCTION(BlueprintCallable, Category = "Tactics|Localization")
	/** Flavor line for CardKey, or empty if none. */
	FText GetCardFlavorText(const FString& CardKey) const;

	// ---- Generic resolution ----

	/**
	 * Resolve any loc key.  The loc key format is "{domain}.{entity_key}.{field}".
	 * Returns the translation override if one is loaded for the current locale,
	 * otherwise returns FallbackSource as a plain FText.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactics|Localization")
	FText ResolveText(const FString& LocKey, const FString& FallbackSource) const;

	/**
	 * Register a source (English) string. Call this if you add runtime content
	 * (e.g. a mod card) that wasn't present when the catalog was loaded.
	 */
	void RegisterSourceString(const FString& Domain, const FString& EntityKey,
		const FString& Field, const FString& EnglishText);

	// ---- Locale management ----

	/**
	 * Load translation overrides for a locale from the CSV at
	 * Content/Localization/{LocaleCode}/ST_Cards.csv.
	 * Calling this with "en" or an empty string unloads overrides (uses source strings).
	 */
	void LoadLocaleOverrides(const FString& LocaleCode);

private:

	/** Source English strings keyed by loc key. Populated at startup from the C++ catalog. */
	TMap<FString, FString> SourceStrings;

	/** Translation overrides for the active locale. Overrides SourceStrings on lookup. */
	TMap<FString, FString> TranslationOverrides;

	static FString MakeLocKey(const FString& Domain, const FString& EntityKey, const FString& Field);
	FText ResolveKey(const FString& LocKey) const;
};
