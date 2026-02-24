#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>
#include <unity.h>

ESPDate date;
ESPScheduler scheduler(date);

static int inlineHits = 0;

static void inlineCallback(void* userData) {
    (void)userData;
    inlineHits++;
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
    DateTime from = date.fromUtc(2025, 1, 1, 7, 0, 1);  // already past the slot
    DateTime next{};
    TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
    TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 1, 2, 6, 0, 0)));
}

static void test_weekly_mask_advances_to_next_weekday() {
    uint8_t weekdaysMask = 0b0111110;  // Mon..Fri
    Schedule s = Schedule::weeklyAtLocal(weekdaysMask, 18, 30);
    DateTime from = date.fromUtc(2025, 3, 4, 19, 0, 0);  // Tuesday 19:00 UTC
    DateTime next{};
    TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
    TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2025, 3, 5, 18, 30, 0)));  // Wednesday 18:30
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
    ScheduleField dow = ScheduleField::only(1);  // Monday = 1 with ESPDate (0=Sun)
    Schedule s = Schedule::custom(ScheduleField::only(0), ScheduleField::only(9), dom, ScheduleField::any(), dow);
    DateTime from = date.fromUtc(2024, 7, 1, 8, 0, 0);  // Monday, day 1
    DateTime next{};
    TEST_ASSERT_TRUE(scheduler.computeNextOccurrence(s, from, next));
    TEST_ASSERT_TRUE(date.isEqual(next, date.fromUtc(2024, 7, 1, 9, 0, 0)));  // passes via DOW even though DOM mismatch
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
    scheduler.tick(now);  // compute next run but do not fire
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
        nullptr);
    TEST_ASSERT_NOT_EQUAL(0u, id);

    localScheduler.cancelAll();
}

static void test_deinit_is_idempotent_and_safe_when_uninitialized() {
    ESPScheduler localScheduler(date);
    TEST_ASSERT_TRUE(localScheduler.isInitialized());

    DateTime when = date.fromUtc(2025, 1, 1, 12, 0, 0);
    uint32_t id = localScheduler.addJobOnceUtc(when, SchedulerJobMode::Inline, &inlineCallback, nullptr);
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
    uint32_t id = localScheduler.addJobOnceUtc(when, SchedulerJobMode::Inline, &inlineCallback, nullptr);
    TEST_ASSERT_NOT_EQUAL(0u, id);
    TEST_ASSERT_TRUE(localScheduler.isInitialized());

    localScheduler.tick(when);
    TEST_ASSERT_EQUAL(1, inlineHits);
}

void setUp() {
    scheduler.cancelAll();
    scheduler.setMinValidUnixSeconds(ESPScheduler::kDefaultMinValidEpochSeconds);
    inlineHits = 0;
}

void tearDown() {}

void setup() {
    setenv("TZ", "UTC", 1);
    tzset();
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
    UNITY_END();
}

void loop() {}
