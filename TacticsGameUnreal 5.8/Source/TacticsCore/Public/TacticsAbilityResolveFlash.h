#pragma once

#include "CoreMinimal.h"
#include "TacticsAbilityVisualGroup.h"

/** Staged ability resolve feedback (legacy cast-time staging - unused for resolve animation). */
struct FTacticsAbilityResolveStaging
{
	ETacticsAbilityVisualGroup Group{ETacticsAbilityVisualGroup::Instant};
	bool bSuccess{true};
	bool bImmediateBurst{false};
	bool bHealEffect{false};
	bool bDamageEffect{false};
	TArray<FIntPoint> TargetCells;
	TArray<FIntPoint> AoEBlastCells;
	TArray<FIntPoint> CasterFootprintCells;
};

/** One actual hit shown after rules apply (mid-animation). */
struct FTacticsAbilityResolveHit
{
	FString EntityId;
	int32 GridX{0};
	int32 GridY{0};
	int32 Amount{0};
	bool bHeal{false};
};

/** One board cell in a directional ability wave, with its ring index. */
struct FTacticsAbilityWaveCell
{
	FIntPoint Cell{0, 0};
	/** Wave ring from caster (0 = nearest row); tiles in the same ring flash together. */
	int32 WaveRing{0};
};

/** Floating damage/heal number after an ability deals damage. */
struct FTacticsAbilityDamagePopup
{
	FString EntityId;
	FIntPoint Cell{0, 0};
	int32 Amount{0};
	bool bHeal{false};
	/** Card/ability name (e.g. Reactive Armor); combined with Amount when damage/heal dealt. */
	FString EventLabel;
	double StartTime{0.0};
	/** Wave ring for directional resolve; INDEX_NONE when not staggered. */
	int32 WaveRing{INDEX_NONE};

	static constexpr float DurationSec = 1.35f;
	static constexpr float FloatSpeedPx = 38.f;
	static constexpr float LabelDurationSec = 1.55f;
	static constexpr float LabelFloatSpeedPx = 32.f;

	bool IsLabelOnly() const
	{
		return !EventLabel.IsEmpty() && Amount <= 0;
	}

	float ActiveDurationSec() const
	{
		return IsLabelOnly() ? LabelDurationSec : DurationSec;
	}

	float ActiveFloatSpeedPx() const
	{
		return IsLabelOnly() ? LabelFloatSpeedPx : FloatSpeedPx;
	}

	FString GetDisplayText() const
	{
		if (!EventLabel.IsEmpty()) {
			if (Amount > 0) {
				return EventLabel + (bHeal ? FString::Printf(TEXT(" +%d"), Amount) : FString::Printf(TEXT(" -%d"), Amount));
			}
			return EventLabel;
		}
		if (bHeal) {
			return FString::Printf(TEXT("+%d"), Amount);
		}
		return Amount == 0 ? TEXT("0") : FString::Printf(TEXT("-%d"), Amount);
	}

	FLinearColor GetDisplayColor(const float Alpha) const
	{
		const FLinearColor Friendly{0.2f, 0.95f, 0.45f, 1.f};
		const FLinearColor Hostile{0.95f, 0.06f, 0.04f, 1.f};
		const FLinearColor Base = bHeal ? Friendly : Hostile;
		return Base.CopyWithNewOpacity(Alpha);
	}

	float GetSlateFontSize() const
	{
		return IsLabelOnly() ? 18.f : 22.f;
	}

	bool IsExpired(const double Now) const
	{
		return (Now - StartTime) >= static_cast<double>(ActiveDurationSec());
	}

	void GetVisual(const double Now, float& OutAlpha, float& OutScale, float& OutOffsetY) const
	{
		OutAlpha = 0.f;
		OutScale = 0.f;
		OutOffsetY = 0.f;
		const float Elapsed = static_cast<float>(Now - StartTime);
		const float Duration = ActiveDurationSec();
		if (Elapsed < 0.f || Elapsed >= Duration) {
			return;
		}
		const float T = Elapsed / Duration;
		if (T < 0.22f) {
			const float U = T / 0.22f;
			OutScale = FMath::Lerp(0.45f, 1.2f, U);
			OutAlpha = FMath::Lerp(0.4f, 1.f, U);
		} else if (T < 0.55f) {
			OutScale = 1.2f;
			OutAlpha = 1.f;
		} else {
			const float U = (T - 0.55f) / 0.45f;
			OutScale = FMath::Lerp(1.2f, 0.85f, U);
			OutAlpha = FMath::Lerp(1.f, 0.f, U);
		}
		OutOffsetY = -Elapsed * ActiveFloatSpeedPx();
	}
};

/** Three-second on-board ability resolve presentation (wave tiles only). */
struct FTacticsAbilityResolvePresentation
{
	TArray<FTacticsAbilityWaveCell> WaveCells;
	bool bDirectionalWave{false};
	bool bFriendlyEffect{false};
	bool bAbilityApplied{false};
	int32 MaxWaveRing{0};
	double StartTime{0.0};

	/** Upper bound for long directional waves; most resolves end sooner via GetWaveDurationSec(). */
	static constexpr float WaveSec = 5.0f;
	static constexpr float TotalSec = 5.0f;
	static constexpr float TilePulseSec = 0.48f;
	static constexpr float RowStaggerSec = 0.24f;
	static constexpr float MultiTargetPostPulseSec = 0.2f;
	/** Single-target resolve: delay before pulse so the flash is readable (50% slower than the first pass). */
	static constexpr float SingleTargetStartDelaySec = 0.525f;
	static constexpr float SingleTargetTilePulseSec = 1.425f;
	static constexpr float SingleTargetPostPulseSec = 0.15f;
	static constexpr FLinearColor WaveColor{0.95f, 0.06f, 0.04f, 1.f};
	/** Friendly buff/heal tile pulse (#0ca83b). */
	static constexpr FLinearColor WaveColorFriendly{12.f / 255.f, 168.f / 255.f, 59.f / 255.f, 1.f};

	FLinearColor ActiveWaveColor() const
	{
		return bFriendlyEffect ? WaveColorFriendly : WaveColor;
	}

	bool IsSingleTargetWave() const
	{
		return WaveCells.Num() == 1;
	}

	float GetWaveDurationSec() const
	{
		if (IsSingleTargetWave()) {
			return SingleTargetStartDelaySec + SingleTargetTilePulseSec + SingleTargetPostPulseSec;
		}
		const float LastRingStart = static_cast<float>(MaxWaveRing) * RowStaggerSec;
		const float Computed = LastRingStart + TilePulseSec + MultiTargetPostPulseSec;
		return FMath::Min(Computed, WaveSec);
	}

	bool IsInWavePhase(const double Now) const
	{
		return (Now - StartTime) < static_cast<double>(GetWaveDurationSec());
	}

	bool IsComplete(const double Now) const
	{
		return (Now - StartTime) >= static_cast<double>(GetWaveDurationSec());
	}

	int32 FindWaveRingForCell(const FIntPoint& Cell) const
	{
		for (const FTacticsAbilityWaveCell& WaveCell : WaveCells) {
			if (WaveCell.Cell == Cell) {
				return WaveCell.WaveRing;
			}
		}
		return INDEX_NONE;
	}

	void GetWaveCellVisual(const double Now, const FIntPoint& Cell, float& OutAlpha, float& OutScale) const
	{
		OutAlpha = 0.f;
		OutScale = 0.f;
		if (!IsInWavePhase(Now)) {
			return;
		}
		const int32 WaveRing = FindWaveRingForCell(Cell);
		if (WaveRing == INDEX_NONE) {
			return;
		}
		const float Elapsed = static_cast<float>(Now - StartTime);
		const bool bSingleTargetPulse = IsSingleTargetWave();
		const float TileDuration = bSingleTargetPulse ? SingleTargetTilePulseSec : TilePulseSec;
		const float StartOffset = bSingleTargetPulse
			? SingleTargetStartDelaySec
			: static_cast<float>(WaveRing) * RowStaggerSec;
		const float LocalT = Elapsed - StartOffset;
		if (LocalT < 0.f || LocalT >= TileDuration) {
			return;
		}
		const float U = LocalT / TileDuration;
		if (U < 0.45f) {
			const float Rise = U / 0.45f;
			OutScale = FMath::Lerp(0.18f, 1.32f, Rise);
			OutAlpha = FMath::Lerp(0.25f, 1.f, Rise);
		} else {
			const float Fall = (U - 0.45f) / 0.55f;
			OutScale = FMath::Lerp(1.32f, 0.f, Fall);
			OutAlpha = FMath::Lerp(1.f, 0.f, Fall);
		}
	}
};

/** @deprecated Short cast-time flash - superseded by FTacticsAbilityResolvePresentation. */
struct FTacticsBoardResolveFlash
{
	TSet<FIntPoint> Cells;
	FLinearColor Color{0.2f, 0.95f, 0.35f, 1.f};
	double StartTime{0.0};
	static constexpr float DurationSec = 0.5f;
};
