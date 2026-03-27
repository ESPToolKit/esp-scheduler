#include "scheduler_service.h"

#include <new>

namespace {
constexpr uint32_t kIdlePollMs = 1000;
}

SchedulerService::SchedulerService(
    ESPDate &date,
    const SchedulerServiceConfig &config,
    int64_t minValidEpochSeconds,
    IExecutorResolver &executors
)
    : date_(date), config_(config), core_(date, minValidEpochSeconds), executors_(executors) {
}

SchedulerService::~SchedulerService() {
	stop();
}

bool SchedulerService::begin() {
	if (started_.load()) {
		return true;
	}

	commandQueue_ = xQueueCreate(config_.commandQueueDepth, sizeof(SchedulerServiceCommand *));
	eventQueue_ = xQueueCreate(config_.eventQueueDepth, sizeof(SchedulerEvent));
	if (!commandQueue_ || !eventQueue_) {
		stop();
		return false;
	}

	queueSet_ = xQueueCreateSet(config_.commandQueueDepth + config_.eventQueueDepth);
	if (!queueSet_) {
		stop();
		return false;
	}
	xQueueAddToSet(commandQueue_, queueSet_);
	xQueueAddToSet(eventQueue_, queueSet_);

	const BaseType_t created = xTaskCreatePinnedToCore(
	    &SchedulerService::taskEntry,
	    "sched-svc",
	    config_.taskStackSize,
	    this,
	    config_.taskPriority,
	    &task_,
	    config_.coreId
	);
	if (created != pdPASS || task_ == nullptr) {
		stop();
		return false;
	}

	started_.store(true);
	return true;
}

void SchedulerService::stop() {
	if (!commandQueue_ && !eventQueue_ && !queueSet_ && !task_) {
		started_.store(false);
		return;
	}

	stopRequested_.store(true);
	SchedulerServiceCommand *wake = nullptr;
	if (commandQueue_) {
		xQueueSend(commandQueue_, &wake, 0);
	}

	const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
	while (task_ && !taskExited_.load() && xTaskGetTickCount() < deadline) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}

	if (task_ && !taskExited_.load()) {
		vTaskDelete(task_);
	}
	task_ = nullptr;

	if (queueSet_) {
		vQueueDelete(queueSet_);
		queueSet_ = nullptr;
	}
	if (commandQueue_) {
		vQueueDelete(commandQueue_);
		commandQueue_ = nullptr;
	}
	if (eventQueue_) {
		vQueueDelete(eventQueue_);
		eventQueue_ = nullptr;
	}

	taskExited_.store(false);
	stopRequested_.store(false);
	started_.store(false);
}

bool SchedulerService::send(SchedulerServiceCommand &command) {
	if (!commandQueue_) {
		return false;
	}
	SchedulerServiceCommand *pointer = &command;
	return xQueueSend(commandQueue_, &pointer, pdMS_TO_TICKS(config_.controlTimeoutMs)) == pdTRUE;
}

void SchedulerService::taskEntry(void *arg) {
	SchedulerService *service = static_cast<SchedulerService *>(arg);
	if (!service) {
		vTaskDelete(nullptr);
		return;
	}
	service->run();
	service->taskExited_.store(true);
	service->task_ = nullptr;
	vTaskDelete(nullptr);
}

void SchedulerService::drainCommands() {
	if (!commandQueue_) {
		return;
	}
	while (true) {
		SchedulerServiceCommand *command = nullptr;
		if (xQueueReceive(commandQueue_, &command, 0) != pdTRUE) {
			break;
		}
		if (!command) {
			continue;
		}
		command->execute(core_, date_, executors_);
		command->signal();
	}
}

void SchedulerService::drainEvents() {
	if (!eventQueue_) {
		return;
	}
	while (true) {
		SchedulerEvent event{};
		if (xQueueReceive(eventQueue_, &event, 0) != pdTRUE) {
			break;
		}
		core_.handleEvent(event, date_.now(), executors_);
	}
}

void SchedulerService::run() {
	while (!stopRequested_.load()) {
		drainCommands();
		drainEvents();

		const DateTime nowUtc = date_.now();
		core_.dispatchDue(nowUtc, executors_);
		activeInvocationCount_.store(core_.activeInvocationCount());

		int64_t nextEpochSeconds = 0;
		TickType_t waitTicks = pdMS_TO_TICKS(kIdlePollMs);
		if (core_.clockValid(nowUtc) && core_.nextDueEpoch(nextEpochSeconds)) {
			if (nextEpochSeconds <= nowUtc.epochSeconds) {
				waitTicks = 0;
			} else {
				const int64_t waitSeconds = nextEpochSeconds - nowUtc.epochSeconds;
				waitTicks = pdMS_TO_TICKS(static_cast<uint32_t>(waitSeconds * 1000));
			}
		}

		QueueSetMemberHandle_t ready =
		    queueSet_ ? xQueueSelectFromSet(queueSet_, waitTicks) : nullptr;
		if (!ready) {
			continue;
		}
	}
}
