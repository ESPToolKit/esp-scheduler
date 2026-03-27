#include "scheduler.h"

#include <new>
#include <utility>
#include <vector>

#include "core/scheduler_core.h"
#include "executors/dedicated_task_executor.h"
#include "executors/worker_pool_executor.h"
#include "service/scheduler_commands.h"
#include "service/scheduler_service.h"

extern "C" {
#include "freertos/queue.h"
}

struct ESPScheduler::Impl : public IExecutorResolver {
	explicit Impl(ESPDate &date, const SchedulerConfig &config)
	    : date(date), config(config), manualCore(date, config.minValidEpochSeconds) {
	}

	~Impl() {
		if (eventQueue) {
			vQueueDelete(eventQueue);
			eventQueue = nullptr;
		}
	}

	ISchedulerExecutor *executorFor(uint8_t executorId) override {
		if (executorId >= executors.size()) {
			return nullptr;
		}
		return executors[executorId];
	}

	bool startExecutors() {
		workerPool.reset(new (std::nothrow) WorkerPoolExecutor(config.defaultWorkerPool));
		dedicatedTask.reset(new (std::nothrow) DedicatedTaskExecutor());
		if (!workerPool || !dedicatedTask) {
			return false;
		}

		executors.clear();
		executors.push_back(workerPool.get());
		executors.push_back(dedicatedTask.get());
		for (ISchedulerExecutor *executor : externalExecutors) {
			executors.push_back(executor);
		}

		for (ISchedulerExecutor *executor : executors) {
			if (!executor || !executor->begin(runtime)) {
				return false;
			}
		}
		return true;
	}

	void stopExecutors(bool drainRunningJobs) {
		for (ISchedulerExecutor *executor : executors) {
			if (executor) {
				executor->end(drainRunningJobs);
			}
		}
		executors.clear();
		dedicatedTask.reset();
		workerPool.reset();
	}

	void drainManualEvents(const DateTime &nowUtc) {
		if (!eventQueue) {
			return;
		}
		while (true) {
			SchedulerEvent event{};
			if (xQueueReceive(eventQueue, &event, 0) != pdTRUE) {
				break;
			}
			manualCore.handleEvent(event, nowUtc, *this);
		}
	}

	ESPDate &date;
	SchedulerConfig config{};
	SchedulerCore manualCore;
	std::unique_ptr<SchedulerService> service{};
	std::unique_ptr<WorkerPoolExecutor> workerPool{};
	std::unique_ptr<DedicatedTaskExecutor> dedicatedTask{};
	std::vector<ISchedulerExecutor *> externalExecutors{};
	std::vector<ISchedulerExecutor *> executors{};
	std::shared_ptr<SchedulerExecutorRuntime> runtime{};
	QueueHandle_t eventQueue = nullptr;
	bool started = false;
	bool draining = false;
};

ESPScheduler::ESPScheduler(ESPDate &date, const SchedulerConfig &config)
    : impl_(new (std::nothrow) Impl(date, config)) {
}

ESPScheduler::~ESPScheduler() {
	end(true);
}

bool ESPScheduler::begin() {
	if (!impl_) {
		return false;
	}
	if (impl_->started) {
		return true;
	}

	impl_->runtime = std::make_shared<SchedulerExecutorRuntime>();
	if (!impl_->runtime) {
		return false;
	}

	if (impl_->config.mode == SchedulerMode::Background) {
		impl_->service.reset(new (std::nothrow) SchedulerService(
		    impl_->date,
		    impl_->config.service,
		    impl_->config.minValidEpochSeconds,
		    *impl_
		));
		if (!impl_->service || !impl_->service->begin()) {
			impl_->service.reset();
			impl_->runtime.reset();
			return false;
		}
		impl_->runtime->eventQueue = impl_->service->eventQueue();
	} else {
		impl_->eventQueue = xQueueCreate(
		    impl_->config.service.eventQueueDepth,
		    sizeof(SchedulerEvent)
		);
		if (!impl_->eventQueue) {
			impl_->runtime.reset();
			return false;
		}
		impl_->runtime->eventQueue = impl_->eventQueue;
	}

	impl_->started = true;
	if (!impl_->startExecutors()) {
		end(false);
		return false;
	}

	impl_->draining = false;
	return true;
}

void ESPScheduler::end(bool waitForRunningJobs, uint32_t timeoutMs) {
	if (!impl_ || !impl_->started) {
		return;
	}

	impl_->draining = true;

	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		CancelAllCommand cancelCommand;
		if (impl_->service->send(cancelCommand)) {
			cancelCommand.wait(impl_->config.service.controlTimeoutMs);
		}

		if (waitForRunningJobs) {
			const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
			while (impl_->service->activeInvocationCount() > 0 &&
			       xTaskGetTickCount() < deadline) {
				vTaskDelay(pdMS_TO_TICKS(10));
			}
		}

		if (impl_->runtime) {
			impl_->runtime->accepting.store(waitForRunningJobs);
			if (!waitForRunningJobs) {
				impl_->runtime->eventQueue = nullptr;
				impl_->runtime->accepting.store(false);
			}
		}
		impl_->stopExecutors(waitForRunningJobs);
		impl_->service->stop();
		impl_->service.reset();
	} else {
		impl_->manualCore.cancelAll();
		const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
		while (waitForRunningJobs && impl_->manualCore.activeInvocationCount() > 0 &&
		       xTaskGetTickCount() < deadline) {
			impl_->drainManualEvents(impl_->date.now());
			vTaskDelay(pdMS_TO_TICKS(10));
		}
		if (impl_->runtime) {
			if (!waitForRunningJobs) {
				impl_->runtime->eventQueue = nullptr;
				impl_->runtime->accepting.store(false);
			}
		}
		impl_->stopExecutors(waitForRunningJobs);
		if (impl_->eventQueue) {
			vQueueDelete(impl_->eventQueue);
			impl_->eventQueue = nullptr;
		}
	}

	if (impl_->runtime) {
		impl_->runtime->eventQueue = nullptr;
		impl_->runtime->accepting.store(false);
		impl_->runtime.reset();
	}
	impl_->started = false;
	impl_->draining = false;
}

bool ESPScheduler::running() const {
	return impl_ && impl_->started && !impl_->draining;
}

bool ESPScheduler::draining() const {
	return impl_ && impl_->draining;
}

SchedulerResult<uint8_t> ESPScheduler::registerExecutor(ISchedulerExecutor *executor) {
	if (!impl_ || !executor) {
		return SchedulerResult<uint8_t>::failure(SchedulerError::ExecutorUnavailable);
	}
	if (impl_->started) {
		return SchedulerResult<uint8_t>::failure(SchedulerError::Busy);
	}
	const uint8_t executorId = static_cast<uint8_t>(2 + impl_->externalExecutors.size());
	impl_->externalExecutors.push_back(executor);
	return SchedulerResult<uint8_t>::success(executorId);
}

SchedulerResult<uint32_t> ESPScheduler::addJob(
    const ScheduleSpec &schedule,
    const JobOptions &options,
    SchedulerCallbackFn callback,
    void *userData
) {
	if (!callback) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}
	CallbackRef ref{};
	ref.kind = CallbackKind::RawFunction;
	ref.rawFn = callback;
	ref.userData = userData;
	return addJobImpl(schedule, options, ref);
}

SchedulerResult<uint32_t> ESPScheduler::addJob(
    const ScheduleSpec &schedule,
    const JobOptions &options,
    SchedulerFunction callback,
    void *userData
) {
	if (!callback) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}
	CallbackRef ref{};
	ref.kind = CallbackKind::OwningFunction;
	ref.userData = userData;
	ref.owningFn = std::make_shared<SchedulerFunction>(std::move(callback));
	if (!ref.owningFn) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
	}
	return addJobImpl(schedule, options, ref);
}

SchedulerResult<uint32_t> ESPScheduler::addJob(
    const ScheduleSpec &schedule, const JobOptions &options, SchedulerFunctionNoData callback
) {
	if (!callback) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::InvalidSchedule);
	}
	SchedulerFunction wrapped = [fn = std::move(callback)](void *) { fn(); };
	CallbackRef ref{};
	ref.kind = CallbackKind::OwningFunction;
	ref.owningFn = std::make_shared<SchedulerFunction>(std::move(wrapped));
	if (!ref.owningFn) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::NoMemory);
	}
	return addJobImpl(schedule, options, ref);
}

SchedulerResult<uint32_t> ESPScheduler::addJobOnceUtc(
    const DateTime &whenUtc,
    const JobOptions &options,
    SchedulerCallbackFn callback,
    void *userData
) {
	return addJob(ScheduleSpec::onceUtc(whenUtc), options, callback, userData);
}

SchedulerResult<uint32_t> ESPScheduler::addJobOnceUtc(
    const DateTime &whenUtc,
    const JobOptions &options,
    SchedulerFunction callback,
    void *userData
) {
	return addJob(ScheduleSpec::onceUtc(whenUtc), options, std::move(callback), userData);
}

SchedulerResult<uint32_t> ESPScheduler::addJobOnceUtc(
    const DateTime &whenUtc, const JobOptions &options, SchedulerFunctionNoData callback
) {
	return addJob(ScheduleSpec::onceUtc(whenUtc), options, std::move(callback));
}

SchedulerResult<uint32_t> ESPScheduler::addJobImpl(
    const ScheduleSpec &schedule, const JobOptions &options, const CallbackRef &callback
) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::NotInitialized);
	}
	if (options.dispatch == DispatchPolicy::Async && impl_->executorFor(options.executorId) == nullptr) {
		return SchedulerResult<uint32_t>::failure(SchedulerError::ExecutorUnavailable);
	}

	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		AddJobCommand command;
		command.schedule = schedule;
		command.options = options;
		if (options.dedicatedTask) {
			command.dedicatedTaskCopy = *options.dedicatedTask;
			command.options.dedicatedTask = &command.dedicatedTaskCopy;
		}
		command.callback = callback;
		if (!impl_->service->send(command)) {
			return SchedulerResult<uint32_t>::failure(SchedulerError::QueueFull);
		}
		if (!command.wait(impl_->config.service.controlTimeoutMs)) {
			return SchedulerResult<uint32_t>::failure(SchedulerError::Timeout);
		}
		return command.result;
	}

	return impl_->manualCore.addJob(schedule, options, callback, impl_->date.now());
}

SchedulerResult<void> ESPScheduler::cancelJob(uint32_t jobId) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		CancelJobCommand command;
		command.jobId = jobId;
		if (!impl_->service->send(command)) {
			return SchedulerResult<void>::failure(SchedulerError::QueueFull);
		}
		if (!command.wait(impl_->config.service.controlTimeoutMs)) {
			return SchedulerResult<void>::failure(SchedulerError::Timeout);
		}
		return command.result;
	}
	return impl_->manualCore.cancelJob(jobId);
}

SchedulerResult<void> ESPScheduler::pauseJob(uint32_t jobId) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		PauseJobCommand command;
		command.jobId = jobId;
		if (!impl_->service->send(command)) {
			return SchedulerResult<void>::failure(SchedulerError::QueueFull);
		}
		if (!command.wait(impl_->config.service.controlTimeoutMs)) {
			return SchedulerResult<void>::failure(SchedulerError::Timeout);
		}
		return command.result;
	}
	return impl_->manualCore.pauseJob(jobId);
}

SchedulerResult<void> ESPScheduler::resumeJob(uint32_t jobId) {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		ResumeJobCommand command;
		command.jobId = jobId;
		if (!impl_->service->send(command)) {
			return SchedulerResult<void>::failure(SchedulerError::QueueFull);
		}
		if (!command.wait(impl_->config.service.controlTimeoutMs)) {
			return SchedulerResult<void>::failure(SchedulerError::Timeout);
		}
		return command.result;
	}
	return impl_->manualCore.resumeJob(jobId, impl_->date.now());
}

SchedulerResult<void> ESPScheduler::cancelAll() {
	if (!impl_ || !impl_->started || impl_->draining) {
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		CancelAllCommand command;
		if (!impl_->service->send(command)) {
			return SchedulerResult<void>::failure(SchedulerError::QueueFull);
		}
		if (!command.wait(impl_->config.service.controlTimeoutMs)) {
			return SchedulerResult<void>::failure(SchedulerError::Timeout);
		}
		return command.result;
	}
	return impl_->manualCore.cancelAll();
}

void ESPScheduler::tick() {
	if (!impl_) {
		return;
	}
	tick(impl_->date.now());
}

void ESPScheduler::tick(const DateTime &nowUtc) {
	if (!impl_ || !impl_->started || impl_->config.mode == SchedulerMode::Background) {
		return;
	}
	impl_->drainManualEvents(nowUtc);
	impl_->manualCore.dispatchDue(nowUtc, *impl_);
	impl_->drainManualEvents(nowUtc);
}

SchedulerResult<size_t> ESPScheduler::jobCount() const {
	if (!impl_ || !impl_->started) {
		return SchedulerResult<size_t>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		JobCountCommand command;
		if (!impl_->service->send(command)) {
			return SchedulerResult<size_t>::failure(SchedulerError::QueueFull);
		}
		if (!command.wait(impl_->config.service.controlTimeoutMs)) {
			return SchedulerResult<size_t>::failure(SchedulerError::Timeout);
		}
		return command.result;
	}
	return impl_->manualCore.jobCount();
}

SchedulerResult<void> ESPScheduler::getJobInfo(uint32_t jobId, JobInfo &out) const {
	if (!impl_ || !impl_->started) {
		out = JobInfo{};
		return SchedulerResult<void>::failure(SchedulerError::NotInitialized);
	}
	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		GetJobInfoCommand command;
		command.jobId = jobId;
		command.info = &out;
		if (!impl_->service->send(command)) {
			out = JobInfo{};
			return SchedulerResult<void>::failure(SchedulerError::QueueFull);
		}
		if (!command.wait(impl_->config.service.controlTimeoutMs)) {
			out = JobInfo{};
			return SchedulerResult<void>::failure(SchedulerError::Timeout);
		}
		return command.result;
	}
	return impl_->manualCore.getJobInfo(jobId, out);
}

void ESPScheduler::setMinValidUnixSeconds(int64_t minEpochSeconds) {
	if (!impl_) {
		return;
	}
	impl_->config.minValidEpochSeconds = minEpochSeconds;
	if (!impl_->started) {
		impl_->manualCore.setMinValidUnixSeconds(minEpochSeconds);
		return;
	}
	if (impl_->config.mode == SchedulerMode::Background && impl_->service) {
		SetMinValidCommand command;
		command.minEpochSeconds = minEpochSeconds;
		if (impl_->service->send(command)) {
			command.wait(impl_->config.service.controlTimeoutMs);
		}
		return;
	}
	impl_->manualCore.setMinValidUnixSeconds(minEpochSeconds);
}

void ESPScheduler::setMinValidUtc(const DateTime &minUtc) {
	setMinValidUnixSeconds(minUtc.epochSeconds);
}

int64_t ESPScheduler::minValidUnixSeconds() const {
	if (!impl_) {
		return kDefaultMinValidEpochSeconds;
	}
	return impl_->config.minValidEpochSeconds;
}

bool ESPScheduler::computeNextOccurrence(
    const ScheduleSpec &schedule, const DateTime &fromUtc, DateTime &outNextUtc
) const {
	if (!impl_) {
		return false;
	}
	return ScheduleCalculator::computeNext(impl_->date, schedule, fromUtc, outNextUtc);
}
