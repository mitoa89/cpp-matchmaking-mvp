#include "stdafx.h"
#include "IntegrationZoneInstance.h"
#include "MatchSession.h"
#include "MatchmakingTicket.h"

CIntegrationZoneInstance::CIntegrationZoneInstance(IntegrationZoneInstanceInfoPtr zone_instance_ptr)
	: m_ZoneID(zone_instance_ptr->m_ZoneID),
	m_ZoneGUID(zone_instance_ptr->m_ZoneGUID),
	m_WorldNo(zone_instance_ptr->m_WorldNo),
	m_WorldSessionKey(zone_instance_ptr->m_WorldSessionKey),
	m_IntegrationZoneInstanceKey(std::make_pair(zone_instance_ptr->m_WorldNo, zone_instance_ptr->m_ZoneGUID)),
	m_IntegrationZoneInstanceInfoPtr(zone_instance_ptr)
{
	for (auto&& playerKey : zone_instance_ptr->m_Players)
		m_PlayerSessions.emplace(playerKey, IntegrationZoneInstancePlayerInfo(playerKey));
}

bool CIntegrationZoneInstance::WriteToPlayers(SMessageWriterPtr message_writer) const
{
	if (message_writer == nullptr)
		return false;

	for (auto&& player_key : GetActivePlayers())
		static_cast<void>(player_key);

	return true;

}

std::set<PLAYER_KEY> CIntegrationZoneInstance::GetActivePlayers() const
{
	std::shared_lock read_lock (m_IntegrationZoneInstanceMutex);

	std::set<PLAYER_KEY> players;
	for (auto&& [player_key, player_session] : m_PlayerSessions)
		players.insert(player_key);

	return players;
}
