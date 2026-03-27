#include "v1_compat.h"

#include <ESPWorker.h>

namespace {
SchedulerConfig makeCompatSchedulerConfig(const ESPSchedulerConfig &config) {
	SchedulerConfig v2Config{};
	v2Config.usePSRAMMetadata = config.usePSRAMBuffers;
	return v2Config;
}

DedicatedTaskOptions makeDedicatedOptions(const SchedulerTaskConfig *taskCfg) {
	DedicatedTaskOptions options{};
	if (!taskCfg) {
		return options;
	}
	options.name = taskCfg->name;
	options.stackSize = taskCfg->stackSize;
	options.priority = taskCfg->priority;
	options.coreId = taskCfg->coreId;
	options.usePsramStack = taskCfg->usePsramStack;
	return options;
}
} // namespace

ESPSchedulerV1Compat::ESPSchedulerV1Compat(ESPDate &date, ESPWorker *worker)
    : ESPSchedulerV1Compat(date, worker, ESPSchedulerConfig{}) {
}

ESPSchedulerV1Compat::ESPSchedulerV1Compat(ESPDate &date, const ESPSchedulerConfig &config)
    : ESPSchedulerV1Compat(date, nullptr, config) {
}

ESPSchedulerV1Compat::ESPSchedulerV1Compat(
    ESPDate &date, ESPWorker *worker, const ESPSchedulerConfig &config
)
    : scheduler_(date, makeCompatSchedulerConfig(config)) {
	if (worker) {
		workerAdapter_.reset(new ESPWorkerExecutorAdapter(*worker));
		if (workerAdapter_) {
			SchedulerResult<uint8_t> registered = scheduler_.registerExecutor(workerAdapter_.get());
			if (registered.ok()) {
				workerExecutorId_ = registered.value;
			}
		}
	}
	scheduler_.begin();
}

void ESPSchedulerV1Compat::deinit() {
	scheduler_.end(true);
	trackedJobIds_.clear();
}

bool ESPSchedulerV1Compat::isInitialized() const {
	return scheduler_.running();
}

void ESPSchedulerV1Compat::setMinValidUnixSeconds(int64_t minEpochSeconds) {
	scheduler_.setMinValidUnixSeconds(minEpochSeconds);
}

void ESPSchedulerV1Compat::setMinValidUtc(const DateTime &minUtc) {
	scheduler_.setMinValidUtc(minUtc);
}

int64_t ESPSchedulerV1Compat::minValidUnixSeconds() const {
	return scheduler_.minValidUnixSeconds();
}

JobOptions ESPSchedulerV1Compat::jobOptionsForMode(
    SchedulerJobMode mode, const SchedulerTaskConfig *taskCfg
) const {
	JobOptions options{};
	if (mode == SchedulerJobMode::Inline) {
		options.dispatch = DispatchPolicy::Inline;
		return options;
	}

	options.dispatch = DispatchPolicy::Async;
	if (taskCfg) {
		dedicatedTaskScratch_ = makeDedicatedOptions(taskCfg);
		options.executorId = scheduler_.defaultDedicatedExecutor();
		options.dedicatedTask = &dedicatedTaskScratch_;
	} else if (workerExecutorId_ >= 0) {
		options.executorId = static_cast<uint8_t>(workerExecutorId_);
	} else {
		options.executorId = scheduler_.defaultWorkerExecutor();
	}
	return options;
}

void ESPSchedulerV1Compat::trackJob(uint32_t jobId) {
	if (jobId != 0) {
		trackedJobIds_.push_back(jobId);
	}
}

void ESPSchedulerV1Compat::pruneTrackedJobs() const {
	std::vector<uint32_t> active{};
	for (uint32_t jobId : trackedJobIds_) {
		JobInfo info{};
		if (scheduler_.getJobInfo(jobId, info).ok()) {
			active.push_back(jobId);
		}
	}
	trackedJobIds_ = active;
}

uint32_t ESPSchedulerV1Compat::addJobOnceUtc(
    const DateTime &whenUtc,
    SchedulerJobMode mode,
    SchedulerCallbackFn callback,
    void *userData,
    const SchedulerTaskConfig *taskCfg
) {
	JobOptions options = jobOptionsForMode(mode, taskCfg);
	SchedulerResult<uint32_t> result = scheduler_.addJobOnceUtc(whenUtc, options, callback, userData);
	trackJob(result.ok() ? result.value : 0);
	return result.ok() ? result.value : 0;
}

uint32_t ESPSchedulerV1Compat::addJobOnceUtc(
    const DateTime &whenUtc,
    SchedulerJobMode mode,
    SchedulerFunction callback,
    void *userData,
    const SchedulerTaskConfig *taskCfg
) {
	JobOptions options = jobOptionsForMode(mode, taskCfg);
	SchedulerResult<uint32_t> result =
	    scheduler_.addJobOnceUtc(whenUtc, options, std::move(callback), userData);
	trackJob(result.ok() ? result.value : 0);
	return result.ok() ? result.value : 0;
}

uint32_t ESPSchedulerV1Compat::addJobOnceUtc(
    const DateTime &whenUtc,
    SchedulerJobMode mode,
    SchedulerFunctionNoData callback,
    const SchedulerTaskConfig *taskCfg
) {
	JobOptions options = jobOptionsForMode(mode, taskCfg);
	SchedulerResult<uint32_t> result =
	    scheduler_.addJobOnceUtc(whenUtc, options, std::move(callback));
	trackJob(result.ok() ? result.value : 0);
	return result.ok() ? result.value : 0;
}

uint32_t ESPSchedulerV1Compat::addJob(
    const ScheduleSpec &schedule,
    SchedulerJobMode mode,
    SchedulerCallbackFn callback,
    void *userData,
    const SchedulerTaskConfig *taskCfg
) {
	JobOptions options = jobOptionsForMode(mode, taskCfg);
	SchedulerResult<uint32_t> result = scheduler_.addJob(schedule, options, callback, userData);
	trackJob(result.ok() ? result.value : 0);
	return result.ok() ? result.value : 0;
}

uint32_t ESPSchedulerV1Compat::addJob(
    const ScheduleSpec &schedule,
    SchedulerJobMode mode,
    SchedulerFunction callback,
    void *userData,
    const SchedulerTaskConfig *taskCfg
) {
	JobOptions options = jobOptionsForMode(mode, taskCfg);
	SchedulerResult<uint32_t> result = scheduler_.addJob(schedule, options, std::move(callback), userData);
	trackJob(result.ok() ? result.value : 0);
	return result.ok() ? result.value : 0;
}

uint32_t ESPSchedulerV1Compat::addJob(
    const ScheduleSpec &schedule,
    SchedulerJobMode mode,
    SchedulerFunctionNoData callback,
    const SchedulerTaskConfig *taskCfg
) {
	JobOptions options = jobOptionsForMode(mode, taskCfg);
	SchedulerResult<uint32_t> result = scheduler_.addJob(schedule, options, std::move(callback));
	trackJob(result.ok() ? result.value : 0);
	return result.ok() ? result.value : 0;
}

bool ESPSchedulerV1Compat::cancelJob(uint32_t jobId) {
	return scheduler_.cancelJob(jobId).ok();
}

bool ESPSchedulerV1Compat::pauseJob(uint32_t jobId) {
	return scheduler_.pauseJob(jobId).ok();
}

bool ESPSchedulerV1Compat::resumeJob(uint32_t jobId) {
	return scheduler_.resumeJob(jobId).ok();
}

void ESPSchedulerV1Compat::cancelAll() {
	scheduler_.cancelAll();
	trackedJobIds_.clear();
}

void ESPSchedulerV1Compat::tick() {
	scheduler_.tick();
}

void ESPSchedulerV1Compat::tick(const DateTime &nowUtc) {
	scheduler_.tick(nowUtc);
}

void ESPSchedulerV1Compat::cleanup() {
	pruneTrackedJobs();
}

bool ESPSchedulerV1Compat::computeNextOccurrence(
    const ScheduleSpec &schedule, const DateTime &fromUtc, DateTime &outNextUtc
) const {
	return scheduler_.computeNextOccurrence(schedule, fromUtc, outNextUtc);
}

bool ESPSchedulerV1Compat::getJobInfo(size_t index, SchedulerV1JobInfo &out) const {
	pruneTrackedJobs();
	size_t current = 0;
	for (uint32_t jobId : trackedJobIds_) {
		JobInfo info{};
		if (!scheduler_.getJobInfo(jobId, info).ok()) {
			continue;
		}
		if (current++ != index) {
			continue;
		}
		out = SchedulerV1JobInfo{};
		out.id = info.id;
		out.enabled = !info.paused;
		out.mode = info.dispatch == DispatchPolicy::Inline ? SchedulerJobMode::Inline
		                                                  : SchedulerJobMode::WorkerTask;
		out.schedule = info.schedule;
		out.nextRunUtc = info.nextRunUtc;
		return true;
	}
	out = SchedulerV1JobInfo{};
	return false;
}
