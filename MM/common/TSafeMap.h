#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

// 락 기반 포인터 맵 템플릿
template <typename MapType, typename KeyType, typename ValueType, typename ValuePtr, typename MutexType>
class TSafeMapBase
{
public:
	using key_type = KeyType;
	using value_type = ValueType;
	using pointer_type = ValuePtr;

	template <typename Func>
	auto Do(Func&& func) -> decltype(func())
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		return func();
	}

	template <typename Func>
	auto Do(Func&& func) const -> decltype(func())
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		return func();
	}

	bool Insert(const KeyType& key, ValuePtr value, bool overwrite = false,
		std::function<void(bool, ValuePtr)> callback = {})
	{
		bool inserted = false;
		ValuePtr stored_value = nullptr;
		{
			const std::lock_guard<MutexType> lock(m_Mutex);
			auto [it, did_insert] = m_Map.emplace(key, value);
			if (did_insert)
			{
				inserted = true;
				stored_value = value;
			}
			else if (overwrite)
			{
				it->second = value;
				inserted = true;
				stored_value = value;
			}
			else
			{
				stored_value = it->second;
			}
		}

		if (callback)
			callback(inserted, stored_value);

		return inserted;
	}

	ValuePtr Erase(const KeyType& key, std::function<void(ValuePtr)> callback = {})
	{
		ValuePtr erased_value = nullptr;
		{
			const std::lock_guard<MutexType> lock(m_Mutex);
			const auto it = m_Map.find(key);
			if (it == m_Map.end())
				return nullptr;

			erased_value = it->second;
			m_Map.erase(it);
		}

		if (callback)
			callback(erased_value);

		return erased_value;
	}

	ValuePtr Find(const KeyType& key) const
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		const auto it = m_Map.find(key);
		if (it == m_Map.end())
			return nullptr;

		return it->second;
	}

	int GetCount() const
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		return static_cast<int>(m_Map.size());
	}

	bool Empty() const
	{
		return GetCount() == 0;
	}

	void Clear()
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		m_Map.clear();
	}

	void DoAll(std::function<void(const KeyType&, ValuePtr)> callback) const
	{
		std::vector<std::pair<KeyType, ValuePtr>> snapshot;
		{
			const std::lock_guard<MutexType> lock(m_Mutex);
			snapshot.reserve(m_Map.size());
			for (const auto& [key, value] : m_Map)
				snapshot.emplace_back(key, value);
		}

		for (const auto& [key, value] : snapshot)
			callback(key, value);
	}

	void DoAll(std::function<void(ValuePtr)> callback) const
	{
		std::vector<ValuePtr> snapshot;
		{
			const std::lock_guard<MutexType> lock(m_Mutex);
			snapshot.reserve(m_Map.size());
			for (const auto& [key, value] : m_Map)
				snapshot.emplace_back(value);
		}

		for (const auto& value : snapshot)
			callback(value);
	}

	void DoAll_Parallel(std::function<void(const KeyType&, ValuePtr)> callback) const
	{
		DoAll(std::move(callback));
	}

	void DoAll_Parallel(std::function<void(ValuePtr)> callback) const
	{
		DoAll(std::move(callback));
	}

private:
	mutable MutexType m_Mutex;
	MapType m_Map;
};

// ordered map 기반 스레드 안전 포인터 맵
template <typename KeyType, typename ValueType, template<typename> typename StorePtr = std::shared_ptr, typename MutexType = std::recursive_mutex>
class TSafeMap : public TSafeMapBase<std::map<KeyType, StorePtr<ValueType>>, KeyType, ValueType, StorePtr<ValueType>, MutexType>
{
};

// unordered map 기반 스레드 안전 포인터 맵
template <typename KeyType, typename ValueType, template<typename> typename StorePtr = std::shared_ptr, typename MutexType = std::recursive_mutex>
class TSafeUnorderedMap : public TSafeMapBase<std::unordered_map<KeyType, StorePtr<ValueType>>, KeyType, ValueType, StorePtr<ValueType>, MutexType>
{
};

// 값 타입용 스레드 안전 맵
template <typename KeyType, typename ValueType, typename MutexType = std::recursive_mutex>
class TSafeMap_Basic
{
public:
	bool Insert(const KeyType& key, const ValueType& value, bool overwrite = false)
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		auto [it, inserted] = m_Map.emplace(key, value);
		if (inserted)
			return true;

		if (overwrite == false)
			return false;

		it->second = value;
		return true;
	}

	void Erase(const KeyType& key)
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		m_Map.erase(key);
	}

	std::optional<ValueType> Find(const KeyType& key) const
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		const auto it = m_Map.find(key);
		if (it == m_Map.end())
			return std::nullopt;

		return it->second;
	}

	int GetCount() const
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		return static_cast<int>(m_Map.size());
	}

	bool Empty() const
	{
		return GetCount() == 0;
	}

	void Clear()
	{
		const std::lock_guard<MutexType> lock(m_Mutex);
		m_Map.clear();
	}

private:
	mutable MutexType m_Mutex;
	std::map<KeyType, ValueType> m_Map;
};
