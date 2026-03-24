#include "stdafx.h"
#include "IntegrationZoneInstance.h"
#include "IntegrationZoneInstanceManager.h"
#include "MatchSession.h"
#include "../matching/MatchPenaltyManager.h"
#include "../matching/PartyMatchmakingManager.h"
#include "../matching/SoloMatchmakingManager.h"

bool CIntegrationZoneInstanceManager::Init()
{
	m_IsRunning = true;

	m_AsyncJobWorker.Start();

	return true;
}

void CIntegrationZoneInstanceManager::Terminate()
{
	m_IsRunning = false;

	m_AsyncJobWorker.Stop();
}

CIntegrationZoneInstancePtr CIntegrationZoneInstanceManager::Insert(IntegrationZoneInstanceInfoPtr zone_instance_info)
{
	if (zone_instance_info == nullptr)
		return nullptr;

	auto zone_instance_ptr = std::make_shared<CIntegrationZoneInstance>(zone_instance_info);

	bool insert_result = m_IntegrationZoneInstanceMap.Do(std::function<bool()>([&]() -> bool
		{
			if (false == m_IntegrationZoneInstanceMap.Insert(zone_instance_ptr->m_IntegrationZoneInstanceKey, zone_instance_ptr))
				return false;

			for (auto&& player_key : zone_instance_ptr->GetActivePlayers())
			{
				if (false == m_IntegrationZoneInstanceFindMapByPlayerKey.Insert(player_key, zone_instance_ptr->m_IntegrationZoneInstanceKey))
					return false;
			}

			return true;
		}));

	if (insert_result == false)
	{
		Erase(zone_instance_ptr->m_IntegrationZoneInstanceKey);
		return nullptr;
	}

	m_AsyncJobWorker.DoAsyncJob(zone_instance_ptr->GetExpireTimeMsec(),
		[](const auto& zoneInstanceKey)
		{
			GetIntegrationZoneInstanceManager()->Process_ZoneInstanceExpireAsync(zoneInstanceKey);
		}, zone_instance_ptr->m_IntegrationZoneInstanceKey);

	return zone_instance_ptr;
}

CIntegrationZoneInstancePtr CIntegrationZoneInstanceManager::Erase(const IntegrationZoneInstanceKey& zone_instance_key)
{
	return m_IntegrationZoneInstanceMap.Do(std::function<CIntegrationZoneInstancePtr()>([&]() -> CIntegrationZoneInstancePtr
		{
			auto zone_instance_ptr = m_IntegrationZoneInstanceMap.Erase(zone_instance_key);
			if (zone_instance_ptr == nullptr)
				return nullptr;

			for (auto&& player_key : zone_instance_ptr->GetActivePlayers())
			{
				m_IntegrationZoneInstanceFindMapByPlayerKey.Erase(player_key);
			}

			return zone_instance_ptr;
		}));
}

CIntegrationZoneInstancePtr CIntegrationZoneInstanceManager::Find(const IntegrationZoneInstanceKey& zone_instance_key) const
{
	return m_IntegrationZoneInstanceMap.Find(zone_instance_key);
}

CIntegrationZoneInstancePtr CIntegrationZoneInstanceManager::FindByPlayerKey(const PLAYER_KEY& player_key) const
{
	auto zone_instance_key = m_IntegrationZoneInstanceFindMapByPlayerKey.Find(player_key);
	if (zone_instance_key.has_value() == false)
		return nullptr;

	return m_IntegrationZoneInstanceMap.Find(*zone_instance_key);
}

CIntegrationZoneInstancePtr CIntegrationZoneInstanceManager::EraseByPlayerKey(const PLAYER_KEY& player_key)
{
	auto zone_instance_ptr = FindByPlayerKey(player_key);
	if (zone_instance_ptr == nullptr)
		return nullptr;

	zone_instance_ptr->RemovePlayerSession(player_key);

	if (zone_instance_ptr->EmptyPlayerSession())
	{
		Erase(zone_instance_ptr->m_IntegrationZoneInstanceKey);
	}

	m_IntegrationZoneInstanceFindMapByPlayerKey.Erase(player_key);

	return zone_instance_ptr;
}

void CIntegrationZoneInstanceManager::Process_IntegrationServerClosed(const long long& session_key, int world_no)
{
	m_AsyncJobWorker.DoAsyncJob(time_msec(),
		[&](const auto& world_no, const auto& session_key)
		{
			GetIntegrationZoneInstanceManager()->Process_IntegrationServerClosedAsync(session_key, world_no);
		}, world_no, session_key);

}

void CIntegrationZoneInstanceManager::Process_IntegrationServerClosedAsync(const long long& session_key, int world_no)
{
	m_IntegrationZoneInstanceMap.DoAll_Parallel([&](CIntegrationZoneInstancePtr zone_instance_ptr)
		{
			if (zone_instance_ptr == nullptr)
				return;

			if (zone_instance_ptr->m_WorldNo != world_no)
				return;

			if (zone_instance_ptr->m_WorldSessionKey < 0 || session_key < 0)
				return;

			if (zone_instance_ptr->m_WorldSessionKey == session_key)
			{
				Erase(zone_instance_ptr->m_IntegrationZoneInstanceKey);
			}
		});
}

void CIntegrationZoneInstanceManager::Process_ZoneInstanceExpireAsync(const IntegrationZoneInstanceKey& zone_instance_key)
{
	auto zone_instance_ptr = Find(zone_instance_key);
	if (zone_instance_ptr == nullptr)
		return;

	if (zone_instance_ptr->GetExpireTimeMsec() > time_msec())
	{
		m_AsyncJobWorker.DoAsyncJob(zone_instance_ptr->GetExpireTimeMsec(),
			[](const auto& zone_instance_key)
			{
				GetIntegrationZoneInstanceManager()->Process_ZoneInstanceExpireAsync(zone_instance_key);
			}, zone_instance_ptr->m_IntegrationZoneInstanceKey);

		return;
	}

	Erase(zone_instance_key);

	return;
}

void CIntegrationZoneInstanceManager::Process_PlayerLogin(const PLAYER_KEY& player_key)
{
	GetPartyMatchmakingManager()->StopMatchmaking(player_key);
	GetSoloMatchmakingManager()->StopMatchmaking(player_key, false);

	auto zone_instance_ptr = FindByPlayerKey(player_key);
	if (zone_instance_ptr == nullptr)
		return;

	auto zone_instance_info = zone_instance_ptr->GetIntegrationZoneInstanceInfo<IntegrationZoneInstanceInfoDungeon>();
}

bool CIntegrationZoneInstanceManager::Process_PlayerLeave_ToIntegrationServer(const PLAYER_KEY& player_key, bool is_dodge)
{
	auto zone_instance_ptr = FindByPlayerKey(player_key);
	if (zone_instance_ptr == nullptr)
		return false;

	m_AsyncJobWorker.DoAsyncJob(time_msec(),
		[](const PLAYER_KEY& request_player_key, bool request_is_dodge)
		{
			GetIntegrationZoneInstanceManager()->Process_PlayerLeaved_FromIntegrationServer(request_player_key, request_is_dodge);
		}, player_key, is_dodge);

	return true;
}

void CIntegrationZoneInstanceManager::Process_PlayerLeaved_FromIntegrationServer(const PLAYER_KEY& player_key, bool is_dodge)
{
	auto zone_instance_ptr = EraseByPlayerKey(player_key);
	if (zone_instance_ptr == nullptr)
		return;

	if (is_dodge)
	{
		GetMatchPenaltyManager()->AddDodgePenalty(player_key, time_msec() + 15000);
	}

	GetMatchPenaltyManager()->FindDodgePenalty(player_key);
}

void CIntegrationZoneInstanceManager::Process_ZoneInstanceInfoUpdated_FromIntegrationServer(const IntegrationZoneInstanceKey& zone_instance_key, IntegrationZoneInstanceInfoPtr zoneInstanceInfoPtr)
{
	auto zone_instance_ptr = Find(zone_instance_key);
	if (zone_instance_ptr == nullptr)
		return;

	zone_instance_ptr->UpdateIntegrationZoneInstanceInfo(zoneInstanceInfoPtr);
}

void CIntegrationZoneInstanceManager::Process_ZoneInstanceTerminated_FromIntegrationServer(const IntegrationZoneInstanceInfo& zone_instance_info)
{
	auto zone_instance_key = std::make_pair(zone_instance_info.m_WorldNo, zone_instance_info.m_ZoneGUID);

	auto zone_instance_ptr = Find(zone_instance_key);
	if (zone_instance_ptr == nullptr)
	{
		return;
	}

	for (auto&& player_key : zone_instance_info.m_Players)
	{
		bool is_dodge = (false == zone_instance_info.m_PaidPlayers.contains(player_key));
		Process_PlayerLeaved_FromIntegrationServer(player_key, is_dodge);
	}

	Erase(zone_instance_key);

	return;
}
