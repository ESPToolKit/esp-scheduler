#pragma once

#include <Arduino.h>

extern "C" {
#include "freertos/FreeRTOS.h"
}

#include <cstdint>

enum class SchedulerMode : uint8_t {
	Manual = 0,
	Background,
};

enum class DispatchPolicy : uint8_t {
	Inline = 0,
	Async,
};

enum class OverlapPolicy : uint8_t {
	SkipIfRunning = 0,
	QueueOne,
	AllowParallel,
};

struct SchedulerServiceConfig {
	uint32_t commandQueueDepth = 16;
	uint32_t eventQueueDepth = 16;
	uint32_t taskStackSize = 4096;
	UBaseType_t taskPriority = 1;
	BaseType_t coreId = tskNO_AFFINITY;
	bool usePsramStack = false;
	uint32_t controlTimeoutMs = 2000;
};

struct WorkerPoolConfig {
	uint8_t workerCount = 1;
	uint32_t queueDepth = 8;
	uint32_t stackSize = 6144;
	UBaseType_t priority = 1;
	BaseType_t coreId = tskNO_AFFINITY;
	bool usePsramStack = false;
};

struct DedicatedTaskOptions {
	const char *name = "sched-job";
	uint32_t stackSize = 4096;
	UBaseType_t priority = 1;
	BaseType_t coreId = tskNO_AFFINITY;
	bool usePsramStack = false;
};

struct SchedulerConfig {
	SchedulerMode mode = SchedulerMode::Background;
	int64_t minValidEpochSeconds = 1577836800;
	bool usePSRAMMetadata = false;
	SchedulerServiceConfig service{};
	WorkerPoolConfig defaultWorkerPool{};
	DedicatedTaskOptions defaultDedicatedTask{};
};
