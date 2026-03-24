#include "stdafx.h"
#include "WorkerJobRequest.h"
#include "MatchmakingTicket.h"

CMatchmakingRequest::CMatchmakingRequest(MatchmakingGroupKey&& matchmaking_group_key, CMatchTicketPtr match_ticket)
	: TWorkerJobRequest<MatchmakingGroupKey>(std::forward<MatchmakingGroupKey>(matchmaking_group_key))
	, m_MatchTicketPtr(match_ticket)
	, m_MatchTicketId(match_ticket ? match_ticket->GetMatchTicketID() : -1)
	, m_PlayerCount(match_ticket ? (int)match_ticket->GetPlayers().size() : 0)
{

}

CAsyncJobRequest::CAsyncJobRequest(const long long& job_execution_time_msec, std::function<void()> job_fucn) 
	: TWorkerJobRequest<int>(0), m_JobFunc(job_fucn), m_JobExecuteTimeMSec(job_execution_time_msec)
{

}
