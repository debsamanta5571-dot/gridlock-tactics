#include "TacticsMapNavigator.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace TacticsMapNavigator
{

FString AssetPathToPackageName(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) {
		return {};
	}
	const int32 Dot = AssetPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (Dot != INDEX_NONE) {
		return AssetPath.Left(Dot);
	}
	return AssetPath;
}

bool IsMapAssetAvailable(const FString& AssetPath, FString& OutPackageName)
{
	OutPackageName = AssetPathToPackageName(AssetPath);
	if (OutPackageName.IsEmpty()) {
		return false;
	}
	if (FPackageName::DoesPackageExist(OutPackageName)) {
		return true;
	}
	FString Filename;
	if (FPackageName::TryConvertLongPackageNameToFilename(OutPackageName, Filename, FPackageName::GetMapPackageExtension())) {
		return FPaths::FileExists(Filename);
	}
	return false;
}

bool ResolveFirstAvailableMap(const TArray<FString>& Candidates, FString& OutAssetPath, FString& OutPackageName,
	FString& OutDebug)
{
	TArray<FString> Missing;
	for (const FString& Candidate : Candidates) {
		FString PackageName;
		if (IsMapAssetAvailable(Candidate, PackageName)) {
			OutAssetPath = Candidate;
			OutPackageName = PackageName;
			OutDebug = FString::Printf(TEXT("Resolved map \"%s\" (package %s)."), *Candidate, *PackageName);
			return true;
		}
		Missing.Add(Candidate);
	}
	OutDebug = FString::Printf(TEXT("No map found. Tried: %s"), *FString::Join(Missing, TEXT(", ")));
	return false;
}

FString BuildGameModeOptions(const FString& GameModeClassPath)
{
	if (GameModeClassPath.IsEmpty()) {
		return {};
	}
	return FString::Printf(TEXT("game=%s"), *GameModeClassPath);
}

FTacticsMapOpenResult OpenMapForGameInstance(UObject* WorldContextObject, const FString& AssetPath,
	const FString& GameModeOptionsSuffix)
{
	FTacticsMapOpenResult Result;
	Result.RequestedAssetPath = AssetPath;
	Result.OpenOptions = GameModeOptionsSuffix;

	FString PackageName;
	if (!IsMapAssetAvailable(AssetPath, PackageName)) {
		Result.DebugMessage =
			FString::Printf(TEXT("Map asset not available: %s (package %s)."), *AssetPath, *PackageName);
		return Result;
	}

	Result.ResolvedPackageName = PackageName;
	Result.OpenLevelName = FPackageName::GetShortName(PackageName);

	if (!WorldContextObject) {
		Result.DebugMessage = TEXT("OpenLevel failed: no world context.");
		return Result;
	}

	UGameplayStatics::OpenLevel(WorldContextObject, FName(*Result.OpenLevelName), true, GameModeOptionsSuffix);
	Result.bOpened = true;
	Result.DebugMessage = FString::Printf(
		TEXT("OpenLevel \"%s\" (package %s)%s%s"),
		*Result.OpenLevelName,
		*PackageName,
		GameModeOptionsSuffix.IsEmpty() ? TEXT("") : TEXT(" options="),
		*GameModeOptionsSuffix);
	return Result;
}

}  // namespace TacticsMapNavigator
