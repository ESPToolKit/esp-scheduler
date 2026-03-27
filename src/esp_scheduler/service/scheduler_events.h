#pragma once

#include <cstddef>
#include <cstdint>

enum class SchedulerEventKind : uint8_t {
	JobFinished = 0,
};

struct SchedulerEvent {
	SchedulerEventKind kind = SchedulerEventKind::JobFinished;
	uint32_t jobId = 0;
	uint32_t generation = 0;
	size_t slotIndex = 0;
};
