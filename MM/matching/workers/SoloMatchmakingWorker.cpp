#include "stdafx.h"

#include "core/MatchmakingTicket.h"
#include "core/MatchmakingRule.h"
#include "../workers/SoloMatchmakingWorker.h"
#include "../MatchSessionManager.h"
#include "../SoloMatchmakingManager.h"
#include "../../workers/WorkerJobRequest.h"


CSoloMatchmakingWorker::CSoloMatchmakingWorker(int worker_index, std::shared_ptr<const MatchmakingRule> matchmaking_rule, const MatchmakingGroupKey& matchmaking_group_key)
	: IWorker(worker_index),
	m_MatchmakingRule(matchmaking_rule),
	m_MatchmakingGroupKey(matchmaking_group_key),
	m_WaitTimeCalculator(QueueTimeCalculator(std::format("[{}] Calc({}{}) ", __FUNCTION__, SStringUtil::ToString(m_MatchmakingGroupKey.second), m_WorkerID)))
{

}

CSoloMatchmakingWorker::~CSoloMatchmakingWorker()
{
}

bool CSoloMatchmakingWorker::EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr)
{
	auto matchmaking_request_ptr = std::dynamic_pointer_cast<CMatchmakingRequest>(worker_job_request_ptr);
	if (matchmaking_request_ptr == nullptr || matchmaking_request_ptr->m_MatchTicketPtr == nullptr)
		return false;

	std::unique_lock lock(m_WorkerMutex);

	if (matchmaking_request_ptr->m_MatchTicketPtr->GetStatus() != EMatchTicketStatus::QUEUED)
		return false;

	if (false == m_WaitingQueue.emplace(matchmaking_request_ptr->m_MatchTicketId, matchmaking_request_ptr).second)
	{
		std::cout << std::format("m_WaitingQueue insert failed.");
		return false;
	}

	return true;
}

bool CSoloMatchmakingWorker::Erase(CMatchTicketPtr match_ticket_ptr)
{
	if (match_ticket_ptr == nullptr)
		return false;

	std::unique_lock lock(m_WorkerMutex);

	m_WaitingQueue.erase(match_ticket_ptr->GetMatchTicketID());
	m_SendProgressingList.erase(match_ticket_ptr->GetMatchTicketID());

	return true;
}

bool CSoloMatchmakingWorker::Cancel(CMatchTicketPtr match_ticket_ptr)
{
	if (match_ticket_ptr == nullptr)
		return false;

	{
		std::unique_lock lock(m_WorkerMutex);

		if (false == match_ticket_ptr->ChangeStatus(EMatchTicketStatus::CANCELLED))
			return false;

		m_WaitingQueue.erase(match_ticket_ptr->GetMatchTicketID());
		m_SendProgressingList.erase(match_ticket_ptr->GetMatchTicketID());
	}

	return true;
}

bool CSoloMatchmakingWorker::DoWorkerAsyncJob()
{
	auto curr_time_msec = time_msec(nullptr);
	nProtocol_Result matchmaking_result = nProtocol_Result::en::Protocol_Success;

	bool send_progressing_list = false;
	bool matching_complete = false;

	std::set<CMatchTicketPtr> matching_list;
	{
		std::unique_lock lock(m_WorkerMutex);

		for (auto it = m_WaitingQueue.begin(); it != m_WaitingQueue.end(); it = m_WaitingQueue.erase(it))
		{
			if (m_SendProgressingList.size() >= m_MatchmakingRule->m_MatchPlayerCount)
				break;
			
			auto matchmaking_request_ptr = it->second;
			if (matchmaking_request_ptr)
			{
				send_progressing_list |= m_SendProgressingList.emplace(matchmaking_request_ptr->m_MatchTicketPtr->GetMatchTicketID(), matchmaking_request_ptr).second;
			}
			else
			{
				std::cout << "CRITICAL ERROR!!" << std::endl;
			}
		}
		
		if (m_SendProgressingList.size() >= m_MatchmakingRule->m_MatchPlayerCount)
			matching_complete = true;

		if (send_progressing_list || matching_complete)
		{
			for (auto&& [match_ticket_id, matchmaking_request_ptr] : m_SendProgressingList)
			{
				if (matching_complete)
				{
					m_WaitTimeCalculator.Add(curr_time_msec - matchmaking_request_ptr->m_StartTimeMsec);

					auto match_ticket_ptr = matchmaking_request_ptr->m_MatchTicketPtr;
					if (nullptr == match_ticket_ptr)
					{
						std::cout << "logic error1" << std::endl;
						matchmaking_result = nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession;
						continue;
					}

					if (false == match_ticket_ptr->ChangeStatus(EMatchTicketStatus::SEARCHING))
					{
						std::cout << std::format("logic error2  - state {}. \n", nMatchTicketStatus(match_ticket_ptr->GetStatus()).get_enum_string()) << std::endl;
						matchmaking_result = nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession;
					}
				}

				matching_list.insert(matchmaking_request_ptr->m_MatchTicketPtr);
			}

			if (matching_complete)
				m_SendProgressingList.clear();
		}
	}

	if (matching_complete == false)
	{
		if (send_progressing_list)
			SendProgressingList(std::move(matching_list));

		return false;
	}

	if (matchmaking_result == nProtocol_Result::en::Protocol_Success)
	{
		GetMatchSessionManager()->CreateMatchSession(m_MatchmakingGroupKey, std::move(matching_list));
	}
	else
	{
		for (auto match_ticket_ptr : matching_list)
		{
			GetSoloMatchmakingManager()->OnFailedMatchmaking(match_ticket_ptr, std::move(matchmaking_result), std::format("{} {}", __FUNCTION__, __LINE__));
		}
	}
	return true;
}

bool CSoloMatchmakingWorker::IsWorkerJobAvailable() const
{
	std::shared_lock lock(m_WorkerMutex);

	return m_WaitingQueue.size() > 0;
}

void CSoloMatchmakingWorker::SendProgressingList(std::set<CMatchTicketPtr>&& matching_list) const
{
	(void)matching_list;
}

CSoloMatchmakingWorkerGroup::CSoloMatchmakingWorkerGroup(const MatchmakingGroupKey& matchmaking_group_key, std::shared_ptr<const MatchmakingRule> matchmaking_rule, int worker_count)
	: IWorkerGroup<MatchmakingGroupKey, CSoloMatchmakingWorker>(matchmaking_group_key, worker_count, matchmaking_rule, matchmaking_group_key)
	, m_MatchmakingRule(matchmaking_rule)
{
}

CSoloMatchmakingWorkerGroup::~CSoloMatchmakingWorkerGroup()
{
}

bool CSoloMatchmakingWorkerGroup::EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr)
{
	auto matchmaking_request_ptr = std::dynamic_pointer_cast<CMatchmakingRequest>(worker_job_request_ptr);
	if (matchmaking_request_ptr == nullptr)
		return false;

	if (matchmaking_request_ptr == nullptr)
		return false;

	auto match_ticket_ptr = matchmaking_request_ptr->m_MatchTicketPtr;
	if (match_ticket_ptr == nullptr)
		return false;

	if (false == m_MatchmakingRule->m_Combination.contains(matchmaking_request_ptr->m_PlayerCount))
		return false;

	if (false == match_ticket_ptr->ChangeStatus(EMatchTicketStatus::QUEUED))
		return false;
	
	matchmaking_request_ptr->SetBaseWorkerIndex(GetWorkerIndex(match_ticket_ptr));

	auto worker = GetWorker(matchmaking_request_ptr->GetBaseWorkerIndex());
	if (worker == nullptr)
	{
		std::cout << std::format("GetWorkerSlot insert failed.");
		return false;
	}

	{
		std::unique_lock<std::mutex> lock(m_WorkerGroupMutex);
		if (false == worker->EnqueueJobRequest(matchmaking_request_ptr))
		{
			worker->Erase(matchmaking_request_ptr->m_MatchTicketPtr);
			std::cout << std::format("m_WaitingQueue insert failed.");
			return false;
		}
	}

	m_Condition.notify_all();
	return true;
}

void CSoloMatchmakingWorkerGroup::RemoveMatchTicket(CMatchTicketPtr match_ticket_ptr)
{
	if (match_ticket_ptr == nullptr)
		return;

	if (auto worker = GetWorker(GetWorkerIndex(match_ticket_ptr)))
		worker->Erase(match_ticket_ptr);
}

bool CSoloMatchmakingWorkerGroup::CancelMatchTicket(CMatchTicketPtr match_ticket_ptr)
{
	if (auto worker = GetWorker(GetWorkerIndex(match_ticket_ptr)))
		return worker->Cancel(match_ticket_ptr);

	return false;
}

int CSoloMatchmakingWorkerGroup::GetWorkerIndex(CMatchTicketPtr match_ticket_ptr) const
{
	if (match_ticket_ptr == nullptr)
		return -1;

	return match_ticket_ptr->GetMatchTicketID() % m_WorkerCount;
}

int CSoloMatchmakingWorkerGroup::GetWorkerIndex(IWorkerJobRequestPtr worker_job_request_ptr) const
{
	const auto matchmaking_request_ptr = std::dynamic_pointer_cast<CMatchmakingRequest> (worker_job_request_ptr);
	if (matchmaking_request_ptr == nullptr)
		return -1;

	return GetWorkerIndex(matchmaking_request_ptr->m_MatchTicketPtr);
}
