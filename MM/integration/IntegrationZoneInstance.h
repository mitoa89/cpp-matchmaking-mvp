#pragma once

#include "matching/MatchmakingTypes.h"

// 존 내 플레이어 세션 정보
struct IntegrationZoneInstancePlayerInfo
{
	PLAYER_KEY m_PlayerKey = -1;

	IntegrationZoneInstancePlayerInfo() = delete;
	explicit IntegrationZoneInstancePlayerInfo(const PLAYER_KEY& player_key) : m_PlayerKey(player_key)
	{}
	auto operator <=> (const IntegrationZoneInstancePlayerInfo& rhs) const
	{
		return m_PlayerKey <=> rhs.m_PlayerKey;
	}
};

using WORLD_NO = int;
using ZONE_GUID = long long;

using IntegrationZoneInstanceKey = std::pair<WORLD_NO, ZONE_GUID>;

// 통합 존 인스턴스
class CIntegrationZoneInstance
{
public:
	static constexpr long long kExpireDelayMsec = 10 * 1000;

	explicit CIntegrationZoneInstance(IntegrationZoneInstanceInfoPtr match_zone_instance_info);
	virtual ~CIntegrationZoneInstance() = default;

public:
	// 플레이어 통지 함수
	bool WriteToPlayers(SMessageWriterPtr message_writer) const;

	// 현재 남아 있는 플레이어 조회 함수
	std::set<PLAYER_KEY> GetActivePlayers() const;

	// zone 정보 갱신 함수
	void UpdateIntegrationZoneInstanceInfo(IntegrationZoneInstanceInfoPtr zoneInstanceInfoPtr)
	{
		m_IntegrationZoneInstanceUpdateTimeMsec = time_msec();

		std::unique_lock writeLock(m_IntegrationZoneInstanceMutex);

		m_IntegrationZoneInstanceInfoPtr = zoneInstanceInfoPtr;
	}
	// 만료 시각 계산 함수
	long long GetExpireTimeMsec() const
	{
		return m_IntegrationZoneInstanceUpdateTimeMsec.load() + kExpireDelayMsec;
	}
	// 플레이어 세션 제거 함수
	void RemovePlayerSession(const PLAYER_KEY& player_key)
	{
		std::unique_lock writeLock(m_IntegrationZoneInstanceMutex);

		m_PlayerSessions.erase(player_key);
	}
	// 플레이어 세션 비어 있음 확인 함수
	bool EmptyPlayerSession() const
	{
		std::shared_lock readLock(m_IntegrationZoneInstanceMutex);

		return m_PlayerSessions.empty();
	}

	template <typename I>
	I GetIntegrationZoneInstanceInfo() const
	{
		std::shared_lock readLock(m_IntegrationZoneInstanceMutex);

		auto instance_info = std::dynamic_pointer_cast<I>(m_IntegrationZoneInstanceInfoPtr);
		if (instance_info == nullptr)
			return I();

		return *(instance_info);
	}
	
public:
	const IntegrationZoneInstanceKey m_IntegrationZoneInstanceKey;
	const int m_WorldNo = -1;
	const long long m_WorldSessionKey = -1;
	const long long m_ZoneGUID = -1;
	const std::wstring m_ZoneID;

private:
	mutable std::shared_mutex m_IntegrationZoneInstanceMutex;
	std::map<PLAYER_KEY, IntegrationZoneInstancePlayerInfo> m_PlayerSessions;
	IntegrationZoneInstanceInfoPtr m_IntegrationZoneInstanceInfoPtr = nullptr;
	std::atomic<long long> m_IntegrationZoneInstanceUpdateTimeMsec = time_msec();
};
