#pragma once

#include <chrono>
#include <initializer_list>

#include "matching/MatchmakingTypes.h"

class CMatchTestHelper
{
public:
	struct QueueSnapshot
	{
		size_t m_PartyQueue = 0;
		size_t m_SoloQueue = 0;
		size_t m_SessionQueue = 0;

		bool IsIdle() const
		{
			return m_PartyQueue == 0 && m_SoloQueue == 0 && m_SessionQueue == 0;
		}
	};

public:
	static PLAYER_KEY IssuePlayerKey();
	static long long IssuePartyKey();

	static MatchTicketRequest MakeMatchTicket(
		const std::wstring& match_zone_id,
		int player_count,
		long long party_key = -1,
		int world_no = 0);
	static MatchTicketRequest MakeMatchTicketWithPlayers(
		const std::wstring& match_zone_id,
		std::initializer_list<PLAYER_KEY> player_keys,
		long long party_key = -1,
		int world_no = 0);

	static void InitializeManagers();
	static void TerminateManagers();
	static QueueSnapshot CaptureQueueSnapshot();
	static bool WaitForQueuesToDrain(std::chrono::seconds timeout);

private:
	CMatchTestHelper() = delete;
};
