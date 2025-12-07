#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);
uint32_t jobId = 0;

void recurringInline(void* userData) {
  (void)userData;
  Serial.println("[scheduler] recurring inline job fired");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler inline pause/resume example");
  Serial.println("Press 'p' to pause, 'r' to resume, 'c' to cancel.");

  // Every minute (all hours/days) using custom cron fields
  Schedule everyMinute = Schedule::custom(
      ScheduleField::every(1),   // minute
      ScheduleField::any(),      // hour
      ScheduleField::any(),      // day of month
      ScheduleField::any(),      // month
      ScheduleField::any());     // day of week

  jobId = scheduler.addJob(everyMinute, SchedulerJobMode::Inline, &recurringInline, nullptr);
}

void loop() {
  scheduler.tick();

  if (Serial.available()) {
    char ch = Serial.read();
    if (ch == 'p') {
      if (scheduler.pauseJob(jobId)) {
        Serial.println("Paused recurring job.");
      }
    } else if (ch == 'r') {
      if (scheduler.resumeJob(jobId)) {
        Serial.println("Resumed recurring job.");
      }
    } else if (ch == 'c') {
      if (scheduler.cancelJob(jobId)) {
        Serial.println("Canceled recurring job.");
      }
    }
  }

  delay(500);
}
