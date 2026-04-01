#pragma once

#include <atomic>
#include <memory>

#include "../core/scheduler_core.h"
#include "../executors/scheduler_executor.h"
#include "scheduler_commands.h"

namespace scheduler_service_detail {
TickType_t nextWakeTicks(
    ESPDate &date,
    const DateTime &nowUtc,
    bool hasNextDue,
    int64_t nextDueEpochSeconds,
    TickType_t idlePollTicks
);
}

class SchedulerService {
  public:
	SchedulerService(
	    ESPDate &date,
	    const SchedulerServiceConfig &config,
	    int64_t minValidEpochSeconds,
	    bool usePSRAMMetadata,
	    std::atomic<bool> &timeContextRefreshRequested,
	    IExecutorResolver &executors
	);
	~SchedulerService();

	bool begin();
	void stop();

	bool send(SchedulerServiceCommand *command);

	QueueHandle_t eventQueue() const {
		return eventQueue_;
	}

	size_t activeInvocationCount() const {
		return activeInvocationCount_.load();
	}

  private:
	static void taskEntry(void *arg);

	void run();
	void drainCommands();
	void drainEvents();
	void refreshTimeContextIfNeeded(const DateTime &nowUtc);

	ESPDate &date_;
	SchedulerServiceConfig config_{};
	SchedulerCore core_;
	std::atomic<bool> &timeContextRefreshRequested_;
	IExecutorResolver &executors_;
	DateTime lastObservedLocalDayStartUtc_{};
	bool hasLastObservedLocalDayStartUtc_ = false;

	QueueHandle_t commandQueue_ = nullptr;
	QueueHandle_t eventQueue_ = nullptr;
	QueueSetHandle_t queueSet_ = nullptr;
	TaskHandle_t task_ = nullptr;
	bool taskCreatedWithCaps_ = false;

	std::atomic<bool> started_{false};
	std::atomic<bool> stopRequested_{false};
	std::atomic<bool> taskExited_{false};
	std::atomic<size_t> activeInvocationCount_{0};
};
