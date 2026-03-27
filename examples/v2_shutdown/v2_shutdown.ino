#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

static bool stopped = false;

static void stopDemo(void *userData) {
	(void)userData;
	Serial.println("[scheduler-v2] one-shot fired");
}

void setup() {
	Serial.begin(115200);
	delay(200);

	scheduler.begin();

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	scheduler.addJobOnceUtc(date.addSeconds(date.now(), 5), options, &stopDemo, nullptr);
}

void loop() {
	if (!stopped && millis() > 10000) {
		scheduler.end(true);
		stopped = true;
		Serial.println("[scheduler-v2] scheduler stopped cleanly");
	}
	delay(100);
}
