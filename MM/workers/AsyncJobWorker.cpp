#include "stdafx.h"
#include "AsyncJobWorker.h"
#include "MatchSessionManager.h"
#include "WorkerJobRequest.h"

bool CAsyncJobWorker::IsWorkerJobAvailable() const
{
	std::shared_lock read_lock(m_WorkerMutex);
	if (m_WaitingQueue.empty())
		return false;

	const auto& next_job = m_WaitingQueue.begin()->second;
	return next_job != nullptr && next_job->m_JobExecuteTimeMSec <= time_msec(nullptr);
}

bool CAsyncJobWorker::DoWorkerAsyncJob()
{
	CAsyncJobRequestPtr async_job_ptr = nullptr;
	
	{
		std::unique_lock write_lock(m_WorkerMutex);

		if (m_WaitingQueue.empty())
			return false;

		auto it = (m_WaitingQueue.begin());

		async_job_ptr = it->second;

		if (async_job_ptr == nullptr)
		{
			m_WaitingQueue.erase(it);
			std::cout << std::format("error matchsession_make_request_ptr is null. \n");
			return false;
		}

		if (async_job_ptr->m_JobExecuteTimeMSec > time_msec(nullptr))
			return false;

		m_WaitingQueue.erase(it);
	}

	if (async_job_ptr)
		async_job_ptr->m_JobFunc();

	return true;
}


long long CAsyncJobWorker::GetJobExecuteTimeMSec() const
{
	std::shared_lock lock(m_WorkerMutex);
	if (m_WaitingQueue.empty())
		return IWorker::GetJobExecuteTimeMSec();

	return m_WaitingQueue.begin()->second->m_JobExecuteTimeMSec;
}

bool CAsyncJobWorker::EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr)
{
	auto async_job_request_ptr = std::dynamic_pointer_cast<CAsyncJobRequest> (worker_job_request_ptr);
	if (async_job_request_ptr == nullptr)
		return false;

	std::unique_lock write_lock(m_WorkerMutex);
	m_WaitingQueue.emplace(async_job_request_ptr->m_JobExecuteTimeMSec, async_job_request_ptr);
	return true;
}

bool CAsyncJobWorkerGroup::EnqueueJobRequest(IWorkerJobRequestPtr worker_job_request_ptr)
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

int CAsyncJobWorkerGroup::GetWorkerIndex(IWorkerJobRequestPtr worker_job_request_ptr) const
{
	auto async_job_request_ptr = std::dynamic_pointer_cast<CAsyncJobRequest> (worker_job_request_ptr);
	if (async_job_request_ptr == nullptr)
		return -1;

	return async_job_request_ptr->m_JobExecuteTimeMSec % m_WorkerCount;
}
