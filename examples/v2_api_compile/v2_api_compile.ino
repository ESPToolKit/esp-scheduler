#include <Arduino.h>
#include <ESPDate.h>
#include <ESPWorker.h>
#include <ESPScheduler.h>

namespace {
class CompileOnlyExecutor : public ISchedulerExecutor {
  public:
	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override {
		runtime_ = runtime;
		return true;
	}

	void end(bool drainRunningJobs) override {
		(void)drainRunningJobs;
		runtime_.reset();
	}

	bool submit(const JobInvocation &invocation) override {
		(void)invocation;
		return true;
	}

	const char *name() const override {
		return "compile-only";
	}

  private:
	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
};

ESPDate date;
ESPWorker worker;
CompileOnlyExecutor extraExecutor;

void rawCallback(void *userData) {
	(void)userData;
}

SchedulerConfig makeSchedulerConfig() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Manual;
	config.usePSRAMMetadata = true;
	config.defaultAsyncBackend = AsyncExecutorBackend::ESPWorker;
	config.espWorker = &worker;
	config.service.usePsramStack = false;
	config.defaultWorkerPool.usePsramStack = false;
	config.defaultDedicatedTask.usePsramStack = false;
	return config;
}
} // namespace

ESPScheduler scheduler(date, makeSchedulerConfig());

void setup() {
	ESPDateConfig dateConfig{};
	dateConfig.timeZone = "UTC0";
	date.init(dateConfig);

	ESPWorker::Config workerConfig{};
	worker.init(workerConfig);

	SchedulerResult<uint8_t> compileExecutorId = scheduler.registerExecutor(&extraExecutor);
	(void)compileExecutorId;

	scheduler.begin();
	scheduler.setMinValidUnixSeconds(0);

	JobOptions inlineOptions{};
	inlineOptions.name = "inline";
	(void)scheduler.addJob(Schedule::dailyAtLocal(8, 0), inlineOptions, &rawCallback, nullptr);

	JobOptions asyncOptions{};
	asyncOptions.dispatch = DispatchPolicy::Async;
	asyncOptions.executorId = scheduler.defaultESPWorkerExecutor();
	asyncOptions.overlap = OverlapPolicy::QueueOne;
	asyncOptions.name = "esp-worker";
	(void)scheduler.addJobOnceUtc(
	    date.fromUtc(2026, 1, 1, 12, 0, 0),
	    asyncOptions,
	    SchedulerFunction([](void *) {})
	);

	DedicatedTaskOptions dedicated{};
	dedicated.name = "compile-task";
	dedicated.stackSize = 8192;
	dedicated.priority = 2;

	JobOptions dedicatedOptions{};
	dedicatedOptions.dispatch = DispatchPolicy::Async;
	dedicatedOptions.executorId = scheduler.defaultDedicatedExecutor();
	dedicatedOptions.dedicatedTask = &dedicated;
	(void)scheduler.addJob(Schedule::weeklyAtLocal(0b0111110, 18, 30), dedicatedOptions, []() {});

	JobOptions customExecutorOptions{};
	customExecutorOptions.dispatch = DispatchPolicy::Async;
	customExecutorOptions.executorId = compileExecutorId.ok() ? compileExecutorId.value
	                                                         : scheduler.defaultESPWorkerExecutor();
	(void)scheduler.addJob(
	    Schedule::custom(
	        ScheduleField::only(0),
	        ScheduleField::only(9),
	        ScheduleField::any(),
	        ScheduleField::any(),
	        ScheduleField::only(1)
	    ),
	    customExecutorOptions,
	    SchedulerFunction([](void *) {})
	);

	JobInfo info{};
	(void)scheduler.jobCount();
	(void)scheduler.getJobInfo(1, info);
	scheduler.cancelAll();
	scheduler.end(true);
	worker.deinit();
}

void loop() {
}
