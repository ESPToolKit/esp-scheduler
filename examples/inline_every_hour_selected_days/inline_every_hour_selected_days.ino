#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void hourOnSelectedDays(void* userData) {
  (void)userData;
  Serial.println("[scheduler] every hour on Tue/Thu/Sat");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler every-hour selected-days example");

  int selectedDays[] = {2, 4, 6};  // Tue, Thu, Sat
  Schedule schedule = Schedule::custom(
      ScheduleField::only(0),                // minute
      ScheduleField::any(),                  // hour
      ScheduleField::any(),                  // day of month
      ScheduleField::any(),                  // month
      ScheduleField::list(selectedDays, 3)   // day of week
  );

  scheduler.addJob(schedule, SchedulerJobMode::Inline, &hourOnSelectedDays, nullptr);
}

void loop() {
  scheduler.tick();
  delay(1000);
}
