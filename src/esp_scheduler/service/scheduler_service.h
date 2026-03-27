#pragma once

#include <atomic>
#include <memory>

#include "../core/scheduler_core.h"
#include "../executors/scheduler_executor.h"
#include "scheduler_commands.h"

class SchedulerService {
  public:
	SchedulerService(
	    ESPDate &date,
	    const SchedulerServiceConfig &config,
	    int64_t minValidEpochSeconds,
	    IExecutorResolver &executors
	);
	~SchedulerService();

	bool begin();
	void stop();

	bool send(SchedulerServiceCommand &command);

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

	ESPDate &date_;
	SchedulerServiceConfig config_{};
	SchedulerCore core_;
	IExecutorResolver &executors_;

	QueueHandle_t commandQueue_ = nullptr;
	QueueHandle_t eventQueue_ = nullptr;
	QueueSetHandle_t queueSet_ = nullptr;
	TaskHandle_t task_ = nullptr;

	std::atomic<bool> started_{false};
	std::atomic<bool> stopRequested_{false};
	std::atomic<bool> taskExited_{false};
	std::atomic<size_t> activeInvocationCount_{0};
};
