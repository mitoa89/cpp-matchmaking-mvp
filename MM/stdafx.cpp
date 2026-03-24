#include "stdafx.h"

long long timediff_with_gmt()
{
	static const auto kTimeDiffWithGmt = []()
	{
		time_t curr_time = time(nullptr);
		std::tm local_tm {};
		std::tm gm_tm {};

#if defined(_MSC_VER)
		localtime_s(&local_tm, &curr_time);
		gmtime_s(&gm_tm, &curr_time);
#else
		localtime_r(&curr_time, &local_tm);
		gmtime_r(&curr_time, &gm_tm);
#endif

		return static_cast<long long>(std::mktime(&local_tm) - std::mktime(&gm_tm));
	}();

	return kTimeDiffWithGmt;
}

unsigned long long time_msec(unsigned long long* dst_time)
{
	const auto curr_time = static_cast<unsigned long long>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());

	if (dst_time != nullptr)
		*dst_time = curr_time;

	return curr_time;
}

long long time_sec(long long* dst_time)
{
	const auto curr_time = static_cast<long long>(
		std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());

	if (dst_time != nullptr)
		*dst_time = curr_time;

	return curr_time;
}
