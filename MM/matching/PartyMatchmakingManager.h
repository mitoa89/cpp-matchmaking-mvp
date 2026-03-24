#pragma once

#include "TSingleton.h"
#include "TSafeMap.h"
#include "matching/MatchmakingTypes.h"
#include "workers/PartyMatchmakingWorker.h"

// 파티 매치 큐 관리자
class CPartyMatchmakingManager : public TSingleton<CPartyMatchmakingManager>
{
protected:
	mutable std::shared_mutex m_MatchmakingMutex;
	std::atomic<bool> m_IsRunning = false;
	std::unordered_map<MATCH_TICKET_ID, CMatchTicketPtr> m_MatchTicketMap;
	std::unordered_map<PLAYER_KEY, MATCH_TICKET_ID>	m_MatchTicketFindMapByPlayerKey;
	TWorkerGroupMap<MatchmakingGroupKey, CPartyMatchmakingWorkerGroup> m_MatchmakingRequestWorkers;

private:
	TSafeMap<std::wstring, const MatchmakingRule> m_MatchmakingRules;

public:
	CPartyMatchmakingManager();
	virtual ~CPartyMatchmakingManager() { Terminate(); }

public:
	bool Init();
	void Terminate();

	// 요청에 포함된 모든 플레이어의 이전 상태 정리 함수
	bool CleanupPrevMatchmaking(const MatchTicketRequest& match_ticket_request);

	// 플레이어 단위 이전 상태 정리 함수
	bool CleanupPrevMatchmaking(const PLAYER_KEY& player_key);

public:
	// 파티 매치 요청 등록 함수
	// 이전 상태 정리, 패널티 검사, 워커 전달 수행
	CMatchTicketPtr StartMatchmaking(MatchTicketRequest&& match_ticket_request);

	// 파티 매치 취소 함수
	CMatchTicketPtr StopMatchmaking(const PLAYER_KEY& player_key);

	// 백필 요청 등록 함수
 	void StartMatchBackfill(MatchTicketRequest&& match_ticket_request);

	// 백필 요청 취소 함수
	void StopMatchBackfill(const MATCH_TICKET_ID& match_ticket_id);

public:
	// 매치 실패 처리 함수
	void OnFailedMatchmaking(CMatchTicketPtr match_ticket_ptr, nProtocol_Result&& status_reason, std::string&& status_message);

	// 만료된 티켓 정리 함수
	void OnExpireMatchmaking(const MATCH_TICKET_ID& match_ticket_id);

private:
	bool EnqueueWorkerJobRequest(CMatchmakingRequestPtr);

private:
	bool InsertMatchTicket(CMatchTicketPtr matchmaking_request_ptr);
	CMatchTicketPtr EraseMatchTicket(const MATCH_TICKET_ID& match_ticket_id);
	CMatchTicketPtr FindMatchTicketByPlayerKey(const PLAYER_KEY& player_key) const;
	CMatchTicketPtr FindMatchTicket(const MATCH_TICKET_ID& match_ticket_id) const;

public:
	// 존별 매치 규칙 등록 함수
	bool InsertMatchmakingRule(MatchmakingRule&& matchmaking_rule);

public:
	size_t GetMatchmakingRequestSize() const
	{
		std::shared_lock lock(m_MatchmakingMutex);//read lock
		return m_MatchTicketMap.size();
	}
};

#define GetPartyMatchmakingManager() CPartyMatchmakingManager::GetInstance()
