#pragma once

#include <cstdint>

/** Bitmask for incremental 3D/Slate board refresh (see `UTacticsMatchSubsystem::MarkBoardVisualDirty`). */
enum class ETacticsBoardVisualDirty : uint32
{
	None = 0,
	Units = 1u << 0,
	Highlights = 1u << 1,
	Terrain = 1u << 2,
	Layout = 1u << 3,
	All = 0xFFFFFFFFu,
};

inline constexpr ETacticsBoardVisualDirty operator|(const ETacticsBoardVisualDirty A, const ETacticsBoardVisualDirty B) noexcept
{
	return static_cast<ETacticsBoardVisualDirty>(static_cast<uint32>(A) | static_cast<uint32>(B));
}
