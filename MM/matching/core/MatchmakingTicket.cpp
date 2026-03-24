#include "stdafx.h"
#include "MatchmakingTicket.h"

namespace
{
	std::atomic<MATCH_TICKET_ID> g_NextMatchTicketId = 1000;
}

CMatchTicket::CMatchTicket(int match_world_group_id, const MatchTicketRequest& request)
	: m_MatchTicketRequest(NormalizeRequest(request))
	, m_StartTimeMsec(time_msec())
	, m_MatchWorldGroupID(match_world_group_id)
{
}

MatchTicketRequest CMatchTicket::NormalizeRequest(const MatchTicketRequest& request)
{
	MatchTicketRequest normalized_request = request;
	if (normalized_request.m_MatchTicketId < 0)
		normalized_request.m_MatchTicketId = IssueMatchTicketId();

	return normalized_request;
}

MATCH_TICKET_ID CMatchTicket::IssueMatchTicketId()
{
	return g_NextMatchTicketId.fetch_add(1);
}

bool CMatchTicket::WriteToPlayers(SMessageWriterPtr message_writer)
{
	if (message_writer == nullptr)
		return false;

	for (auto player_info : GetPlayers())
	{
		;
	}
	return true;
}
