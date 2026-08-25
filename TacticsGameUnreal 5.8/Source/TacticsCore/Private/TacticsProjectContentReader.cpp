#include "TacticsProjectContentReader.h"

#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool TacticsProjectContentReader::ReadUtf8File(const std::string& RelPathFromContent, std::string& OutUtf8, std::string& Err)
{
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), UTF8_TO_TCHAR(RelPathFromContent.c_str()));
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path)) {
		Err = "missing file: " + std::string(TCHAR_TO_UTF8(*Path));
		UE_LOG(LogTemp, Warning, TEXT("TacticsData missing loose file: %s"), *Path);
		return false;
	}
	FTCHARToUTF8 Utf8(*Json);
	OutUtf8.assign(Utf8.Get(), Utf8.Length());
	Err.clear();
	return true;
}