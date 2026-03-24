#pragma once

#include "WorkerGroup.h"

struct MatchmakingRule;

// 파티 큐 워커
class CPartyMatchmakingWorker : public IWorker
{
public:
	CPartyMatchmakingWorker() = delete;
	explicit CPartyMatchmakingWorker(int worker_index, std::shared_ptr<const MatchmakingRule> matchmaking_rule)
		: IWorker(worker_index)
		, m_MatchmakingRule(std::move(matchmaking_rule))
	{}
	virtual ~CPartyMatchmakingWorker() {}

	friend class CPartyMatchmakingWorkerGroup;

private:
	using WaitingQueue = std::map<MATCH_TICKET_ID, CMatchmakingRequestPtr>;
	WaitingQueue m_WaitingQueue;
	std::unordered_map<int, WaitingQueue> m_WaitingQueueByPlayerCount;
	std::unordered_map<int, int> m_QueuedCountByPlayerCount;
	const std::shared_ptr<const MatchmakingRule> m_MatchmakingRule;

protected:
	bool HasMatchmakingCandidateLocked(const MatchmakingRule& matchmaking_rule) const;
	std::optional<std::list<CMatchmakingRequestPtr>> BuildCandidateLocked(const std::map<int, int>& combination) const;
	std::list<CMatchmakingRequestPtr> MakeMatchmakingLocked(const MatchmakingRule& matchmaking_rule) const;
	std::list<CMatchmakingRequestPtr> MakeMatchmaking(const MatchmakingRule& matchmaking_rule) const;
	bool Erase(CMatchTicketPtr match_ticket_ptr);

public:
	bool EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request) override;
	bool DoWorkerAsyncJob() override { return true; }
	bool IsWorkerJobAvailable() const override;
};

// 파티 워커 그룹
class CPartyMatchmakingWorkerGroup : public IWorkerGroup<MatchmakingGroupKey, CPartyMatchmakingWorker>
{
private:
	std::multimap<long long, CMatchmakingRequestPtr> m_MatchmakingRequestSlotExpandTimeMap;
	std::map<int, QueueTimeCalculator> m_WaitTimeCalculator;

	friend class CPartyMatchmakingWorker;
private:
	const std::shared_ptr<const MatchmakingRule> m_MatchmakingRule;

public:
	CPartyMatchmakingWorkerGroup(const MatchmakingGroupKey &matchmaking_group_key, std::shared_ptr<const MatchmakingRule> match_config,
		                             int worker_count);
	virtual ~CPartyMatchmakingWorkerGroup();

public:
	// 워커 큐 등록
	bool EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request) override;

	// 티켓 제거
	void RemoveMatchTicket(CMatchTicketPtr match_ticket_id);

	// 티켓 취소
	bool CancelMatchTicket(CMatchTicketPtr match_ticket_ptr);

protected:
	void WorkerGroupThreadFunc() override;
	bool DoWorkerAsyncJob(int worker_id) override;

protected:
	bool IsAvailableMatchmakingExpandSlot() const { return m_Workers.size() > 1; }

	// 매칭 결과 커밋
	bool TryCommitMatchmakingRequests(const std::list<CMatchmakingRequestPtr>& matching_request_list);

	// 대기 요청 탐색 범위 확장
	void ExpandMatchmakingWorkerSlot();

	int GetWorkerIndex(IWorkerJobRequestPtr worker_job_request_ptr) const override;

};
