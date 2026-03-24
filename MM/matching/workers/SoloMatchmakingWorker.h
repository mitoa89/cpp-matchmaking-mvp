#pragma once

#include "WorkerGroup.h"

struct MatchmakingRule;

//==============================================================================================================

// 솔로 큐 워커
class CSoloMatchmakingWorker : public IWorker
{
public:
	CSoloMatchmakingWorker() = delete;
	explicit CSoloMatchmakingWorker(int worker_index, std::shared_ptr<const MatchmakingRule> match_rule, const MatchmakingGroupKey& matchmaking_group_key);
	virtual ~CSoloMatchmakingWorker();

	friend class CSoloMatchmakingWorkerGroup;
	const std::shared_ptr<const MatchmakingRule> m_MatchmakingRule;
	const MatchmakingGroupKey m_MatchmakingGroupKey;
private:
	using WaitingQueue = std::map<MATCH_TICKET_ID, CMatchmakingRequestPtr>;
	WaitingQueue m_WaitingQueue;
	WaitingQueue m_SendProgressingList;
	QueueTimeCalculator m_WaitTimeCalculator;

protected:
	bool Erase(CMatchTicketPtr match_ticket_ptr);
	bool Cancel(CMatchTicketPtr match_ticket_ptr);

public:
	bool EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr) override;
	bool DoWorkerAsyncJob() override;
	bool IsWorkerJobAvailable() const override;
	void SendProgressingList(std::set<CMatchTicketPtr>&& matching_list) const;
};

//=============================================================================================================

// 솔로 워커 그룹
class CSoloMatchmakingWorkerGroup : public IWorkerGroup<MatchmakingGroupKey, CSoloMatchmakingWorker>
{
private:
	friend class CSoloMatchmakingWorker;

private:
	const std::shared_ptr<const MatchmakingRule> m_MatchmakingRule;

public:
	CSoloMatchmakingWorkerGroup(const MatchmakingGroupKey& matchmaking_group_key, std::shared_ptr<const MatchmakingRule> matchmaking_rule, int worker_count);
	virtual ~CSoloMatchmakingWorkerGroup();

public:
	// 워커 큐 등록 함수
	bool EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr) override;

	// 티켓 제거 함수
	void RemoveMatchTicket(CMatchTicketPtr match_ticket_ptr);

	// 티켓 취소 함수
	bool CancelMatchTicket(CMatchTicketPtr match_ticket_ptr);
	int GetWorkerIndex(IWorkerJobRequestPtr worker_job_request_ptr) const override;
	int GetWorkerIndex(CMatchTicketPtr match_ticket_ptr) const;
};
