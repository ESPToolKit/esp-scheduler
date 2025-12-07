#include <Arduino.h>
#include <ESPDate.h>
#include <ESPWorker.h>
#include <ESPScheduler.h>

ESPDate date;
ESPWorker worker;
ESPScheduler scheduler(date, &worker);

void heavyReport(void* userData) {
  (void)userData;
  Serial.println("[scheduler] worker task running report...");
  vTaskDelay(pdMS_TO_TICKS(1500));
  Serial.println("[scheduler] worker task done");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler worker example");
  Serial.println("Make sure SNTP/time zone are configured so local matching works.");

  worker.init({.maxWorkers = 4});

  uint8_t weekdaysMask = 0b0111110;  // Monday..Friday
  SchedulerTaskConfig cfg{};
  cfg.name = "weekly-report";
  cfg.stackSize = 10 * 1024;
  cfg.priority = 2;
  cfg.usePsramStack = true;

  scheduler.addJob(
      Schedule::weeklyAtLocal(weekdaysMask, 18, 30),
      SchedulerJobMode::WorkerTask,
      &heavyReport,
      nullptr,
      &cfg);

  // One-shot UTC reminder 2 minutes from now
  DateTime inTwoMinutes = date.addMinutes(date.now(), 2);
  scheduler.addJobOnceUtc(inTwoMinutes, SchedulerJobMode::WorkerTask, &heavyReport, nullptr, &cfg);
}

void loop() {
  scheduler.tick();  // worker jobs self-drive but tick is safe to call
  vTaskDelay(pdMS_TO_TICKS(5000));
}
