#include "worker_pool_executor.h"

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

struct WorkerPoolExecutor::TaskItem {
	JobInvocation invocation{};
};

struct WorkerPoolExecutor::WorkerContext {
	WorkerPoolExecutor *owner = nullptr;
	bool createdWithCaps = false;
};

WorkerPoolExecutor::WorkerPoolExecutor(const WorkerPoolConfig &config)
    : config_(config), workers_(false) {
}

WorkerPoolExecutor::~WorkerPoolExecutor() {
	end(true);
}

bool WorkerPoolExecutor::begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) {
	if (started_.load()) {
		return true;
	}

	runtime_ = runtime;
	queue_ = xQueueCreate(config_.queueDepth, sizeof(TaskItem *));
	if (!queue_) {
		return false;
	}

	workers_.clear();
	workersRunning_.store(0);
	for (uint8_t index = 0; index < config_.workerCount; ++index) {
		WorkerContext *context = new (std::nothrow) WorkerContext{};
		if (!context) {
			end(false);
			return false;
		}
		context->owner = this;

		TaskHandle_t handle = nullptr;
		bool createdWithCaps = false;
		const BaseType_t created = scheduler_task_support::createTaskPinned(
		    &WorkerPoolExecutor::workerTaskEntry,
		    "sched-pool",
		    config_.stackSize,
		    context,
		    config_.priority,
		    &handle,
		    config_.coreId,
		    config_.usePsramStack,
		    createdWithCaps
		);
		if (created != pdPASS || handle == nullptr) {
			delete context;
			end(false);
			return false;
		}
		context->createdWithCaps = createdWithCaps;
		if (!workers_.pushBack({handle, createdWithCaps})) {
			scheduler_task_support::deleteTask(handle, createdWithCaps);
			end(false);
			return false;
		}
		workersRunning_.fetch_add(1);
	}

	started_.store(true);
	return true;
}

void WorkerPoolExecutor::end(bool drainRunningJobs) {
	if (!queue_) {
		started_.store(false);
		runtime_.reset();
		return;
	}

	if (!drainRunningJobs) {
		TaskItem *pending = nullptr;
		while (xQueueReceive(queue_, &pending, 0) == pdTRUE) {
			delete pending;
		}
	}

	for (size_t index = 0; index < workers_.size(); ++index) {
		TaskItem *sentinel = nullptr;
		xQueueSend(queue_, &sentinel, 0);
	}

	const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
	while (workersRunning_.load() > 0 && xTaskGetTickCount() < deadline) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}

	if (workersRunning_.load() > 0) {
		for (size_t index = 0; index < workers_.size(); ++index) {
			scheduler_task_support::deleteTask(
			    workers_[index].task,
			    workers_[index].createdWithCaps
			);
		}
	}

	if (queue_) {
		vQueueDelete(queue_);
		queue_ = nullptr;
	}
	workers_.clear();
	started_.store(false);
	runtime_.reset();
}

bool WorkerPoolExecutor::submit(const JobInvocation &invocation) {
	if (!queue_ || !started_.load()) {
		return false;
	}

	TaskItem *item = new (std::nothrow) TaskItem{};
	if (!item) {
		return false;
	}
	item->invocation = invocation;
	item->invocation.runtime = runtime_;

	if (xQueueSend(queue_, &item, 0) != pdTRUE) {
		delete item;
		return false;
	}
	return true;
}

const char *WorkerPoolExecutor::name() const {
	return "worker-pool";
}

void WorkerPoolExecutor::workerTaskEntry(void *arg) {
	WorkerContext *context = static_cast<WorkerContext *>(arg);
	if (!context || !context->owner) {
		delete context;
		vTaskDelete(nullptr);
		return;
	}

	WorkerPoolExecutor *owner = context->owner;
	const bool createdWithCaps = context->createdWithCaps;
	delete context;

	while (true) {
		TaskItem *item = nullptr;
		if (xQueueReceive(owner->queue_, &item, portMAX_DELAY) != pdTRUE) {
			continue;
		}
		if (!item) {
			break;
		}

		item->invocation.callback.invoke();
		postCompletion(
		    owner->runtime_,
		    item->invocation.jobId,
		    item->invocation.generation,
		    item->invocation.slotIndex
		);
		delete item;
	}

	owner->workersRunning_.fetch_sub(1);
	scheduler_task_support::deleteCurrentTask(createdWithCaps);
}
