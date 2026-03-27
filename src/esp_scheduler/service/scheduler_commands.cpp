#include "scheduler_commands.h"

SchedulerServiceCommand::SchedulerServiceCommand() {
	completion_ = xSemaphoreCreateBinaryStatic(&completionBuffer_);
}

SchedulerServiceCommand::~SchedulerServiceCommand() {
	if (completion_) {
		vSemaphoreDelete(completion_);
		completion_ = nullptr;
	}
}

bool SchedulerServiceCommand::wait(uint32_t timeoutMs) {
	if (!completion_) {
		return false;
	}
	return xSemaphoreTake(completion_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void SchedulerServiceCommand::signal() {
	if (completion_) {
		xSemaphoreGive(completion_);
	}
}

void SchedulerServiceCommand::abandon() {
	abandoned_.store(true, std::memory_order_release);
}

bool SchedulerServiceCommand::abandoned() const {
	return abandoned_.load(std::memory_order_acquire);
}

void AddJobCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)executors;
	result = core.addJob(schedule, options, callback, date.now());
}

void CancelJobCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.cancelJob(jobId);
}

void PauseJobCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.pauseJob(jobId);
}

void ResumeJobCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)executors;
	result = core.resumeJob(jobId, date.now());
}

void CancelAllCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.cancelAll();
}

void JobCountCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	result = core.jobCount();
}

void GetJobInfoCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	if (!info) {
		result = SchedulerResult<void>::failure(SchedulerError::NotFound);
		return;
	}
	result = core.getJobInfo(jobId, *info);
}

void SetMinValidCommand::execute(SchedulerCore &core, ESPDate &date, IExecutorResolver &executors) {
	(void)date;
	(void)executors;
	core.setMinValidUnixSeconds(minEpochSeconds);
	result = SchedulerResult<void>::success();
}
