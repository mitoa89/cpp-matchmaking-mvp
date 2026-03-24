#pragma once
#include "TSingleton.h"
#include "MatchmakingTypes.h"
#include "AsyncJobWorker.h"

using namespace std::chrono;
struct PlayerMatchAcceptInfo;

// 매치 세션 관리자
class CMatchSessionManager : public TSingleton<CMatchSessionManager>
{
public:
	using MatchCreatedObserver = std::function<void(const CMatchSessionPtr&, long long created_time_msec)>;

protected:
	mutable std::shared_mutex m_MatchSessionMutex;
	std::unordered_map<MATCH_ID, CMatchSessionPtr> m_MatchSessionMap;
	std::unordered_map<PLAYER_KEY, MATCH_ID> m_MatchSessionFindMapByPlayerKey;
	CAsyncJobWorkerGroup m_AsyncJobWorker;
	mutable std::shared_mutex m_MatchCreatedObserverMutex;
	MatchCreatedObserver m_MatchCreatedObserver;

public:
	CMatchSessionManager();
	virtual ~CMatchSessionManager() { Terminate(); }

public:
	bool Init();
	void Terminate();
	
public:
	// 매치 세션 생성
	void CreateMatchSession(const MatchmakingGroupKey& matchmaking_group_key, std::set<CMatchTicketPtr>&& match_ticket_list);

	// 플레이어 수락 응답 반영
	void AcceptMatchSession(PlayerMatchAcceptInfo&& accept_info);

	// 세션 배치 시작
	void PlaceMatchSession(const MATCH_ID& match_id);

public:
	// 수락 대기 타임아웃 처리
	void OnTimeOutAcceptMatchSession(const MATCH_ID& match_id);

	// 배치 대기 타임아웃 처리
	void OnTimeOutPlaceMatchSession(const MATCH_ID& match_id);

	// 세션 실패 처리
	void OnFailedMatchSession(const MATCH_ID& match_id, nProtocol_Result&& status_reason, std::string&& status_message);

	// 세션 완료 처리
	void OnCompleteMatchSession(IntegrationZoneInstanceInfo&& match_zone_instance_info, nProtocol_Result result);

private:
	bool InsertMatchSession(CMatchSessionPtr match_session_ptr);
	CMatchSessionPtr EraseMatchSession(const MATCH_ID& match_id);
	CMatchSessionPtr FindMatchSession(const MATCH_ID& match_id) const;
	CMatchSessionPtr FindMatchSessionByPlayerKey(const PLAYER_KEY& player_key) const;

public:
	size_t GetMatchSessionSize() const;

	// 세션 생성 콜백 등록
	void SetMatchCreatedObserver(MatchCreatedObserver observer);

private:
	int GetNextIntegrationWorldNo(int world_group_no);

private:
	static constexpr int kAcceptTimeoutSeconds = 60;
	static constexpr int kPlaceTimeoutSeconds = 10;
	static constexpr int kDemoCompletionDelaySeconds = 2;
	static constexpr int kDemoAutoAcceptDelayMilliseconds = 1000;

private:
	std::atomic<bool> m_IsRunning = false;
	std::set<int> m_IntegrateWorlds;
	std::atomic<int> m_LastIntegrationWorldNo = -1;
};


#define GetMatchSessionManager() CMatchSessionManager::GetInstance()
