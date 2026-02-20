#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void singleTask(void* userData) {
  (void)userData;
  Serial.println("[scheduler] one-shot worker task fired");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler one-shot worker example");
  Serial.println("Make sure system time is set before scheduling.");

  SchedulerTaskConfig cfg{};
  cfg.name = "single-run";
  cfg.stackSize = 6 * 1024;
  cfg.usePsramStack = true;

  DateTime when = date.addMinutes(date.now(), 2);  // run once, 2 minutes from now (UTC)
  scheduler.addJobOnceUtc(when, SchedulerJobMode::WorkerTask, &singleTask, nullptr, &cfg);
}

void loop() {
  scheduler.tick();  // inline-safe even when only worker jobs exist
  vTaskDelay(pdMS_TO_TICKS(5000));
}
