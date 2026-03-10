#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>
#include <cmath>
#include <unity.h>

ESPDate date;
ESPScheduler scheduler(date);

static int inlineHits = 0;

static void inlineCallback(void *userData) {
	(void)userData;
	inlineHits++;
}

static double circularDistanceDegrees(double a, double b) {
	double delta = std::fmod(std::fabs(a - b), 360.0);
	if (delta > 180.0) {
		delta = 360.0 - delta;
	}
	return delta;
}

static void test_daily_at_local_next_same_day() {
	Schedule s = Schedule::dailyAtLocal(9, 30);
	DateTime from = date.fromUtc(2025, 1, 1, 8, 15, 10);
	DateTime expected = date.fromUtc(2025, 1, 1, 9, 30, 0);
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(expected, next));
}

static void test_daily_at_local_rolls_to_next_day() {
	Schedule s = Schedule::dailyAtLocal(6, 0);
	DateTime from = date.fromUtc(2025, 1, 1, 7, 0, 1); // already past the slot
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 1, 2, 6, 0, 0)));
}

static void test_weekly_mask_advances_to_next_weekday() {
	uint8_t weekdaysMask = 0b0111110; // Mon..Fri
	Schedule s = Schedule::weeklyAtLocal(weekdaysMask, 18, 30);
	DateTime from = date.fromUtc(2025, 3, 4, 19, 0, 0); // Tuesday 19:00 UTC
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 3, 5, 18, 30, 0))); // Wednesday 18:30
}

static void test_weekly_zero_mask_defaults_to_any_day() {
	Schedule s = Schedule::weeklyAtLocal(0, 10, 45);
	DateTime from = date.fromUtc(2025, 3, 1, 10, 0, 0);
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 3, 1, 10, 45, 0)));
}

static void test_dom_dow_or_logic_matches_either() {
	ScheduleField dom = ScheduleField::only(10);
	ScheduleField dow = ScheduleField::only(1); // Monday = 1 with ESPDate (0=Sun)
	Schedule s = Schedule::custom(
	    ScheduleField::only(0),
	    ScheduleField::only(9),
	    dom,
	    ScheduleField::any(),
	    dow
	);
	DateTime from = date.fromUtc(2024, 7, 1, 8, 0, 0); // Monday, day 1
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
	TEST_ASSERT_TRUE(
	    date.isEqual(next, date.fromUtc(2024, 7, 1, 9, 0, 0))
	); // passes via DOW even though DOM mismatch
}

static void test_inline_tick_runs_and_reschedules() {
	inlineHits = 0;
	Schedule s = Schedule::dailyAtLocal(6, 0);
	uint32_t id = scheduler.addJob(s, SchedulerJobMode::Inline, &inlineCallback, nullptr);
	TEST_ASSERT_NOT_EQUAL(0u, id);

	DateTime first = date.fromUtc(2025, 1, 1, 6, 0, 0);
	scheduler.tick(first);
	TEST_ASSERT_EQUAL(1, inlineHits);

	// Same day later should not trigger again
	scheduler.tick(date.fromUtc(2025, 1, 1, 23, 0, 0));
	TEST_ASSERT_EQUAL(1, inlineHits);

	// Next day at slot should run again
	scheduler.tick(date.fromUtc(2025, 1, 2, 6, 0, 0));
	TEST_ASSERT_EQUAL(2, inlineHits);
}

static void test_get_job_info_reports_next_run() {
	inlineHits = 0;
	Schedule s = Schedule::dailyAtLocal(6, 0);
	uint32_t id = scheduler.addJob(s, SchedulerJobMode::Inline, &inlineCallback, nullptr);
	TEST_ASSERT_NOT_EQUAL(0u, id);

	DateTime now = date.fromUtc(2025, 1, 1, 0, 0, 0);
	scheduler.tick(now); // compute next run but do not fire
	TEST_ASSERT_EQUAL(0, inlineHits);

	JobInfo info{};
	TEST_ASSERT_TRUE(scheduler.getJobInfo(0, info));
	TEST_ASSERT_EQUAL(id, info.id);
	TEST_ASSERT_TRUE(info.enabled);
	TEST_ASSERT_EQUAL(static_cast<int>(SchedulerJobMode::Inline), static_cast<int>(info.mode));
	TEST_ASSERT_TRUE(date.isEqual(info.nextRunUtc, date.fromUtc(2025, 1, 1, 6, 0, 0)));
}

static void test_tick_waits_until_clock_valid() {
	inlineHits = 0;
	Schedule s = Schedule::dailyAtLocal(6, 0);
	uint32_t id = scheduler.addJob(s, SchedulerJobMode::Inline, &inlineCallback, nullptr);
	TEST_ASSERT_NOT_EQUAL(0u, id);

	DateTime invalid = date.fromUtc(1970, 1, 1, 0, 0, 0);
	scheduler.tick(invalid);
	TEST_ASSERT_EQUAL(0, inlineHits);

	DateTime valid = date.fromUtc(2025, 1, 1, 6, 0, 0);
	scheduler.tick(valid);
	TEST_ASSERT_EQUAL(1, inlineHits);
}

static void test_psram_buffer_config_constructor_adds_inline_job() {
	ESPSchedulerConfig cfg{};
	cfg.usePSRAMBuffers = true;
	ESPScheduler localScheduler(date, cfg);

	uint32_t id = localScheduler.addJob(
	    Schedule::dailyAtLocal(12, 0),
	    SchedulerJobMode::Inline,
	    &inlineCallback,
	    nullptr
	);
	TEST_ASSERT_NOT_EQUAL(0u, id);

	localScheduler.cancelAll();
}

static void test_deinit_is_idempotent_and_safe_when_uninitialized() {
	ESPScheduler localScheduler(date);
	TEST_ASSERT_TRUE(localScheduler.isInitialized());

	DateTime when = date.fromUtc(2025, 1, 1, 12, 0, 0);
	uint32_t id =
	    localScheduler.addJobOnceUtc(when, SchedulerJobMode::Inline, &inlineCallback, nullptr);
	TEST_ASSERT_NOT_EQUAL(0u, id);

	localScheduler.deinit();
	TEST_ASSERT_FALSE(localScheduler.isInitialized());

	JobInfo info{};
	TEST_ASSERT_FALSE(localScheduler.getJobInfo(0, info));
	TEST_ASSERT_FALSE(localScheduler.cancelJob(id));
	TEST_ASSERT_FALSE(localScheduler.pauseJob(id));
	TEST_ASSERT_FALSE(localScheduler.resumeJob(id));

	localScheduler.deinit();
	TEST_ASSERT_FALSE(localScheduler.isInitialized());
}

static void test_scheduler_reinitializes_after_deinit() {
	ESPScheduler localScheduler(date);
	localScheduler.deinit();
	TEST_ASSERT_FALSE(localScheduler.isInitialized());

	inlineHits = 0;
	DateTime when = date.fromUtc(2025, 1, 1, 12, 0, 0);
	uint32_t id =
	    localScheduler.addJobOnceUtc(when, SchedulerJobMode::Inline, &inlineCallback, nullptr);
	TEST_ASSERT_NOT_EQUAL(0u, id);
	TEST_ASSERT_TRUE(localScheduler.isInitialized());

	localScheduler.tick(when);
	TEST_ASSERT_EQUAL(1, inlineHits);
}

static void test_sunrise_next_occurrence_with_offsets() {
	DateTime from = date.fromUtc(2025, 6, 1, 0, 0, 0);
	SunCycleResult riseToday = date.sunrise(from);
	TEST_ASSERT_TRUE(riseToday.ok);

	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunrise(), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, riseToday.value));

	DateTime expectedPlus = date.addMinutes(riseToday.value, 30);
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunrise(30), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, expectedPlus));

	DateTime expectedMinus = date.addMinutes(riseToday.value, -30);
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunrise(-30), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, expectedMinus));
}

static void test_sunset_next_occurrence_with_offsets() {
	DateTime from = date.fromUtc(2025, 6, 1, 0, 0, 0);
	SunCycleResult setToday = date.sunset(from);
	TEST_ASSERT_TRUE(setToday.ok);

	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunset(), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, setToday.value));

	DateTime expectedPlus = date.addMinutes(setToday.value, 20);
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunset(20), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, expectedPlus));

	DateTime expectedMinus = date.addMinutes(setToday.value, -20);
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunset(-20), from, next));
	TEST_ASSERT_TRUE(date.isEqual(next, expectedMinus));
}

static void test_moon_phase_name_last_quarter_next_occurrence() {
	Schedule phaseSchedule = Schedule::moonPhase(MoonPhaseName::LastQuarter, 2);
	DateTime from = date.fromUtc(2024, 3, 25, 0, 0, 0);
	DateTime next{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(phaseSchedule, from, next));
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
	TEST_ASSERT_TRUE(date.differenceInHours(second, first) > 24);
}

static void test_invalid_astronomical_schedule_validation() {
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler
	        .addJob(Schedule::sunrise(1500), SchedulerJobMode::Inline, &inlineCallback, nullptr)
	);
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler
	        .addJob(Schedule::sunset(-1500), SchedulerJobMode::Inline, &inlineCallback, nullptr)
	);
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler.addJob(
	        Schedule::moonPhaseAngle(-1, 1),
	        SchedulerJobMode::Inline,
	        &inlineCallback,
	        nullptr
	    )
	);
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler.addJob(
	        Schedule::moonPhaseAngle(360, 1),
	        SchedulerJobMode::Inline,
	        &inlineCallback,
	        nullptr
	    )
	);
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler.addJob(
	        Schedule::moonPhaseAngle(270, 31),
	        SchedulerJobMode::Inline,
	        &inlineCallback,
	        nullptr
	    )
	);
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler.addJob(
	        Schedule::moonIlluminationPercent(101.0, 0.5),
	        SchedulerJobMode::Inline,
	        &inlineCallback,
	        nullptr
	    )
	);
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler.addJob(
	        Schedule::moonIlluminationPercent(50.0, 0.0),
	        SchedulerJobMode::Inline,
	        &inlineCallback,
	        nullptr
	    )
	);
	TEST_ASSERT_EQUAL(
	    0u,
	    scheduler.addJob(
	        Schedule::moonIlluminationPercent(50.0, 51.0),
	        SchedulerJobMode::Inline,
	        &inlineCallback,
	        nullptr
	    )
	);
}

static void test_tick_runs_sunrise_and_moon_schedules() {
	inlineHits = 0;
	DateTime sunriseFrom = date.fromUtc(2025, 6, 1, 0, 0, 0);
	DateTime sunriseDue{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(Schedule::sunrise(), sunriseFrom, sunriseDue));

	uint32_t sunriseId =
	    scheduler.addJob(Schedule::sunrise(), SchedulerJobMode::Inline, &inlineCallback, nullptr);
	TEST_ASSERT_NOT_EQUAL(0u, sunriseId);
	scheduler.tick(sunriseDue);
	TEST_ASSERT_EQUAL(1, inlineHits);

	scheduler.cancelAll();

	Schedule moonSchedule = Schedule::moonPhase(MoonPhaseName::LastQuarter, 2);
	DateTime moonFrom = date.fromUtc(2024, 3, 25, 0, 0, 0);
	DateTime moonDue{};
	TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(moonSchedule, moonFrom, moonDue));
	uint32_t moonId =
	    scheduler.addJob(moonSchedule, SchedulerJobMode::Inline, &inlineCallback, nullptr);
	TEST_ASSERT_NOT_EQUAL(0u, moonId);
	scheduler.tick(moonDue);
	TEST_ASSERT_EQUAL(2, inlineHits);
}

void setUp() {
	scheduler.cancelAll();
	scheduler.setMinValidUnixSeconds(ESPScheduler::kDefaultMinValidEpochSeconds);
	inlineHits = 0;
}

void tearDown() {
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
	RUN_TEST(test_daily_at_local_next_same_day);
	RUN_TEST(test_daily_at_local_rolls_to_next_day);
	RUN_TEST(test_weekly_mask_advances_to_next_weekday);
	RUN_TEST(test_weekly_zero_mask_defaults_to_any_day);
	RUN_TEST(test_dom_dow_or_logic_matches_either);
	RUN_TEST(test_inline_tick_runs_and_reschedules);
	RUN_TEST(test_get_job_info_reports_next_run);
	RUN_TEST(test_tick_waits_until_clock_valid);
	RUN_TEST(test_psram_buffer_config_constructor_adds_inline_job);
	RUN_TEST(test_deinit_is_idempotent_and_safe_when_uninitialized);
	RUN_TEST(test_scheduler_reinitializes_after_deinit);
	RUN_TEST(test_sunrise_next_occurrence_with_offsets);
	RUN_TEST(test_sunset_next_occurrence_with_offsets);
	RUN_TEST(test_moon_phase_name_last_quarter_next_occurrence);
	RUN_TEST(test_moon_illumination_crossing_and_reschedule);
	RUN_TEST(test_invalid_astronomical_schedule_validation);
	RUN_TEST(test_tick_runs_sunrise_and_moon_schedules);
	UNITY_END();
}

void loop() {
}
