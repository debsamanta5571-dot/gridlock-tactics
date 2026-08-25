#pragma once

#include "CoreMinimal.h"
#include "Math/Vector2D.h"
#include "Styling/SlateBrush.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTexture2D;

/** Loads TacticsData/card_art PNGs for Slate and 3D board unit portraits. */
namespace TacticsCardArtUi
{
/** Default portrait for spawned tokens and any unit without catalog art. */
inline const TCHAR* DefaultTokenArtId()
{
	return TEXT("token/token");
}

/** Absolute path to Content/TacticsData/card_art/<ArtId>.png. */
FString CardArtFilePathFromId(const FString& ArtId);
/** Loads and caches the PNG for ArtId. Returns null if the file is missing. */
UTexture2D* LoadCardArtTexture(const FString& ArtId);
/** Slate brush for the card art, scaled to DisplaySize. */
const FSlateBrush* GetCardArtBrush(const FString& ArtId, FVector2D DisplaySize = FVector2D(64.f, 64.f));
/** Same PNG as GetCardArtBrush, drawn as a 9-slice box so 3D bevels stay in the corners. */
const FSlateBrush* GetNineSliceBrush(const FString& ArtId, FVector2D DisplaySize, const FMargin& SliceMargin);
/** Applies the texture to the mesh with a masked-unlit material instance. */
UMaterialInstanceDynamic* ApplyCardArtToMesh(UStaticMeshComponent* Mesh, UTexture2D* Texture, UMaterialInstanceDynamic* ExistingMid,
	UObject* Outer);
}
