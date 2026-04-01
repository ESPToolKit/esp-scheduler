#include "scheduler_service.h"

#include <new>

#include "../executors/task_support.h"

namespace {
constexpr uint32_t kIdlePollMs = 1000;
}

TickType_t scheduler_service_detail::nextWakeTicks(
    ESPDate &date,
    const DateTime &nowUtc,
    bool hasNextDue,
    int64_t nextDueEpochSeconds,
    TickType_t idlePollTicks
) {
	int64_t nextWakeEpoch = 0;
	bool hasWake = false;

	if (hasNextDue) {
		nextWakeEpoch = nextDueEpochSeconds;
		hasWake = true;
	}

	const DateTime nextLocalMidnightUtc = date.startOfDayLocal(date.addDays(nowUtc, 1));
	if (!hasWake || nextLocalMidnightUtc.epochSeconds < nextWakeEpoch) {
		nextWakeEpoch = nextLocalMidnightUtc.epochSeconds;
		hasWake = true;
	}

	if (!hasWake) {
		return idlePollTicks;
	}
	if (nextWakeEpoch <= nowUtc.epochSeconds) {
		return 0;
	}
	const int64_t waitSeconds = nextWakeEpoch - nowUtc.epochSeconds;
	return pdMS_TO_TICKS(static_cast<uint32_t>(waitSeconds * 1000));
}

SchedulerService::SchedulerService(
    ESPDate &date,
    const SchedulerServiceConfig &config,
    int64_t minValidEpochSeconds,
    bool usePSRAMMetadata,
    std::atomic<bool> &timeContextRefreshRequested,
    IExecutorResolver &executors
)
    : date_(date),
      config_(config),
      core_(date, minValidEpochSeconds, usePSRAMMetadata),
      timeContextRefreshRequested_(timeContextRefreshRequested),
      executors_(executors) {
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

	bool createdWithCaps = false;
	const BaseType_t created = scheduler_task_support::createTaskPinned(
	    &SchedulerService::taskEntry,
	    "sched-svc",
	    config_.taskStackSize,
	    this,
	    config_.taskPriority,
	    &task_,
	    config_.coreId,
	    config_.usePsramStack,
	    createdWithCaps
	);
	if (created != pdPASS || task_ == nullptr) {
		stop();
		return false;
	}
	taskCreatedWithCaps_ = createdWithCaps;

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
		scheduler_task_support::deleteTask(task_, taskCreatedWithCaps_);
	}
	task_ = nullptr;
	taskCreatedWithCaps_ = false;

	if (queueSet_) {
		vQueueDelete(queueSet_);
		queueSet_ = nullptr;
	}
	if (commandQueue_) {
		while (true) {
			SchedulerServiceCommand *pending = nullptr;
			if (xQueueReceive(commandQueue_, &pending, 0) != pdTRUE) {
				break;
			}
			if (!pending) {
				continue;
			}
			// Producer owns the command until send() succeeds, then may free it only after wait()
			// completes. The service may free only abandoned commands, and must decide before signal().
			const bool shouldDelete = pending->abandoned();
			pending->signal();
			if (shouldDelete) {
				delete pending;
			}
		}
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

bool SchedulerService::send(SchedulerServiceCommand *command) {
	if (!commandQueue_) {
		return false;
	}
	return xQueueSend(commandQueue_, &command, 0) == pdTRUE;
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
	scheduler_task_support::deleteCurrentTask(service->taskCreatedWithCaps_);
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
		// Producer owns the command until send() succeeds, then may free it only after wait()
		// completes. The service may free only abandoned commands, and must decide before signal().
		const bool shouldDelete = command->abandoned();
		command->signal();
		if (shouldDelete) {
			delete command;
		}
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

void SchedulerService::refreshTimeContextIfNeeded(const DateTime &nowUtc) {
	bool refreshRequested = timeContextRefreshRequested_.exchange(false);
	const DateTime currentLocalDayStartUtc = date_.startOfDayLocal(nowUtc);
	if (!hasLastObservedLocalDayStartUtc_) {
		lastObservedLocalDayStartUtc_ = currentLocalDayStartUtc;
		hasLastObservedLocalDayStartUtc_ = true;
	} else if (!date_.isEqual(currentLocalDayStartUtc, lastObservedLocalDayStartUtc_)) {
		lastObservedLocalDayStartUtc_ = currentLocalDayStartUtc;
		refreshRequested = true;
	}

	if (refreshRequested) {
		(void)core_.refreshAllSchedules(nowUtc);
	}
}

void SchedulerService::run() {
	while (!stopRequested_.load()) {
		drainCommands();
		drainEvents();

		const DateTime nowUtc = date_.now();
		refreshTimeContextIfNeeded(nowUtc);
		core_.dispatchDue(nowUtc, executors_);
		activeInvocationCount_.store(core_.activeInvocationCount());

		int64_t nextEpochSeconds = 0;
		TickType_t waitTicks = pdMS_TO_TICKS(kIdlePollMs);
		if (core_.clockValid(nowUtc)) {
			const bool hasNextDue = core_.nextDueEpoch(nextEpochSeconds);
			waitTicks = scheduler_service_detail::nextWakeTicks(
			    date_,
			    nowUtc,
			    hasNextDue,
			    nextEpochSeconds,
			    waitTicks
			);
		}

		QueueSetMemberHandle_t ready =
		    queueSet_ ? xQueueSelectFromSet(queueSet_, waitTicks) : nullptr;
		if (!ready) {
			continue;
		}
	}
}
