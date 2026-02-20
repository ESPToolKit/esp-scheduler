#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void weeklyReport(void* userData) {
  (void)userData;
  Serial.println("[scheduler] weekly worker task running...");
  vTaskDelay(pdMS_TO_TICKS(1500));
  Serial.println("[scheduler] weekly worker task done");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler weekly worker example");
  Serial.println("Configure SNTP/time zone so local matching works.");

  uint8_t weekdaysMask = 0b0111110;  // Monday..Friday
  SchedulerTaskConfig cfg{};
  cfg.name = "weekly-report";
  cfg.stackSize = 10 * 1024;
  cfg.priority = 2;
  cfg.usePsramStack = true;

  scheduler.addJob(
      Schedule::weeklyAtLocal(weekdaysMask, 18, 30),
      SchedulerJobMode::WorkerTask,
      &weeklyReport,
      nullptr,
      &cfg);
}

void loop() {
  scheduler.tick();  // worker jobs self-drive but tick is harmless
  vTaskDelay(pdMS_TO_TICKS(5000));
}
