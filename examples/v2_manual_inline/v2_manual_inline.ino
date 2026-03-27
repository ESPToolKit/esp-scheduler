#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;

SchedulerConfig schedulerConfig() {
	SchedulerConfig config{};
	config.mode = SchedulerMode::Manual;
	return config;
}

ESPScheduler scheduler(date, schedulerConfig());

static void manualJob(void *userData) {
	(void)userData;
	Serial.println("[scheduler-v2] manual inline callback");
}

void setup() {
	Serial.begin(115200);
	delay(200);

	scheduler.begin();
	JobOptions options{};
	scheduler.addJob(Schedule::dailyAtLocal(8, 15), options, &manualJob, nullptr);
}

void loop() {
	scheduler.tick();
	delay(1000);
}
