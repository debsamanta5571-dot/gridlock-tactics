#pragma once

#include "CoreMinimal.h"

/** Result of resolving and opening a tactics map. */
struct FTacticsMapOpenResult
{
	bool bOpened{false};
	FString RequestedAssetPath;
	FString ResolvedPackageName;
	FString OpenLevelName;
	FString OpenOptions;
	FString DebugMessage;
};

/** Resolves map asset paths and opens levels with the correct game mode options. */
namespace TacticsMapNavigator
{
/** Strips the asset name so OpenLevel can load the package. */
FString AssetPathToPackageName(const FString& AssetPath);
/** True if the map asset exists on disk or in the cooked registry. */
bool IsMapAssetAvailable(const FString& AssetPath, FString& OutPackageName);
/** Picks the first candidate map that can actually be loaded. */
bool ResolveFirstAvailableMap(const TArray<FString>& Candidates, FString& OutAssetPath, FString& OutPackageName,
	FString& OutDebug);
/** Travels to AssetPath with optional game-mode options. */
FTacticsMapOpenResult OpenMapForGameInstance(UObject* WorldContextObject, const FString& AssetPath,
	const FString& GameModeOptionsSuffix);
/** Builds the OpenLevel options string that selects a game mode class. */
FString BuildGameModeOptions(const FString& GameModeClassPath);
}  // namespace TacticsMapNavigator
