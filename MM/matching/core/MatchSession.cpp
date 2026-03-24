#include "stdafx.h"
#include "MatchSession.h"

#include "MatchmakingTicket.h"

CMatchSession::CMatchSession(const std::wstring& match_zone_id, const int& world_no, const MATCH_ID& match_id, std::set<CMatchTicketPtr>&& matchticket_list, bool is_backfill) 
	: m_MatchZoneID(match_zone_id),
	m_MatchID(match_id),
	m_MatchTicketList(std::move(matchticket_list)),
	m_MatchWorldNo(world_no),
	m_IsBackfill(is_backfill)
{
	for (auto&& match_ticket_ptr : m_MatchTicketList)
	{
		if (match_ticket_ptr)
		{
			for (auto&& player_info : match_ticket_ptr->GetPlayers())
			{
				m_PlayerMatchAcceptInfos.emplace(player_info.m_PlayerKey, PlayerMatchAcceptInfo(player_info.m_PlayerKey));
			}
				
		}
	}
}

CMatchSession::~CMatchSession()
{
}

bool CMatchSession::WriteToPlayers(SMessageWriterPtr message_writer)
{
	if (message_writer == nullptr)
		return false;

	for (auto match_ticket_ptr : m_MatchTicketList)
	{
		match_ticket_ptr->WriteToPlayers(message_writer);
	}

	return true;
}


void CMatchSession::SetStatusReason(nProtocol_Result&& status_reason, std::string&& status_message)
{
	std::unique_lock lock(m_MatchSessionMutex);
	for (auto match_ticket : m_MatchTicketList)
	{
		match_ticket->SetStatusReason(nProtocol_Result(status_reason), std::string(status_message));
	}

	m_StatusReason = std::move(status_reason);
	m_StatusMessage = std::move(status_message);
}

bool CMatchSession::ChangeStatus(EMatchTicketStatus status)
{
	std::unique_lock lock(m_MatchSessionMutex);

	if (false == nMatchTicketStatusEx::CanChangeStatus(m_Status, status))
		return false;

	std::vector<std::pair<CMatchTicketPtr, EMatchTicketStatus>> ticket_statuses;
	ticket_statuses.reserve(m_MatchTicketList.size());

	for (auto match_ticket : m_MatchTicketList)
	{
		if (match_ticket == nullptr)
			return false;

		const auto current_status = match_ticket->GetStatus();
		if (false == nMatchTicketStatusEx::CanChangeStatus(current_status, status))
			return false;

		ticket_statuses.emplace_back(match_ticket, current_status);
	}

	std::vector<std::pair<CMatchTicketPtr, EMatchTicketStatus>> changed_tickets;
	changed_tickets.reserve(ticket_statuses.size());

	for (const auto& [match_ticket, current_status] : ticket_statuses)
	{
		if (match_ticket->ChangeStatusIfCurrent(current_status, status) == false)
		{
			for (const auto& [changed_ticket, previous_status] : changed_tickets)
				changed_ticket->ForceSetStatus(previous_status);

			return false;
		}

		changed_tickets.emplace_back(match_ticket, current_status);
	}

	m_Status = status;
	return true;
}

bool CMatchSession::IsAllAcceptPlayerSession() const
{
	std::shared_lock read_lock(m_MatchSessionMutex);

	for (auto&& [player_key, player_session] : m_PlayerMatchAcceptInfos)
	{
		if (player_session.m_IsAccept == false)
			return false;
	}
	return true;
}

bool CMatchSession::AcceptPlayerSession(const PlayerMatchAcceptInfo& accept_info)
{
	std::unique_lock write_lock(m_MatchSessionMutex);

	auto it = m_PlayerMatchAcceptInfos.find(accept_info.m_PlayerKey);
	if (it == m_PlayerMatchAcceptInfos.end())
		return false;

	if (it->second.m_HasResponded)
		return false;

	auto updated_accept_info = accept_info;
	updated_accept_info.m_HasResponded = true;
	it->second = std::move(updated_accept_info);

	return true;
}

std::set<MatchTicketRequest> CMatchSession::GetMatchTicketRequests() const
{
	std::set<MatchTicketRequest> match_tickets;
	for (auto match_ticket_ptr : m_MatchTicketList)
		match_tickets.emplace(match_ticket_ptr->GetMatchTicketRequest());
	return match_tickets;
}

std::set<PLAYER_KEY> CMatchSession::GetPlayers() const
{
	std::set<PLAYER_KEY> players;
	for (auto&& player_key : m_PlayerMatchAcceptInfos)
		players.insert(player_key.first);
	return players;
}
