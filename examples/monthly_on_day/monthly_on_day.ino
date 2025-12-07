#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void monthlyInline(void* userData) {
  (void)userData;
  Serial.println("[scheduler] monthly inline job fired");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler monthly-on-day example");

  // Run on the 31st at 07:00; scheduler clamps to the last valid day per month
  Schedule monthly = Schedule::monthlyOnDayLocal(31, 7, 0);
  scheduler.addJob(monthly, SchedulerJobMode::Inline, &monthlyInline, nullptr);
}

void loop() {
  scheduler.tick();
  delay(2000);
}
