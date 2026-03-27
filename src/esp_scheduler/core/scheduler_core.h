#pragma once

#include "../schedule/schedule_calculator.h"
#include "../scheduler_result.h"
#include "../service/scheduler_events.h"
#include "due_heap.h"
#include "job_record.h"

class SchedulerCore {
  public:
	SchedulerCore(ESPDate &date, int64_t minValidEpochSeconds, bool usePSRAMMetadata);

	void setMinValidUnixSeconds(int64_t minEpochSeconds);
	int64_t minValidUnixSeconds() const;
	bool clockValid(const DateTime &nowUtc) const;

	SchedulerResult<uint32_t> addJob(
	    const ScheduleSpec &schedule,
	    const JobOptions &options,
	    const CallbackRef &callback,
	    const DateTime &nowUtc
	);
	SchedulerResult<void> cancelJob(uint32_t jobId);
	SchedulerResult<void> pauseJob(uint32_t jobId);
	SchedulerResult<void> resumeJob(uint32_t jobId, const DateTime &nowUtc);
	SchedulerResult<void> cancelAll();
	SchedulerResult<size_t> jobCount() const;
	SchedulerResult<void> getJobInfo(uint32_t jobId, JobInfo &out) const;

	void dispatchDue(const DateTime &nowUtc, IExecutorResolver &executors);
	void handleEvent(
	    const SchedulerEvent &event, const DateTime &nowUtc, IExecutorResolver &executors
	);

	bool nextDueEpoch(int64_t &outEpochSeconds);
	size_t activeInvocationCount() const;

  private:
	SchedulerResult<size_t> findJobSlot(uint32_t jobId) const;
	bool computeNextForJob(JobRecord &record, const DateTime &fromUtc);
	bool pushDue(size_t slotIndex, const JobRecord &record);
	bool queueScheduling(size_t slotIndex, const DateTime &fromUtc);
	void clearScheduling(size_t slotIndex);
	void drainPendingSchedules(const DateTime &nowUtc);
	bool validateDueEntry(const DueHeapEntry &entry) const;
	void pruneInvalidDueEntries();
	void retireJob(size_t slotIndex);
	void finalizeCanceledIfIdle(size_t slotIndex);
	void dispatchDeferredIfNeeded(
	    size_t slotIndex, const DateTime &nowUtc, IExecutorResolver &executors
	);
	void dispatchOne(
	    size_t slotIndex, const DateTime &nowUtc, IExecutorResolver &executors, bool deferred
	);
	void handleCompletion(
	    size_t slotIndex, const DateTime &nowUtc, IExecutorResolver &executors
	);

	ESPDate &date_;
	int64_t minValidEpochSeconds_ = 0;
	bool usePSRAMMetadata_ = false;
	uint32_t nextId_ = 1;
	SchedulerArray<JobRecord> jobs_{};
	SchedulerArray<size_t> freeSlots_{};
	SchedulerArray<size_t> pendingSchedules_{};
	SchedulerIdIndex jobIndex_{};
	DueHeap dueHeap_{};
};
