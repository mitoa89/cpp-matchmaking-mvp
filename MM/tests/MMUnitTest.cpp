#include "stdafx.h"

#include <array>
#include <random>

#include <gtest/gtest.h>

#include "IntegrationZoneInstanceManager.h"
#include "MatchSession.h"
#include "matching/core/MatchmakingRule.h"
#include "matching/MatchmakingConfig.h"
#include "matching/MatchPenaltyManager.h"
#include "matching/SoloMatchmakingManager.h"
#include "matching/MatchSessionManager.h"
#include "MatchmakingTicket.h"
#include "matching/PartyMatchmakingManager.h"
#include "tests/MatchTestHelper.h"

namespace
{
	using TestHelper = CMatchTestHelper;
	using QueueSnapshot = TestHelper::QueueSnapshot;

	struct PerformanceResult
	{
		size_t m_TicketCount = 0;
		size_t m_MatchCount = 0;
		std::chrono::milliseconds m_SubmitElapsed { 0 };
		std::chrono::milliseconds m_DrainElapsed { 0 };
		long long m_LatencyMinMs = 0;
		long long m_LatencyP50Ms = 0;
		long long m_LatencyP95Ms = 0;
		long long m_LatencyP99Ms = 0;
		long long m_LatencyMaxMs = 0;
	};

	void SubmitSoloTraffic(size_t ticket_count)
	{
		for (size_t i = 0; i < ticket_count; ++i)
			GetSoloMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1));
	}

	struct PartyTrafficSubmission
	{
		size_t m_RequestCount = 0;
		size_t m_PlayerCount = 0;
		size_t m_MatchCount = 0;
		size_t m_FailedSubmissionCount = 0;
	};

	// 요청 수는 정확히 맞추고, 전체 큐는 끝까지 비워질 수 있는 파티 조합 생성
	PartyTrafficSubmission SubmitRandomPartyTraffic(size_t target_request_count, std::uint32_t seed)
	{
		static constexpr std::array<std::array<int, 4>, 5> kPartyCombinations =
		{ {
			{ 1, 1, 1, 1 },
			{ 2, 1, 1, 0 },
			{ 2, 2, 0, 0 },
			{ 3, 1, 0, 0 },
			{ 4, 0, 0, 0 },
		} };

		static constexpr std::array<size_t, 5> kRequestCounts = { 4, 3, 2, 2, 1 };
		static constexpr std::array<size_t, 5> kPlayerCounts = { 4, 4, 4, 4, 4 };

		std::mt19937 rng(seed);
		PartyTrafficSubmission submission;
		size_t submitted_request_count = 0;

		while (submitted_request_count < target_request_count)
		{
			std::vector<size_t> selectable_combinations;
			for (size_t combination_index = 0; combination_index < kRequestCounts.size(); ++combination_index)
			{
				if (kRequestCounts[combination_index] <= target_request_count - submitted_request_count)
					selectable_combinations.emplace_back(combination_index);
			}

			std::uniform_int_distribution<size_t> combination_dist(0, selectable_combinations.size() - 1);
			const auto selected_combination = selectable_combinations[combination_dist(rng)];

			for (const int party_size : kPartyCombinations[selected_combination])
			{
				if (party_size == 0)
					break;

				const auto match_ticket_ptr = GetPartyMatchmakingManager()->StartMatchmaking(
					TestHelper::MakeMatchTicket(
						MatchmakingConfig::kDefaultMatchZoneId,
						party_size,
						TestHelper::IssuePartyKey()));
				if (match_ticket_ptr == nullptr)
					++submission.m_FailedSubmissionCount;
				else
				{
					++submission.m_RequestCount;
					submission.m_PlayerCount += static_cast<size_t>(party_size);
				}
			}

			submitted_request_count += kRequestCounts[selected_combination];
			++submission.m_MatchCount;
		}

		return submission;
	}

	template <typename Predicate>
	bool WaitUntil(std::chrono::milliseconds timeout, Predicate&& predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (predicate())
				return true;

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		return predicate();
	}

	class NullStreamBuffer : public std::streambuf
	{
	protected:
		int overflow(int c) override
		{
			return traits_type::not_eof(c);
		}
	};

	class ScopedStreamSilencer
	{
	public:
		ScopedStreamSilencer()
			: m_OldCout(std::cout.rdbuf(&m_NullBuffer))
			, m_OldCerr(std::cerr.rdbuf(&m_NullBuffer))
		{
		}

		~ScopedStreamSilencer()
		{
			std::cout.rdbuf(m_OldCout);
			std::cerr.rdbuf(m_OldCerr);
		}

	private:
		inline static NullStreamBuffer m_NullBuffer;
		std::streambuf* m_OldCout = nullptr;
		std::streambuf* m_OldCerr = nullptr;
	};

	long long PercentileValue(std::vector<long long> values, double percentile)
	{
		if (values.empty())
			return 0;

		std::sort(values.begin(), values.end());
		const auto index = static_cast<size_t>(std::clamp(percentile, 0.0, 1.0) * static_cast<double>(values.size() - 1));
		return values[index];
	}

	class MatchmakingScenarioTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			std::srand(static_cast<unsigned int>(::time(nullptr)));
			TestHelper::InitializeManagers();
			GetMatchSessionManager()->SetMatchCreatedObserver(nullptr);
		}

		void TearDown() override
		{
			GetMatchSessionManager()->SetMatchCreatedObserver(nullptr);
			TestHelper::TerminateManagers();
		}

		void ExpectIdleQueues(const char* scenario_name)
		{
			const auto snapshot = TestHelper::CaptureQueueSnapshot();
			EXPECT_TRUE(snapshot.IsIdle())
				<< scenario_name
				<< " left queues busy"
				<< " party=" << snapshot.m_PartyQueue
				<< " solo=" << snapshot.m_SoloQueue
				<< " session=" << snapshot.m_SessionQueue;
		}

		void RunRandomPartyPerformanceScenario(size_t request_count, std::uint32_t seed, const char* scenario_name)
		{
			static constexpr auto kTimeout = std::chrono::minutes(5);

			std::mutex latency_mutex;
			std::vector<long long> latency_samples;
			size_t created_match_count = 0;
			GetMatchSessionManager()->SetMatchCreatedObserver([&](const CMatchSessionPtr& match_session_ptr, long long created_time_msec)
			{
				if (match_session_ptr == nullptr)
					return;

				std::lock_guard lock(latency_mutex);
				++created_match_count;
				for (const auto& match_ticket_ptr : match_session_ptr->m_MatchTicketList)
				{
					if (match_ticket_ptr == nullptr)
						continue;

					latency_samples.emplace_back(std::max(0LL, created_time_msec - match_ticket_ptr->GetStartTimeMsec()));
				}
			});

			const auto started_at = std::chrono::steady_clock::now();
			PerformanceResult result;
			double tickets_per_second = 0.0;
			double matches_per_second = 0.0;
			{
				ScopedStreamSilencer silencer;
				const auto submission = SubmitRandomPartyTraffic(request_count, seed);
				ASSERT_EQ(0u, submission.m_FailedSubmissionCount);
				EXPECT_EQ(request_count, submission.m_RequestCount);

				const auto submit_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started_at);
				const auto settled = TestHelper::WaitForQueuesToDrain(kTimeout);
				const auto drain_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started_at);

				ASSERT_TRUE(settled) << scenario_name << " did not settle within timeout";
				ExpectIdleQueues(scenario_name);

				std::vector<long long> latency_snapshot;
				size_t observed_match_count = 0;
				{
					std::lock_guard lock(latency_mutex);
					latency_snapshot = latency_samples;
					observed_match_count = created_match_count;
				}

				ASSERT_EQ(latency_snapshot.size(), submission.m_RequestCount);
				ASSERT_EQ(observed_match_count, submission.m_MatchCount);

				result =
				{
					submission.m_RequestCount,
					observed_match_count,
					submit_elapsed,
					drain_elapsed,
					PercentileValue(latency_snapshot, 0.00),
					PercentileValue(latency_snapshot, 0.50),
					PercentileValue(latency_snapshot, 0.95),
					PercentileValue(latency_snapshot, 0.99),
					PercentileValue(latency_snapshot, 1.00),
				};

				const auto elapsed_seconds = std::max(0.001, result.m_DrainElapsed.count() / 1000.0);
				tickets_per_second = static_cast<double>(result.m_TicketCount) / elapsed_seconds;
				matches_per_second = static_cast<double>(result.m_MatchCount) / elapsed_seconds;

				RecordProperty("request_count", std::to_string(result.m_TicketCount));
				RecordProperty("player_count", std::to_string(submission.m_PlayerCount));
				RecordProperty("match_count", std::to_string(result.m_MatchCount));
				RecordProperty("seed", std::to_string(seed));
				RecordProperty("submit_elapsed_ms", std::to_string(result.m_SubmitElapsed.count()));
				RecordProperty("drain_elapsed_ms", std::to_string(result.m_DrainElapsed.count()));
				RecordProperty("latency_min_ms", std::to_string(result.m_LatencyMinMs));
				RecordProperty("latency_p50_ms", std::to_string(result.m_LatencyP50Ms));
				RecordProperty("latency_p95_ms", std::to_string(result.m_LatencyP95Ms));
				RecordProperty("latency_p99_ms", std::to_string(result.m_LatencyP99Ms));
				RecordProperty("latency_max_ms", std::to_string(result.m_LatencyMaxMs));
				RecordProperty("tickets_per_second", std::format("{:.2f}", tickets_per_second));
				RecordProperty("matches_per_second", std::format("{:.2f}", matches_per_second));
			}

			std::cout << std::format(
				"[PERF] {} tickets:{} matches:{} seed:{} submit_ms:{} drain_ms:{} latency_ms[min/p50/p95/p99/max]={}/{}/{}/{}/{} tickets_per_sec:{:.2f} matches_per_sec:{:.2f}",
				scenario_name,
				result.m_TicketCount,
				result.m_MatchCount,
				seed,
				result.m_SubmitElapsed.count(),
				result.m_DrainElapsed.count(),
				result.m_LatencyMinMs,
				result.m_LatencyP50Ms,
				result.m_LatencyP95Ms,
				result.m_LatencyP99Ms,
				result.m_LatencyMaxMs,
				tickets_per_second,
				matches_per_second)
				<< std::endl;
		}
	};
}

// 기본 솔로 1+1+1+1 조합 검증
TEST_F(MatchmakingScenarioTest, SoloOnePlusOnePlusOnePlusOne)
{
	for (int i = 0; i < 4; ++i)
		GetSoloMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1));

	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("SoloOnePlusOnePlusOnePlusOne");
}

// 기본 파티 2+2 조합 검증
TEST_F(MatchmakingScenarioTest, PartyTwoPlusTwo)
{
	GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 2, TestHelper::IssuePartyKey()));
	GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 2, TestHelper::IssuePartyKey()));

	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("PartyTwoPlusTwo");
}

// 기본 파티 3+1 조합 검증
TEST_F(MatchmakingScenarioTest, PartyThreePlusOne)
{
	GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 3, TestHelper::IssuePartyKey()));
	GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1, TestHelper::IssuePartyKey()));

	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("PartyThreePlusOne");
}

// 솔로/파티 혼합 요청 소진 검증
TEST_F(MatchmakingScenarioTest, MixedSoloAndParty)
{
	for (int i = 0; i < 4; ++i)
		GetSoloMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1));

	GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 2, TestHelper::IssuePartyKey()));
	GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 2, TestHelper::IssuePartyKey()));

	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("MixedSoloAndParty");
}

// 1인 파티 4건 조합 검증
TEST_F(MatchmakingScenarioTest, PartyOnePlusOnePlusOnePlusOne)
{
	for (int i = 0; i < 4; ++i)
		GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1, TestHelper::IssuePartyKey()));

	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("PartyOnePlusOnePlusOnePlusOne");
}

// 4인 풀파티 즉시 매칭 검증
TEST_F(MatchmakingScenarioTest, PartyFullGroupMatchesImmediately)
{
	GetPartyMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 4, TestHelper::IssuePartyKey()));

	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("PartyFullGroupMatchesImmediately");
}

TEST_F(MatchmakingScenarioTest, StartMatchmakingAssignsUniqueTicketIds)
{
	std::vector<MatchTicketRequest> requests;
	for (int i = 0; i < 4; ++i)
		requests.emplace_back(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1));

	for (const auto& request : requests)
		EXPECT_EQ(-1, request.m_MatchTicketId);

	std::set<MATCH_TICKET_ID> ticket_ids;
	for (auto& request : requests)
	{
		auto match_ticket_ptr = GetSoloMatchmakingManager()->StartMatchmaking(std::move(request));
		ASSERT_NE(nullptr, match_ticket_ptr);
		EXPECT_NE(-1, match_ticket_ptr->GetMatchTicketID());
		EXPECT_EQ(match_ticket_ptr->GetMatchTicketID(), match_ticket_ptr->GetMatchTicketRequest().m_MatchTicketId);
		ticket_ids.insert(match_ticket_ptr->GetMatchTicketID());
	}

	EXPECT_EQ(requests.size(), ticket_ids.size());
	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("StartMatchmakingAssignsUniqueTicketIds");
}

// 파티 닷지 패널티 재큐 차단 검증
TEST_F(MatchmakingScenarioTest, PartyDodgePenaltyBlocksRequeue)
{
	auto party_ticket_a = TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 2, TestHelper::IssuePartyKey());
	auto party_ticket_b = TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 2, TestHelper::IssuePartyKey());
	const PLAYER_KEY dodging_player = party_ticket_a.m_RequestPlayerKey;

	GetPartyMatchmakingManager()->StartMatchmaking(std::move(party_ticket_a));
	GetPartyMatchmakingManager()->StartMatchmaking(std::move(party_ticket_b));

	ASSERT_TRUE(WaitUntil(std::chrono::seconds(5), []()
	{
		return GetMatchSessionManager()->GetMatchSessionSize() > 0;
	}));

	GetMatchSessionManager()->AcceptMatchSession(PlayerMatchAcceptInfo { dodging_player, false, false });

	ASSERT_TRUE(WaitUntil(std::chrono::seconds(5), []()
	{
		return GetMatchSessionManager()->GetMatchSessionSize() == 0;
	}));

	EXPECT_TRUE(GetMatchPenaltyManager()->HasDodgePenalty(dodging_player));

	auto retry_ticket = TestHelper::MakeMatchTicketWithPlayers(MatchmakingConfig::kDefaultMatchZoneId, { dodging_player }, TestHelper::IssuePartyKey());
	EXPECT_EQ(nullptr, GetPartyMatchmakingManager()->StartMatchmaking(std::move(retry_ticket)));
}

// 월드 그룹 라우팅 검증
TEST_F(MatchmakingScenarioTest, NonDefaultWorldGroupRoutesSession)
{
	std::mutex observer_mutex;
	std::vector<int> observed_worlds;

	GetMatchSessionManager()->SetMatchCreatedObserver([&](const CMatchSessionPtr& match_session_ptr, long long)
	{
		if (match_session_ptr == nullptr)
			return;

		std::lock_guard lock(observer_mutex);
		observed_worlds.emplace_back(match_session_ptr->m_MatchWorldNo);
	});

	for (int i = 0; i < 4; ++i)
		GetSoloMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1, -1, 2));

	EXPECT_TRUE(TestHelper::WaitForQueuesToDrain(std::chrono::seconds(10)));
	ExpectIdleQueues("NonDefaultWorldGroupRoutesSession");

	std::lock_guard lock(observer_mutex);
	ASSERT_FALSE(observed_worlds.empty());
	for (const auto world_no : observed_worlds)
		EXPECT_EQ(2, world_no);
}

// zone 인스턴스 즉시 만료 방지 검증
TEST_F(MatchmakingScenarioTest, IntegrationZoneInstanceDoesNotExpireImmediately)
{
	auto zone_instance_info = std::make_shared<IntegrationZoneInstanceInfo>();
	zone_instance_info->m_WorldNo = 1;
	zone_instance_info->m_ZoneGUID = 101;
	zone_instance_info->m_MatchID = 202;
	zone_instance_info->m_ZoneID = MatchmakingConfig::kDefaultMatchZoneId;
	zone_instance_info->m_Players = { TestHelper::IssuePlayerKey() };

	const IntegrationZoneInstanceKey zone_instance_key(zone_instance_info->m_WorldNo, zone_instance_info->m_ZoneGUID);
	auto zone_instance_ptr = GetIntegrationZoneInstanceManager()->Insert(zone_instance_info);
	ASSERT_NE(nullptr, zone_instance_ptr);

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	EXPECT_NE(nullptr, GetIntegrationZoneInstanceManager()->Find(zone_instance_key));
}

// 통합 서버 종료 정리 범위 검증
TEST_F(MatchmakingScenarioTest, IntegrationServerClosedRemovesOnlyMatchingSessionInstance)
{
	auto zone_instance_info_a = std::make_shared<IntegrationZoneInstanceInfo>();
	zone_instance_info_a->m_WorldNo = 3;
	zone_instance_info_a->m_WorldSessionKey = 9001;
	zone_instance_info_a->m_ZoneGUID = 303;
	zone_instance_info_a->m_MatchID = 404;
	zone_instance_info_a->m_ZoneID = MatchmakingConfig::kDefaultMatchZoneId;
	zone_instance_info_a->m_Players = { TestHelper::IssuePlayerKey() };

	auto zone_instance_info_b = std::make_shared<IntegrationZoneInstanceInfo>();
	zone_instance_info_b->m_WorldNo = 3;
	zone_instance_info_b->m_WorldSessionKey = 9002;
	zone_instance_info_b->m_ZoneGUID = 304;
	zone_instance_info_b->m_MatchID = 405;
	zone_instance_info_b->m_ZoneID = MatchmakingConfig::kDefaultMatchZoneId;
	zone_instance_info_b->m_Players = { TestHelper::IssuePlayerKey() };

	const IntegrationZoneInstanceKey zone_instance_key_a(zone_instance_info_a->m_WorldNo, zone_instance_info_a->m_ZoneGUID);
	const IntegrationZoneInstanceKey zone_instance_key_b(zone_instance_info_b->m_WorldNo, zone_instance_info_b->m_ZoneGUID);
	ASSERT_NE(nullptr, GetIntegrationZoneInstanceManager()->Insert(zone_instance_info_a));
	ASSERT_NE(nullptr, GetIntegrationZoneInstanceManager()->Insert(zone_instance_info_b));

	GetIntegrationZoneInstanceManager()->Process_IntegrationServerClosed(zone_instance_info_a->m_WorldSessionKey, zone_instance_info_a->m_WorldNo);

	EXPECT_TRUE(WaitUntil(std::chrono::seconds(2), [&]()
	{
		return GetIntegrationZoneInstanceManager()->Find(zone_instance_key_a) == nullptr;
	}));
	EXPECT_NE(nullptr, GetIntegrationZoneInstanceManager()->Find(zone_instance_key_b));
}

// 중복 수락 응답 무시 검증
TEST_F(MatchmakingScenarioTest, DuplicateAcceptResponseIgnored)
{
	const PLAYER_KEY player_key = TestHelper::IssuePlayerKey();

	auto match_ticket_request = TestHelper::MakeMatchTicketWithPlayers(MatchmakingConfig::kDefaultMatchZoneId, { player_key });
	auto match_ticket_ptr = std::make_shared<CMatchTicket>(0, match_ticket_request);
	std::set<CMatchTicketPtr> match_ticket_list { match_ticket_ptr };
	CMatchSession match_session(
		MatchmakingConfig::kDefaultMatchZoneId,
		0,
		match_ticket_ptr->GetMatchTicketID(),
		std::move(match_ticket_list));

	EXPECT_TRUE(match_session.AcceptPlayerSession(PlayerMatchAcceptInfo { player_key, true, false }));
	EXPECT_TRUE(match_session.IsAllAcceptPlayerSession());
	EXPECT_FALSE(match_session.AcceptPlayerSession(PlayerMatchAcceptInfo { player_key, false, false }));
	EXPECT_TRUE(match_session.IsAllAcceptPlayerSession());
}

// 로그인 시 파티 큐 정리 검증
TEST_F(MatchmakingScenarioTest, PlayerLoginClearsPartyQueue)
{
	auto party_ticket = TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1, TestHelper::IssuePartyKey());
	const PLAYER_KEY player_key = party_ticket.m_RequestPlayerKey;

	ASSERT_NE(nullptr, GetPartyMatchmakingManager()->StartMatchmaking(std::move(party_ticket)));
	ASSERT_TRUE(WaitUntil(std::chrono::seconds(2), []()
	{
		return GetPartyMatchmakingManager()->GetMatchmakingRequestSize() == 1;
	}));

	GetIntegrationZoneInstanceManager()->Process_PlayerLogin(player_key);

	EXPECT_TRUE(WaitUntil(std::chrono::seconds(2), []()
	{
		return GetPartyMatchmakingManager()->GetMatchmakingRequestSize() == 0;
	}));
}

// 통합 존 이탈 정리 및 패널티 검증
TEST_F(MatchmakingScenarioTest, PlayerLeaveToIntegrationServerCleansUpMembership)
{
	const PLAYER_KEY player_key = TestHelper::IssuePlayerKey();

	auto zone_instance_info = std::make_shared<IntegrationZoneInstanceInfo>();
	zone_instance_info->m_WorldNo = 4;
	zone_instance_info->m_WorldSessionKey = 9010;
	zone_instance_info->m_ZoneGUID = 401;
	zone_instance_info->m_MatchID = 402;
	zone_instance_info->m_ZoneID = MatchmakingConfig::kDefaultMatchZoneId;
	zone_instance_info->m_Players = { player_key };

	ASSERT_NE(nullptr, GetIntegrationZoneInstanceManager()->Insert(zone_instance_info));
	ASSERT_NE(nullptr, GetIntegrationZoneInstanceManager()->FindByPlayerKey(player_key));

	EXPECT_TRUE(GetIntegrationZoneInstanceManager()->Process_PlayerLeave_ToIntegrationServer(player_key, true));
	EXPECT_TRUE(WaitUntil(std::chrono::seconds(2), [&]()
	{
		return GetIntegrationZoneInstanceManager()->FindByPlayerKey(player_key) == nullptr;
	}));
	EXPECT_TRUE(GetMatchPenaltyManager()->HasDodgePenalty(player_key));
}

// cleanup 경로 무패널티 검증
TEST_F(MatchmakingScenarioTest, CleanupPrevMatchmakingDoesNotApplyDodgePenalty)
{
	auto solo_ticket = TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1);
	const PLAYER_KEY player_key = solo_ticket.m_RequestPlayerKey;

	ASSERT_NE(nullptr, GetSoloMatchmakingManager()->StartMatchmaking(std::move(solo_ticket)));
	ASSERT_TRUE(WaitUntil(std::chrono::seconds(2), []()
	{
		return GetSoloMatchmakingManager()->GetMatchmakingRequestSize() == 1;
	}));

	EXPECT_TRUE(GetSoloMatchmakingManager()->CleanupPrevMatchmaking(player_key));
	EXPECT_TRUE(WaitUntil(std::chrono::seconds(2), []()
	{
		return GetSoloMatchmakingManager()->GetMatchmakingRequestSize() == 0;
	}));
	EXPECT_FALSE(GetMatchPenaltyManager()->HasDodgePenalty(player_key));
}

// 세션 완료 실패 시 누수 방지 검증
TEST_F(MatchmakingScenarioTest, CompleteSessionInsertFailureDoesNotLeakSession)
{
	struct ObservedMatchSession
	{
		MATCH_ID m_MatchId = -1;
		int m_WorldNo = -1;
		std::set<PLAYER_KEY> m_Players;
	};

	std::mutex observer_mutex;
	std::optional<ObservedMatchSession> observed_match;

	GetMatchSessionManager()->SetMatchCreatedObserver([&](const CMatchSessionPtr& match_session_ptr, long long)
	{
		if (match_session_ptr == nullptr)
			return;

		std::lock_guard lock(observer_mutex);
		observed_match = ObservedMatchSession
		{
			match_session_ptr->m_MatchID,
			match_session_ptr->m_MatchWorldNo,
			match_session_ptr->GetPlayers()
		};
	});

	for (int i = 0; i < 4; ++i)
		GetSoloMatchmakingManager()->StartMatchmaking(TestHelper::MakeMatchTicket(MatchmakingConfig::kDefaultMatchZoneId, 1));

	ASSERT_TRUE(WaitUntil(std::chrono::seconds(5), [&]()
	{
		std::lock_guard lock(observer_mutex);
		return observed_match.has_value();
	}));

	ObservedMatchSession match_info;
	{
		std::lock_guard lock(observer_mutex);
		match_info = *observed_match;
	}

	for (const auto player_key : match_info.m_Players)
	{
		GetMatchSessionManager()->AcceptMatchSession(PlayerMatchAcceptInfo { player_key, true, false });
	}

	auto conflicting_zone = std::make_shared<IntegrationZoneInstanceInfo>();
	conflicting_zone->m_WorldNo = match_info.m_WorldNo;
	conflicting_zone->m_WorldSessionKey = match_info.m_MatchId;
	conflicting_zone->m_ZoneGUID = match_info.m_MatchId;
	conflicting_zone->m_MatchID = match_info.m_MatchId + 1000000;
	conflicting_zone->m_ZoneID = MatchmakingConfig::kDefaultMatchZoneId;
	conflicting_zone->m_Players = { TestHelper::IssuePlayerKey() };

	ASSERT_NE(nullptr, GetIntegrationZoneInstanceManager()->Insert(conflicting_zone));
	EXPECT_TRUE(WaitUntil(std::chrono::seconds(6), []()
	{
		return GetMatchSessionManager()->GetMatchSessionSize() == 0;
	}));
}

// 대량 솔로 처리량 및 지연 통계 측정
TEST_F(MatchmakingScenarioTest, SoloHundredThousandPerformance)
{
	static constexpr size_t kTicketCount = 100000;
	static constexpr auto kTimeout = std::chrono::minutes(5);

	std::mutex latency_mutex;
	std::vector<long long> latency_samples;
	GetMatchSessionManager()->SetMatchCreatedObserver([&](const CMatchSessionPtr& match_session_ptr, long long created_time_msec)
	{
		if (match_session_ptr == nullptr)
			return;

		std::lock_guard lock(latency_mutex);
		for (const auto& match_ticket_ptr : match_session_ptr->m_MatchTicketList)
		{
			if (match_ticket_ptr == nullptr)
				continue;

				latency_samples.emplace_back(std::max(0LL, created_time_msec - match_ticket_ptr->GetStartTimeMsec()));
		}
	});

	const auto started_at = std::chrono::steady_clock::now();
	PerformanceResult result;
	double tickets_per_second = 0.0;
	double matches_per_second = 0.0;
	{
		ScopedStreamSilencer silencer;
		SubmitSoloTraffic(kTicketCount);
		const auto submit_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started_at);
		const auto settled = TestHelper::WaitForQueuesToDrain(kTimeout);
		const auto drain_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started_at);

		ASSERT_TRUE(settled) << "Performance scenario did not settle within timeout";
		ExpectIdleQueues("SoloHundredThousandPerformance");

		std::vector<long long> latency_snapshot;
		{
			std::lock_guard lock(latency_mutex);
			latency_snapshot = latency_samples;
		}

		ASSERT_EQ(latency_snapshot.size(), kTicketCount);

		result =
		{
			kTicketCount,
			kTicketCount / 4,
			submit_elapsed,
			drain_elapsed,
			PercentileValue(latency_snapshot, 0.00),
			PercentileValue(latency_snapshot, 0.50),
			PercentileValue(latency_snapshot, 0.95),
			PercentileValue(latency_snapshot, 0.99),
			PercentileValue(latency_snapshot, 1.00),
		};

		const auto elapsed_seconds = std::max(0.001, result.m_DrainElapsed.count() / 1000.0);
		tickets_per_second = static_cast<double>(result.m_TicketCount) / elapsed_seconds;
		matches_per_second = static_cast<double>(result.m_MatchCount) / elapsed_seconds;

		RecordProperty("ticket_count", std::to_string(result.m_TicketCount));
		RecordProperty("match_count", std::to_string(result.m_MatchCount));
		RecordProperty("submit_elapsed_ms", std::to_string(result.m_SubmitElapsed.count()));
		RecordProperty("drain_elapsed_ms", std::to_string(result.m_DrainElapsed.count()));
		RecordProperty("latency_min_ms", std::to_string(result.m_LatencyMinMs));
		RecordProperty("latency_p50_ms", std::to_string(result.m_LatencyP50Ms));
		RecordProperty("latency_p95_ms", std::to_string(result.m_LatencyP95Ms));
		RecordProperty("latency_p99_ms", std::to_string(result.m_LatencyP99Ms));
		RecordProperty("latency_max_ms", std::to_string(result.m_LatencyMaxMs));
		RecordProperty("tickets_per_second", std::format("{:.2f}", tickets_per_second));
		RecordProperty("matches_per_second", std::format("{:.2f}", matches_per_second));
	}

	std::cout << std::format(
		"[PERF] tickets:{} matches:{} submit_ms:{} drain_ms:{} latency_ms[min/p50/p95/p99/max]={}/{}/{}/{}/{} tickets_per_sec:{:.2f} matches_per_sec:{:.2f}",
		result.m_TicketCount,
		result.m_MatchCount,
		result.m_SubmitElapsed.count(),
		result.m_DrainElapsed.count(),
		result.m_LatencyMinMs,
		result.m_LatencyP50Ms,
		result.m_LatencyP95Ms,
		result.m_LatencyP99Ms,
		result.m_LatencyMaxMs,
		tickets_per_second,
		matches_per_second)
		<< std::endl;
}

// 랜덤 파티 처리량 및 지연 통계 측정
TEST_F(MatchmakingScenarioTest, PartyRandomPerformance)
{
	static constexpr std::uint32_t kSeed = 20260323;
	RunRandomPartyPerformanceScenario(1000, kSeed, "PartyRandomPerformance");
}

// 랜덤 파티 요청 10만 건 스트레스 테스트
TEST_F(MatchmakingScenarioTest, PartyRandomHundredThousandPerformance)
{
	static constexpr std::uint32_t kSeed = 20260323;
	RunRandomPartyPerformanceScenario(100000, kSeed, "PartyRandomHundredThousandPerformance");
}
