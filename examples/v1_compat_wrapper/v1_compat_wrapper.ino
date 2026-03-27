#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPSchedulerV1Compat scheduler(date);

static void compatJob(void *userData) {
	(void)userData;
	Serial.println("[scheduler-v1-compat] callback");
}

void setup() {
	Serial.begin(115200);
	delay(200);
	scheduler.addJob(Schedule::dailyAtLocal(8, 0), SchedulerJobMode::Inline, &compatJob, nullptr);
}

void loop() {
	scheduler.tick();
	delay(1000);
}
