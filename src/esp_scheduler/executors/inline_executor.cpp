#include "inline_executor.h"

bool InlineExecutor::begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
	runtime_ = runtime;
	return true;
}

void InlineExecutor::end(bool drainRunningJobs) {
	(void)drainRunningJobs;
	runtime_.reset();
}

bool InlineExecutor::submit(const JobInvocation &invocation) {
	invocation.callback.invoke();
	return true;
}

const char *InlineExecutor::name() const {
	return "inline";
}
