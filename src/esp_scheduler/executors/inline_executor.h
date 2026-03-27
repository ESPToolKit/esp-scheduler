#pragma once

#include "scheduler_executor.h"

class InlineExecutor : public ISchedulerExecutor {
  public:
	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override;
	void end(bool drainRunningJobs) override;
	bool submit(const JobInvocation &invocation) override;
	const char *name() const override;

  private:
	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
};
