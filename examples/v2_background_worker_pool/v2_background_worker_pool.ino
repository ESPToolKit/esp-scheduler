#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

static void backgroundJob(void *userData) {
	(void)userData;
	Serial.println("[scheduler-v2] worker-pool callback start");
	delay(250);
	Serial.println("[scheduler-v2] worker-pool callback done");
}

void setup() {
	Serial.begin(115200);
	delay(200);

	scheduler.begin();

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	scheduler.addJob(Schedule::weeklyAtLocal(0b0111110, 18, 30), options, &backgroundJob, nullptr);
}

void loop() {
	delay(2000);
}
