# ESPScheduler

ESPScheduler v2 is a C++17 scheduler for ESP32 firmware that keeps the cron-style DSL from v1, but replaces the old task-per-job async model with one central scheduler core, one optional background service task, and pluggable executors.

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-scheduler/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-scheduler/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-scheduler?sort=semver)](https://github.com/ESPToolKit/esp-scheduler/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## What Changed In v2
- One `SchedulerCore` owns job state, next-run computation, overlap handling, and rescheduling.
- Background mode uses one scheduler task and queue-based control-plane serialization. No mandatory `tick()` in background mode.
- Async execution goes through executors. The default path is a fixed worker pool instead of one FreeRTOS task per job.
- Manual mode still exists for tight firmware loops and uses the same core as background mode.
- The public API now uses `SchedulerResult<T>` for mutating/query operations.
- A thin `ESPSchedulerV1Compat` wrapper is shipped for migration.

## Features
- Cron-style schedule DSL with `ScheduleField`, `Schedule`, one-shot UTC helpers, sunrise/sunset helpers, and moon helpers.
- Manual mode and background mode under one API.
- `DispatchPolicy::Inline` or `DispatchPolicy::Async`.
- `OverlapPolicy::SkipIfRunning`, `QueueOne`, and `AllowParallel`.
- Built-in worker-pool executor, dedicated-task executor, and `ESPWorkerExecutorAdapter`.
- Deterministic lifecycle with `begin()` / `end()`.
- Clock validity guard via `setMinValidUnixSeconds()` / `setMinValidUtc()`.
- Arduino / ESP-IDF friendly metadata and device tests.

## Install
- PlatformIO:
  ```ini
  lib_deps =
    https://github.com/ESPToolKit/esp-date.git
    https://github.com/ESPToolKit/esp-worker.git
    https://github.com/ESPToolKit/esp-scheduler.git
  ```
- Arduino CLI:
  ```bash
  arduino-cli lib install "ArduinoJson"
  arduino-cli lib install --git-url https://github.com/ESPToolKit/esp-date.git
  arduino-cli lib install --git-url https://github.com/ESPToolKit/esp-worker.git
  arduino-cli lib install --git-url https://github.com/ESPToolKit/esp-scheduler.git
  ```

## Quick Start

### Manual mode
```cpp
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

static void pulse(void* /*userData*/) {
    Serial.println("manual inline pulse");
}

void setup() {
    Serial.begin(115200);
    scheduler.begin();

    JobOptions options{};
    scheduler.addJob(Schedule::dailyAtLocal(8, 15), options, &pulse, nullptr);
}

void loop() {
    scheduler.tick();
    delay(1000);
}
```

### Background mode with worker pool
```cpp
#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date); // background mode by default

static void syncJob(void* /*userData*/) {
    Serial.println("background worker-pool job");
}

void setup() {
    Serial.begin(115200);
    scheduler.begin();

    JobOptions options{};
    options.dispatch = DispatchPolicy::Async;

    scheduler.addJob(
        Schedule::weeklyAtLocal(0b0111110, 18, 30),
        options,
        &syncJob,
        nullptr
    );
}

void loop() {
    delay(2000);
}
```

## Public API
- `bool begin()` / `void end(bool waitForRunningJobs = true, uint32_t timeoutMs = 5000)`
- `SchedulerResult<uint32_t> addJob(...)`
- `SchedulerResult<uint32_t> addJobOnceUtc(...)`
- `SchedulerResult<void> cancelJob(...)`, `pauseJob(...)`, `resumeJob(...)`, `cancelAll()`
- `void tick()` / `tick(nowUtc)` for manual mode
- `SchedulerResult<size_t> jobCount() const`
- `SchedulerResult<void> getJobInfo(jobId, out) const`
- `SchedulerResult<uint8_t> registerExecutor(ISchedulerExecutor*)` before `begin()`
- `uint8_t defaultWorkerExecutor() const`
- `uint8_t defaultDedicatedExecutor() const`

### Scheduling types
- `ScheduleField`: `any()`, `only()`, `range()`, `every()`, `rangeEvery()`, `list()`
- `Schedule`: `onceUtc`, `dailyAtLocal`, `weeklyAtLocal`, `monthlyOnDayLocal`, `sunrise`, `sunset`, `moonPhase`, `moonPhaseAngle`, `moonIlluminationPercent`, `custom`

### Dispatch and overlap
```cpp
JobOptions options{};
options.dispatch = DispatchPolicy::Async;
options.overlap = OverlapPolicy::SkipIfRunning;
options.executorId = scheduler.defaultWorkerExecutor();
options.name = "db-sync";
```

### Dedicated-task opt-in
```cpp
DedicatedTaskOptions task{};
task.name = "isolated-job";
task.stackSize = 8192;
task.priority = 2;

JobOptions options{};
options.dispatch = DispatchPolicy::Async;
options.executorId = scheduler.defaultDedicatedExecutor();
options.dedicatedTask = &task;
```

## Executor Model
- `InlineExecutor`: runs in scheduler context.
- `WorkerPoolExecutor`: default async executor for ESP32.
- `ESPWorkerExecutorAdapter`: bridges to an existing `ESPWorker`.
- `DedicatedTaskExecutor`: advanced opt-in path, also used by the v1 compatibility wrapper for per-job task config.

## Time Semantics
- Recurring schedules are evaluated in local time.
- One-shot UTC schedules stay exact.
- `dayOfMonth` and `dayOfWeek` follow classic cron OR semantics.
- Moon helpers trigger on crossings with tolerance windows.
- The scheduler idles until `now >= minValidUnixSeconds`.

## Examples
- `examples/v2_manual_inline`
- `examples/v2_background_worker_pool`
- `examples/v2_espworker_adapter`
- `examples/v2_shutdown`
- `examples/v1_compat_wrapper`

The older v1-style sketches are still present and routed through `ESPSchedulerV1Compat`.

## v1 Compatibility
`ESPSchedulerV1Compat` preserves the old shape:
- `SchedulerJobMode::Inline` maps to v2 inline dispatch.
- `SchedulerJobMode::WorkerTask` maps to async dispatch.
- Per-job `SchedulerTaskConfig` is routed through the dedicated-task executor.
- `deinit()`, `cleanup()`, and index-based `getJobInfo()` remain available on the compatibility wrapper.

## Testing
- Device Unity tests live under `test/test_esp_scheduler`.
- CI builds all examples with PlatformIO and Arduino CLI.

## License
ESPScheduler is released under the [MIT License](LICENSE.md).

## ESPToolKit
- Website: <https://www.esptoolkit.hu/>
- GitHub: <https://github.com/ESPToolKit>
- Support: <https://ko-fi.com/esptoolkit>
