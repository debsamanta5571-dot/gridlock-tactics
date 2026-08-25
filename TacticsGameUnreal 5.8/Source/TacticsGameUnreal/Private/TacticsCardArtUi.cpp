#include "TacticsCardArtUi.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Styling/SlateBrush.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "UObject/Package.h"

namespace TacticsCardArtUi
{

namespace
{

/** First cooked unlit/masked material that can sample card-art textures. */
UMaterialInterface* LoadCardArtBaseMaterial()
{
	static UMaterialInterface* Cached = nullptr;
	if (IsValid(Cached)) {
		return Cached;
	}
	Cached = nullptr;
	// Paper2D masked-unlit is cooked with the plugin and is what combat viz already uses
	// successfully in packaged builds. M_CardPortrait is a project uasset that LoadObject
	// misses (editor + cooked), and engine emissive materials are often stripped from cooks
	// unless a cooked asset hard-references them.
	static const TCHAR* Paths[] = {
		TEXT("/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"),
		TEXT("/Paper2D/DefaultSpriteMaterial.DefaultSpriteMaterial"),
		TEXT("/Game/TacticsData/Materials/M_CardPortrait.M_CardPortrait"),
		TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"),
		TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"),
		TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"),
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
	};
	for (const TCHAR* Path : Paths) {
		if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Path)) {
			Mat->AddToRoot();
			Cached = Mat;
			UE_LOG(LogTemp, Log, TEXT("TacticsCardArtUi: board portrait material = %s"), Path);
			return Cached;
		}
	}
	UE_LOG(LogTemp, Error, TEXT("TacticsCardArtUi: no board portrait material could be loaded"));
	return nullptr;
}

/** Sets every common texture/color parameter name so unknown parent materials still sample the PNG. */
void SetTextureOnMaterial(UMaterialInstanceDynamic* Mid, UTexture2D* Texture)
{
	if (!Mid || !Texture) {
		return;
	}
	static const FName ParamNames[] = {
		FName(TEXT("Texture")),
		FName(TEXT("EmissiveTexture")),
		FName(TEXT("DiffuseTexture")),
		FName(TEXT("BaseColorTexture")),
		FName(TEXT("SpriteTexture")),
		FName(TEXT("Image")),
		FName(TEXT("MainTexture")),
	};
	for (const FName Param : ParamNames) {
		Mid->SetTextureParameterValue(Param, Texture);
	}
	Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
	Mid->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor::White);
	Mid->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor::White);
	Mid->SetVectorParameterValue(TEXT("SpriteColor"), FLinearColor::White);
}

// Loads a PNG and builds a UTexture2D with a full mip chain so small UI icons downscale cleanly
// (FImageUtils::ImportFileAsTexture2D produces a single-mip texture, which aliases when Slate draws
// a 128px icon at ~24px). Returns nullptr on failure so the caller can fall back.
UTexture2D* LoadMippedTextureFromFile(const FString& Path)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path)) {
		return nullptr;
	}
	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num())) {
		return nullptr;
	}
	TArray<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) {
		return nullptr;
	}
	const int32 W = Wrapper->GetWidth();
	const int32 H = Wrapper->GetHeight();
	if (W <= 0 || H <= 0 || Raw.Num() < W * H * 4) {
		return nullptr;
	}

	TArray<FColor> Cur;
	Cur.SetNumUninitialized(W * H);
	FMemory::Memcpy(Cur.GetData(), Raw.GetData(), static_cast<int64>(W) * H * 4);

	int32 NumMips = 1;
	for (int32 w = W, h = H; w > 1 || h > 1; ) {
		w = FMath::Max(1, w >> 1);
		h = FMath::Max(1, h >> 1);
		++NumMips;
	}

	UTexture2D* Tex = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Tex) {
		return nullptr;
	}
	FTexturePlatformData* PD = new FTexturePlatformData();
	PD->SizeX = W;
	PD->SizeY = H;
	PD->PixelFormat = PF_B8G8R8A8;
	Tex->SetPlatformData(PD);

	int32 cw = W, ch = H;
	for (int32 m = 0; m < NumMips; ++m) {
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Mip->SizeX = cw;
		Mip->SizeY = ch;
		Mip->SizeZ = 1;
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Dst = Mip->BulkData.Realloc(static_cast<int64>(cw) * ch * sizeof(FColor));
		FMemory::Memcpy(Dst, Cur.GetData(), static_cast<int64>(cw) * ch * sizeof(FColor));
		Mip->BulkData.Unlock();
		PD->Mips.Add(Mip);

		if (m + 1 < NumMips) {
			const int32 nw = FMath::Max(1, cw >> 1);
			const int32 nh = FMath::Max(1, ch >> 1);
			TArray<FColor> Next;
			Next.SetNumUninitialized(nw * nh);
			for (int32 y = 0; y < nh; ++y) {
				for (int32 x = 0; x < nw; ++x) {
					const int32 x0 = FMath::Min(x * 2, cw - 1);
					const int32 y0 = FMath::Min(y * 2, ch - 1);
					const int32 x1 = FMath::Min(x * 2 + 1, cw - 1);
					const int32 y1 = FMath::Min(y * 2 + 1, ch - 1);
					const FColor S[4] = { Cur[y0 * cw + x0], Cur[y0 * cw + x1], Cur[y1 * cw + x0], Cur[y1 * cw + x1] };
					// Premultiplied-alpha box filter so transparent pixels don't bleed colour into edges.
					float R = 0.f, G = 0.f, B = 0.f, A = 0.f;
					for (const FColor& P : S) {
						const float a = P.A / 255.f;
						R += P.R * a;
						G += P.G * a;
						B += P.B * a;
						A += a;
					}
					FColor O;
					if (A > 0.f) {
						O.R = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(R / A, 0.f, 255.f)));
						O.G = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(G / A, 0.f, 255.f)));
						O.B = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(B / A, 0.f, 255.f)));
					} else {
						O.R = O.G = O.B = 0;
					}
					O.A = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(A / 4.f * 255.f, 0.f, 255.f)));
					Next[y * nw + x] = O;
				}
			}
			Cur = MoveTemp(Next);
			cw = nw;
			ch = nh;
		}
	}

	Tex->SRGB = true;
	Tex->NeverStream = true;
	Tex->Filter = TF_Trilinear;
	Tex->CompressionSettings = TC_EditorIcon;  // uncompressed RGBA - crisp UI
	Tex->LODGroup = TEXTUREGROUP_World;
	Tex->UpdateResource();
	return Tex;
}

}  // namespace

FString CardArtFilePathFromId(const FString& ArtId)
{
	if (ArtId.IsEmpty()) {
		return {};
	}
	FString Relative = ArtId;
	Relative.ReplaceInline(TEXT("/"), TEXT("\\"));
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("TacticsData"), TEXT("card_art"), Relative + TEXT(".png"));
}

UTexture2D* LoadCardArtTexture(const FString& ArtId)
{
	const FString Path = CardArtFilePathFromId(ArtId);
	if (Path.IsEmpty() || !IFileManager::Get().FileExists(*Path)) {
		return nullptr;
	}
	static TMap<FString, TObjectPtr<UTexture2D>> CachedTextures;
	if (const TObjectPtr<UTexture2D>* Found = CachedTextures.Find(Path)) {
		if (IsValid(*Found)) {
			return *Found;
		}
		CachedTextures.Remove(Path);
	}
	// UI icons are drawn far smaller than their source (128px -> ~24px), so build a mipmap chain for
	// clean minification. Card portraits keep the simple single-mip import.
	UTexture2D* Texture = LoadMippedTextureFromFile(Path);
	if (!Texture) {
		Texture = FImageUtils::ImportFileAsTexture2D(Path);
		if (!Texture) {
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->Filter = TF_Trilinear;
		Texture->NeverStream = true;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->UpdateResource();
	}
	Texture->AddToRoot();
	CachedTextures.Add(Path, Texture);
	return Texture;
}

UMaterialInstanceDynamic* ApplyCardArtToMesh(UStaticMeshComponent* Mesh, UTexture2D* Texture, UMaterialInstanceDynamic* ExistingMid,
	UObject* Outer)
{
	if (!Mesh || !Texture || !Outer) {
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = ExistingMid;
	if (!Mid) {
		UMaterialInterface* BaseMat = LoadCardArtBaseMaterial();
		if (!BaseMat) {
			return nullptr;
		}
		Mid = UMaterialInstanceDynamic::Create(BaseMat, Outer);
		if (!Mid) {
			return nullptr;
		}
	}
	SetTextureOnMaterial(Mid, Texture);
	Mesh->SetMaterial(0, Mid);
	Mesh->MarkRenderStateDirty();
	return Mid;
}

const FSlateBrush* GetCardArtBrush(const FString& ArtId, const FVector2D DisplaySize)
{
	if (ArtId.IsEmpty() || DisplaySize.X < 1.f || DisplaySize.Y < 1.f) {
		return nullptr;
	}
	UTexture2D* Texture = LoadCardArtTexture(ArtId);
	if (!Texture) {
		return nullptr;
	}
	const FString CacheKey =
		FString::Printf(TEXT("%s_%d_%d"), *ArtId, FMath::RoundToInt(DisplaySize.X), FMath::RoundToInt(DisplaySize.Y));
	static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> CachedBrushes;
	if (const TSharedPtr<FSlateDynamicImageBrush>* Found = CachedBrushes.Find(CacheKey)) {
		return Found->IsValid() ? Found->Get() : nullptr;
	}
	TSharedPtr<FSlateDynamicImageBrush> Brush =
		MakeShared<FSlateDynamicImageBrush>(Texture, DisplaySize, FName(*CacheKey));
	CachedBrushes.Add(CacheKey, Brush);
	return Brush.Get();
}

const FSlateBrush* GetNineSliceBrush(const FString& ArtId, const FVector2D DisplaySize, const FMargin& SliceMargin)
{
	if (ArtId.IsEmpty() || DisplaySize.X < 1.f || DisplaySize.Y < 1.f) {
		return nullptr;
	}
	UTexture2D* Texture = LoadCardArtTexture(ArtId);
	if (!Texture) {
		return nullptr;
	}
	const FString CacheKey = FString::Printf(TEXT("9s_%s_%d_%d_%.3f_%.3f_%.3f_%.3f"), *ArtId,
		FMath::RoundToInt(DisplaySize.X), FMath::RoundToInt(DisplaySize.Y), SliceMargin.Left, SliceMargin.Top,
		SliceMargin.Right, SliceMargin.Bottom);
	static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> CachedNine;
	if (const TSharedPtr<FSlateDynamicImageBrush>* Found = CachedNine.Find(CacheKey)) {
		return Found->IsValid() ? Found->Get() : nullptr;
	}
	TSharedPtr<FSlateDynamicImageBrush> Brush =
		MakeShared<FSlateDynamicImageBrush>(Texture, DisplaySize, FName(*CacheKey));
	Brush->DrawAs = ESlateBrushDrawType::Box;
	Brush->Margin = SliceMargin;
	Brush->Tiling = ESlateBrushTileType::NoTile;
	CachedNine.Add(CacheKey, Brush);
	return Brush.Get();
}

}  // namespace TacticsCardArtUi
