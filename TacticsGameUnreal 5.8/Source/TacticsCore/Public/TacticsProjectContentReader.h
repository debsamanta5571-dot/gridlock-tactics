#pragma once

#include <string>

namespace TacticsProjectContentReader
{
/** Reads UTF-8 text from `FPaths::ProjectContentDir()` + RelPathFromContent. */
bool ReadUtf8File(const std::string& RelPathFromContent, std::string& OutUtf8, std::string& Err);
}