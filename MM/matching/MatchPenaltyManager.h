#pragma once

#include "TSingleton.h"
#include "MatchmakingTypes.h"
#include "AsyncJobWorker.h"

// 닷지 패널티 관리기
class CMatchPenaltyManager : public TSingleton<CMatchPenaltyManager>
{
public:
	CMatchPenaltyManager();
	virtual ~CMatchPenaltyManager() { Terminate(); }

	bool Init();
	void Terminate();

	// 닷지 패널티 등록 함수
	bool AddDodgePenalty(PLAYER_KEY player_key, long long penalty_expire_time_msec);

	// 닷지 패널티 제거 함수
	void ClearDodgePenalty(const PLAYER_KEY& player_key);

	// 플레이어 패널티 확인 함수
	bool HasDodgePenalty(const PLAYER_KEY& player_key, long long curr_time_msec = time_msec()) const;

	// 요청 포함 플레이어 전체 패널티 확인 함수
	bool HasDodgePenalty(const MatchTicketRequest& match_ticket_request) const;

	// 플레이어 패널티 조회 함수
	std::optional<PlayerMatchDodgePenalty> FindDodgePenalty(const PLAYER_KEY& player_key) const;

private:
	mutable std::shared_mutex m_PenaltyMutex;
	std::unordered_map<PLAYER_KEY, PlayerMatchDodgePenalty> m_PlayerDodgePenaltyMap;
	CAsyncJobWorkerGroup m_AsyncJobWorker;
	std::atomic<bool> m_IsRunning = false;
};

#define GetMatchPenaltyManager() CMatchPenaltyManager::GetInstance()
