#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void workHoursPulse(void* userData) {
  (void)userData;
  Serial.println("[scheduler] every 15 minutes during 09:00-17:59");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler every-15-minutes work-hours example");

  // Useful for periodic checks while staff is active.
  Schedule schedule = Schedule::custom(
      ScheduleField::every(15),  // minute: 0,15,30,45
      ScheduleField::range(9, 17),
      ScheduleField::any(),
      ScheduleField::any(),
      ScheduleField::any());

  scheduler.addJob(schedule, SchedulerJobMode::Inline, &workHoursPulse, nullptr);
}

void loop() {
  scheduler.tick();
  delay(1000);
}
