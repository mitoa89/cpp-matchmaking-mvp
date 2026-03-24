#pragma once
#include "WorkerJobRequest.h"

// 워커 인터페이스
class IWorker : public std::enable_shared_from_this<IWorker>
{
protected:
	mutable std::shared_mutex m_WorkerMutex;

public:
	const int m_WorkerID = -1;

	template <typename... Args>
	explicit IWorker(const int worker_index, Args&&... args)
		: m_WorkerID(worker_index)
	{}
	virtual ~IWorker()
	{}

protected:
	virtual long long GetJobExecuteTimeMSec() const { return time(nullptr) * 1000 + std::chrono::milliseconds(600000).count(); }

public:
	virtual bool DoWorkerAsyncJob() = 0;
	virtual bool IsWorkerJobAvailable() const = 0;
	virtual bool EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr) = 0;

	std::chrono::system_clock::time_point GetJobExecuteTimePoint() const
	{
		auto job_execution_time_msec = GetJobExecuteTimeMSec();
		return std::chrono::system_clock::from_time_t(job_execution_time_msec / 1000) + std::chrono::milliseconds(job_execution_time_msec % 1000);
	}
};

// 워커 그룹 템플릿
template <typename WORKER_GROUP_TYPE, typename WORKER, typename WORKER_PTR = std::shared_ptr<WORKER>,
	typename = std::enable_if_t<std::is_base_of<IWorker, WORKER>::value>>
class IWorkerGroup : public std::enable_shared_from_this<IWorkerGroup<WORKER_GROUP_TYPE, WORKER>>
{
public:
	friend class IWorker;

	IWorkerGroup() = delete;
	template <typename... Args>
	explicit IWorkerGroup(const WORKER_GROUP_TYPE& worker_group_type, int worker_count, Args&&... args)
		: m_WorkerGroupKey(worker_group_type)
		, m_WorkerCount(worker_count)
		, m_Workers(InitWorkers(worker_count, std::forward<Args>(args)...))
	{}
	virtual ~IWorkerGroup()
	{
		Stop();
	}

	// 워커 스레드 시작
	// 파생 생성자 초기화 전 호출 금지
	bool Start()
	{
		this->m_Running = true;

		for (int i = 0; i < m_WorkerCount; ++i)
			m_WorkerThreadList.emplace_back(&IWorkerGroup::WorkerThreadFunc, this, i);

		m_WorkerGroupThread = std::thread(&IWorkerGroup::WorkerGroupThreadFunc, this);
		return true;
	}

	void Stop()
	{
		this->m_Running = false;

		m_Condition.notify_all();

		for (auto&& worker_thread : m_WorkerThreadList)
		{
			if (worker_thread.joinable() == true)
				worker_thread.join();
		}

		m_WorkerThreadList.clear();

		if (m_WorkerGroupThread.joinable() == true)
			m_WorkerGroupThread.join();

		return;
	}

	virtual bool EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr)
	{
		if (worker_job_request_ptr == nullptr)
			return false;

		worker_job_request_ptr->SetBaseWorkerIndex(GetWorkerIndex(worker_job_request_ptr));

		auto worker = GetWorker(worker_job_request_ptr->GetBaseWorkerIndex());
		if (worker == nullptr)
			return false;

		{
			std::unique_lock<std::mutex> lock(m_WorkerGroupMutex);
			if (worker->EnqueueJobRequest(worker_job_request_ptr) == false)
				return false;
		}

		m_Condition.notify_all();
		return true;
	}

	virtual int GetWorkerIndex(IWorkerJobRequestPtr worker_job_request_ptr) const = 0;
	
protected:
	const WORKER_GROUP_TYPE m_WorkerGroupKey;
	std::atomic<bool> m_Running = false;
	std::condition_variable m_Condition;
	std::mutex m_WorkerGroupMutex;
	std::thread	m_WorkerGroupThread;

protected:
	virtual void WorkerGroupThreadFunc() {}

public:
	virtual bool IsRunning() const { return m_Running; }
	virtual bool IsJobRemained() const { return false; }
	const WORKER_GROUP_TYPE& GetWorkerGroupKey() const { return m_WorkerGroupKey; }

public:
	const int m_WorkerCount = 1;
	using WorkerPtr = std::shared_ptr<WORKER>;
	using Workers = std::unordered_map<int, WorkerPtr>;
	const Workers m_Workers;
	std::list<std::thread> m_WorkerThreadList;

protected:
	template <typename... Args>
	static Workers InitWorkers(int worker_count, Args&&... args)
	{
		Workers workers;

		for (int worker_index = 0; worker_index < worker_count; ++worker_index)
			workers.emplace(worker_index, std::make_shared<WORKER>(worker_index, std::forward<Args>(args)...));

		return workers;
	}

	WorkerPtr GetWorker(int worker_id) const
	{
		try
		{
			return m_Workers.at(worker_id);
		}
		catch (...) { return nullptr; }
	}

	virtual bool DoWorkerAsyncJob(int worker_id)
	{
		auto worker_ptr = GetWorker(worker_id);
		if (worker_ptr == nullptr)
			return false;

		return worker_ptr->DoWorkerAsyncJob();
	}

	// 작업 가능 시점까지 대기 후 워커 작업 실행
	virtual void WorkerThreadFunc(int worker_id)
	{
		auto worker_ptr = GetWorker(worker_id);
		if (worker_ptr == nullptr)
			return;

		while (IsRunning())
			{
				{
					std::unique_lock<std::mutex> lock(m_WorkerGroupMutex);
					while (IsRunning() && worker_ptr->IsWorkerJobAvailable() == false)
						m_Condition.wait_until(lock, worker_ptr->GetJobExecuteTimePoint());
				}

			if (IsRunning() == false)
				break;

			DoWorkerAsyncJob(worker_id);
		}

	}

public:
	int	GetMaxWorkerIndex() const { return m_WorkerCount - 1; }

};

// 워커 그룹 맵
template <typename WORKER_GROUP_TYPE, typename MATCH_WORKER_GROUP, typename WORKER_GROUP_PTR = std::shared_ptr<MATCH_WORKER_GROUP>>
	class TWorkerGroupMap
{
private:
	mutable std::shared_mutex m_Mutex;
	std::map<WORKER_GROUP_TYPE, WORKER_GROUP_PTR> m_WorkerGroupMap;

public:
	TWorkerGroupMap() = default;
	virtual ~TWorkerGroupMap() { Stop(); }

public:
	bool Insert(const WORKER_GROUP_TYPE& worker_group_type, WORKER_GROUP_PTR worker_group_ptr)
	{
		std::unique_lock write_lock(m_Mutex);

		return m_WorkerGroupMap.emplace(worker_group_type, worker_group_ptr).second;
	}

	void Start()
	{
		std::shared_lock read_lock(m_Mutex);

		for (auto&& [group, workers] : m_WorkerGroupMap)
		{
			workers->Start();
		}
	}

	void Stop()
	{
		std::vector<WORKER_GROUP_PTR> workers_to_stop;
		{
			std::unique_lock write_lock(m_Mutex);
			for (auto&& [group, worker] : m_WorkerGroupMap)
				workers_to_stop.emplace_back(worker);

			m_WorkerGroupMap.clear();
		}

		for (auto&& worker : workers_to_stop)
		{
			if (worker)
				worker->Stop();
		}
	}

	WORKER_GROUP_PTR GetWorkerGroup(const WORKER_GROUP_TYPE& worker_group_type)
	{
		std::shared_lock read_lock(m_Mutex);

		auto it = m_WorkerGroupMap.find(worker_group_type);
		if (it == m_WorkerGroupMap.end())
			return nullptr;

		return it->second;
	}
};
