#pragma once

#include <atomic>
#include <vector>

#include "scheduler_executor.h"

class WorkerPoolExecutor : public ISchedulerExecutor {
  public:
	explicit WorkerPoolExecutor(const WorkerPoolConfig &config);
	~WorkerPoolExecutor() override;

	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override;
	void end(bool drainRunningJobs) override;
	bool submit(const JobInvocation &invocation) override;
	const char *name() const override;

  private:
	struct TaskItem;
	struct WorkerContext;

	static void workerTaskEntry(void *arg);

	WorkerPoolConfig config_{};
	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
	QueueHandle_t queue_ = nullptr;
	std::vector<TaskHandle_t> workers_{};
	std::atomic<bool> started_{false};
	std::atomic<int> workersRunning_{0};
};
