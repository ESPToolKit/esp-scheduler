#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void everyHour(void* userData) {
  (void)userData;
  Serial.println("[scheduler] every hour (minute 0)");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler inline every-hour example");

  // Fires at HH:00 every day.
  Schedule schedule = Schedule::custom(
      ScheduleField::only(0),   // minute
      ScheduleField::any(),     // hour
      ScheduleField::any(),     // day of month
      ScheduleField::any(),     // month
      ScheduleField::any());    // day of week

  scheduler.addJob(schedule, SchedulerJobMode::Inline, &everyHour, nullptr);
}

void loop() {
  scheduler.tick();
  delay(1000);
}
