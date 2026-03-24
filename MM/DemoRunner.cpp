#include "stdafx.h"

#include "DemoRunner.h"

#include "matching/MatchmakingConfig.h"
#include "matching/PartyMatchmakingManager.h"
#include "matching/SoloMatchmakingManager.h"
#include "tests/MatchTestHelper.h"

int RunDemo()
{
	CMatchTestHelper::InitializeManagers();
	Defer shutdown([]() { CMatchTestHelper::TerminateManagers(); });

	std::mutex tracked_player_keys_mutex;
	std::unordered_set<PLAYER_KEY> tracked_player_keys;

	auto print_queue_status = []()
	{
		const auto snapshot = CMatchTestHelper::CaptureQueueSnapshot();
		std::cout
			<< std::format(
				"[status] party:{} solo:{} session:{}",
				snapshot.m_PartyQueue,
				snapshot.m_SoloQueue,
				snapshot.m_SessionQueue)
			<< std::endl;
	};

	auto track_players = [&](const MatchTicketRequest& match_ticket_request)
	{
		std::lock_guard lock(tracked_player_keys_mutex);
		for (const auto& player : match_ticket_request.m_Players)
			tracked_player_keys.insert(player.m_PlayerKey);
	};

	auto cancel_outstanding_tickets = [&]()
	{
		std::vector<PLAYER_KEY> player_keys;
		{
			std::lock_guard lock(tracked_player_keys_mutex);
			player_keys.assign(tracked_player_keys.begin(), tracked_player_keys.end());
		}

		for (const auto player_key : player_keys)
		{
			GetPartyMatchmakingManager()->StopMatchmaking(player_key);
			GetSoloMatchmakingManager()->StopMatchmaking(player_key);
		}
	};

	auto parse_player_count = [](std::string input) -> std::optional<int>
	{
		input.erase(std::remove_if(input.begin(), input.end(), [](unsigned char ch)
		{
			return std::isspace(ch) != 0;
		}), input.end());

		if (input.empty())
			return std::nullopt;

		try
		{
			size_t parsed_length = 0;
			const int player_count = std::stoi(input, &parsed_length);
			if (parsed_length != input.size())
				return std::nullopt;

			return player_count;
		}
		catch (...)
		{
			return std::nullopt;
		}
	};

	auto is_exit_command = [](std::string input)
	{
		std::transform(input.begin(), input.end(), input.begin(), [](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});

		input.erase(std::remove_if(input.begin(), input.end(), [](unsigned char ch)
		{
			return std::isspace(ch) != 0;
		}), input.end());

		return input == "q" || input == "quit" || input == "exit";
	};

	auto enqueue_interactive_request = [&](int player_count)
	{
		auto match_ticket_info = CMatchTestHelper::MakeMatchTicket(
			MatchmakingConfig::kDefaultMatchZoneId,
			player_count,
			player_count == 1 ? -1 : CMatchTestHelper::IssuePartyKey());
		track_players(match_ticket_info);

		if (player_count == 1)
		{
			GetSoloMatchmakingManager()->StartMatchmaking(std::move(match_ticket_info));
			std::cout << "[enqueue] solo ticket queued" << std::endl;
			return;
		}

		GetPartyMatchmakingManager()->StartMatchmaking(std::move(match_ticket_info));
		std::cout << std::format("[enqueue] party ticket queued ({})", player_count) << std::endl;
	};

	std::jthread status_thread([&](const std::stop_token& stop_token)
	{
		while (stop_token.stop_requested() == false)
		{
			const auto snapshot = CMatchTestHelper::CaptureQueueSnapshot();
			if (snapshot.IsIdle() == false)
				print_queue_status();

			std::this_thread::sleep_for(std::chrono::seconds(3));
		}
	});

	std::cout << "Matchmaking MVP interactive demo start" << std::endl;
	std::cout << "Enter player count to queue matchmaking. Use 1 for solo, 2-4 for party, q to quit." << std::endl;

	while (true)
	{
		std::cout << "player_count> " << std::flush;

		std::string input;
		if (!std::getline(std::cin, input))
			break;

		if (is_exit_command(input))
			break;

		const auto player_count = parse_player_count(input);
		if (player_count.has_value() == false || player_count.value() < 1 || player_count.value() > 4)
		{
			std::cout << "Please enter a number from 1 to 4, or q to quit." << std::endl;
			continue;
		}

		enqueue_interactive_request(player_count.value());
	}

	std::cout << "Shutdown requested. Cancelling pending matchmaking and waiting for queues to drain..." << std::endl;
	cancel_outstanding_tickets();
	const bool settled = CMatchTestHelper::WaitForQueuesToDrain(std::chrono::seconds(30));

	status_thread.request_stop();
	status_thread.join();

	print_queue_status();
	if (settled)
		std::cout << "Queues drained successfully." << std::endl;
	else
		std::cout << "Timed out while waiting for queues to drain." << std::endl;

	std::cout << "Demo finished. Production-only integration details are intentionally omitted from this package." << std::endl;
	return 0;
}
