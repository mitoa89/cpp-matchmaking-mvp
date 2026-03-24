#pragma once

#include "TSingleton.h"
#include "TSafeMap.h"
#include "matching/MatchmakingTypes.h"
#include "workers/SoloMatchmakingWorker.h"

class CSoloMatchmakingWorkerGroup;

// 솔로 매치 큐 관리자
class CSoloMatchmakingManager : public TSingleton<CSoloMatchmakingManager>
{
protected:
	mutable std::shared_mutex m_MatchmakingMutex;
	std::atomic<bool> m_IsRunning = false;
	std::unordered_map<MATCH_TICKET_ID, CMatchTicketPtr> m_MatchTicketMap;
	std::unordered_map<PLAYER_KEY, MATCH_TICKET_ID>	m_MatchTicketFindMapByPlayerKey;
	TWorkerGroupMap<MatchmakingGroupKey, CSoloMatchmakingWorkerGroup> m_MatchmakingWorkers;

public:
	CSoloMatchmakingManager();
	virtual ~CSoloMatchmakingManager() { Terminate(); }

public:
	bool Init();
	void Terminate();
	size_t GetMatchmakingRequestSize() const;

public:
	// 솔로 매치 요청 등록
	CMatchTicketPtr StartMatchmaking(MatchTicketRequest&& match_ticket_request);

	// 솔로 매치 취소
	CMatchTicketPtr StopMatchmaking(const PLAYER_KEY& player_key, bool apply_dodge_penalty = true);

	// 요청 단위 이전 상태 정리
	bool CleanupPrevMatchmaking(const MatchTicketRequest& match_ticket_request);

	// 플레이어 단위 이전 상태 정리
	bool CleanupPrevMatchmaking(const PLAYER_KEY& player_key);
	
public:
	// 매치 실패 처리
	void OnFailedMatchmaking(CMatchTicketPtr match_ticket_ptr, nProtocol_Result&& status_reason, std::string&& status_message);

	// 만료 티켓 정리
	void OnExpireMatchmaking(const MATCH_TICKET_ID& match_ticket_id);

private:
	bool EnqueueWorkerJobRequest(CMatchmakingRequestPtr);

private:
	bool InsertMatchTicket(CMatchTicketPtr matchmaking_request_ptr);
	CMatchTicketPtr EraseMatchTicket(const MATCH_TICKET_ID& match_ticket_id);
	CMatchTicketPtr FindMatchTicketByPlayerKey(const PLAYER_KEY& player_key) const;
	CMatchTicketPtr FindMatchTicket(const MATCH_TICKET_ID& match_ticket_id) const;

private:
	TSafeMap<std::wstring, const MatchmakingRule> m_MatchmakingRules;

public: 
	// 존별 매치 규칙 등록
	bool InsertMatchmakingRule(MatchmakingRule&& matchmaking_rule);

};

#define GetSoloMatchmakingManager() CSoloMatchmakingManager::GetInstance()
