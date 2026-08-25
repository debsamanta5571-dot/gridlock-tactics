#include "TacticsMapSettings.h"

const UTacticsMapSettings& UTacticsMapSettings::Get()
{
	return *GetDefault<UTacticsMapSettings>();
}

TArray<FString> UTacticsMapSettings::GetMatchMapCandidates() const
{
	TArray<FString> Out;
	if (!MatchMap.IsEmpty()) {
		Out.Add(MatchMap);
	}
	for (const FString& Fallback : MatchMapFallbacks) {
		if (!Fallback.IsEmpty()) {
			Out.AddUnique(Fallback);
		}
	}
	return Out;
}
