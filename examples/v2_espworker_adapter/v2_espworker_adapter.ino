#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>
#include <ESPWorker.h>

ESPDate date;
ESPWorker worker;
ESPScheduler scheduler(date);
ESPWorkerExecutorAdapter adapter(worker);

static void adapterJob(void *userData) {
	(void)userData;
	Serial.println("[scheduler-v2] ESPWorker adapter callback");
}

void setup() {
	Serial.begin(115200);
	delay(200);

	worker.init({});
	SchedulerResult<uint8_t> adapterId = scheduler.registerExecutor(&adapter);
	if (!adapterId.ok()) {
		Serial.println("failed to register adapter");
		return;
	}

	if (!scheduler.begin()) {
		Serial.println("scheduler begin failed");
		return;
	}

	JobOptions options{};
	options.dispatch = DispatchPolicy::Async;
	options.executorId = adapterId.value;
	scheduler.addJob(Schedule::dailyAtLocal(7, 30), options, &adapterJob, nullptr);
}

void loop() {
	delay(2000);
}
