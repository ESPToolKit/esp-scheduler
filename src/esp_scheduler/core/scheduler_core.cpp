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
      pendingSchedules_(usePSRAMMetadata),
      jobIndex_(usePSRAMMetadata),
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
	size_t slotIndex = 0;
	if (!jobIndex_.get(jobId, slotIndex) || slotIndex >= jobs_.size()) {
		return SchedulerResult<size_t>::failure(SchedulerError::NotFound);
	}
	const JobRecord &record = jobs_[slotIndex];
	if (!record.occupied || record.canceled || record.id != jobId) {
		return SchedulerResult<size_t>::failure(SchedulerError::NotFound);
	}
	return SchedulerResult<size_t>::success(slotIndex);
}

bool SchedulerCore::computeNextForJob(JobRecord &record, const DateTime &fromUtc) {
	if (record.canceled || record.paused) {
		record.hasNext = false;
		return false;
	}
	record.refreshPending = false;
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

bool SchedulerCore::queueScheduling(size_t slotIndex, const DateTime &fromUtc) {
	if (slotIndex >= jobs_.size()) {
		return false;
	}
	JobRecord &record = jobs_[slotIndex];
	if (!record.occupied || record.canceled || record.paused) {
		record.pendingSchedule = false;
		record.hasNext = false;
		record.refreshPending = false;
		return true;
	}
	record.scheduleFromUtc = fromUtc;
	record.hasNext = false;
	if (record.pendingSchedule) {
		return true;
	}
	record.pendingSchedule = true;
	if (!pendingSchedules_.pushBack(slotIndex)) {
		record.pendingSchedule = false;
		return false;
	}
	return true;
}

void SchedulerCore::clearScheduling(size_t slotIndex) {
	if (slotIndex >= jobs_.size()) {
		return;
	}
	JobRecord &record = jobs_[slotIndex];
	record.pendingSchedule = false;
	record.hasNext = false;
	record.refreshPending = false;
}

void SchedulerCore::drainPendingSchedules(const DateTime &nowUtc) {
	if (!clockValid(nowUtc)) {
		return;
	}
	while (!pendingSchedules_.empty()) {
		const size_t slotIndex = pendingSchedules_[pendingSchedules_.size() - 1];
		pendingSchedules_.popBack();
		if (slotIndex >= jobs_.size()) {
			continue;
		}
		JobRecord &record = jobs_[slotIndex];
		record.pendingSchedule = false;
		if (!record.occupied || record.canceled || record.paused || record.runningCount > 0) {
			record.hasNext = false;
			continue;
		}
		if (computeNextForJob(record, record.scheduleFromUtc) && !pushDue(slotIndex, record)) {
			record.hasNext = false;
		}
	}
}

bool SchedulerCore::validateDueEntry(const DueHeapEntry &entry) const {
	if (entry.slotIndex >= jobs_.size()) {
		return false;
	}
	const JobRecord &record = jobs_[entry.slotIndex];
	return record.occupied && !record.canceled && record.hasNext &&
	       record.generation == entry.generation &&
	       record.nextRunUtc.epochSeconds == entry.nextEpoch;
}

void SchedulerCore::pruneInvalidDueEntries() {
	while (!dueHeap_.empty() && !validateDueEntry(dueHeap_.top())) {
		dueHeap_.pop();
	}
}

void SchedulerCore::retireJob(size_t slotIndex) {
	if (slotIndex >= jobs_.size()) {
		return;
	}
	JobRecord &record = jobs_[slotIndex];
	if (record.id != 0) {
		jobIndex_.remove(record.id);
	}
	record.occupied = false;
	record.canceled = false;
	record.paused = false;
	record.queuedWhileRunning = false;
	record.hasNext = false;
	record.pendingSchedule = false;
	record.refreshPending = false;
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
	const bool shouldPrimeImmediately = !record.paused && clockValid(nowUtc);
	if (shouldPrimeImmediately) {
		computeNextForJob(record, nowUtc);
	}

	size_t slotIndex = 0;
	bool reusedSlot = false;
	uint32_t reusedGeneration = 0;
	if (!freeSlots_.empty()) {
		slotIndex = freeSlots_[freeSlots_.size() - 1];
		freeSlots_.popBack();
		reusedGeneration = jobs_[slotIndex].generation;
		record.generation = reusedGeneration;
		jobs_[slotIndex] = std::move(record);
		reusedSlot = true;
	} else {
		slotIndex = jobs_.size();
		if (!jobs_.pushBack(std::move(record))) {
			return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
		}
	}

	if (!jobIndex_.set(jobs_[slotIndex].id, slotIndex)) {
		jobIndex_.remove(jobs_[slotIndex].id);
		if (reusedSlot) {
			JobRecord empty(usePSRAMMetadata_);
			empty.generation = reusedGeneration;
			jobs_[slotIndex] = std::move(empty);
			freeSlots_.pushBack(slotIndex);
		} else {
			jobs_.popBack();
		}
		return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
	}

	if (!jobs_[slotIndex].paused) {
		if (shouldPrimeImmediately) {
			if (jobs_[slotIndex].hasNext && !pushDue(slotIndex, jobs_[slotIndex])) {
				jobs_[slotIndex].hasNext = false;
			}
		} else if (!queueScheduling(slotIndex, nowUtc)) {
			jobIndex_.remove(jobs_[slotIndex].id);
			if (reusedSlot) {
				JobRecord empty(usePSRAMMetadata_);
				empty.generation = reusedGeneration;
				jobs_[slotIndex] = std::move(empty);
				freeSlots_.pushBack(slotIndex);
			} else {
				jobs_.popBack();
			}
			return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
		}
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
	clearScheduling(slotResult.value);
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
	clearScheduling(slotResult.value);
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
	if (record.runningCount == 0 && !queueScheduling(slotResult.value, nowUtc)) {
		return SchedulerResult<void>::failure(SchedulerError::NoMemory);
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
		clearScheduling(index);
		finalizeCanceledIfIdle(index);
	}
	return SchedulerResult<void>::success();
}

SchedulerResult<void> SchedulerCore::refreshAllSchedules(const DateTime &nowUtc) {
	for (size_t index = 0; index < jobs_.size(); ++index) {
		JobRecord &record = jobs_[index];
		if (!record.occupied || record.canceled || record.paused) {
			continue;
		}
		if (record.schedule.isOneShot || record.schedule.kind == ScheduleKind::OneShotUtc) {
			continue;
		}

		record.pendingSchedule = false;
		if (record.runningCount > 0 && record.overlap != OverlapPolicy::AllowParallel) {
			record.refreshPending = true;
			record.queuedWhileRunning = false;
			record.hasNext = false;
			record.nextRunUtc = DateTime{};
			continue;
		}

		record.refreshPending = false;
		record.hasNext = false;
		record.nextRunUtc = DateTime{};
		if (computeNextForJob(record, nowUtc) && !pushDue(index, record)) {
			record.hasNext = false;
		}
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
	invocation.slotIndex = slotIndex;
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

	if (record.refreshPending && record.runningCount == 0) {
		record.refreshPending = false;
		record.queuedWhileRunning = false;
		record.hasNext = false;
		(void)queueScheduling(slotIndex, nowUtc);
		return;
	}

	if (record.queuedWhileRunning && record.runningCount == 0) {
		dispatchDeferredIfNeeded(slotIndex, nowUtc, executors);
		return;
	}

	if (record.runningCount == 0 && !record.paused && !record.hasNext &&
	    !(record.schedule.isOneShot || record.schedule.kind == ScheduleKind::OneShotUtc)) {
		(void)queueScheduling(slotIndex, nowUtc);
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
	drainPendingSchedules(nowUtc);
	pruneInvalidDueEntries();
	while (!dueHeap_.empty()) {
		pruneInvalidDueEntries();
		if (dueHeap_.empty()) {
			break;
		}
		const DueHeapEntry entry = dueHeap_.top();
		if (entry.nextEpoch > nowUtc.epochSeconds) {
			break;
		}
		dueHeap_.pop();
		JobRecord &record = jobs_[entry.slotIndex];
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
	if (event.slotIndex >= jobs_.size()) {
		return;
	}
	const JobRecord &record = jobs_[event.slotIndex];
	if (!record.occupied || record.id != event.jobId || record.generation != event.generation) {
		return;
	}
	handleCompletion(event.slotIndex, nowUtc, executors);
}

bool SchedulerCore::nextDueEpoch(int64_t &outEpochSeconds) {
	pruneInvalidDueEntries();
	if (dueHeap_.empty()) {
		return false;
	}
	outEpochSeconds = dueHeap_.top().nextEpoch;
	return true;
}

size_t SchedulerCore::activeInvocationCount() const {
	size_t count = 0;
	for (size_t index = 0; index < jobs_.size(); ++index) {
		count += jobs_[index].runningCount;
	}
	return count;
}
