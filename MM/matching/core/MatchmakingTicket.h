#pragma once

#include "matching/MatchmakingTypes.h"

// 런타임 매치 티켓
class CMatchTicket
{
public:
	const MatchTicketRequest		m_MatchTicketRequest;
	const long long					m_StartTimeMsec = -1;
	const int						m_MatchWorldGroupID = 0;

private:
	mutable std::shared_mutex		m_MatchTicketMutex;
	EMatchTicketStatus				m_Status = EMatchTicketStatus::None;
	nProtocol_Result				m_StatusReason;
	std::string						m_StatusMessage;

private:
	static MatchTicketRequest NormalizeRequest(const MatchTicketRequest& request);
	static MATCH_TICKET_ID IssueMatchTicketId();

public:
	explicit CMatchTicket(int match_world_group_id, const MatchTicketRequest& request);

	virtual ~CMatchTicket(){}

	MATCH_TICKET_ID GetMatchTicketID() const { return m_MatchTicketRequest.m_MatchTicketId; }
	const MatchTicketRequest& GetMatchTicketRequest() const { return m_MatchTicketRequest; }
	const std::set<MatchPlayerInfo>& GetPlayers() const { return m_MatchTicketRequest.m_Players; }
	const std::wstring& GetType() const { return m_MatchTicketRequest.m_MatchZoneID; }
	MatchmakingGroupKey GetMatchingGroupKey() const { return std::make_pair(m_MatchWorldGroupID, m_MatchTicketRequest.m_MatchZoneID); }
	long long GetStartTimeMsec() const { return m_StartTimeMsec; }
	// 플레이어 통지
	bool WriteToPlayers(SMessageWriterPtr message_writer);

	EMatchTicketStatus GetStatus() const
	{
		std::shared_lock lock(m_MatchTicketMutex); 
		return m_Status; 
	}

	void ForceSetStatus(EMatchTicketStatus status)
	{
		std::unique_lock lock(m_MatchTicketMutex);
		m_Status = status;
	}

	void SetStatusReason(nProtocol_Result&& status_reason, std::string&& status_message)
	{
		std::unique_lock lock(m_MatchTicketMutex);
		m_StatusReason = std::move(status_reason);
		m_StatusMessage = std::move(status_message);
	}

	// 상태 전이
	bool ChangeStatus(EMatchTicketStatus status)
	{
		std::unique_lock lock(m_MatchTicketMutex);

		if (false == nMatchTicketStatusEx::CanChangeStatus(m_Status, status))
			return false;

		m_Status = status;
		return true;
	}

	// 조건부 상태 전이
	bool ChangeStatusIfCurrent(EMatchTicketStatus expected_status, EMatchTicketStatus next_status)
	{
		std::unique_lock lock(m_MatchTicketMutex);

		if (m_Status != expected_status)
			return false;

		if (false == nMatchTicketStatusEx::CanChangeStatus(m_Status, next_status))
			return false;

		m_Status = next_status;
		return true;
	}
};
