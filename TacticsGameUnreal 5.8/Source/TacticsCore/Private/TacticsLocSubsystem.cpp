#include "TacticsLocSubsystem.h"
#include "TacticsDeckLibrarySubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Engine/GameInstance.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

FString UTacticsLocSubsystem::MakeLocKey(const FString& Domain, const FString& EntityKey, const FString& Field)
{
	return FString::Printf(TEXT("%s.%s.%s"), *Domain, *EntityKey, *Field);
}

FText UTacticsLocSubsystem::ResolveKey(const FString& LocKey) const
{
	if (const FString* Override = TranslationOverrides.Find(LocKey))
	{
		return FText::FromString(*Override);
	}
	if (const FString* Source = SourceStrings.Find(LocKey))
	{
		return FText::FromString(*Source);
	}
	return FText::GetEmpty();
}

// ---------------------------------------------------------------------------
// UGameInstanceSubsystem
// ---------------------------------------------------------------------------

void UTacticsLocSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Ensure DeckLibrary is initialized first; it owns the card catalog.
	Collection.InitializeDependency(UTacticsDeckLibrarySubsystem::StaticClass());

	// Delegate population to DeckLibrary - it holds the cpp_core headers; we don't.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UTacticsDeckLibrarySubsystem* DeckLib = GI->GetSubsystem<UTacticsDeckLibrarySubsystem>())
		{
			DeckLib->PopulateLocSubsystem(this);
		}
	}
}

// ---------------------------------------------------------------------------
// Registration (called by UTacticsDeckLibrarySubsystem::PopulateLocSubsystem)
// ---------------------------------------------------------------------------

void UTacticsLocSubsystem::RegisterSourceString(const FString& Domain, const FString& EntityKey,
	const FString& Field, const FString& EnglishText)
{
	if (!EnglishText.IsEmpty())
	{
		SourceStrings.Add(MakeLocKey(Domain, EntityKey, Field), EnglishText);
	}
}

// ---------------------------------------------------------------------------
// Locale overrides
// ---------------------------------------------------------------------------

void UTacticsLocSubsystem::LoadLocaleOverrides(const FString& LocaleCode)
{
	TranslationOverrides.Empty();

	if (LocaleCode.IsEmpty() || LocaleCode.Equals(TEXT("en"), ESearchCase::IgnoreCase))
	{
		return;
	}

	const FString CsvPath = FPaths::Combine(
		FPaths::ProjectContentDir(),
		TEXT("Localization"), LocaleCode, TEXT("ST_Cards.csv"));

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *CsvPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("TacticsLoc: locale CSV not found: %s"), *CsvPath);
		return;
	}

	int32 Loaded = 0;
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) continue;

		int32 CommaIdx;
		if (!Line.FindChar(TEXT(','), CommaIdx)) continue;

		FString Key   = Line.Left(CommaIdx).TrimStartAndEnd();
		FString Value = Line.RightChop(CommaIdx + 1).TrimStartAndEnd();

		if (Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
		{
			Value = Value.Mid(1, Value.Len() - 2);
			Value.ReplaceInline(TEXT("\"\""), TEXT("\""));
		}

		if (!Key.IsEmpty() && !Value.IsEmpty())
		{
			TranslationOverrides.Add(Key, Value);
			++Loaded;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TacticsLoc: loaded %d override strings for locale '%s'"),
		Loaded, *LocaleCode);
}

// ---------------------------------------------------------------------------
// Public resolution API
// ---------------------------------------------------------------------------

FText UTacticsLocSubsystem::ResolveText(const FString& LocKey, const FString& FallbackSource) const
{
	if (const FString* Override = TranslationOverrides.Find(LocKey))
	{
		return FText::FromString(*Override);
	}
	if (const FString* Source = SourceStrings.Find(LocKey))
	{
		return FText::FromString(*Source);
	}
	if (!FallbackSource.IsEmpty())
	{
		return FText::FromString(FallbackSource);
	}
	return FText::GetEmpty();
}

// ---------------------------------------------------------------------------
// Card text convenience accessors
// ---------------------------------------------------------------------------

FText UTacticsLocSubsystem::GetCardName(const FString& CardKey) const
{
	return ResolveKey(MakeLocKey(TEXT("card"), CardKey, TEXT("name")));
}

FText UTacticsLocSubsystem::GetCardRulesText(const FString& CardKey) const
{
	return ResolveKey(MakeLocKey(TEXT("card"), CardKey, TEXT("rules_text")));
}

FText UTacticsLocSubsystem::GetCardNormalRulesText(const FString& CardKey) const
{
	const FText Normal = ResolveKey(MakeLocKey(TEXT("card"), CardKey, TEXT("normal_rules_text")));
	if (!Normal.IsEmpty()) {
		return Normal;
	}
	// Legacy loc CSV rows used simple_rules_text.
	return ResolveKey(MakeLocKey(TEXT("card"), CardKey, TEXT("simple_rules_text")));
}

FText UTacticsLocSubsystem::GetCardRulesTextForDisplay(const FString& CardKey, bool bPreferNormal) const
{
	const FText Normal = GetCardNormalRulesText(CardKey);
	const FText Advanced = GetCardRulesText(CardKey);
	if (bPreferNormal) {
		return !Normal.IsEmpty() ? Normal : Advanced;
	}
	if (!Normal.IsEmpty()) {
		if (!Advanced.IsEmpty() && !Normal.EqualTo(Advanced)) {
			return FText::Format(FText::FromString(TEXT("{0}\n\n{1}")), Normal, Advanced);
		}
		return Normal;
	}
	return Advanced;
}

FText UTacticsLocSubsystem::GetCardFlavorText(const FString& CardKey) const
{
	return ResolveKey(MakeLocKey(TEXT("card"), CardKey, TEXT("flavor_text")));
}
