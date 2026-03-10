#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

void astroCallback(void *userData) {
	const char *label = static_cast<const char *>(userData);
	char nowBuf[32];
	if (date.nowUtcString(nowBuf, sizeof(nowBuf), ESPDateFormat::DateTime)) {
		Serial.printf("[scheduler] %s at %s UTC\n", label ? label : "job", nowBuf);
	} else {
		Serial.printf("[scheduler] %s\n", label ? label : "job");
	}
}

void setup() {
	Serial.begin(115200);
	delay(200);
	Serial.println("ESPScheduler astronomical example");

	ESPDateConfig cfg{};
	cfg.latitude = 47.4979f;
	cfg.longitude = 19.0402f;
	cfg.timeZone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
	date.init(cfg);

	scheduler.setMinValidUtc(date.fromUtc(2020, 1, 1, 0, 0, 0));

	scheduler
	    .addJob(Schedule::sunrise(), SchedulerJobMode::Inline, &astroCallback, (void *)"sunrise");
	scheduler.addJob(
	    Schedule::sunset(15),
	    SchedulerJobMode::Inline,
	    &astroCallback,
	    (void *)"sunset +15m"
	);
	scheduler.addJob(
	    Schedule::moonPhase(MoonPhaseName::LastQuarter, 2),
	    SchedulerJobMode::Inline,
	    &astroCallback,
	    (void *)"last quarter"
	);
	scheduler.addJob(
	    Schedule::moonIlluminationPercent(75.0, 0.5),
	    SchedulerJobMode::Inline,
	    &astroCallback,
	    (void *)"illumination 75%"
	);
}

void loop() {
	scheduler.tick();
	delay(1000);
}
