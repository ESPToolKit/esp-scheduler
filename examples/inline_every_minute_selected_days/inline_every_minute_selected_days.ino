#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void minuteOnSelectedDays(void* userData) {
  (void)userData;
  Serial.println("[scheduler] every minute on Mon/Wed/Fri");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler every-minute selected-days example");

  int selectedDays[] = {1, 3, 5};  // 0=Sun, 1=Mon, ... 6=Sat
  Schedule schedule = Schedule::custom(
      ScheduleField::every(1),              // minute
      ScheduleField::any(),                 // hour
      ScheduleField::any(),                 // day of month
      ScheduleField::any(),                 // month
      ScheduleField::list(selectedDays, 3)  // day of week
  );

  scheduler.addJob(schedule, SchedulerJobMode::Inline, &minuteOnSelectedDays, nullptr);
}

void loop() {
  scheduler.tick();
  delay(500);
}
