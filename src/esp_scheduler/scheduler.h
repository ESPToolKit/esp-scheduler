#pragma once

#include <Arduino.h>
#include <ESPDate.h>

#include <functional>
#include <memory>
#include <string>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "schedule/schedule_calculator.h"
#include "schedule/schedule_spec.h"
#include "scheduler_config.h"
#include "scheduler_result.h"

class ESPWorker;
class ISchedulerExecutor;
struct CallbackRef;

using SchedulerCallbackFn = void (*)(void *userData);
using SchedulerFunction = std::function<void(void *userData)>;
using SchedulerFunctionNoData = std::function<void()>;

struct JobOptions {
	DispatchPolicy dispatch = DispatchPolicy::Inline;
	OverlapPolicy overlap = OverlapPolicy::SkipIfRunning;
	uint8_t executorId = 0;
	bool startPaused = false;
	const char *name = nullptr;
	const DedicatedTaskOptions *dedicatedTask = nullptr;
};

struct JobInfo {
	uint32_t id = 0;
	const char *name = nullptr;
	bool paused = false;
	bool running = false;
	bool queuedWhileRunning = false;
	DispatchPolicy dispatch = DispatchPolicy::Inline;
	OverlapPolicy overlap = OverlapPolicy::SkipIfRunning;
	uint8_t executorId = 0;
	bool hasNext = false;
	DateTime nextRunUtc{};
	ScheduleSpec schedule{};
};

class ESPScheduler {
  public:
	static constexpr int64_t kDefaultMinValidEpochSeconds = 1577836800;
	static constexpr uint8_t kInvalidExecutorId = 0xFF;

	explicit ESPScheduler(ESPDate &date, const SchedulerConfig &config = SchedulerConfig{});
	~ESPScheduler();

	bool begin();
	void end(bool waitForRunningJobs = true, uint32_t timeoutMs = 5000);
	bool running() const;
	bool draining() const;

	SchedulerResult<uint8_t> registerExecutor(ISchedulerExecutor *executor);

	SchedulerResult<uint32_t> addJob(
	    const ScheduleSpec &schedule,
	    const JobOptions &options,
	    SchedulerCallbackFn callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJob(
	    const ScheduleSpec &schedule,
	    const JobOptions &options,
	    SchedulerFunction callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJob(
	    const ScheduleSpec &schedule, const JobOptions &options, SchedulerFunctionNoData callback
	);

	SchedulerResult<uint32_t> addJobOnceUtc(
	    const DateTime &whenUtc,
	    const JobOptions &options,
	    SchedulerCallbackFn callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJobOnceUtc(
	    const DateTime &whenUtc,
	    const JobOptions &options,
	    SchedulerFunction callback,
	    void *userData = nullptr
	);
	SchedulerResult<uint32_t> addJobOnceUtc(
	    const DateTime &whenUtc, const JobOptions &options, SchedulerFunctionNoData callback
	);

	SchedulerResult<void> cancelJob(uint32_t jobId);
	SchedulerResult<void> pauseJob(uint32_t jobId);
	SchedulerResult<void> resumeJob(uint32_t jobId);
	SchedulerResult<void> cancelAll();
	SchedulerResult<void> refreshAllSchedules();

	void tick();
	void tick(const DateTime &nowUtc);

	SchedulerResult<size_t> jobCount() const;
	SchedulerResult<void> getJobInfo(uint32_t jobId, JobInfo &out) const;

	void setMinValidUnixSeconds(int64_t minEpochSeconds);
	void setMinValidUtc(const DateTime &minUtc);
	int64_t minValidUnixSeconds() const;

	bool computeNextOccurrence(
	    const ScheduleSpec &schedule, const DateTime &fromUtc, DateTime &outNextUtc
	) const;

	uint8_t defaultWorkerExecutor() const;
	uint8_t defaultESPWorkerExecutor() const;
	uint8_t defaultDedicatedExecutor() const;

  private:
	struct Impl;

	SchedulerResult<uint32_t> addJobImpl(
	    const ScheduleSpec &schedule,
	    const JobOptions &options,
	    const CallbackRef &callback
	);

	std::unique_ptr<Impl> impl_;
};
