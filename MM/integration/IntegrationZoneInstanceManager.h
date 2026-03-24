#pragma once
#include "TSingleton.h"
#include "TSafeMap.h"
#include "AsyncJobWorker.h"
#include "IntegrationZoneInstance.h"

// 통합 존 인스턴스 관리자
class CIntegrationZoneInstanceManager : public TSingleton<CIntegrationZoneInstanceManager>
{
protected:
	TSafeMap<IntegrationZoneInstanceKey, CIntegrationZoneInstance> m_IntegrationZoneInstanceMap;
	TSafeMap_Basic<PLAYER_KEY, IntegrationZoneInstanceKey> m_IntegrationZoneInstanceFindMapByPlayerKey;
	CAsyncJobWorkerGroup m_AsyncJobWorker;

public:
	CIntegrationZoneInstanceManager()
		: m_AsyncJobWorker(1) {}
	virtual ~CIntegrationZoneInstanceManager() { Terminate(); }

public:
	bool Init();
	void Terminate();

public:
	// 존 인스턴스 등록 함수
	CIntegrationZoneInstancePtr Insert(IntegrationZoneInstanceInfoPtr zone_instance_info);

	// 존 키 기준 제거 함수
	CIntegrationZoneInstancePtr Erase(const IntegrationZoneInstanceKey& zone_instance_key);

	// 존 키 기준 조회 함수
	CIntegrationZoneInstancePtr Find(const IntegrationZoneInstanceKey& zone_instance_key) const;

	// 플레이어 기준 조회 함수
	CIntegrationZoneInstancePtr FindByPlayerKey(const PLAYER_KEY& player_key) const;

	// 플레이어 기준 제거 함수
	CIntegrationZoneInstancePtr EraseByPlayerKey(const PLAYER_KEY& player_key);

public:
	// 통합 서버 종료 처리 함수
	void Process_IntegrationServerClosed(const long long& session_key, int world_no);

	// 로그인 시 기존 상태 정리 함수
	void Process_PlayerLogin(const PLAYER_KEY& player_key);

	// 통합 존 이탈 예약 함수
	// 실제 닷지 경로에서만 is_dodge=true 사용
	bool Process_PlayerLeave_ToIntegrationServer(const PLAYER_KEY& player_key, bool is_dodge);

	// 통합 존 이탈 완료 처리 함수
	void Process_PlayerLeaved_FromIntegrationServer(const PLAYER_KEY& player_key, bool is_dodge);

	// 존 정보 갱신 처리 함수
	void Process_ZoneInstanceInfoUpdated_FromIntegrationServer(const IntegrationZoneInstanceKey& zone_instance_key, IntegrationZoneInstanceInfoPtr zoneInstanceInfoPtr);

	// 존 종료 처리 함수
	void Process_ZoneInstanceTerminated_FromIntegrationServer(const IntegrationZoneInstanceInfo& zone_instance_info);

private:
	// 비동기 종료 정리 함수
	void Process_IntegrationServerClosedAsync(const long long& session_key, int world_no);

	// 비동기 만료 정리 함수
	void Process_ZoneInstanceExpireAsync(const IntegrationZoneInstanceKey& zone_instance_key);
private:
	std::atomic<bool> m_IsRunning = false;
};

#define GetIntegrationZoneInstanceManager() CIntegrationZoneInstanceManager::GetInstance()
