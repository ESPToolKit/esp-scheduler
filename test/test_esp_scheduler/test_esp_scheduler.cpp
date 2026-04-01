#include <Arduino.h>
#include <ESPDate.h>
#include <ESPWorker.h>
#include <ESPScheduler.h>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <time.h>
#include <unity.h>

#include "esp_scheduler/executors/scheduler_executor.h"
#include "esp_scheduler/service/scheduler_events.h"

ESPDate date;

SchedulerConfig manualConfig() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Manual;
	config.service.eventQueueDepth = 16;
	return config;
}

ESPScheduler scheduler(date, manualConfig());

static int inlineHits = 0;
static int asyncHits = 0;
static int slowHits = 0;

static void inlineCallback(void *userData) {
	(void)userData;
	inlineHits++;
}

static void asyncCallback(void *userData) {
	(void)userData;
	asyncHits++;
}

static void slowCallback(void *userData) {
	(void)userData;
	slowHits++;
	delay(250);
}

static double circularDistanceDegrees(double a, double b) {
	double delta = std::fmod(std::fabs(a - b), 360.0);
	if (delta > 180.0) {
		delta = 360.0 - delta;
	}
	return delta;
}

class TestQueueExecutor : public ISchedulerExecutor {
  public:
	bool begin(const std::shared_ptr<SchedulerExecutorRuntime> &runtime) override {
		runtime_ = runtime;
		count_ = 0;
		return true;
	}

	void end(bool drainRunningJobs) override {
		(void)drainRunningJobs;
		runtime_.reset();
		count_ = 0;
	}

	bool submit(const JobInvocation &invocation) override {
		if (count_ >= kMaxInvocations) {
			return false;
		}
		invocations_[count_++] = invocation;
		return true;
	}

	const char *name() const override {
		return "test-queue";
	}

	size_t queued() const {
		return count_;
	}

	void completeOne() {
		TEST_ASSERT_TRUE(count_ > 0);
		JobInvocation invocation = invocations_[0];
		for (size_t index = 1; index < count_; ++index) {
			invocations_[index - 1] = invocations_[index];
		}
		--count_;
		invocation.callback.invoke();
		SchedulerEvent event{};
		event.kind = SchedulerEventKind::JobFinished;
		event.jobId = invocation.jobId;
		event.generation = invocation.generation;
		event.slotIndex = invocation.slotIndex;
		TEST_ASSERT_NOT_NULL(runtime_->eventQueue);
		TEST_ASSERT_EQUAL(pdTRUE, xQueueSend(runtime_->eventQueue, &event, 0));
	}

  private:
	static constexpr size_t kMaxInvocations = 8;
	JobInvocation invocations_[kMaxInvocations]{};
	size_t count_ = 0;
	std::shared_ptr<SchedulerExecutorRuntime> runtime_{};
};

static void test_begin_is_idempotent_and_end_is_explicit() {
	ESPScheduler local(date, manualConfig());
	TEST_ASSERT_FALSE(local.running());
	TEST_ASSERT_TRUE(local.begin());
	TEST_ASSERT_TRUE(local.begin());
	TEST_ASSERT_TRUE(local.running());
	local.end(true);
	TEST_ASSERT_FALSE(local.running());
}

static void test_daily_at_local_next_same_day() {
	Schedule s = Schedule::dailyAtLocal(9, 30);
	DateTime from = date.fromUtc(2025, 1, 1, 8, 15, 10);
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 1, 1, 9, 30, 0)));
}

static void test_daily_at_local_rolls_to_next_day() {
	Schedule s = Schedule::dailyAtLocal(6, 0);
	DateTime from = date.fromUtc(2025, 1, 1, 7, 0, 1);
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 1, 2, 6, 0, 0)));
}

static void test_weekly_mask_advances_to_next_weekday() {
	uint8_t weekdaysMask = 0b0111110;
	Schedule s = Schedule::weeklyAtLocal(weekdaysMask, 18, 30);
	DateTime from = date.fromUtc(2025, 3, 4, 19, 0, 0);
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 3, 5, 18, 30, 0)));
}

static void test_dom_dow_or_logic_matches_either() {
	Schedule s = Schedule::custom(
	    ScheduleField::only(0),
	    ScheduleField::only(9),
	    ScheduleField::only(10),
	    ScheduleField::any(),
	    ScheduleField::only(1)
	);
	DateTime from = date.fromUtc(2024, 7, 1, 8, 0, 0);
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2024, 7, 1, 9, 0, 0)));
}

static void test_inline_tick_runs_and_reschedules() {
	JobOptions options{};
	SchedulerResult<uint32_t> added =
	    scheduler.addJob(Schedule::dailyAtLocal(6, 0), options, &inlineCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	scheduler.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	TEST_ASSERT_EQUAL(1, inlineHits);

	scheduler.tick(date.fromUtc(2025, 1, 1, 23, 0, 0));
	TEST_ASSERT_EQUAL(1, inlineHits);

	scheduler.tick(date.fromUtc(2025, 1, 2, 6, 0, 0));
	TEST_ASSERT_EQUAL(2, inlineHits);
}

static void test_get_job_info_reports_next_run_by_job_id() {
	JobOptions options{};
	SchedulerResult<uint32_t> added =
	    scheduler.addJob(Schedule::dailyAtLocal(6, 0), options, &inlineCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	scheduler.tick(date.fromUtc(2025, 1, 1, 0, 0, 0));

	JobInfo info{};
	TEST_ASSERT_TRUE(scheduler.getJobInfo(added.value, info).ok());
	TEST_ASSERT_EQUAL(added.value, info.id);
	TEST_ASSERT_FALSE(info.paused);
	TEST_ASSERT_TRUE(info.hasNext);
	TEST_ASSERT_TRUE(date.isEqual(info.nextRunUtc, date.fromUtc(2025, 1, 1, 6, 0, 0)));
}

static void test_tick_waits_until_clock_valid_and_primes_once() {
	JobOptions options{};
	SchedulerResult<uint32_t> added =
	    scheduler.addJobOnceUtc(date.fromUtc(2025, 1, 1, 6, 0, 0), options, &inlineCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	scheduler.tick(date.fromUtc(1970, 1, 1, 0, 0, 0));
	TEST_ASSERT_EQUAL(0, inlineHits);

	scheduler.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	TEST_ASSERT_EQUAL(1, inlineHits);

	scheduler.tick(date.fromUtc(2025, 1, 1, 6, 1, 0));
	TEST_ASSERT_EQUAL(1, inlineHits);
}

static void test_pause_resume_cancel_and_job_count() {
	JobOptions options{};
	SchedulerResult<uint32_t> first =
	    scheduler.addJob(Schedule::dailyAtLocal(6, 0), options, &inlineCallback, nullptr);
	SchedulerResult<uint32_t> second =
	    scheduler.addJob(Schedule::dailyAtLocal(7, 0), options, &inlineCallback, nullptr);
	TEST_ASSERT_TRUE(first.ok());
	TEST_ASSERT_TRUE(second.ok());
	TEST_ASSERT_EQUAL(static_cast<size_t>(2), scheduler.jobCount().value);

	TEST_ASSERT_TRUE(scheduler.pauseJob(first.value).ok());
	TEST_ASSERT_TRUE(scheduler.resumeJob(first.value).ok());
	TEST_ASSERT_TRUE(scheduler.cancelJob(second.value).ok());
	TEST_ASSERT_EQUAL(static_cast<size_t>(1), scheduler.jobCount().value);
}

static void test_slot_reuse_keeps_old_job_id_invalid() {
	ESPScheduler local(date, manualConfig());
	TEST_ASSERT_TRUE(local.begin());

	JobOptions options{};
	SchedulerResult<uint32_t> first =
	    local.addJobOnceUtc(date.fromUtc(2025, 1, 1, 6, 0, 0), options, &inlineCallback, nullptr);
	TEST_ASSERT_TRUE(first.ok());

	local.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	JobInfo info{};
	TEST_ASSERT_EQUAL(SchedulerError::NotFound, local.getJobInfo(first.value, info).error);

	SchedulerResult<uint32_t> second =
	    local.addJob(Schedule::dailyAtLocal(7, 0), options, &inlineCallback, nullptr);
	TEST_ASSERT_TRUE(second.ok());
	TEST_ASSERT_TRUE(first.value != second.value);

	local.tick(date.fromUtc(2025, 1, 1, 6, 1, 0));
	TEST_ASSERT_TRUE(local.getJobInfo(second.value, info).ok());
	TEST_ASSERT_EQUAL(second.value, info.id);
	local.end(true);
}

static void test_executor_unavailable_is_reported() {
	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	options.executorId = 99;
	TEST_ASSERT_EQUAL(
	    SchedulerError::ExecutorUnavailable,
	    scheduler.addJob(Schedule::dailyAtLocal(6, 0), options, &asyncCallback, nullptr).error
	);
}

static void test_sunrise_next_occurrence_with_offsets() {
	DateTime from = date.fromUtc(2025, 6, 1, 0, 0, 0);
	SunCycleResult riseToday = date.sunrise(from);
	TEST_ASSERT_TRUE(riseToday.ok);

	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunrise(), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, riseToday.value));

	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunrise(30), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.addMinutes(riseToday.value, 30)));
}

static void test_sunset_next_occurrence_with_offsets() {
	DateTime from = date.fromUtc(2025, 6, 1, 0, 0, 0);
	SunCycleResult setToday = date.sunset(from);
	TEST_ASSERT_TRUE(setToday.ok);

	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunset(), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, setToday.value));

	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunset(-20), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.addMinutes(setToday.value, -20)));
}

static void test_moon_phase_name_last_quarter_next_occurrence() {
	DateTime from = date.fromUtc(2024, 3, 25, 0, 0, 0);
	DateTime next{};
	TEST_ASSERT_TRUE(
	    scheduler.computeNextOccurrence(Schedule::moonPhase(MoonPhaseName::LastQuarter, 2), from, next)
	);
	TEST_ASSERT_TRUE(date.differenceInDays(next, from) <= 40);

	MoonPhaseResult phaseAtNext = date.moonPhase(next);
	TEST_ASSERT_TRUE(phaseAtNext.ok);
	TEST_ASSERT_TRUE(
	    circularDistanceDegrees(static_cast<double>(phaseAtNext.angleDegrees), 270.0) <= 6.0
	);
}

static void test_moon_illumination_crossing_and_reschedule() {
	Schedule illumSchedule = Schedule::moonIlluminationPercent(75.0, 0.5);
	DateTime from = date.fromUtc(2024, 1, 1, 0, 0, 0);

	DateTime first{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(illumSchedule, from, first));
	MoonPhaseResult firstPhase = date.moonPhase(first);
	TEST_ASSERT_TRUE(firstPhase.ok);
	TEST_ASSERT_TRUE(std::fabs(firstPhase.illumination * 100.0 - 75.0) <= 2.0);

	DateTime second{};
	TEST_ASSERT_TRUE(
	    scheduler.computeNextOccurrence(illumSchedule, date.addMinutes(first, 1), second)
	);
	TEST_ASSERT_TRUE(date.isAfter(second, first));
}

static void test_invalid_astronomical_schedule_validation() {
	JobOptions options{};
	TEST_ASSERT_FALSE(
	    scheduler.addJob(Schedule::sunrise(1500), options, &inlineCallback, nullptr).ok()
	);
	TEST_ASSERT_FALSE(
	    scheduler.addJob(Schedule::moonPhaseAngle(360, 1), options, &inlineCallback, nullptr).ok()
	);
	TEST_ASSERT_FALSE(
	    scheduler.addJob(
	                 Schedule::moonIlluminationPercent(50.0, 0.0),
	                 options,
	                 &inlineCallback,
	                 nullptr
	             )
	        .ok()
	);
}

static void test_skip_if_running_behavior() {
	SchedulerConfig config = manualConfig();
	ESPScheduler local(date, config);
	TestQueueExecutor executor;
	SchedulerResult<uint8_t> executorId = local.registerExecutor(&executor);
	TEST_ASSERT_TRUE(executorId.ok());
	TEST_ASSERT_TRUE(local.begin());

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	options.executorId = executorId.value;

	SchedulerResult<uint32_t> skipJob =
	    local.addJob(Schedule::dailyAtLocal(6, 0), options, &asyncCallback, nullptr);
	TEST_ASSERT_TRUE(skipJob.ok());

	local.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	TEST_ASSERT_EQUAL(static_cast<size_t>(1), executor.queued());
	local.tick(date.fromUtc(2025, 1, 2, 6, 0, 0));
	TEST_ASSERT_EQUAL(static_cast<size_t>(1), executor.queued());
	executor.completeOne();
	local.tick(date.fromUtc(2025, 1, 2, 6, 0, 0));
	TEST_ASSERT_EQUAL(1, asyncHits);
	TEST_ASSERT_EQUAL(static_cast<size_t>(0), executor.queued());
	local.tick(date.fromUtc(2025, 1, 3, 6, 0, 0));
	TEST_ASSERT_EQUAL(static_cast<size_t>(1), executor.queued());

	local.end(true);
}

static void test_queue_one_behavior() {
	SchedulerConfig config = manualConfig();
	ESPScheduler local(date, config);
	TestQueueExecutor executor;
	SchedulerResult<uint8_t> executorId = local.registerExecutor(&executor);
	TEST_ASSERT_TRUE(executorId.ok());
	TEST_ASSERT_TRUE(local.begin());

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	options.executorId = executorId.value;
	options.overlap = OverlapPolicy::QueueOne;
	SchedulerResult<uint32_t> added =
	    local.addJob(Schedule::dailyAtLocal(6, 0), options, &asyncCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	local.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	local.tick(date.fromUtc(2025, 1, 2, 6, 0, 0));
	local.tick(date.fromUtc(2025, 1, 3, 6, 0, 0));
	TEST_ASSERT_EQUAL(static_cast<size_t>(1), executor.queued());

	executor.completeOne();
	local.tick(date.fromUtc(2025, 1, 3, 6, 0, 0));
	TEST_ASSERT_EQUAL(static_cast<size_t>(1), executor.queued());
	executor.completeOne();
	local.tick(date.fromUtc(2025, 1, 3, 6, 1, 0));
	TEST_ASSERT_EQUAL(2, asyncHits);
	local.end(true);
}

static void test_allow_parallel_behavior() {
	SchedulerConfig config = manualConfig();
	ESPScheduler local(date, config);
	TestQueueExecutor executor;
	SchedulerResult<uint8_t> executorId = local.registerExecutor(&executor);
	TEST_ASSERT_TRUE(executorId.ok());
	TEST_ASSERT_TRUE(local.begin());

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	options.executorId = executorId.value;
	options.overlap = OverlapPolicy::AllowParallel;
	SchedulerResult<uint32_t> added =
	    local.addJob(Schedule::dailyAtLocal(6, 0), options, &asyncCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	local.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	local.tick(date.fromUtc(2025, 1, 2, 6, 0, 0));
	TEST_ASSERT_EQUAL(static_cast<size_t>(2), executor.queued());
	executor.completeOne();
	executor.completeOne();
	local.tick(date.fromUtc(2025, 1, 2, 6, 1, 0));
	TEST_ASSERT_EQUAL(2, asyncHits);
	local.end(true);
}

static void test_cancel_running_async_job_and_stale_completion_is_ignored() {
	SchedulerConfig config = manualConfig();
	ESPScheduler local(date, config);
	TestQueueExecutor executor;
	SchedulerResult<uint8_t> executorId = local.registerExecutor(&executor);
	TEST_ASSERT_TRUE(executorId.ok());
	TEST_ASSERT_TRUE(local.begin());

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	options.executorId = executorId.value;
	SchedulerResult<uint32_t> added =
	    local.addJob(Schedule::dailyAtLocal(6, 0), options, &asyncCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	local.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	TEST_ASSERT_EQUAL(static_cast<size_t>(1), executor.queued());
	TEST_ASSERT_TRUE(local.cancelJob(added.value).ok());
	executor.completeOne();
	local.tick(date.fromUtc(2025, 1, 1, 6, 1, 0));

	JobInfo info{};
	TEST_ASSERT_EQUAL(SchedulerError::NotFound, local.getJobInfo(added.value, info).error);
	local.end(true);
}

static void test_background_async_runs_without_tick() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Background;
	ESPScheduler background(date, config);
	TEST_ASSERT_TRUE(background.begin());
	background.setMinValidUnixSeconds(0);

	JobOptions asyncOptions{};
	asyncOptions.dispatch = DispatchPolicy::Async;
	SchedulerResult<uint32_t> added = background.addJobOnceUtc(
	    date.addSeconds(date.now(), 1),
	    asyncOptions,
	    &asyncCallback,
	    nullptr
	);
	TEST_ASSERT_TRUE(added.ok());

	const uint32_t startedMs = millis();
	while (asyncHits == 0 && (millis() - startedMs) < 5000) {
		delay(25);
	}
	TEST_ASSERT_EQUAL(1, asyncHits);
	background.end(true);
}

static void test_background_multiple_add_job_commands_do_not_corrupt_command_lifetime() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Background;
	ESPScheduler background(date, config);
	TEST_ASSERT_TRUE(background.begin());
	background.setMinValidUnixSeconds(0);

	JobOptions options{};
	SchedulerResult<uint32_t> recurring =
	    background.addJob(Schedule::dailyAtLocal(6, 0), options, &inlineCallback, nullptr);
	SchedulerResult<uint32_t> oneShot = background.addJobOnceUtc(
	    date.addSeconds(date.now(), 60),
	    options,
	    &inlineCallback,
	    nullptr
	);

	TEST_ASSERT_TRUE(recurring.ok());
	TEST_ASSERT_TRUE(oneShot.ok());

	SchedulerResult<size_t> count = background.jobCount();
	TEST_ASSERT_TRUE(count.ok());
	TEST_ASSERT_EQUAL(static_cast<size_t>(2), count.value);

	background.end(true);
}

static void test_background_command_roundtrip_stress() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Background;
	ESPScheduler background(date, config);
	TEST_ASSERT_TRUE(background.begin());
	background.setMinValidUnixSeconds(0);

	JobOptions options{};
	for (uint32_t iteration = 0; iteration < 64; ++iteration) {
		SchedulerResult<uint32_t> added = background.addJobOnceUtc(
		    date.addSeconds(date.now(), static_cast<int32_t>(iteration + 60)),
		    options,
		    &inlineCallback,
		    nullptr
		);
		TEST_ASSERT_TRUE(added.ok());

		JobInfo info{};
		SchedulerResult<void> infoResult = background.getJobInfo(added.value, info);
		TEST_ASSERT_TRUE(infoResult.ok());
		TEST_ASSERT_EQUAL(added.value, info.id);
		TEST_ASSERT_TRUE(info.hasNext);

		SchedulerResult<size_t> count = background.jobCount();
		TEST_ASSERT_TRUE(count.ok());
		TEST_ASSERT_EQUAL(static_cast<size_t>(1), count.value);

		SchedulerResult<void> cancelResult = background.cancelJob(added.value);
		TEST_ASSERT_TRUE(cancelResult.ok());

		SchedulerResult<size_t> afterCancel = background.jobCount();
		TEST_ASSERT_TRUE(afterCancel.ok());
		TEST_ASSERT_EQUAL(static_cast<size_t>(0), afterCancel.value);
	}

	background.end(true);
}

static void test_begin_fails_for_missing_builtin_espworker() {
	SchedulerConfig config = manualConfig();
	config.defaultAsyncBackend = AsyncExecutorBackend::ESPWorker;
	config.espWorker = nullptr;
	ESPScheduler local(date, config);
	TEST_ASSERT_FALSE(local.begin());
	TEST_ASSERT_EQUAL(ESPScheduler::kInvalidExecutorId, local.defaultESPWorkerExecutor());
}

static void test_builtin_espworker_executor_id_available_when_configured() {
	ESPWorker worker;
	ESPWorker::Config workerConfig{};
	worker.init(workerConfig);

	SchedulerConfig config = manualConfig();
	config.defaultAsyncBackend = AsyncExecutorBackend::ESPWorker;
	config.espWorker = &worker;
	ESPScheduler local(date, config);
	TEST_ASSERT_EQUAL(0, local.defaultESPWorkerExecutor());
	TEST_ASSERT_EQUAL(ESPScheduler::kInvalidExecutorId, local.defaultWorkerExecutor());
	TEST_ASSERT_TRUE(local.begin());
	local.end(true);
	worker.deinit();
}

static void test_v1_compat_cleanup_prunes_canceled_jobs() {
	ESPSchedulerV1Compat compat(date);
	uint32_t jobId = compat.addJob(Schedule::dailyAtLocal(6, 0), SchedulerJobMode::Inline, &inlineCallback);
	TEST_ASSERT_TRUE(jobId != 0);

	SchedulerV1JobInfo info{};
	TEST_ASSERT_TRUE(compat.getJobInfo(0, info));
	TEST_ASSERT_EQUAL(jobId, info.id);

	TEST_ASSERT_TRUE(compat.cancelJob(jobId));
	compat.cleanup();
	TEST_ASSERT_FALSE(compat.getJobInfo(0, info));
	compat.deinit();
}

static void test_end_wait_true_drains_manual_async_invocation() {
	SchedulerConfig config = manualConfig();
	ESPScheduler local(date, config);
	TEST_ASSERT_TRUE(local.begin());

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	SchedulerResult<uint32_t> added =
	    local.addJobOnceUtc(date.fromUtc(2025, 1, 1, 6, 0, 0), options, &slowCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	local.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	local.end(true, 3000);
	TEST_ASSERT_EQUAL(1, slowHits);
	TEST_ASSERT_FALSE(local.running());
}

static void test_end_wait_false_returns_without_drain() {
	SchedulerConfig config = manualConfig();
	ESPScheduler local(date, config);
	TEST_ASSERT_TRUE(local.begin());

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	SchedulerResult<uint32_t> added =
	    local.addJobOnceUtc(date.fromUtc(2025, 1, 1, 6, 0, 0), options, &slowCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	local.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
	const uint32_t startedMs = millis();
	local.end(false, 10);
	TEST_ASSERT_TRUE((millis() - startedMs) < 200);
}

static void test_begin_fails_for_invalid_service_stack_size() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Background;
	config.service.taskStackSize = 1000;
	ESPScheduler local(date, config);
	TEST_ASSERT_FALSE(local.begin());
}

void setUp() {
	inlineHits = 0;
	asyncHits = 0;
	slowHits = 0;
	scheduler.begin();
	scheduler.setMinValidUnixSeconds(ESPScheduler::kDefaultMinValidEpochSeconds);
	scheduler.cancelAll();
}

void tearDown() {
	scheduler.end(true);
}

void setup() {
	setenv("TZ", "UTC0", 1);
	tzset();
	ESPDateConfig config{};
	config.latitude = 47.4979f;
	config.longitude = 19.0402f;
	config.timeZone = "UTC0";
	date.init(config);
	delay(2000);

	UNITY_BEGIN();
	RUN_TEST(test_begin_is_idempotent_and_end_is_explicit);
	RUN_TEST(test_daily_at_local_next_same_day);
	RUN_TEST(test_daily_at_local_rolls_to_next_day);
	RUN_TEST(test_weekly_mask_advances_to_next_weekday);
	RUN_TEST(test_dom_dow_or_logic_matches_either);
	RUN_TEST(test_inline_tick_runs_and_reschedules);
	RUN_TEST(test_get_job_info_reports_next_run_by_job_id);
	RUN_TEST(test_tick_waits_until_clock_valid_and_primes_once);
	RUN_TEST(test_pause_resume_cancel_and_job_count);
	RUN_TEST(test_slot_reuse_keeps_old_job_id_invalid);
	RUN_TEST(test_executor_unavailable_is_reported);
	RUN_TEST(test_sunrise_next_occurrence_with_offsets);
	RUN_TEST(test_sunset_next_occurrence_with_offsets);
	RUN_TEST(test_moon_phase_name_last_quarter_next_occurrence);
	RUN_TEST(test_moon_illumination_crossing_and_reschedule);
	RUN_TEST(test_invalid_astronomical_schedule_validation);
	RUN_TEST(test_skip_if_running_behavior);
	RUN_TEST(test_queue_one_behavior);
	RUN_TEST(test_allow_parallel_behavior);
	RUN_TEST(test_cancel_running_async_job_and_stale_completion_is_ignored);
	RUN_TEST(test_background_multiple_add_job_commands_do_not_corrupt_command_lifetime);
	RUN_TEST(test_background_command_roundtrip_stress);
	RUN_TEST(test_background_async_runs_without_tick);
	RUN_TEST(test_begin_fails_for_missing_builtin_espworker);
	RUN_TEST(test_builtin_espworker_executor_id_available_when_configured);
	RUN_TEST(test_end_wait_true_drains_manual_async_invocation);
	RUN_TEST(test_end_wait_false_returns_without_drain);
	RUN_TEST(test_begin_fails_for_invalid_service_stack_size);
	RUN_TEST(test_v1_compat_cleanup_prunes_canceled_jobs);
	UNITY_END();
}

void loop() {
}
