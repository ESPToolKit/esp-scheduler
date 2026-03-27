#pragma once

#include <string>

#include "../executors/scheduler_executor.h"

struct JobRecord {
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
	uint16_t runningCount = 0;

	DateTime nextRunUtc{};
	CallbackRef callback{};
	std::string name{};
	DedicatedTaskOptions dedicatedTask{};
	bool hasDedicatedTaskOptions = false;
};
