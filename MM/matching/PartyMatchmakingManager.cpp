#include "stdafx.h"
#include "core/MatchmakingRule.h"
#include "core/MatchmakingTicket.h"
#include "PartyMatchmakingManager.h"
#include "workers/PartyMatchmakingWorker.h"
#include "MatchSessionManager.h"
#include "MatchPenaltyManager.h"
#include "WorkerJobRequest.h"
#include "../integration/IntegrationZoneInstanceManager.h"
#include "MatchmakingConfig.h"

CPartyMatchmakingManager::CPartyMatchmakingManager() 
{
}

bool CPartyMatchmakingManager::Init()
{
	m_IsRunning = true;

	m_MatchmakingRules.DoAll([&](std::shared_ptr<const MatchmakingRule> matchmaking_rule)
	{
		if (matchmaking_rule == nullptr)
			return;

		for (const auto world_group : MatchmakingConfig::kDefaultWorldGroups)
		{
			const MatchmakingGroupKey matchmaking_group_key(world_group, matchmaking_rule->m_MatchZoneID);
			m_MatchmakingRequestWorkers.Insert(
				matchmaking_group_key,
				std::make_shared<CPartyMatchmakingWorkerGroup>(
					matchmaking_group_key,
					matchmaking_rule,
					MatchmakingConfig::kDefaultWorkerCount));
		}
	});

	m_MatchmakingRequestWorkers.Start();

	return true;
}

void CPartyMatchmakingManager::Terminate()
{
	m_IsRunning = false;
	m_MatchmakingRequestWorkers.Stop();
}

bool CPartyMatchmakingManager::CleanupPrevMatchmaking(const MatchTicketRequest& match_ticket_request)
{
	bool cleanup_processed = false;

	for (auto&& player : match_ticket_request.m_Players)
		cleanup_processed |= CleanupPrevMatchmaking(player.m_PlayerKey);

	return cleanup_processed;
}

bool CPartyMatchmakingManager::CleanupPrevMatchmaking(const PLAYER_KEY& player_key)
{
	bool cleanup_processed = false;

	if (FindMatchTicketByPlayerKey(player_key) != nullptr)
	{
		cleanup_processed |= (nullptr != StopMatchmaking(player_key));
	}

	cleanup_processed |= GetIntegrationZoneInstanceManager()->Process_PlayerLeave_ToIntegrationServer(player_key, false);

	return cleanup_processed;
}

CMatchTicketPtr CPartyMatchmakingManager::StartMatchmaking(MatchTicketRequest&& match_ticket_request)
{
	if (m_IsRunning == false)
		return nullptr;

	if (CleanupPrevMatchmaking(match_ticket_request))
	{
		return nullptr;
	}

	if (GetMatchPenaltyManager()->HasDodgePenalty(match_ticket_request))
	{
		return nullptr;
	}

	int matchWorldGroupKey = match_ticket_request.m_WorldNo;

	CMatchTicketPtr match_ticket_ptr = std::make_shared<CMatchTicket>(matchWorldGroupKey, match_ticket_request);

	if (false == EnqueueWorkerJobRequest(std::make_shared<CMatchmakingRequest>(match_ticket_ptr->GetMatchingGroupKey(), match_ticket_ptr)))
	{
		match_ticket_ptr->ChangeStatus(EMatchTicketStatus::FAILED);
		EraseMatchTicket(match_ticket_ptr->GetMatchTicketID());
		return match_ticket_ptr;
	}

	return match_ticket_ptr;
}

CMatchTicketPtr CPartyMatchmakingManager::StopMatchmaking(const PLAYER_KEY& player_key)
{
	auto match_ticket_ptr = FindMatchTicketByPlayerKey(player_key);
	if (match_ticket_ptr == nullptr)
	{
		return nullptr;
	}

	if (auto worker_group_ptr = m_MatchmakingRequestWorkers.GetWorkerGroup(match_ticket_ptr->GetMatchingGroupKey()))
	{
		if (worker_group_ptr->CancelMatchTicket(match_ticket_ptr) == false)
			return match_ticket_ptr;
	}
	else if (false == match_ticket_ptr->ChangeStatus(EMatchTicketStatus::CANCELLED))
	{
		return match_ticket_ptr;
	}

	EraseMatchTicket(match_ticket_ptr->GetMatchTicketID());
	return match_ticket_ptr;
}

void CPartyMatchmakingManager::StartMatchBackfill(MatchTicketRequest&& match_ticket_request)
{
	CMatchTicketPtr match_ticket_ptr = std::make_shared<CMatchTicket>(match_ticket_request.m_WorldNo, match_ticket_request);
	if (false == EnqueueWorkerJobRequest(std::make_shared<CMatchmakingRequest>(match_ticket_ptr->GetMatchingGroupKey(), match_ticket_ptr)))
	{
		EraseMatchTicket(match_ticket_ptr->GetMatchTicketID());
		match_ticket_ptr->ChangeStatus(EMatchTicketStatus::FAILED);

		return;
	}

	return;
}


void CPartyMatchmakingManager::StopMatchBackfill(const MATCH_TICKET_ID& match_ticket_id)
{
	auto match_ticket_ptr = FindMatchTicket(match_ticket_id);
	if (match_ticket_ptr == nullptr)
	{
		return;
	}

	if (auto worker_group_ptr = m_MatchmakingRequestWorkers.GetWorkerGroup(match_ticket_ptr->GetMatchingGroupKey()))
	{
		if (worker_group_ptr->CancelMatchTicket(match_ticket_ptr) == false)
			return;
	}
	else if (false == match_ticket_ptr->ChangeStatus(EMatchTicketStatus::CANCELLED))
	{
		return;
	}

	EraseMatchTicket(match_ticket_ptr->GetMatchTicketID());
}

bool CPartyMatchmakingManager::EnqueueWorkerJobRequest(CMatchmakingRequestPtr matchmaking_request_ptr)
{
	if (nullptr == matchmaking_request_ptr)
		return false;

	auto worker_group_ptr = m_MatchmakingRequestWorkers.GetWorkerGroup(matchmaking_request_ptr->m_WorkerGroupKey);
	if (worker_group_ptr == nullptr)
	{
		return false;
	}

	if (false == InsertMatchTicket(matchmaking_request_ptr->m_MatchTicketPtr))
	{
		return false;
	}

	if (false == worker_group_ptr->EnqueueJobRequest(matchmaking_request_ptr))
	{
		EraseMatchTicket(matchmaking_request_ptr->m_MatchTicketPtr->GetMatchTicketID());
		return false;
	}

	return true;
}

void CPartyMatchmakingManager::OnFailedMatchmaking(CMatchTicketPtr match_ticket_ptr, nProtocol_Result&& status_reason, std::string&& status_message)
{
	if (match_ticket_ptr == nullptr)
		return;

	if (false == match_ticket_ptr->ChangeStatus(EMatchTicketStatus::FAILED))
	{
		std::cout << std::format("error failed to fail match ticket:{} on failed match session : {} {}", match_ticket_ptr->GetMatchTicketID(), status_reason.asInt(), status_message) << std::endl;
		return;
	}

	EraseMatchTicket(match_ticket_ptr->GetMatchTicketID());

	std::cout << std::format("match failed ticket:{} on failed match session : {} {}", match_ticket_ptr->GetMatchTicketID(), status_reason.asInt(), status_message) << std::endl;

	match_ticket_ptr->SetStatusReason(std::move(status_reason), std::move(status_message));

	match_ticket_ptr->WriteToPlayers(std::make_shared<SMessageWriter>());
}

void CPartyMatchmakingManager::OnExpireMatchmaking(const MATCH_TICKET_ID& match_ticket_id)
{
	EraseMatchTicket(match_ticket_id);
}

bool CPartyMatchmakingManager::InsertMatchTicket(CMatchTicketPtr match_ticket_ptr)
{
	if (match_ticket_ptr == nullptr)
		return false;

	{
		std::unique_lock lock(m_MatchmakingMutex);
		std::vector<PLAYER_KEY> inserted_player_keys;

		if (false == m_MatchTicketMap.emplace(match_ticket_ptr->GetMatchTicketID(), match_ticket_ptr).second)
		{
			std::cout << std::format("m_MatchTicketMap insert failed.\n");
			return false;
		}

		for (auto&& player_info : match_ticket_ptr->GetPlayers())
		{
			if (false == m_MatchTicketFindMapByPlayerKey.emplace(player_info.m_PlayerKey, match_ticket_ptr->GetMatchTicketID()).second)
			{
				for (const auto& inserted_player_key : inserted_player_keys)
					m_MatchTicketFindMapByPlayerKey.erase(inserted_player_key);

				m_MatchTicketMap.erase(match_ticket_ptr->GetMatchTicketID());
				std::cout << std::format("m_MatchmakingRequestPlayerFindMap insert failed.\n");
				return false;
			}

			inserted_player_keys.emplace_back(player_info.m_PlayerKey);
		}
	}

	return true;
}

CMatchTicketPtr CPartyMatchmakingManager::EraseMatchTicket(const MATCH_TICKET_ID& match_ticket_id)
{
	std::unique_lock lock(m_MatchmakingMutex);

	CMatchTicketPtr match_ticket_ptr = nullptr;
	{
		auto it = m_MatchTicketMap.find(match_ticket_id);
		if (it == m_MatchTicketMap.end())
			return nullptr;

		match_ticket_ptr = it->second;

		m_MatchTicketMap.erase(it);
	}

	for (auto&& player_info : match_ticket_ptr->GetPlayers())
	{
		auto it = m_MatchTicketFindMapByPlayerKey.find(player_info.m_PlayerKey);
		if (it == m_MatchTicketFindMapByPlayerKey.end())
		{
			continue;
		}

		if (it->second == match_ticket_ptr->GetMatchTicketID())
		{
			m_MatchTicketFindMapByPlayerKey.erase(player_info.m_PlayerKey);
		}
	}

	return match_ticket_ptr;
}

CMatchTicketPtr CPartyMatchmakingManager::FindMatchTicketByPlayerKey(const PLAYER_KEY& player_key) const
{
	MATCH_TICKET_ID match_ticket_id;
	{
		std::shared_lock lock(m_MatchmakingMutex);
		auto it = m_MatchTicketFindMapByPlayerKey.find(player_key);
		if (it == m_MatchTicketFindMapByPlayerKey.end())
			return nullptr;

		match_ticket_id = it->second;
	}

	return FindMatchTicket(match_ticket_id);
}

CMatchTicketPtr CPartyMatchmakingManager::FindMatchTicket(const MATCH_TICKET_ID& match_ticket_id) const
{
	std::shared_lock lock(m_MatchmakingMutex);
	auto it = m_MatchTicketMap.find(match_ticket_id);
	if (it == m_MatchTicketMap.end())
		return nullptr;

	return it->second;
}

bool CPartyMatchmakingManager::InsertMatchmakingRule(MatchmakingRule&& matchmaking_rule)
{
	return m_MatchmakingRules.Insert(matchmaking_rule.m_MatchZoneID,
		std::make_shared<MatchmakingRule>(std::move(matchmaking_rule)));
}
