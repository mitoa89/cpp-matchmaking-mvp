#pragma once

#include <compare>
#include <format>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_map>

using PLAYER_KEY = long long;
using PLAYER_COUNT = int;
using MATCH_TICKET_ID = long long;
using MATCH_ID = long long;

extern unsigned long long time_msec(unsigned long long* dst_time = nullptr);
extern long long time_sec(long long* dst_time = nullptr);

class CMatchTicket;
using CMatchTicketPtr = std::shared_ptr<CMatchTicket>;

class CMatchSession;
using CMatchSessionPtr = std::shared_ptr<CMatchSession>;

class IWorkerJobRequest;
using IWorkerJobRequestPtr = std::shared_ptr<IWorkerJobRequest>;

class CAsyncJobRequest;
using CAsyncJobRequestPtr = std::shared_ptr<CAsyncJobRequest>;

class CMatchmakingRequest;
using CMatchmakingRequestPtr = std::shared_ptr<CMatchmakingRequest>;

class CIntegrationZoneInstance;
using CIntegrationZoneInstancePtr = std::shared_ptr<CIntegrationZoneInstance>;

struct IntegrationZoneInstanceInfo;
using IntegrationZoneInstanceInfoPtr = std::shared_ptr<IntegrationZoneInstanceInfo>;

struct IntegrationZoneInstanceInfoDungeon;
using IntegrationZoneInstanceInfoDungeonPtr = std::shared_ptr<IntegrationZoneInstanceInfoDungeon>;

using MatchmakingGroupKey = std::pair<int, std::wstring>;

// worker 대기 시간 통계
struct QueueTimeCalculator
{
	long long m_LastWaitTimeMsec = 0;
	long long m_MinWaitTimeMsec = std::numeric_limits<long long>::max();
	long long m_MaxWaitTimeMsec = 0;
	long long m_AverageWaitTimeMsec = 0;
	long long m_TotalWaitTimeMsec = 0;
	long long m_ProcessedQueueSize = 0;
	std::string m_Desc;

	explicit QueueTimeCalculator(std::string&& desc)
		: m_Desc(std::move(desc))
	{
	}

	void Add(long long wait_time_msec)
	{
		m_LastWaitTimeMsec = wait_time_msec;
		m_MinWaitTimeMsec = std::min(wait_time_msec, m_MinWaitTimeMsec);
		m_MaxWaitTimeMsec = std::max(wait_time_msec, m_MaxWaitTimeMsec);
		m_TotalWaitTimeMsec += wait_time_msec;
		++m_ProcessedQueueSize;
		m_AverageWaitTimeMsec = m_TotalWaitTimeMsec / m_ProcessedQueueSize;
	}

	std::string GetString() const
	{
		return std::format("{}[Calc] queue:{} average:{} min:{} max:{}",
			m_Desc, m_ProcessedQueueSize, m_AverageWaitTimeMsec, m_MinWaitTimeMsec, m_MaxWaitTimeMsec);
	}
};

enum class EMatchTicketStatus
{
	None = -1,
	QUEUED,
	SEARCHING,
	REQUIRES_ACCEPTANCE,
	PLACING,
	COMPLETED,
	FAILED,
	CANCELLED,
	TIMED_OUT,
	Max
};

enum class EProtocol_Result : int
{
	None = -1,
	Protocol_Success,
	Protocol_PacketError,
	Protocol_Matchmaking_CreateMatchSession,
	Protocol_MatchSession_AcceptStatus,
	Protocol_MatchSession_AsyncJobError,
	Max,
};

// 상태 값 문자열 포맷터
class nMatchTicketStatus
{
public:
	using en = EMatchTicketStatus;

	nMatchTicketStatus() = default;
	nMatchTicketStatus(en value)
		: m_Value(value)
	{
	}

	operator en() const noexcept
	{
		return m_Value;
	}

	bool operator==(en rhs) const noexcept
	{
		return m_Value == rhs;
	}

	int asInt() const noexcept
	{
		return static_cast<int>(m_Value);
	}

	const char* get_enum_string() const
	{
		switch (m_Value)
		{
		case en::QUEUED: return "QUEUED";
		case en::SEARCHING: return "SEARCHING";
		case en::REQUIRES_ACCEPTANCE: return "REQUIRES_ACCEPTANCE";
		case en::PLACING: return "PLACING";
		case en::COMPLETED: return "COMPLETED";
		case en::FAILED: return "FAILED";
		case en::CANCELLED: return "CANCELLED";
		case en::TIMED_OUT: return "TIMED_OUT";
		default: return "None";
		}
	}

private:
	en m_Value = en::None;
};

// 결과 코드 문자열 포맷터
class nProtocol_Result
{
public:
	using en = EProtocol_Result;

	nProtocol_Result() = default;
	nProtocol_Result(en value)
		: m_Value(value)
	{
	}

	operator en() const noexcept
	{
		return m_Value;
	}

	bool operator==(en rhs) const noexcept
	{
		return m_Value == rhs;
	}

	int asInt() const noexcept
	{
		return static_cast<int>(m_Value);
	}

	const char* get_enum_string() const
	{
		switch (m_Value)
		{
		case en::Protocol_Success: return "Protocol_Success";
		case en::Protocol_PacketError: return "Protocol_PacketError";
		case en::Protocol_Matchmaking_CreateMatchSession: return "Protocol_Matchmaking_CreateMatchSession";
		case en::Protocol_MatchSession_AcceptStatus: return "Protocol_MatchSession_AcceptStatus";
		case en::Protocol_MatchSession_AsyncJobError: return "Protocol_MatchSession_AsyncJobError";
		default: return "None";
		}
	}

private:
	en m_Value = en::None;
};

// 상태 전이 규칙 테이블
class nMatchTicketStatusEx
{
	inline static const std::unordered_map<EMatchTicketStatus, std::set<EMatchTicketStatus>> m_StateChangeMap =
	{
		{EMatchTicketStatus::None, {EMatchTicketStatus::QUEUED, EMatchTicketStatus::CANCELLED, EMatchTicketStatus::FAILED}},
		{EMatchTicketStatus::QUEUED, {EMatchTicketStatus::SEARCHING, EMatchTicketStatus::CANCELLED, EMatchTicketStatus::FAILED}},
		{EMatchTicketStatus::SEARCHING, {EMatchTicketStatus::REQUIRES_ACCEPTANCE, EMatchTicketStatus::PLACING, EMatchTicketStatus::FAILED}},
		{EMatchTicketStatus::REQUIRES_ACCEPTANCE, {EMatchTicketStatus::PLACING, EMatchTicketStatus::TIMED_OUT, EMatchTicketStatus::FAILED}},
		{EMatchTicketStatus::PLACING, {EMatchTicketStatus::COMPLETED, EMatchTicketStatus::TIMED_OUT, EMatchTicketStatus::FAILED}},
		{EMatchTicketStatus::COMPLETED, {}},
		{EMatchTicketStatus::FAILED, {}},
		{EMatchTicketStatus::CANCELLED, {}},
		{EMatchTicketStatus::TIMED_OUT, {}}
	};

public:
	static bool CanChangeStatus(EMatchTicketStatus curr, EMatchTicketStatus next)
	{
		try
		{
			return m_StateChangeMap.at(curr).contains(next);
		}
		catch (...)
		{
			std::cout << std::format("error change from {} to {} ",
				nMatchTicketStatus(curr).get_enum_string(),
				nMatchTicketStatus(next).get_enum_string());
		}
		return false;
	}
};

// 데모용 플레이어 통지 작성자
struct SMessageWriter
{
};
using SMessageWriterPtr = std::shared_ptr<SMessageWriter>;

// 던전 진행 정보
struct MissionData
{
	std::wstring m_MissionID = L"";
	int m_MissionState = 0;
	int m_MissionFailCount = 0;
	int m_Count = 0;
	int m_FailCount = 0;
	long long m_ExpireTime = 0;
};

// 매칭 대상 플레이어 정보
struct MatchPlayerInfo
{
	PLAYER_KEY m_PlayerKey = -1;
	std::wstring m_PlayerName;
	long long m_OffensePower = 0;
	int m_PlayerLevel = 0;
	int m_Job = 0;

	MatchPlayerInfo() = default;
	explicit MatchPlayerInfo(const PLAYER_KEY& player_key)
		: m_PlayerKey(player_key)
	{
	}

	auto operator<=>(const MatchPlayerInfo& rhs) const
	{
		return m_PlayerKey <=> rhs.m_PlayerKey;
	}
};

// 큐 등록 입력 데이터
struct MatchTicketRequest
{
	int m_WorldNo = -1;
	std::wstring m_MatchZoneID;
	MATCH_TICKET_ID m_MatchTicketId = -1;
	long long m_PartyKey = -1;
	PLAYER_KEY m_RequestPlayerKey = -1;
	std::set<MatchPlayerInfo> m_Players;

	std::weak_ordering operator<=>(const MatchTicketRequest& rhs) const
	{
		if (m_MatchTicketId >= 0 && rhs.m_MatchTicketId >= 0)
			return std::compare_weak_order_fallback(m_MatchTicketId, rhs.m_MatchTicketId);

		return std::tie(m_WorldNo, m_MatchZoneID, m_PartyKey, m_RequestPlayerKey, m_Players)
			<=> std::tie(rhs.m_WorldNo, rhs.m_MatchZoneID, rhs.m_PartyKey, rhs.m_RequestPlayerKey, rhs.m_Players);
	}
};

// 플레이어별 닷지 패널티 정보
struct PlayerMatchDodgePenalty
{
	PLAYER_KEY m_PlayerKey = -1;
	long long m_PenaltyExpireTimeMSec = -1;
};

// 세션 수락 응답 정보
struct PlayerMatchAcceptInfo
{
	PLAYER_KEY m_PlayerKey = -1;
	bool m_IsAccept = false;
	bool m_AdditionalInfo = false;
	bool m_HasResponded = false;
};

// 배치 완료 zone 상태
struct IntegrationZoneInstanceInfo
{
	long long m_ZoneGUID = -1;
	int m_WorldNo = -1;
	long long m_WorldSessionKey = -1;
	MATCH_ID m_MatchID = -1;
	std::wstring m_ZoneID;
	std::set<PLAYER_KEY> m_Players;
	std::set<PLAYER_KEY> m_EnterPlayers;
	std::set<PLAYER_KEY> m_PaidPlayers;

	virtual ~IntegrationZoneInstanceInfo() = default;
};

// 던전 확장 zone 상태
struct IntegrationZoneInstanceInfoDungeon : public IntegrationZoneInstanceInfo
{
	MissionData m_MissionData;

	IntegrationZoneInstanceInfoDungeon() = default;
	explicit IntegrationZoneInstanceInfoDungeon(const IntegrationZoneInstanceInfo& info)
		: IntegrationZoneInstanceInfo(info)
	{
	}

	virtual ~IntegrationZoneInstanceInfoDungeon() = default;
};

// 배치된 세션 요약 정보
struct MatchSessionInfo
{
	IntegrationZoneInstanceInfo m_IntegrationZoneInstanceInfo;
	long long m_SessionExpireTimeSec = -1;
};
