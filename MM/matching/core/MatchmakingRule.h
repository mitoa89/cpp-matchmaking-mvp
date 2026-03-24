#pragma once

#include <list>
#include <map>
#include <string>
#include <unordered_map>

// 파티 조합 계산기
struct Number
{
	using Combination = std::list<std::map<int, int>>;
	using CombinationMap = std::unordered_map<int, Combination>;

	static Combination EmptyCombination;
	static CombinationMap MakeCombination(int match_player_count, int party_available_player_count);
};

// 존별 매치 규칙
struct MatchmakingRule
{
	std::wstring m_MatchZoneID;
	int m_MatchPlayerCount = 1;
	Number::CombinationMap m_Combination;

	MatchmakingRule(const std::wstring& match_zone_id, int match_player_count, int party_available_player_count)
		: m_MatchZoneID(match_zone_id), m_MatchPlayerCount(match_player_count),
		m_Combination(Number::MakeCombination(match_player_count, party_available_player_count))
	{}

	const Number::Combination& GetCombination(int player_count) const;
};
