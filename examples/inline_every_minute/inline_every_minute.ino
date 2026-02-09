#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void everyMinute(void* userData) {
  (void)userData;
  Serial.println("[scheduler] every minute");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler inline every-minute example");

  Schedule schedule = Schedule::custom(
      ScheduleField::every(1),   // minute
      ScheduleField::any(),      // hour
      ScheduleField::any(),      // day of month
      ScheduleField::any(),      // month
      ScheduleField::any());     // day of week

  scheduler.addJob(schedule, SchedulerJobMode::Inline, &everyMinute, nullptr);
}

void loop() {
  scheduler.tick();
  delay(500);
}
