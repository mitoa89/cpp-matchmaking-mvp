#pragma once

#include <array>

namespace MatchmakingConfig
{
	inline constexpr wchar_t kDefaultMatchZoneId[] = L"Dungeon";
	inline constexpr int kDefaultMatchPlayerCount = 4;
	inline constexpr int kDefaultSoloMaxPartySize = 1;
	inline constexpr int kDefaultPartyMaxPartySize = 4;
	inline constexpr int kDefaultWorkerCount = 1;
	inline constexpr std::array<int, 4> kDefaultWorldGroups { 0, 1, 2, 3 };
}
