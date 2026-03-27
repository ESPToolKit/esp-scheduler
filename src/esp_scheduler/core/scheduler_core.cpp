#include "scheduler_core.h"

namespace {
constexpr int64_t kRetryDelaySeconds = 1;

CallbackRef makeEmptyCallback() {
	return CallbackRef{};
}
} // namespace

SchedulerCore::SchedulerCore(ESPDate &date, int64_t minValidEpochSeconds, bool usePSRAMMetadata)
    : date_(date),
      minValidEpochSeconds_(minValidEpochSeconds),
      usePSRAMMetadata_(usePSRAMMetadata),
      jobs_(usePSRAMMetadata),
      freeSlots_(usePSRAMMetadata),
      dueHeap_(usePSRAMMetadata) {
}

void SchedulerCore::setMinValidUnixSeconds(int64_t minEpochSeconds) {
	minValidEpochSeconds_ = minEpochSeconds;
}

int64_t SchedulerCore::minValidUnixSeconds() const {
	return minValidEpochSeconds_;
}

bool SchedulerCore::clockValid(const DateTime &nowUtc) const {
	return nowUtc.epochSeconds >= minValidEpochSeconds_;
}

SchedulerResult<size_t> SchedulerCore::findJobSlot(uint32_t jobId) const {
	for (size_t index = 0; index < jobs_.size(); ++index) {
		const JobRecord &record = jobs_[index];
		if (record.occupied && !record.canceled && record.id == jobId) {
			return SchedulerResult<size_t>::success(index);
		}
	}
	return SchedulerResult<size_t>::failure(SchedulerError::NotFound);
}

bool SchedulerCore::computeNextForJob(JobRecord &record, const DateTime &fromUtc) {
	if (record.canceled || record.paused) {
		record.hasNext = false;
		return false;
	}
	if (record.schedule.isOneShot || record.schedule.kind == ScheduleKind::OneShotUtc) {
		record.nextRunUtc = record.schedule.onceAtUtc;
		record.hasNext = true;
		return true;
	}
	record.hasNext =
	    ScheduleCalculator::computeNext(date_, record.schedule, fromUtc, record.nextRunUtc);
	return record.hasNext;
}

bool SchedulerCore::pushDue(size_t slotIndex, const JobRecord &record) {
	if (!record.occupied || record.canceled || !record.hasNext) {
		return true;
	}
	return dueHeap_.push({record.nextRunUtc.epochSeconds, slotIndex, record.generation});
}

void SchedulerCore::retireJob(size_t slotIndex) {
	if (slotIndex >= jobs_.size()) {
		return;
	}
	JobRecord &record = jobs_[slotIndex];
	record.occupied = false;
	record.canceled = false;
	record.paused = false;
	record.queuedWhileRunning = false;
	record.hasNext = false;
	record.runningCount = 0;
	record.callback = makeEmptyCallback();
	record.name.clear();
	record.id = 0;
	record.generation++;
	freeSlots_.pushBack(slotIndex);
}

void SchedulerCore::finalizeCanceledIfIdle(size_t slotIndex) {
	if (slotIndex >= jobs_.size()) {
		return;
	}
	JobRecord &record = jobs_[slotIndex];
	if (record.occupied && record.canceled && record.runningCount == 0) {
		retireJob(slotIndex);
	}
}

SchedulerResult<uint32_t> SchedulerCore::addJob(
    const ScheduleSpec &schedule,
    const JobOptions &options,
    const CallbackRef &callback,
    const DateTime &nowUtc
) {
	if (!callback.valid()) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}
	if (!ScheduleCalculator::validate(schedule)) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}

	JobRecord record(usePSRAMMetadata_);
	record.occupied = true;
	record.id = nextId_++;
	record.schedule = schedule;
	record.dispatch = options.dispatch;
	record.overlap = options.overlap;
	record.executorId = options.executorId;
	record.paused = options.startPaused;
	record.callback = callback;
	record.hasDedicatedTaskOptions = options.dedicatedTask != nullptr;
	if (!record.name.assign(options.name)) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
	}
	if (options.dedicatedTask) {
		record.dedicatedTask = *options.dedicatedTask;
	}
	if (clockValid(nowUtc) && !record.paused) {
		computeNextForJob(record, nowUtc);
	}

	size_t slotIndex = 0;
	bool reusedSlot = false;
	if (!freeSlots_.empty()) {
		slotIndex = freeSlots_[freeSlots_.size() - 1];
		freeSlots_.popBack();
		record.generation = jobs_[slotIndex].generation;
		jobs_[slotIndex] = std::move(record);
		reusedSlot = true;
	} else {
		slotIndex = jobs_.size();
		if (!jobs_.pushBack(std::move(record))) {
			return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
		}
	}

	if (!pushDue(slotIndex, jobs_[slotIndex])) {
		if (reusedSlot) {
			jobs_[slotIndex] = JobRecord(usePSRAMMetadata_);
		} else {
			jobs_.popBack();
		}
		return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
	}
	return SchedulerResult<uint32_t>::success(jobs_[slotIndex].id);
}

SchedulerResult<void> SchedulerCore::cancelJob(uint32_t jobId) {
	SchedulerResult<size_t> slotResult = findJobSlot(jobId);
	if (!slotResult.ok()) {
		return SchedulerResult<void>::failure(slotResult.error);
	}
	JobRecord &record = jobs_[slotResult.value];
	record.canceled = true;
	record.paused = false;
	record.queuedWhileRunning = false;
	record.hasNext = false;
	finalizeCanceledIfIdle(slotResult.value);
	return SchedulerResult<void>::success();
}

SchedulerResult<void> SchedulerCore::pauseJob(uint32_t jobId) {
	SchedulerResult<size_t> slotResult = findJobSlot(jobId);
	if (!slotResult.ok()) {
		return SchedulerResult<void>::failure(slotResult.error);
	}
	JobRecord &record = jobs_[slotResult.value];
	record.paused = true;
	record.queuedWhileRunning = false;
	record.hasNext = false;
	return SchedulerResult<void>::success();
}

SchedulerResult<void> SchedulerCore::resumeJob(uint32_t jobId, const DateTime &nowUtc) {
	SchedulerResult<size_t> slotResult = findJobSlot(jobId);
	if (!slotResult.ok()) {
		return SchedulerResult<void>::failure(slotResult.error);
	}
	JobRecord &record = jobs_[slotResult.value];
	record.paused = false;
	record.queuedWhileRunning = false;
	if (clockValid(nowUtc) && record.runningCount == 0) {
		computeNextForJob(record, nowUtc);
		if (!pushDue(slotResult.value, record)) {
			record.hasNext = false;
			return SchedulerResult<void>::failure(SchedulerError::NoMemory);
		}
	}
	return SchedulerResult<void>::success();
}

SchedulerResult<void> SchedulerCore::cancelAll() {
	for (size_t index = 0; index < jobs_.size(); ++index) {
		JobRecord &record = jobs_[index];
		if (!record.occupied || record.canceled) {
			continue;
		}
		record.canceled = true;
		record.paused = false;
		record.queuedWhileRunning = false;
		record.hasNext = false;
		finalizeCanceledIfIdle(index);
	}
	return SchedulerResult<void>::success();
}

SchedulerResult<size_t> SchedulerCore::jobCount() const {
	size_t count = 0;
	for (size_t index = 0; index < jobs_.size(); ++index) {
		const JobRecord &record = jobs_[index];
		if (record.occupied && !record.canceled) {
			++count;
		}
	}
	return SchedulerResult<size_t>::success(count);
}

SchedulerResult<void> SchedulerCore::getJobInfo(uint32_t jobId, JobInfo &out) const {
	SchedulerResult<size_t> slotResult = findJobSlot(jobId);
	if (!slotResult.ok()) {
		out = JobInfo{};
		return SchedulerResult<void>::failure(slotResult.error);
	}

	const JobRecord &record = jobs_[slotResult.value];
	out = JobInfo{};
	out.id = record.id;
	out.name = record.name.empty() ? nullptr : record.name.c_str();
	out.paused = record.paused;
	out.running = record.runningCount > 0;
	out.queuedWhileRunning = record.queuedWhileRunning;
	out.dispatch = record.dispatch;
	out.overlap = record.overlap;
	out.executorId = record.executorId;
	out.hasNext = record.hasNext;
	out.nextRunUtc = record.nextRunUtc;
	out.schedule = record.schedule;
	return SchedulerResult<void>::success();
}

void SchedulerCore::dispatchOne(
    size_t slotIndex, const DateTime &nowUtc, IExecutorResolver &executors, bool deferred
) {
	if (slotIndex >= jobs_.size()) {
		return;
	}
	JobRecord &record = jobs_[slotIndex];
	if (!record.occupied || record.canceled || record.paused) {
		return;
	}
	const DateTime currentDue = record.nextRunUtc;

	JobInvocation invocation{};
	invocation.jobId = record.id;
	invocation.generation = record.generation;
	invocation.name = record.name.empty() ? nullptr : record.name.c_str();
	invocation.callback = record.callback;
	invocation.dedicatedTask =
	    record.hasDedicatedTaskOptions ? record.dedicatedTask : DedicatedTaskOptions{};

	if (record.dispatch == DispatchPolicy::Inline) {
		ISchedulerExecutor *executor = executors.inlineExecutor();
		if (!executor) {
			record.hasNext = true;
			record.nextRunUtc = date_.addSeconds(nowUtc, kRetryDelaySeconds);
			if (!pushDue(slotIndex, record)) {
				record.hasNext = false;
			}
			return;
		}
		record.runningCount++;
		if (record.schedule.isOneShot || record.schedule.kind == ScheduleKind::OneShotUtc) {
			record.hasNext = false;
		} else if (computeNextForJob(record, date_.addMinutes(currentDue, 1))) {
			if (!pushDue(slotIndex, record)) {
				record.hasNext = false;
			}
		} else {
			record.hasNext = false;
		}
		if (!executor->submit(invocation)) {
			record.runningCount--;
			record.hasNext = true;
			record.nextRunUtc = date_.addSeconds(nowUtc, kRetryDelaySeconds);
			if (!pushDue(slotIndex, record)) {
				record.hasNext = false;
			}
			return;
		}
		handleCompletion(slotIndex, nowUtc, executors);
		return;
	}

	ISchedulerExecutor *executor = executors.executorFor(record.executorId);
	if (!executor) {
		record.hasNext = true;
		record.nextRunUtc = date_.addSeconds(nowUtc, kRetryDelaySeconds);
		if (!pushDue(slotIndex, record)) {
			record.hasNext = false;
		}
		return;
	}

	if (record.hasDedicatedTaskOptions && record.executorId != 1) {
		record.hasNext = true;
		record.nextRunUtc = date_.addSeconds(nowUtc, kRetryDelaySeconds);
		if (!pushDue(slotIndex, record)) {
			record.hasNext = false;
		}
		return;
	}

	if (!executor->submit(invocation)) {
		record.hasNext = true;
		record.nextRunUtc = date_.addSeconds(nowUtc, kRetryDelaySeconds);
		if (!pushDue(slotIndex, record)) {
			record.hasNext = false;
		}
		return;
	}

	record.runningCount++;
	if (deferred || record.schedule.isOneShot || record.schedule.kind == ScheduleKind::OneShotUtc) {
		record.hasNext = false;
		return;
	}
	if (record.overlap == OverlapPolicy::AllowParallel) {
		if (computeNextForJob(record, date_.addMinutes(currentDue, 1))) {
			if (!pushDue(slotIndex, record)) {
				record.hasNext = false;
			}
		}
		return;
	}
	if (computeNextForJob(record, date_.addMinutes(currentDue, 1))) {
		if (!pushDue(slotIndex, record)) {
			record.hasNext = false;
		}
	}
}

void SchedulerCore::dispatchDeferredIfNeeded(
    size_t slotIndex, const DateTime &nowUtc, IExecutorResolver &executors
) {
	if (slotIndex >= jobs_.size()) {
		return;
	}
	JobRecord &record = jobs_[slotIndex];
	if (!record.occupied || record.canceled || record.paused || !record.queuedWhileRunning ||
	    record.runningCount != 0) {
		return;
	}
	record.queuedWhileRunning = false;
	dispatchOne(slotIndex, nowUtc, executors, true);
}

void SchedulerCore::handleCompletion(
    size_t slotIndex, const DateTime &nowUtc, IExecutorResolver &executors
) {
	if (slotIndex >= jobs_.size()) {
		return;
	}
	JobRecord &record = jobs_[slotIndex];
	if (!record.occupied) {
		return;
	}
	if (record.runningCount > 0) {
		record.runningCount--;
	}

	if (record.canceled) {
		finalizeCanceledIfIdle(slotIndex);
		return;
	}

	if (record.queuedWhileRunning && record.runningCount == 0) {
		dispatchDeferredIfNeeded(slotIndex, nowUtc, executors);
		return;
	}

	if (record.runningCount == 0 && !record.paused && !record.hasNext &&
	    !(record.schedule.isOneShot || record.schedule.kind == ScheduleKind::OneShotUtc)) {
		if (computeNextForJob(record, nowUtc)) {
			if (!pushDue(slotIndex, record)) {
				record.hasNext = false;
			}
		}
	}

	if (record.runningCount == 0 && !record.hasNext &&
	    (record.schedule.isOneShot || record.schedule.kind == ScheduleKind::OneShotUtc)) {
		retireJob(slotIndex);
	}
}

void SchedulerCore::dispatchDue(const DateTime &nowUtc, IExecutorResolver &executors) {
	if (!clockValid(nowUtc)) {
		return;
	}
	for (size_t index = 0; index < jobs_.size(); ++index) {
		JobRecord &record = jobs_[index];
		if (!record.occupied || record.canceled || record.paused || record.runningCount > 0 ||
		    record.hasNext || record.queuedWhileRunning) {
			continue;
		}
		if (computeNextForJob(record, nowUtc)) {
			if (!pushDue(index, record)) {
				record.hasNext = false;
			}
		}
	}
	while (!dueHeap_.empty()) {
		const DueHeapEntry entry = dueHeap_.top();
		if (entry.nextEpoch > nowUtc.epochSeconds) {
			break;
		}
		dueHeap_.pop();
		if (entry.slotIndex >= jobs_.size()) {
			continue;
		}
		JobRecord &record = jobs_[entry.slotIndex];
		if (!record.occupied || record.canceled || record.generation != entry.generation ||
		    !record.hasNext || record.nextRunUtc.epochSeconds != entry.nextEpoch) {
			continue;
		}
		if (record.paused) {
			record.hasNext = false;
			continue;
		}
		if (record.runningCount > 0 && record.overlap != OverlapPolicy::AllowParallel) {
			if (record.overlap == OverlapPolicy::QueueOne) {
				record.queuedWhileRunning = true;
			}
			record.hasNext = false;
			continue;
		}
		dispatchOne(entry.slotIndex, nowUtc, executors, false);
	}
}

void SchedulerCore::handleEvent(
    const SchedulerEvent &event, const DateTime &nowUtc, IExecutorResolver &executors
) {
	if (event.kind != SchedulerEventKind::JobFinished) {
		return;
	}

	for (size_t index = 0; index < jobs_.size(); ++index) {
		JobRecord &record = jobs_[index];
		if (!record.occupied) {
			continue;
		}
		if (record.id == event.jobId && record.generation == event.generation) {
			handleCompletion(index, nowUtc, executors);
			return;
		}
	}
}

bool SchedulerCore::nextDueEpoch(int64_t &outEpochSeconds) const {
	bool found = false;
	int64_t nextEpoch = 0;
	for (size_t index = 0; index < dueHeap_.size(); ++index) {
		const DueHeapEntry &entry = dueHeap_.at(index);
		if (entry.slotIndex >= jobs_.size()) {
			continue;
		}
		const JobRecord &record = jobs_[entry.slotIndex];
		if (!record.occupied || record.canceled || !record.hasNext ||
		    record.generation != entry.generation ||
		    record.nextRunUtc.epochSeconds != entry.nextEpoch) {
			continue;
		}
		if (!found || entry.nextEpoch < nextEpoch) {
			nextEpoch = entry.nextEpoch;
			found = true;
		}
	}
	if (found) {
		outEpochSeconds = nextEpoch;
	}
	return found;
}

size_t SchedulerCore::activeInvocationCount() const {
	size_t count = 0;
	for (size_t index = 0; index < jobs_.size(); ++index) {
		count += jobs_[index].runningCount;
	}
	return count;
}
