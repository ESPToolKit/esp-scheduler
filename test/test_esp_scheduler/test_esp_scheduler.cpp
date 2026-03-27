#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>
#include <cmath>
#include <unity.h>

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

static void inlineCallback(void *userData) {
	(void)userData;
	inlineHits++;
}

static void asyncCallback(void *userData) {
	(void)userData;
	asyncHits++;
}

static double circularDistanceDegrees(double a, double b) {
	double delta = std::fmod(std::fabs(a - b), 360.0);
	if (delta > 180.0) {
		delta = 360.0 - delta;
	}
	return delta;
}

static void test_begin_and_end_are_explicit() {
	ESPScheduler local(date, manualConfig());
	TEST_ASSERT_FALSE(local.running());
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

static void test_tick_waits_until_clock_valid() {
	JobOptions options{};
	SchedulerResult<uint32_t> added =
	    scheduler.addJob(Schedule::dailyAtLocal(6, 0), options, &inlineCallback, nullptr);
	TEST_ASSERT_TRUE(added.ok());

	scheduler.tick(date.fromUtc(1970, 1, 1, 0, 0, 0));
	TEST_ASSERT_EQUAL(0, inlineHits);

	scheduler.tick(date.fromUtc(2025, 1, 1, 6, 0, 0));
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
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::moonPhase(MoonPhaseName::LastQuarter, 2), from, next));
	TEST_ASSERT_TRUE(date.differenceInDays(next, from) <= 40);

	MoonPhaseResult phaseAtNext = date.moonPhase(next);
	TEST_ASSERT_TRUE(phaseAtNext.ok);
	TEST_ASSERT_TRUE(circularDistanceDegrees(static_cast<double>(phaseAtNext.angleDegrees), 270.0) <= 6.0);
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
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(illumSchedule, date.addMinutes(first, 1), second));
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

static void test_background_async_runs_without_tick() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Background;
	ESPScheduler background(date, config);
	TEST_ASSERT_TRUE(background.begin());

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

void setUp() {
	inlineHits = 0;
	asyncHits = 0;
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
	RUN_TEST(test_begin_and_end_are_explicit);
	RUN_TEST(test_daily_at_local_next_same_day);
	RUN_TEST(test_daily_at_local_rolls_to_next_day);
	RUN_TEST(test_weekly_mask_advances_to_next_weekday);
	RUN_TEST(test_dom_dow_or_logic_matches_either);
	RUN_TEST(test_inline_tick_runs_and_reschedules);
	RUN_TEST(test_get_job_info_reports_next_run_by_job_id);
	RUN_TEST(test_tick_waits_until_clock_valid);
	RUN_TEST(test_pause_resume_cancel_and_job_count);
	RUN_TEST(test_sunrise_next_occurrence_with_offsets);
	RUN_TEST(test_sunset_next_occurrence_with_offsets);
	RUN_TEST(test_moon_phase_name_last_quarter_next_occurrence);
	RUN_TEST(test_moon_illumination_crossing_and_reschedule);
	RUN_TEST(test_invalid_astronomical_schedule_validation);
	RUN_TEST(test_background_async_runs_without_tick);
	UNITY_END();
}

void loop() {
}
