#pragma once

#include <future>
#include <map>

#include "WorkerGroup.h"

// 예약 작업 워커
class CAsyncJobWorker : public IWorker
{

private:
	std::multimap<long long, CAsyncJobRequestPtr> m_WaitingQueue;
	friend class CAsyncJobWorkerGroup;

public:
	CAsyncJobWorker(int worker_index)
		: IWorker(worker_index)
	{}
	virtual ~CAsyncJobWorker()
	{}

protected:
	long long GetJobExecuteTimeMSec() const override;

public:
	bool EnqueueJobRequest(IWorkerJobRequestPtr matchsession_make_requst_ptr) override;
	bool DoWorkerAsyncJob() override;
	bool IsWorkerJobAvailable() const override;
};

// 예약 작업 워커 그룹
class CAsyncJobWorkerGroup : public IWorkerGroup<int, CAsyncJobWorker>
{
public:
	CAsyncJobWorkerGroup(int worker_count)
		: IWorkerGroup<int, CAsyncJobWorker>(0, worker_count)
	{}
	virtual ~CAsyncJobWorkerGroup(){}

	bool EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr) override;
	int GetWorkerIndex(IWorkerJobRequestPtr worker_job_request_ptr) const override;

	// 예약 작업 등록
	template <class Func, class... Args>
	bool DoAsyncJob(const long long& job_execution_time_msec, Func job_func, Args... args);
};

template <class Func, class... Args>
bool CAsyncJobWorkerGroup::DoAsyncJob(const long long& job_execution_time_msec, Func job_func, Args... args)
{
	auto job = std::make_shared<std::packaged_task<void()>>(
		std::bind(std::forward<Func>(job_func), std::forward<Args>(args)...));

	if (false == EnqueueJobRequest(
		std::make_shared<CAsyncJobRequest>(job_execution_time_msec, [job]() { (*job)(); })))
	{
		return false;
	}

	return true;
}
