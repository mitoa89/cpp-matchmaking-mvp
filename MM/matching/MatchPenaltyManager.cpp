#include "stdafx.h"

#include "MatchPenaltyManager.h"

CMatchPenaltyManager::CMatchPenaltyManager()
	: m_AsyncJobWorker(1)
{
}

bool CMatchPenaltyManager::Init()
{
	m_IsRunning = true;
	m_AsyncJobWorker.Start();
	return true;
}

void CMatchPenaltyManager::Terminate()
{
	m_IsRunning = false;
	m_AsyncJobWorker.Stop();
}

bool CMatchPenaltyManager::AddDodgePenalty(PLAYER_KEY player_key, long long penalty_expire_time_msec)
{
	{
		std::unique_lock lock(m_PenaltyMutex);
		m_PlayerDodgePenaltyMap.insert_or_assign(player_key, PlayerMatchDodgePenalty { player_key, penalty_expire_time_msec });
	}

	m_AsyncJobWorker.DoAsyncJob(penalty_expire_time_msec, [](PLAYER_KEY penalty_player_key)
	{
		auto dodge_penalty = GetMatchPenaltyManager()->FindDodgePenalty(penalty_player_key);
		if (dodge_penalty.has_value() && dodge_penalty->m_PenaltyExpireTimeMSec <= time_msec())
			GetMatchPenaltyManager()->ClearDodgePenalty(penalty_player_key);
	}, player_key);

	return true;
}

void CMatchPenaltyManager::ClearDodgePenalty(const PLAYER_KEY& player_key)
{
	std::unique_lock lock(m_PenaltyMutex);
	m_PlayerDodgePenaltyMap.erase(player_key);
}

bool CMatchPenaltyManager::HasDodgePenalty(const PLAYER_KEY& player_key, long long curr_time_msec) const
{
	auto dodge_penalty = FindDodgePenalty(player_key);
	return dodge_penalty.has_value() && dodge_penalty->m_PenaltyExpireTimeMSec > curr_time_msec;
}

bool CMatchPenaltyManager::HasDodgePenalty(const MatchTicketRequest& match_ticket_request) const
{
	const auto curr_time_msec = time_msec();
	for (const auto& player : match_ticket_request.m_Players)
	{
		if (HasDodgePenalty(player.m_PlayerKey, curr_time_msec))
			return true;
	}

	return false;
}

std::optional<PlayerMatchDodgePenalty> CMatchPenaltyManager::FindDodgePenalty(const PLAYER_KEY& player_key) const
{
	std::shared_lock lock(m_PenaltyMutex);
	const auto it = m_PlayerDodgePenaltyMap.find(player_key);
	if (it == m_PlayerDodgePenaltyMap.end())
		return std::nullopt;

	return it->second;
}
