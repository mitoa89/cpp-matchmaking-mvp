#include "stdafx.h"

#include "tests/MatchTestHelper.h"

#include "IntegrationZoneInstanceManager.h"
#include "matching/core/MatchmakingRule.h"
#include "matching/MatchmakingConfig.h"
#include "matching/MatchPenaltyManager.h"
#include "matching/MatchSessionManager.h"
#include "matching/PartyMatchmakingManager.h"
#include "matching/SoloMatchmakingManager.h"

namespace
{
	std::atomic<PLAYER_KEY> g_NextPlayerKey = 1;
	std::atomic<long long> g_NextPartyKey = 5000;

	MatchTicketRequest BuildMatchTicketRequest(
		const std::wstring& match_zone_id,
		std::set<MatchPlayerInfo>&& players,
		long long party_key,
		int world_no)
	{
		MatchTicketRequest match_ticket_request;
		match_ticket_request.m_WorldNo = world_no;
		match_ticket_request.m_MatchZoneID = match_zone_id;
		match_ticket_request.m_PartyKey = party_key;
		match_ticket_request.m_RequestPlayerKey = players.empty() ? -1 : players.begin()->m_PlayerKey;
		match_ticket_request.m_Players = std::move(players);
		return match_ticket_request;
	}
}

PLAYER_KEY CMatchTestHelper::IssuePlayerKey()
{
	return g_NextPlayerKey.fetch_add(1);
}

long long CMatchTestHelper::IssuePartyKey()
{
	return g_NextPartyKey.fetch_add(1);
}

MatchTicketRequest CMatchTestHelper::MakeMatchTicket(
	const std::wstring& match_zone_id,
	int player_count,
	long long party_key,
	int world_no)
{
	std::set<MatchPlayerInfo> players;
	for (int i = 0; i < player_count; ++i)
		players.emplace(IssuePlayerKey());

	return BuildMatchTicketRequest(match_zone_id, std::move(players), party_key, world_no);
}

MatchTicketRequest CMatchTestHelper::MakeMatchTicketWithPlayers(
	const std::wstring& match_zone_id,
	std::initializer_list<PLAYER_KEY> player_keys,
	long long party_key,
	int world_no)
{
	std::set<MatchPlayerInfo> players;
	for (const auto player_key : player_keys)
		players.emplace(player_key);

	return BuildMatchTicketRequest(match_zone_id, std::move(players), party_key, world_no);
}

void CMatchTestHelper::InitializeManagers()
{
	GetSoloMatchmakingManager()->InsertMatchmakingRule(MatchmakingRule(
		MatchmakingConfig::kDefaultMatchZoneId,
		MatchmakingConfig::kDefaultMatchPlayerCount,
		MatchmakingConfig::kDefaultSoloMaxPartySize));
	GetPartyMatchmakingManager()->InsertMatchmakingRule(MatchmakingRule(
		MatchmakingConfig::kDefaultMatchZoneId,
		MatchmakingConfig::kDefaultMatchPlayerCount,
		MatchmakingConfig::kDefaultPartyMaxPartySize));

	GetPartyMatchmakingManager()->Init();
	GetSoloMatchmakingManager()->Init();
	GetMatchPenaltyManager()->Init();
	GetMatchSessionManager()->Init();
	GetIntegrationZoneInstanceManager()->Init();
}

void CMatchTestHelper::TerminateManagers()
{
	GetIntegrationZoneInstanceManager()->Terminate();
	GetMatchSessionManager()->Terminate();
	GetMatchPenaltyManager()->Terminate();
	GetSoloMatchmakingManager()->Terminate();
	GetPartyMatchmakingManager()->Terminate();
}

CMatchTestHelper::QueueSnapshot CMatchTestHelper::CaptureQueueSnapshot()
{
	return
	{
		GetPartyMatchmakingManager()->GetMatchmakingRequestSize(),
		GetSoloMatchmakingManager()->GetMatchmakingRequestSize(),
		GetMatchSessionManager()->GetMatchSessionSize(),
	};
}

bool CMatchTestHelper::WaitForQueuesToDrain(std::chrono::seconds timeout)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	int consecutive_idle_checks = 0;

	while (std::chrono::steady_clock::now() < deadline)
	{
		const auto snapshot = CaptureQueueSnapshot();
		if (snapshot.IsIdle())
		{
			++consecutive_idle_checks;
			if (consecutive_idle_checks >= 3)
				return true;
		}
		else
		{
			consecutive_idle_checks = 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	return false;
}
