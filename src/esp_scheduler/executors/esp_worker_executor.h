#pragma once

#include "scheduler_executor.h"

class ESPWorker;

class ESPWorkerExecutorAdapter : public ISchedulerExecutor {
  public:
	explicit ESPWorkerExecutorAdapter(ESPWorker &worker);

	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override;
	void end(bool drainRunningJobs) override;
	bool submit(const JobInvocation &invocation) override;
	const char *name() const override;

  private:
	ESPWorker &worker_;
	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
};
