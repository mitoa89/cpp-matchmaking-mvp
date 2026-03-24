#include "stdafx.h"
#include "core/MatchSession.h"
#include "MatchSessionManager.h"
#include "AsyncJobWorker.h"
#include "PartyMatchmakingManager.h"
#include "WorkerJobRequest.h"
#include "core/MatchmakingTicket.h"
#include "IntegrationZoneInstance.h"
#include "MatchmakingConfig.h"
#include "MatchPenaltyManager.h"
#include "SoloMatchmakingManager.h"
#include "../integration/IntegrationZoneInstanceManager.h"

namespace
{
	bool IsPartyTicket(const CMatchTicketPtr& match_ticket_ptr)
	{
		if (match_ticket_ptr == nullptr)
			return false;

		return match_ticket_ptr->GetMatchTicketRequest().m_PartyKey > 0 || match_ticket_ptr->GetPlayers().size() > 1;
	}

	void NotifyMatchmakingFailure(const CMatchTicketPtr& match_ticket_ptr, nProtocol_Result&& status_reason, std::string&& status_message)
	{
		if (IsPartyTicket(match_ticket_ptr))
		{
			GetPartyMatchmakingManager()->OnFailedMatchmaking(match_ticket_ptr, std::move(status_reason), std::move(status_message));
			return;
		}

		GetSoloMatchmakingManager()->OnFailedMatchmaking(match_ticket_ptr, std::move(status_reason), std::move(status_message));
	}

	void ExpireMatchmakingOwnership(const CMatchTicketPtr& match_ticket_ptr)
	{
		if (match_ticket_ptr == nullptr)
			return;

		if (IsPartyTicket(match_ticket_ptr))
		{
			GetPartyMatchmakingManager()->OnExpireMatchmaking(match_ticket_ptr->GetMatchTicketID());
			return;
		}

		GetSoloMatchmakingManager()->OnExpireMatchmaking(match_ticket_ptr->GetMatchTicketID());
	}
}

CMatchSessionManager::CMatchSessionManager()
	: m_AsyncJobWorker(1)
{
	for (const auto world_group : MatchmakingConfig::kDefaultWorldGroups)
		m_IntegrateWorlds.emplace(world_group);
}

bool CMatchSessionManager::Init()
{
	m_IsRunning = true;

	m_AsyncJobWorker.Start();

	return true;
}

void CMatchSessionManager::Terminate()
{
	m_IsRunning = false;

	m_AsyncJobWorker.Stop();
}

bool CMatchSessionManager::InsertMatchSession(CMatchSessionPtr match_session_ptr)
{
	if (match_session_ptr == nullptr)
		return false;

	{
		std::unique_lock lock(m_MatchSessionMutex);
		std::vector<PLAYER_KEY> inserted_player_keys;

		if (false == m_MatchSessionMap.emplace(match_session_ptr->m_MatchID, match_session_ptr).second)
		{
			std::cout << std::format("m_MatchSessionMap insert failed.") << std::endl;
			return false;
		}

		for (auto&& match_ticket_ptr : match_session_ptr->m_MatchTicketList)
		{
			for (auto&& player_info : match_ticket_ptr->GetPlayers())
			{
				if (false == m_MatchSessionFindMapByPlayerKey.emplace(player_info.m_PlayerKey, match_session_ptr->m_MatchID).second)
				{
					for (const auto& inserted_player_key : inserted_player_keys)
						m_MatchSessionFindMapByPlayerKey.erase(inserted_player_key);

					m_MatchSessionMap.erase(match_session_ptr->m_MatchID);
					std::cout << std::format("m_MatchSessionFindMapByPlayerKey insert failed.") << std::endl;
					return false;
				}

				inserted_player_keys.emplace_back(player_info.m_PlayerKey);
			}
		}
	}

	return true;
}

CMatchSessionPtr CMatchSessionManager::EraseMatchSession(const MATCH_ID& match_id)
{
	std::unique_lock lock(m_MatchSessionMutex);

	auto it = m_MatchSessionMap.find(match_id);
	if (it == m_MatchSessionMap.end())
		return nullptr;

	auto match_session_ptr = it->second;
	if (match_session_ptr)
	{
		for (auto&& match_ticket_ptr : match_session_ptr->m_MatchTicketList)
		{
			for (auto&& player_info : match_ticket_ptr->GetPlayers())
			{
				auto it = m_MatchSessionFindMapByPlayerKey.find(player_info.m_PlayerKey);
				if (it == m_MatchSessionFindMapByPlayerKey.end())
				{
					continue;
				}

				if (it->second == match_session_ptr->m_MatchID)
				{
					m_MatchSessionFindMapByPlayerKey.erase(player_info.m_PlayerKey);
				}
			}
		}
	}

	m_MatchSessionMap.erase(it);
	return match_session_ptr;
}

CMatchSessionPtr CMatchSessionManager::FindMatchSessionByPlayerKey(const PLAYER_KEY& player_key) const
{
	MATCH_ID match_id;
	{
		std::shared_lock lock(m_MatchSessionMutex);
		auto it = m_MatchSessionFindMapByPlayerKey.find(player_key);
		if (it == m_MatchSessionFindMapByPlayerKey.end())
			return nullptr;

		match_id = it->second;
	}

	return FindMatchSession(match_id);
}

CMatchSessionPtr CMatchSessionManager::FindMatchSession(const MATCH_ID& match_id) const
{
	std::shared_lock lock(m_MatchSessionMutex);

	auto it = m_MatchSessionMap.find(match_id);
	if (it == m_MatchSessionMap.end())
		return nullptr;

	return it->second;
}

size_t CMatchSessionManager::GetMatchSessionSize() const
{
	std::shared_lock lock(m_MatchSessionMutex);
	return m_MatchSessionMap.size();
}

void CMatchSessionManager::SetMatchCreatedObserver(MatchCreatedObserver observer)
{
	std::unique_lock lock(m_MatchCreatedObserverMutex);
	m_MatchCreatedObserver = std::move(observer);
}

int CMatchSessionManager::GetNextIntegrationWorldNo(int world_group_no)
{
	if (m_IntegrateWorlds.empty())
		return -1;

	if (m_IntegrateWorlds.contains(world_group_no))
		return world_group_no;

	std::vector<int> integration_worlds(m_IntegrateWorlds.begin(), m_IntegrateWorlds.end());
	const auto next_index = (m_LastIntegrationWorldNo.fetch_add(1) + 1) % static_cast<int>(integration_worlds.size());
	return integration_worlds[next_index];
}

void CMatchSessionManager::CreateMatchSession(const MatchmakingGroupKey& matchmaking_group_key, std::set<CMatchTicketPtr>&& match_ticket_list)
{
	if (match_ticket_list.empty())
		return;

	CMatchTicketPtr match_ticket_ptr = *(match_ticket_list.begin());
	MATCH_ID match_id = match_ticket_ptr->GetMatchTicketID();

	int	integration_world_no = GetNextIntegrationWorldNo(matchmaking_group_key.first);
	if (integration_world_no == -1)
	{
		std::cout << std::format("[Matchmaking] Can't find world group [worldGroupNo:{}][MatchID:{}", matchmaking_group_key.first, match_id) << std::endl;

		for (auto match_ticket_ptr : match_ticket_list)
		{
			NotifyMatchmakingFailure(match_ticket_ptr, nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession, std::format("{} {}", __FUNCTION__, __LINE__));
		}
		return;
	}
	CMatchSessionPtr match_session_ptr = std::make_shared<CMatchSession>(matchmaking_group_key.second, integration_world_no, match_id, std::move(match_ticket_list));
	match_session_ptr->ForceSetStatus(EMatchTicketStatus::SEARCHING);

	{
		std::shared_lock lock(m_MatchCreatedObserverMutex);
		if (m_MatchCreatedObserver)
			m_MatchCreatedObserver(match_session_ptr, time_msec());
	}


	if (false == InsertMatchSession(match_session_ptr))
	{
		EraseMatchSession(match_id);

		for (auto match_ticket_ptr : match_session_ptr->m_MatchTicketList)
		{
			NotifyMatchmakingFailure(match_ticket_ptr, nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession, std::format("{} {}", __FUNCTION__, __LINE__));
		}
		return;
	}

	if (false == match_session_ptr->ChangeStatus(EMatchTicketStatus::REQUIRES_ACCEPTANCE))
	{
		OnFailedMatchSession(match_id, nProtocol_Result::en::Protocol_MatchSession_AcceptStatus, std::format("{} {}", __FUNCTION__, __LINE__));
		return;
	}

	for (auto&& session_match_ticket_ptr : match_session_ptr->m_MatchTicketList)
		ExpireMatchmakingOwnership(session_match_ticket_ptr);

	auto expire_time_msec = time_msec() + duration_cast<milliseconds>(seconds(kAcceptTimeoutSeconds)).count();
	m_AsyncJobWorker.DoAsyncJob(expire_time_msec, [](const MATCH_ID& match_id)
		{
			GetMatchSessionManager()->OnTimeOutAcceptMatchSession(match_id);
		}, match_id);


	int demo_accept_order = 0;
	for (auto match_ticket_ptr : match_session_ptr->m_MatchTicketList)
	{
		for (auto&& player : match_ticket_ptr->GetPlayers())
		{
			auto demo_accept_time_msec = time_msec() + (kDemoAutoAcceptDelayMilliseconds * (++demo_accept_order));
			m_AsyncJobWorker.DoAsyncJob(demo_accept_time_msec, [](const PLAYER_KEY& player_key)
				{
					GetMatchSessionManager()->AcceptMatchSession(PlayerMatchAcceptInfo(player_key, true));
				}, player.m_PlayerKey);
		}
	}

	return;
}

void CMatchSessionManager::OnTimeOutAcceptMatchSession(const MATCH_ID& match_id)
{
	auto match_session_ptr = FindMatchSession(match_id);
	if (match_session_ptr == nullptr)
		return;

	if (match_session_ptr->GetStatus() != EMatchTicketStatus::REQUIRES_ACCEPTANCE)
		return;

	if (false == match_session_ptr->ChangeStatus(EMatchTicketStatus::TIMED_OUT))
		return;

	EraseMatchSession(match_id);

	match_session_ptr->SetStatusReason(nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession, __FUNCTION__);
	match_session_ptr->WriteToPlayers(std::make_shared<SMessageWriter>());

	std::cout << std::format("timed out accept the match room {} ", match_id) << std::endl;
}

void CMatchSessionManager::OnTimeOutPlaceMatchSession(const MATCH_ID& match_id)
{
	auto match_session_ptr = FindMatchSession(match_id);
	if (match_session_ptr == nullptr)
		return;

	if (match_session_ptr->GetStatus() != EMatchTicketStatus::PLACING)
		return;

	if (false == match_session_ptr->ChangeStatus(EMatchTicketStatus::TIMED_OUT))
		return;

	EraseMatchSession(match_id);
	match_session_ptr->SetStatusReason(nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession, __FUNCTION__);
	match_session_ptr->WriteToPlayers(std::make_shared<SMessageWriter>());

	std::cout << std::format("timed out placing the match room {} ", match_id) << std::endl;
}

void CMatchSessionManager::OnFailedMatchSession(const MATCH_ID& match_id, nProtocol_Result&& status_reason, std::string&& status_message)
{
	auto match_session_ptr = FindMatchSession(match_id);
	if (match_session_ptr == nullptr)
		return;

	if (false == match_session_ptr->ChangeStatus(EMatchTicketStatus::FAILED))
	{
		std::cout << std::format("can't fail current status:{}", nMatchTicketStatus(match_session_ptr->GetStatus()).get_enum_string()) << std::endl;
		return;
	}

	const std::string status_reason_string = status_reason.get_enum_string();
	const std::string status_message_copy = status_message;

	EraseMatchSession(match_id);
	match_session_ptr->SetStatusReason(std::move(status_reason), std::move(status_message));
	match_session_ptr->WriteToPlayers(std::make_shared<SMessageWriter>());

	std::cout << std::format("match id:{} on failed match room : {} {}", match_id, status_reason_string, status_message_copy) << std::endl;
}

void CMatchSessionManager::AcceptMatchSession(PlayerMatchAcceptInfo&&  accept_info)
{
	auto match_session_ptr = FindMatchSessionByPlayerKey(accept_info.m_PlayerKey);
	if (match_session_ptr == nullptr)
		return;

	if (match_session_ptr->IsAllAcceptPlayerSession())
		return;

	if (match_session_ptr->GetStatus() != EMatchTicketStatus::REQUIRES_ACCEPTANCE)
		return;

	if (match_session_ptr->AcceptPlayerSession(accept_info))
	{
		if (accept_info.m_IsAccept == false)
		{
			GetMatchPenaltyManager()->AddDodgePenalty(accept_info.m_PlayerKey, time_msec() + 15000);

			OnFailedMatchSession(match_session_ptr->m_MatchID, nProtocol_Result::en::Protocol_MatchSession_AcceptStatus, __FUNCTION__);
			return;
		}
	}
	else
	{
		return;
	}

	if (match_session_ptr->IsAllAcceptPlayerSession())
	{
		PlaceMatchSession(match_session_ptr->m_MatchID);
	}
	return;
}

void CMatchSessionManager::PlaceMatchSession(const MATCH_ID& match_id)
{
	auto match_session_ptr = FindMatchSession(match_id);
	if (match_session_ptr == nullptr)
		return;

	if (match_session_ptr->GetStatus() == EMatchTicketStatus::PLACING)
		return;

	if (false == match_session_ptr->ChangeStatus(EMatchTicketStatus::PLACING))
	{
		OnFailedMatchSession(match_session_ptr->m_MatchID, nProtocol_Result::en::Protocol_MatchSession_AsyncJobError, std::format("{} {}", __FUNCTION__, __LINE__));
		return;
	}

	auto curr_time_msec = time_msec();
	auto timeout_time_msec = curr_time_msec + duration_cast<milliseconds>(seconds(kPlaceTimeoutSeconds)).count();
	
	m_AsyncJobWorker.DoAsyncJob(timeout_time_msec, [](const MATCH_ID& match_id)
		{
			GetMatchSessionManager()->OnTimeOutPlaceMatchSession(match_id);
		}, match_id);

	// 데모 환경에서는 방 생성 응답을 비동기 완료 콜백으로 대체
	auto complete_time_msec = curr_time_msec + duration_cast<milliseconds>(std::chrono::seconds(kDemoCompletionDelaySeconds)).count();

	IntegrationZoneInstanceInfo match_zone_instance;
	match_zone_instance.m_MatchID = match_id;
	match_zone_instance.m_ZoneGUID = match_id;
	match_zone_instance.m_ZoneID = match_session_ptr->m_MatchZoneID;
	match_zone_instance.m_WorldNo = match_session_ptr->m_MatchWorldNo;
	match_zone_instance.m_WorldSessionKey = match_id;
	match_zone_instance.m_Players = match_session_ptr->GetPlayers();

	m_AsyncJobWorker.DoAsyncJob(complete_time_msec, [](const MATCH_ID& match_id, IntegrationZoneInstanceInfo match_zone_instance)
		{
			GetMatchSessionManager()->OnCompleteMatchSession(std::forward<IntegrationZoneInstanceInfo>(match_zone_instance), nProtocol_Result::en::Protocol_Success);

		}, match_id, match_zone_instance);

	return;
}

void CMatchSessionManager::OnCompleteMatchSession(IntegrationZoneInstanceInfo&& match_zone_instance_info, nProtocol_Result result)
{
	if (result != nProtocol_Result::en::Protocol_Success)
	{
		OnFailedMatchSession(match_zone_instance_info.m_MatchID, std::move(result), std::format("{} {}", __FUNCTION__, __LINE__));
		return;
	}

	bool is_failed_to_complete_match_session = false;
	Defer defer([&]()
		{
			if (is_failed_to_complete_match_session)
			{
				std::cout << "ERROR complete match session." << std::endl;
				OnFailedMatchSession(match_zone_instance_info.m_MatchID, nProtocol_Result::en::Protocol_MatchSession_AsyncJobError, std::format("{} {}", __FUNCTION__, __LINE__));
			}
		});


	auto match_session_ptr = FindMatchSession(match_zone_instance_info.m_MatchID);
	if (match_session_ptr == nullptr)
	{
		is_failed_to_complete_match_session = true;
		return;
	}

	IntegrationZoneInstanceInfoPtr zone_instnace_info_ptr = nullptr;
	
	zone_instnace_info_ptr = std::make_shared<IntegrationZoneInstanceInfoDungeon>(match_zone_instance_info);

	auto zone_instance = GetIntegrationZoneInstanceManager()->Insert(zone_instnace_info_ptr);
	if (zone_instance == nullptr)
	{
		is_failed_to_complete_match_session = true;
		return;
	}

	if (false == match_session_ptr->ChangeStatus(EMatchTicketStatus::COMPLETED))
	{
		GetIntegrationZoneInstanceManager()->Erase(zone_instance->m_IntegrationZoneInstanceKey);
		is_failed_to_complete_match_session = true;
		return;
	}
	
	EraseMatchSession(match_zone_instance_info.m_MatchID);

	match_session_ptr->WriteToPlayers(std::make_shared<SMessageWriter>());

	return;
}
