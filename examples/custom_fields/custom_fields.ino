#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void customInline(void* userData) {
  const char* label = static_cast<const char*>(userData);
  Serial.print("[scheduler] custom match -> ");
  Serial.println(label ? label : "(null)");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESPScheduler custom fields example");

  int weekdays[] = {1, 3, 5};  // Mon, Wed, Fri (0 = Sun)
  Schedule custom = Schedule::custom(
      ScheduleField::every(5),             // every 5 minutes
      ScheduleField::range(9, 17),         // during hours 9..17
      ScheduleField::any(),                // any day of month
      ScheduleField::any(),                // any month
      ScheduleField::list(weekdays, 3));   // selected weekdays

  scheduler.addJob(custom, SchedulerJobMode::Inline, &customInline, (void*)"MWF 9-17 every 5min");
}

void loop() {
  scheduler.tick();
  delay(2000);
}
