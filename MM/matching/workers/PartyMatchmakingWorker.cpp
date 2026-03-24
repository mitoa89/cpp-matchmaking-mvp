#include "stdafx.h"
#include "core/MatchmakingTicket.h"
#include "core/MatchmakingRule.h"
#include "../workers/PartyMatchmakingWorker.h"
#include "../MatchSessionManager.h"
#include "../PartyMatchmakingManager.h"
#include "../../workers/WorkerJobRequest.h"


bool CPartyMatchmakingWorker::EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request)
{
	auto matchmaking_request_ptr = std::dynamic_pointer_cast<CMatchmakingRequest>(worker_job_request);
	if (matchmaking_request_ptr == nullptr)
		return false;

	std::unique_lock lock(m_WorkerMutex);

	if (false == m_WaitingQueue.emplace(matchmaking_request_ptr->m_MatchTicketId, matchmaking_request_ptr).second)
	{
		std::cout << std::format("m_WaitingQueue insert failed.");
		return false;
	}

	if (false == m_WaitingQueueByPlayerCount[matchmaking_request_ptr->m_PlayerCount].emplace(matchmaking_request_ptr->m_MatchTicketId, matchmaking_request_ptr).second)
	{
		m_WaitingQueue.erase(matchmaking_request_ptr->m_MatchTicketId);
		std::cout << std::format("m_WaitingQueue_ByPlayerCount insert failed.");
		return false;
	}

	++m_QueuedCountByPlayerCount[matchmaking_request_ptr->m_PlayerCount];
	
	return true;
}

bool CPartyMatchmakingWorker::Erase(CMatchTicketPtr match_ticket_ptr)
{
	if (match_ticket_ptr == nullptr)
		return false;

	std::unique_lock lock(m_WorkerMutex);

	const auto erased_waiting_count = m_WaitingQueue.erase(match_ticket_ptr->GetMatchTicketID());

	auto it = m_WaitingQueueByPlayerCount.find((int)match_ticket_ptr->GetPlayers().size());
	size_t erased_player_count_queue = 0;
	if (it != m_WaitingQueueByPlayerCount.end())
	{
		erased_player_count_queue = it->second.erase(match_ticket_ptr->GetMatchTicketID());
	}

	if (erased_waiting_count > 0 && erased_player_count_queue > 0)
	{
		auto queued_count_it = m_QueuedCountByPlayerCount.find((int)match_ticket_ptr->GetPlayers().size());
		if (queued_count_it != m_QueuedCountByPlayerCount.end())
		{
			queued_count_it->second -= 1;
			if (queued_count_it->second <= 0)
				m_QueuedCountByPlayerCount.erase(queued_count_it);
		}
	}

	return true;
}

bool CPartyMatchmakingWorker::IsWorkerJobAvailable() const
{
	std::shared_lock lock(m_WorkerMutex);

	if (m_MatchmakingRule == nullptr || m_WaitingQueue.empty())
		return false;

	return m_WaitingQueue.size() > 0;
}

std::optional<std::list<CMatchmakingRequestPtr>> CPartyMatchmakingWorker::BuildCandidateLocked(const std::map<int, int>& combination) const
{
	for (const auto& [need_player_count, need_ticket_count] : combination)
	{
		auto queued_count_it = m_QueuedCountByPlayerCount.find(need_player_count);
		if (queued_count_it == m_QueuedCountByPlayerCount.end() || queued_count_it->second < need_ticket_count)
			return std::nullopt;
	}

	std::list<CMatchmakingRequestPtr> matching_list;

	for (const auto& [need_player_count, need_ticket_count] : combination)
	{
		auto waiting_queue_it = m_WaitingQueueByPlayerCount.find(need_player_count);
		if (waiting_queue_it == m_WaitingQueueByPlayerCount.end())
			return std::nullopt;

		int selected_ticket_count = 0;
		for (const auto& [match_ticket_id, matchmaking_request_ptr] : waiting_queue_it->second)
		{
			if (selected_ticket_count >= need_ticket_count)
				break;

			if (matchmaking_request_ptr == nullptr)
				continue;

			auto match_ticket_ptr = matchmaking_request_ptr->m_MatchTicketPtr;
			if (match_ticket_ptr == nullptr || match_ticket_ptr->GetStatus() != EMatchTicketStatus::QUEUED)
				continue;

			matching_list.emplace_back(matchmaking_request_ptr);
			++selected_ticket_count;
		}

		if (selected_ticket_count < need_ticket_count)
			return std::nullopt;
	}

	if (matching_list.empty())
		return std::nullopt;

	return matching_list;
}

std::list<CMatchmakingRequestPtr> CPartyMatchmakingWorker::MakeMatchmakingLocked(const MatchmakingRule& matchmaking_rule) const
{
	if (m_WaitingQueue.empty())
		return {};

	MATCH_TICKET_ID best_oldest_ticket_id = std::numeric_limits<MATCH_TICKET_ID>::max();
	std::list<CMatchmakingRequestPtr> best_matching_list;

	for (const auto& [player_count, queued_count] : m_QueuedCountByPlayerCount)
	{
		if (queued_count <= 0)
			continue;

		for (const auto& combination : matchmaking_rule.GetCombination(player_count))
		{
			auto matching_list_opt = BuildCandidateLocked(combination);
			if (matching_list_opt.has_value() == false)
				continue;

			int sum_player_count = 0;
			MATCH_TICKET_ID oldest_ticket_id = std::numeric_limits<MATCH_TICKET_ID>::max();
			for (const auto& matchmaking_request_ptr : matching_list_opt.value())
			{
				if (matchmaking_request_ptr == nullptr)
					continue;

				sum_player_count += matchmaking_request_ptr->m_PlayerCount;
				oldest_ticket_id = std::min(oldest_ticket_id, matchmaking_request_ptr->m_MatchTicketId);
			}

			if (sum_player_count != matchmaking_rule.m_MatchPlayerCount || oldest_ticket_id == std::numeric_limits<MATCH_TICKET_ID>::max())
				continue;

			if (best_matching_list.empty() || oldest_ticket_id < best_oldest_ticket_id)
			{
				best_oldest_ticket_id = oldest_ticket_id;
				best_matching_list = std::move(matching_list_opt.value());
			}
		}
	}

	return best_matching_list;
}

std::list<CMatchmakingRequestPtr> CPartyMatchmakingWorker::MakeMatchmaking(const MatchmakingRule& matchmaking_rule) const
{
	std::shared_lock lock(m_WorkerMutex);
	return MakeMatchmakingLocked(matchmaking_rule);
}

CPartyMatchmakingWorkerGroup::CPartyMatchmakingWorkerGroup(const MatchmakingGroupKey& matchmaking_group_key, std::shared_ptr<const MatchmakingRule> match_rule, int worker_count)
	: IWorkerGroup<MatchmakingGroupKey, CPartyMatchmakingWorker>(matchmaking_group_key, worker_count, match_rule)
	, m_MatchmakingRule(match_rule)
{
	if (m_MatchmakingRule == nullptr)
		return;

	for (auto&& [player_count, combination] : m_MatchmakingRule->m_Combination)
	{
		m_WaitTimeCalculator.emplace(player_count, QueueTimeCalculator(std::format("{} matchmaking - player count:{}", SStringUtil::ToString(m_WorkerGroupKey.second), player_count)));
	}
}

CPartyMatchmakingWorkerGroup::~CPartyMatchmakingWorkerGroup()
{
}

void CPartyMatchmakingWorkerGroup::WorkerGroupThreadFunc()
{
	if (IsAvailableMatchmakingExpandSlot())
	{
		while (IsRunning())
		{
			std::this_thread::sleep_for(std::chrono::seconds(5));

			ExpandMatchmakingWorkerSlot();
		}
	}
}

bool CPartyMatchmakingWorkerGroup::DoWorkerAsyncJob(int worker_id)
{
	auto worker_ptr = GetWorker(worker_id);
	if (worker_ptr == nullptr)
		return false;

	auto curr_time_msec = time_msec(nullptr);
	nProtocol_Result matchmaking_result = nProtocol_Result::en::Protocol_Success;

	if (m_MatchmakingRule == nullptr)
		return false;

	std::list<CMatchmakingRequestPtr> matching_request_list = worker_ptr->MakeMatchmaking(*m_MatchmakingRule);
	if (matching_request_list.empty())
		return false;

	if (TryCommitMatchmakingRequests(matching_request_list) == false)
		return false;

	std::set<CMatchTicketPtr> match_ticket_list;

	int sumPlayerCount = 0;
	CMatchmakingRequestPtr matchBackFillRequestPtr = nullptr;
	for (auto&& matchmaking_request_ptr : matching_request_list)
	{
		auto match_ticket_ptr = matchmaking_request_ptr->m_MatchTicketPtr;
		if (nullptr == match_ticket_ptr)
		{
			matchmaking_result = nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession;
			continue;
		}

		sumPlayerCount += matchmaking_request_ptr->m_PlayerCount;

		m_WaitTimeCalculator.at(matchmaking_request_ptr->m_PlayerCount).Add(curr_time_msec - matchmaking_request_ptr->m_StartTimeMsec);

		match_ticket_list.insert(matchmaking_request_ptr->m_MatchTicketPtr);
	}

	if (m_MatchmakingRule == nullptr || m_MatchmakingRule->m_MatchPlayerCount != sumPlayerCount)
	{
		matchmaking_result = nProtocol_Result::en::Protocol_Matchmaking_CreateMatchSession;
	}

	if (matchmaking_result == nProtocol_Result::en::Protocol_Success)
	{
			GetMatchSessionManager()->CreateMatchSession(m_WorkerGroupKey, std::move(match_ticket_list));
	}
	else
	{
		for (auto match_ticket_ptr : match_ticket_list)
		{
			GetPartyMatchmakingManager()->OnFailedMatchmaking(match_ticket_ptr, std::move(matchmaking_result), std::format("{} {}", __FUNCTION__, __LINE__));
		}
	}
	return true;
}

bool CPartyMatchmakingWorkerGroup::TryCommitMatchmakingRequests(const std::list<CMatchmakingRequestPtr>& matching_request_list)
{
	if (matching_request_list.empty())
		return false;

	std::unique_lock lock(m_WorkerGroupMutex);
	std::vector<CMatchTicketPtr> changed_tickets;
	changed_tickets.reserve(matching_request_list.size());

	for (const auto& matchmaking_request_ptr : matching_request_list)
	{
		if (matchmaking_request_ptr == nullptr || matchmaking_request_ptr->m_MatchTicketPtr == nullptr)
			return false;

		if (matchmaking_request_ptr->m_MatchTicketPtr->GetStatus() != EMatchTicketStatus::QUEUED)
			return false;
	}

	for (const auto& matchmaking_request_ptr : matching_request_list)
	{
		if (matchmaking_request_ptr->m_MatchTicketPtr->ChangeStatusIfCurrent(EMatchTicketStatus::QUEUED, EMatchTicketStatus::SEARCHING) == false)
		{
			for (const auto& changed_ticket : changed_tickets)
				changed_ticket->ForceSetStatus(EMatchTicketStatus::QUEUED);

			return false;
		}

		changed_tickets.emplace_back(matchmaking_request_ptr->m_MatchTicketPtr);
	}

	for (const auto& matchmaking_request_ptr : matching_request_list)
	{
		for (auto slot_index = matchmaking_request_ptr->GetMinWorkerIndex(GetMaxWorkerIndex()); slot_index <= matchmaking_request_ptr->GetMaxWorkerIndex(GetMaxWorkerIndex()); ++slot_index)
		{
			if (auto worker = GetWorker(slot_index))
				worker->Erase(matchmaking_request_ptr->m_MatchTicketPtr);
		}
	}

	return true;
}

void CPartyMatchmakingWorkerGroup::ExpandMatchmakingWorkerSlot()
{
	bool expand_slot_success = false;
	Defer defer([&]()
		{
			if (expand_slot_success)
				m_Condition.notify_all();
		});

	std::unique_lock lock(m_WorkerGroupMutex);

	auto curr_time_sec = time_sec(nullptr);
	auto slot_expire_time_utc_sec = curr_time_sec - 1;
	std::list<CMatchmakingRequestPtr> temp_expand_matching_request_list;

	for (auto it = m_MatchmakingRequestSlotExpandTimeMap.begin(); it != m_MatchmakingRequestSlotExpandTimeMap.end(); it = m_MatchmakingRequestSlotExpandTimeMap.erase(it))
	{
		auto&& [slot_expand_time_sec, matchmaking_request_ptr] = *it;

		if (slot_expand_time_sec > slot_expire_time_utc_sec)
			break;

		if (matchmaking_request_ptr == nullptr )
			continue;

		auto match_ticket_ptr = matchmaking_request_ptr->m_MatchTicketPtr;
		if (match_ticket_ptr == nullptr || match_ticket_ptr->GetStatus() != EMatchTicketStatus::QUEUED)
			continue;

		expand_slot_success = true;

		int new_slot_expension_level = matchmaking_request_ptr->GetWorkerExpansionLevel() + 1;

		int new_min_slot_index = std::clamp<int>(matchmaking_request_ptr->GetBaseWorkerIndex() - new_slot_expension_level, 0, GetMaxWorkerIndex());
		if (new_min_slot_index != matchmaking_request_ptr->GetMinWorkerIndex(GetMaxWorkerIndex()))
		{
			if (auto worker = GetWorker(new_min_slot_index))
				worker->EnqueueJobRequest(matchmaking_request_ptr);
		}

		int new_max_slot_index = std::clamp<int>(matchmaking_request_ptr->GetBaseWorkerIndex() + new_slot_expension_level, 0, GetMaxWorkerIndex());
		if (new_max_slot_index != matchmaking_request_ptr->GetMaxWorkerIndex(GetMaxWorkerIndex()))
		{
			if (auto worker = GetWorker(new_max_slot_index))
				worker->EnqueueJobRequest(matchmaking_request_ptr);
		}

		matchmaking_request_ptr->SetWorkerExpansionLevel(new_slot_expension_level);

		if (new_min_slot_index > 0 || new_max_slot_index < GetMaxWorkerIndex())
		{
			temp_expand_matching_request_list.emplace_back(matchmaking_request_ptr);
		}
	}

	for (auto matchmaking_request_ptr : temp_expand_matching_request_list)
	{
		m_MatchmakingRequestSlotExpandTimeMap.emplace(curr_time_sec, matchmaking_request_ptr);
	}

	return;
}

int CPartyMatchmakingWorkerGroup::GetWorkerIndex(IWorkerJobRequestPtr worker_job_request_ptr) const
{
	auto matchmaking_request_ptr = std::dynamic_pointer_cast<CMatchmakingRequest> (worker_job_request_ptr);
	if (matchmaking_request_ptr == nullptr)
		return -1;

	return matchmaking_request_ptr->m_MatchTicketId % m_WorkerCount;
}

bool CPartyMatchmakingWorkerGroup::EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request)
{
	auto matchmaking_request_ptr = std::dynamic_pointer_cast<CMatchmakingRequest>(worker_job_request);
	if (matchmaking_request_ptr == nullptr)
		return false;

	auto match_ticket_ptr = matchmaking_request_ptr->m_MatchTicketPtr;
	if (match_ticket_ptr == nullptr)
		return false;

	if (m_MatchmakingRule == nullptr || false == m_MatchmakingRule->m_Combination.contains(matchmaking_request_ptr->m_PlayerCount))
		return false;

	matchmaking_request_ptr->SetBaseWorkerIndex(GetWorkerIndex(worker_job_request));

	{
		std::unique_lock lock(m_WorkerGroupMutex);

		if (IsAvailableMatchmakingExpandSlot())
		{
			m_MatchmakingRequestSlotExpandTimeMap.emplace(time_sec(nullptr), matchmaking_request_ptr);
		}

		auto worker = GetWorker(matchmaking_request_ptr->GetBaseWorkerIndex());
		if (worker == nullptr)
		{
			std::cout << std::format("GetWorkerSlot insert failed.");
			return false;
		}

		if (false == worker->EnqueueJobRequest(matchmaking_request_ptr))
		{
			worker->Erase(matchmaking_request_ptr->m_MatchTicketPtr);

			std::cout << std::format("m_WaitingQueue insert failed.");
			return false;
		}

		match_ticket_ptr->ChangeStatus(EMatchTicketStatus::QUEUED);
	}

	m_Condition.notify_all();
	return true;
}

void CPartyMatchmakingWorkerGroup::RemoveMatchTicket(CMatchTicketPtr match_ticket_ptr)
{
	if (match_ticket_ptr == nullptr)
		return;

	std::unique_lock lock(m_WorkerGroupMutex);

	for (auto&& [worker_index, worker] : m_Workers)
	{
		if (worker)
			worker->Erase(match_ticket_ptr);
	}
}

bool CPartyMatchmakingWorkerGroup::CancelMatchTicket(CMatchTicketPtr match_ticket_ptr)
{
	if (match_ticket_ptr == nullptr)
		return false;

	std::unique_lock lock(m_WorkerGroupMutex);

	if (match_ticket_ptr->ChangeStatus(EMatchTicketStatus::CANCELLED) == false)
		return false;

	for (auto&& [worker_index, worker] : m_Workers)
	{
		if (worker)
			worker->Erase(match_ticket_ptr);
	}

	return true;
}
