#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void dailyInline(void* userData) {
  (void)userData;
  Serial.println("[scheduler] Inline job fired");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler inline example");
  Serial.println("Ensure system time and time zone are configured before running tick().");

  // Run every day at 08:15 local time
  Schedule daily = Schedule::dailyAtLocal(8, 15);
  scheduler.addJob(daily, SchedulerJobMode::Inline, &dailyInline, nullptr);
}

void loop() {
  scheduler.tick();  // drive inline jobs based on ESPDate::now()
  delay(5000);
}
