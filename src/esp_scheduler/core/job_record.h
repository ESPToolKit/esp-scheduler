#pragma once

#include "../executors/scheduler_executor.h"
#include "runtime_containers.h"

struct JobRecord {
	explicit JobRecord(bool usePSRAMMetadata = false) : name(usePSRAMMetadata) {
	}

	uint32_t id = 0;
	uint32_t generation = 1;
	bool occupied = false;

	ScheduleSpec schedule{};
	DispatchPolicy dispatch = DispatchPolicy::Inline;
	OverlapPolicy overlap = OverlapPolicy::SkipIfRunning;
	uint8_t executorId = 0;

	bool paused = false;
	bool canceled = false;
	bool queuedWhileRunning = false;
	bool hasNext = false;
	bool pendingSchedule = false;
	bool refreshPending = false;
	uint16_t runningCount = 0;

	DateTime nextRunUtc{};
	DateTime scheduleFromUtc{};
	CallbackRef callback{};
	SchedulerOwnedString name{};
	DedicatedTaskOptions dedicatedTask{};
	bool hasDedicatedTaskOptions = false;
};
