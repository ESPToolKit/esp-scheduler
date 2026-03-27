#include "dedicated_task_executor.h"

#include <new>

#include "../service/scheduler_events.h"
#include "task_support.h"

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

struct DedicatedTaskExecutor::TaskContext {
	JobInvocation invocation{};
	bool createdWithCaps = false;
};

bool DedicatedTaskExecutor::begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
	runtime_ = runtime;
	return true;
}

void DedicatedTaskExecutor::end(bool drainRunningJobs) {
	(void)drainRunningJobs;
	runtime_.reset();
}

bool DedicatedTaskExecutor::submit(const JobInvocation &invocation) {
	TaskContext *context = new (std::nothrow) TaskContext{};
	if (!context) {
		return false;
	}
	context->invocation = invocation;
	context->invocation.runtime = runtime_;

	TaskHandle_t handle = nullptr;
	const DedicatedTaskOptions &task = invocation.dedicatedTask;
	const BaseType_t created = scheduler_task_support::createTaskPinned(
	    &DedicatedTaskExecutor::taskEntry,
	    task.name ? task.name : "sched-task",
	    task.stackSize,
	    context,
	    task.priority,
	    &handle,
	    task.coreId,
	    task.usePsramStack,
	    context->createdWithCaps
	);
	if (created != pdPASS || handle == nullptr) {
		delete context;
		return false;
	}
	return true;
}

const char *DedicatedTaskExecutor::name() const {
	return "dedicated-task";
}

void DedicatedTaskExecutor::taskEntry(void *arg) {
	TaskContext *context = static_cast<TaskContext *>(arg);
	if (!context) {
		vTaskDelete(nullptr);
		return;
	}

	context->invocation.callback.invoke();
	postCompletion(
	    context->invocation.runtime,
	    context->invocation.jobId,
	    context->invocation.generation,
	    context->invocation.slotIndex
	);
	const bool createdWithCaps = context->createdWithCaps;
	delete context;
	scheduler_task_support::deleteCurrentTask(createdWithCaps);
}
