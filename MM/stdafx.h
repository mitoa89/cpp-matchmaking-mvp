#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <compare>
#include <condition_variable>
#include <ctime>
#include <format>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TSafeMap.h"
#include "SStringUtil.h"

#ifndef ACCOREMODULE_API
#define ACCOREMODULE_API
#endif

#ifndef UENUM
#define UENUM(...)
#endif

#ifndef TEXT
#define TEXT(x) L##x
#endif

namespace cbstd_svr
{
	template <typename store_value>
	class shared_lock_object
	{
	public:
		shared_lock_object()
			: m_store_value(store_value())
		{
		}

		shared_lock_object(store_value&& value)
			: m_store_value(std::move(value))
		{
		}

		shared_lock_object(const store_value& value)
			: m_store_value(value)
		{
		}

		virtual ~shared_lock_object()
		{
			std::unique_lock<std::shared_mutex> lock(m_obj_lock);
		}

		shared_lock_object(const shared_lock_object&) = delete;
		shared_lock_object& operator=(const shared_lock_object&) = delete;

	public:
		void operator=(store_value&& value)
		{
			std::unique_lock<std::shared_mutex> lock(m_obj_lock);
			m_store_value = std::move(value);
		}

		operator store_value() const
		{
			std::shared_lock<std::shared_mutex> lock(m_obj_lock);
			return m_store_value;
		}

		template <typename Result>
		Result _do_write_lock_job(std::function<Result(store_value&)> func)
		{
			std::unique_lock<std::shared_mutex> lock(m_obj_lock);
			return func(m_store_value);
		}

		template <typename Result>
		Result _do_read_lock_job(std::function<Result(const store_value&)> func) const
		{
			std::shared_lock<std::shared_mutex> lock(m_obj_lock);
			return func(m_store_value);
		}

		void _do_write_lock_job(std::function<void(store_value&)> job_func)
		{
			std::unique_lock<std::shared_mutex> lock(m_obj_lock);
			job_func(m_store_value);
		}

		void _do_read_lock_job(std::function<void(const store_value&)> job_func) const
		{
			std::shared_lock<std::shared_mutex> lock(m_obj_lock);
			job_func(m_store_value);
		}

	private:
		mutable std::shared_mutex m_obj_lock;
		store_value m_store_value;
	};
}

struct Defer
{
	std::function<void()> func;

	explicit Defer(std::function<void()> f)
		: func(std::move(f))
	{
	}

	~Defer()
	{
		func();
	}
};
