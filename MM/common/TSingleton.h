#pragma once

#include <atomic>
#include <memory>

// shared_ptr 기반 싱글톤 템플릿
template <typename TYPE>
class TSingleton
{
public:
	TSingleton() = default;
	virtual ~TSingleton()
	{
		s_Instance.reset();
	}

	static std::shared_ptr<TYPE> GetInstance()
	{
		TryInit();
		return s_Instance;
	}

private:
	static void TryInit()
	{
		if (s_InitComplete.load())
			return;

		if (s_InitStart.exchange(true) == false)
		{
			s_Instance = std::make_shared<TYPE>();
			s_InitComplete.store(true);
		}
		else
		{
			while (s_InitComplete.load() == false)
			{
			}
		}
	}

private:
	TSingleton(const TSingleton<TYPE>&);
	const TSingleton<TYPE>& operator=(const TSingleton<TYPE>&);

private:
	inline static std::shared_ptr<TYPE> s_Instance = nullptr;
	inline static std::atomic<bool> s_InitStart = false;
	inline static std::atomic<bool> s_InitComplete = false;
};
