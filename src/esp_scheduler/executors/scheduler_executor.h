#pragma once

#include <atomic>
#include <memory>

#include "../scheduler.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

struct SchedulerExecutorRuntime {
	std::atomic<bool> accepting{true};
	QueueHandle_t eventQueue = nullptr;
};

enum class CallbackKind : uint8_t {
	RawFunction = 0,
	OwningFunction,
};

struct CallbackRef {
	CallbackKind kind = CallbackKind::RawFunction;
	SchedulerCallbackFn rawFn = nullptr;
	void *userData = nullptr;
	std::shared_ptr<SchedulerFunction> owningFn{};

	bool valid() const {
		return rawFn != nullptr || static_cast<bool>(owningFn);
	}

	void invoke() const {
#if defined(__cpp_exceptions)
		try {
#endif
			if (owningFn) {
				(*owningFn)(userData);
				return;
			}
			if (rawFn) {
				rawFn(userData);
			}
#if defined(__cpp_exceptions)
		} catch (...) {
		}
#endif
	}
};

struct JobInvocation {
	uint32_t jobId = 0;
	uint32_t generation = 0;
	size_t slotIndex = 0;
	const char *name = nullptr;
	CallbackRef callback{};
	DedicatedTaskOptions dedicatedTask{};
	std::shared_ptr<SchedulerExecutorRuntime> runtime{};
};

class ISchedulerExecutor {
  public:
	virtual ~ISchedulerExecutor() = default;

	virtual bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) = 0;
	virtual void end(bool drainRunningJobs) = 0;
	virtual bool submit(const JobInvocation &invocation) = 0;
	virtual const char *name() const = 0;
};

class IExecutorResolver {
  public:
	virtual ~IExecutorResolver() = default;
	virtual ISchedulerExecutor *inlineExecutor() = 0;
	virtual ISchedulerExecutor *executorFor(uint8_t executorId) = 0;
};
