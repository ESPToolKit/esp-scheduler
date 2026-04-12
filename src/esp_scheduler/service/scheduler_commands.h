#pragma once

#include <atomic>

#include "../core/scheduler_core.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
}

class SchedulerServiceCommand {
  public:
	SchedulerServiceCommand();
	virtual ~SchedulerServiceCommand();

	bool wait(uint32_t timeoutMs);
	void signal();
	void abandon();
	bool abandoned() const;

	virtual void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) = 0;

  private:
	SemaphoreHandle_t completion_ = nullptr;
	StaticSemaphore_t completionBuffer_{};
	std::atomic<bool> abandoned_{false};
};

class AddJobCommand : public SchedulerServiceCommand {
  public:
	ScheduleSpec schedule{};
	JobOptions options{};
	DedicatedTaskOptions dedicatedTaskCopy{};
	CallbackRef callback{};
	SchedulerResult<uint32_t> result =
	    SchedulerResult<uint32_t>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class CancelJobCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class PauseJobCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class ResumeJobCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class CancelAllCommand : public SchedulerServiceCommand {
  public:
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class RefreshAllSchedulesCommand : public SchedulerServiceCommand {
  public:
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class JobCountCommand : public SchedulerServiceCommand {
  public:
	SchedulerResult<size_t> result =
	    SchedulerResult<size_t>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class GetJobInfoCommand : public SchedulerServiceCommand {
  public:
	uint32_t jobId = 0;
	JobInfo *info = nullptr;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};

class SetMinValidCommand : public SchedulerServiceCommand {
  public:
	int64_t minEpochSeconds = 0;
	SchedulerResult<void> result = SchedulerResult<void>::failure(SchedulerError::NotInitialized);

	void execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) override;
};
