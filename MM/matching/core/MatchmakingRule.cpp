#include "stdafx.h"
#include "MatchmakingRule.h"

Number::Combination Number::EmptyCombination;

//매칭 가능한 파티 인원수 조합 만들기
// match_player_count : 매칭 결과 인원수
// party_available_player_count : 파티 최대 인원수
Number::CombinationMap Number::MakeCombination(const int match_player_count, const int party_available_player_count)
{
	CombinationMap combination_map;
	if (match_player_count <= 0 || party_available_player_count <= 0)
		return combination_map;

	struct CombinationFrame
	{
		int m_RemainingCount = 0;
		int m_NextPartSize = 1;
		int m_MaxPartSize = 0;
		int m_SelectedPartSize = 0;
	};

	std::vector<int> current_parts;
	current_parts.reserve(match_player_count);

	std::vector<CombinationFrame> combination_stack;
	combination_stack.reserve(match_player_count + 1);
	combination_stack.push_back({ match_player_count, 1, std::min(match_player_count, party_available_player_count), 0 });

	while (combination_stack.empty() == false)
	{
		auto& frame = combination_stack.back();

		if (frame.m_RemainingCount == 0)
		{
			std::map<int, int> grouped_combination;
			for (const int player_count : current_parts)
				grouped_combination[player_count] += 1;

			for (const auto& [player_count, _] : grouped_combination)
				combination_map[player_count].emplace_back(grouped_combination);

			if (frame.m_SelectedPartSize > 0)
				current_parts.pop_back();

			combination_stack.pop_back();
			continue;
		}

		if (frame.m_NextPartSize > frame.m_MaxPartSize)
		{
			if (frame.m_SelectedPartSize > 0)
				current_parts.pop_back();

			combination_stack.pop_back();
			continue;
		}

		const int next_part_size = frame.m_NextPartSize++;
		const int remaining_count = frame.m_RemainingCount - next_part_size;
		current_parts.emplace_back(next_part_size);

		combination_stack.push_back({
			remaining_count,
			next_part_size,
			std::min(remaining_count, party_available_player_count),
			next_part_size
		});
	}

	return combination_map;
}

const Number::Combination& MatchmakingRule::GetCombination(int player_count) const
{
	auto it = m_Combination.find(player_count);
	if (it == m_Combination.end())
		return Number::EmptyCombination;

	return it->second;
}
