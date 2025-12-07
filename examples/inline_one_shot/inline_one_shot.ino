#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void onceInline(void* userData) {
  (void)userData;
  Serial.println("[scheduler] one-shot inline callback fired");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler inline one-shot example");
  Serial.println("Set system time (SNTP/manual) before using date.now().");

  DateTime when = date.addMinutes(date.now(), 1);  // run once, 1 minute from now
  scheduler.addJobOnceUtc(when, SchedulerJobMode::Inline, &onceInline, nullptr);
}

void loop() {
  scheduler.tick();
  delay(2000);
}
