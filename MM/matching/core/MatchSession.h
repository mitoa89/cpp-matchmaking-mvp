#pragma once

#include "matching/MatchmakingTypes.h"

// 런타임 매치 세션
class CMatchSession
{
public:
	explicit CMatchSession(const std::wstring& match_zone_id, const int& world_no, const MATCH_ID& match_id, std::set<CMatchTicketPtr>&& matchticket_list, bool is_backfill = false);
	virtual ~CMatchSession();
	
	EMatchTicketStatus GetStatus() const
	{
		std::shared_lock lock(m_MatchSessionMutex); 
		return m_Status; 
	}

	void ForceSetStatus(EMatchTicketStatus status)
	{
		std::unique_lock write_lock(m_MatchSessionMutex); 
		m_Status = status;
	}

	void SetStatusReason(nProtocol_Result&& status_reason, std::string&& status_message);
	bool ChangeStatus(EMatchTicketStatus status);

	// 플레이어 통지
	bool WriteToPlayers(SMessageWriterPtr message_writer);

	// 전체 수락 여부 확인
	bool IsAllAcceptPlayerSession() const;

	// 플레이어 응답 반영
	bool AcceptPlayerSession(const PlayerMatchAcceptInfo& accept_info);
	std::set<MatchTicketRequest> GetMatchTicketRequests() const;
	std::set<PLAYER_KEY> GetPlayers() const;
public:
	const MATCH_ID						m_MatchID = -1;
	const std::set<CMatchTicketPtr>		m_MatchTicketList;
	const int							m_MatchWorldNo;
	const std::wstring					m_MatchZoneID;
	const bool							m_IsBackfill = false;

private:
	mutable std::shared_mutex			m_MatchSessionMutex;
	EMatchTicketStatus				m_Status = EMatchTicketStatus::None;
	std::string							m_StatusMessage;
	nProtocol_Result					m_StatusReason;
	std::map<PLAYER_KEY, PlayerMatchAcceptInfo>	m_PlayerMatchAcceptInfos;
};
