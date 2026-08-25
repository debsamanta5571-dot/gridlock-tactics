#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TacticsMapSettings.generated.h"

/** Project map paths for menu / match / deck builder (`Config/DefaultGame.ini` → [/Script/TacticsGameUnreal.TacticsMapSettings]). */
UCLASS(Config = Game, DefaultConfig)
class TACTICSGAMEUNREAL_API UTacticsMapSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Project-default map settings from DefaultGame.ini. */
	static const UTacticsMapSettings& Get();

	/** Primary 3D match level. */
	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FString MatchMap = TEXT("/Game/TacticsMaps/L_Match.L_Match");

	/** Tried in order when MatchMap is missing on disk or in the asset registry. */
	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	TArray<FString> MatchMapFallbacks = {
		TEXT("/Engine/Maps/Templates/Template_Open.Template_Open"),
		TEXT("/Game/TopDown/Lvl_TopDown.Lvl_TopDown"),
	};

	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FString MainMenuMap = TEXT("/Game/TacticsMaps/L_MainMenu.L_MainMenu");

	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FString DeckBuilderMap = TEXT("/Game/TacticsMaps/L_DeckBuilder.L_DeckBuilder");

	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FSoftClassPath MatchGameMode = FSoftClassPath(TEXT("/Script/TacticsGameUnreal.Tactics3DBoardGameMode"));

	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FSoftClassPath MenuGameMode = FSoftClassPath(TEXT("/Script/TacticsGameUnreal.TacticsMenuGameMode"));

	/** Match map plus fallbacks, in the order they should be tried. */
	TArray<FString> GetMatchMapCandidates() const;
};
