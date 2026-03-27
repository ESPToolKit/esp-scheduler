#pragma once

#include <vector>

#include "../scheduler.h"
#include "../executors/esp_worker_executor.h"

class ESPWorker;

enum class SchedulerJobMode : uint8_t {
	Inline = 0,
	WorkerTask,
};

struct SchedulerTaskConfig {
	const char *name = "sched-job";
	uint32_t stackSize = 4096;
	UBaseType_t priority = 1;
	BaseType_t coreId = tskNO_AFFINITY;
	bool usePsramStack = false;
};

struct ESPSchedulerConfig {
	bool usePSRAMBuffers = false;
};

struct SchedulerV1JobInfo {
	uint32_t id = 0;
	bool enabled = false;
	SchedulerJobMode mode = SchedulerJobMode::Inline;
	ScheduleSpec schedule{};
	DateTime nextRunUtc{};
};

class ESPSchedulerV1Compat {
  public:
	ESPSchedulerV1Compat(ESPDate &date, ESPWorker *worker = nullptr);
	ESPSchedulerV1Compat(ESPDate &date, const ESPSchedulerConfig &config);
	ESPSchedulerV1Compat(ESPDate &date, ESPWorker *worker, const ESPSchedulerConfig &config);

	void deinit();
	bool isInitialized() const;

	void setMinValidUnixSeconds(int64_t minEpochSeconds);
	void setMinValidUtc(const DateTime &minUtc);
	int64_t minValidUnixSeconds() const;

	uint32_t addJobOnceUtc(
	    const DateTime &whenUtc,
	    SchedulerJobMode mode,
	    SchedulerCallbackFn callback,
	    void *userData = nullptr,
	    const SchedulerTaskConfig *taskCfg = nullptr
	);
	uint32_t addJobOnceUtc(
	    const DateTime &whenUtc,
	    SchedulerJobMode mode,
	    SchedulerFunction callback,
	    void *userData = nullptr,
	    const SchedulerTaskConfig *taskCfg = nullptr
	);
	uint32_t addJobOnceUtc(
	    const DateTime &whenUtc,
	    SchedulerJobMode mode,
	    SchedulerFunctionNoData callback,
	    const SchedulerTaskConfig *taskCfg = nullptr
	);

	uint32_t addJob(
	    const ScheduleSpec &schedule,
	    SchedulerJobMode mode,
	    SchedulerCallbackFn callback,
	    void *userData = nullptr,
	    const SchedulerTaskConfig *taskCfg = nullptr
	);
	uint32_t addJob(
	    const ScheduleSpec &schedule,
	    SchedulerJobMode mode,
	    SchedulerFunction callback,
	    void *userData = nullptr,
	    const SchedulerTaskConfig *taskCfg = nullptr
	);
	uint32_t addJob(
	    const ScheduleSpec &schedule,
	    SchedulerJobMode mode,
	    SchedulerFunctionNoData callback,
	    const SchedulerTaskConfig *taskCfg = nullptr
	);

	bool cancelJob(uint32_t jobId);
	bool pauseJob(uint32_t jobId);
	bool resumeJob(uint32_t jobId);
	void cancelAll();

	void tick();
	void tick(const DateTime &nowUtc);
	void cleanup();

	bool computeNextOccurrence(
	    const ScheduleSpec &schedule, const DateTime &fromUtc, DateTime &outNextUtc
	) const;
	bool getJobInfo(size_t index, SchedulerV1JobInfo &out) const;

  private:
	JobOptions jobOptionsForMode(SchedulerJobMode mode, const SchedulerTaskConfig *taskCfg) const;
	void trackJob(uint32_t jobId);
	void pruneTrackedJobs() const;

	mutable std::vector<uint32_t> trackedJobIds_{};
	mutable DedicatedTaskOptions dedicatedTaskScratch_{};
	std::unique_ptr<ESPWorkerExecutorAdapter> workerAdapter_{};
	int workerExecutorId_ = -1;
	ESPScheduler scheduler_;
};
