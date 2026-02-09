#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void everyDay(void* userData) {
  (void)userData;
  Serial.println("[scheduler] every day at 00:00");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler inline every-day example");

  // One callback per day at local midnight.
  Schedule schedule = Schedule::dailyAtLocal(0, 0);
  scheduler.addJob(schedule, SchedulerJobMode::Inline, &everyDay, nullptr);
}

void loop() {
  scheduler.tick();
  delay(1000);
}
