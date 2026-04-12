#include "esp_worker_executor.h"

#include <ESPWorker.h>

#include "../service/scheduler_events.h"

namespace {
bool postCompletion(
    const std::shared_ptr<SchedulerExecutorRuntime> &runtime,
    uint32_t jobId,
    uint32_t generation,
    size_t slotIndex
) {
	if (!runtime || !runtime->accepting.load() || runtime->eventQueue == nullptr) {
		return false;
	}
	SchedulerEvent event{};
	event.kind = SchedulerEventKind::JobFinished;
	event.jobId = jobId;
	event.generation = generation;
	event.slotIndex = slotIndex;
	return xQueueSend(runtime->eventQueue, &event, 0) == pdTRUE;
}
} // namespace

ESPWorkerExecutorAdapter::ESPWorkerExecutorAdapter(ESPWorker &worker) : worker_(worker) {
}

bool ESPWorkerExecutorAdapter::begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
	runtime_ = runtime;
	return true;
}

void ESPWorkerExecutorAdapter::end(bool drainRunningJobs) {
	(void)drainRunningJobs;
	runtime_.reset();
}

bool ESPWorkerExecutorAdapter::submit(const JobInvocation &invocation) {
	WorkerConfig config{};
	if (invocation.name) {
		config.name = invocation.name;
	}
	std::shared_ptr<SchedulerExecutorRuntime> runtime = runtime_;
	WorkerResult result = worker_.spawn(
	    [invocation, runtime]() {
		    invocation.callback.invoke();
		    postCompletion(runtime, invocation.jobId, invocation.generation, invocation.slotIndex);
	    },
	    config
	);
	return static_cast<bool>(result);
}

const char *ESPWorkerExecutorAdapter::name() const {
	return "esp-worker";
}
