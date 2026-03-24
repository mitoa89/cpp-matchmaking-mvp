#pragma once

#include <algorithm>
#include <functional>
#include <MatchmakingTypes.h>
#include <shared_mutex>

// 워커 작업 요청 베이스
class IWorkerJobRequest
{
public:
	const long long	m_StartTimeMsec = 0;
	int m_BaseWorkerIndex = 0;

	IWorkerJobRequest(long long start_time_msec = time_msec())
		: m_StartTimeMsec(start_time_msec)
	{}
	virtual ~IWorkerJobRequest() = default;

	int GetBaseWorkerIndex() const { std::shared_lock lock(m_JobRequestMutex);  return m_BaseWorkerIndex; }
	void SetBaseWorkerIndex(int match_slot_index) { std::unique_lock lock(m_JobRequestMutex);  m_BaseWorkerIndex = match_slot_index; }

	// 확장 레벨 반영 최소 슬롯 인덱스
	int GetMinWorkerIndex(int max_slot_index) const
	{
		std::shared_lock lock(m_JobRequestMutex);
		return std::clamp<int>(m_BaseWorkerIndex - GetWorkerExpansionLevel(), 0, max_slot_index);
	}

	// 확장 레벨 반영 최대 슬롯 인덱스
	int GetMaxWorkerIndex(int max_slot_index) const
	{
		std::shared_lock lock(m_JobRequestMutex);
		return std::clamp<int>(m_BaseWorkerIndex + GetWorkerExpansionLevel(), 0, max_slot_index);
	}

	int GetWorkerExpansionLevel() const { std::shared_lock lock(m_JobRequestMutex);  return m_WorkerExpansionLevel; }
	void SetWorkerExpansionLevel(int val) { std::unique_lock lock(m_JobRequestMutex);  m_WorkerExpansionLevel = val; }

protected:
	mutable std::shared_mutex m_JobRequestMutex;
	int	m_WorkerExpansionLevel = 0;
};

// 워커 그룹 키를 포함한 작업 요청 템플릿
template <typename WORKER_GROUP_TYPE>
class TWorkerJobRequest : public IWorkerJobRequest
{
public:
	const WORKER_GROUP_TYPE m_WorkerGroupKey;

	TWorkerJobRequest(const WORKER_GROUP_TYPE& worker_group_type)
		: IWorkerJobRequest(), m_WorkerGroupKey(worker_group_type)
	{}
	virtual ~TWorkerJobRequest() = default;
};

// 매치 큐 작업 요청
class CMatchmakingRequest : public TWorkerJobRequest<MatchmakingGroupKey>
{
public:
	const CMatchTicketPtr	m_MatchTicketPtr = nullptr;
	const MATCH_TICKET_ID	m_MatchTicketId = -1;
	const int				m_PlayerCount = 0;

	CMatchmakingRequest(MatchmakingGroupKey&& matchmaking_group_key, CMatchTicketPtr match_ticket);
	virtual ~CMatchmakingRequest() = default;
};

// 예약 작업 요청
class CAsyncJobRequest : public  TWorkerJobRequest<int>
{
public:
	CAsyncJobRequest(const long long& job_execution_time_msec, std::function<void()> job_fucn);

	const long long m_JobExecuteTimeMSec = 0;
	const std::function<void()> m_JobFunc;
};
