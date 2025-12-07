# ESPScheduler

ESPScheduler is a C++17, class-based scheduler for ESP32 firmware that brings cron-like calendar patterns without parsing cron strings. It builds on [ESPDate](https://github.com/ESPToolKit/esp-date) for all wall-clock math and can run jobs either inline (driven by `tick()`) or on dedicated FreeRTOS tasks via [ESPWorker](https://github.com/ESPToolKit/esp-worker).

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-scheduler/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-scheduler/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-scheduler?sort=semver)](https://github.com/ESPToolKit/esp-scheduler/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- **Cron-style patterns, no strings**: express minute/hour/day/month/weekday filters with `ScheduleField` objects and helpers for daily/weekly/monthly runs.
- **Inline or worker execution**: run callbacks inside `tick()` or on their own FreeRTOS task via ESPWorker (with PSRAM stack option).
- **One-shot UTC triggers**: schedule absolute UTC times alongside recurring patterns.
- **Calendar-aware**: respects classic cron `dayOfMonth` vs `dayOfWeek` logic and always operates in local time.
- **Class-based API**: everything hangs off an `ESPScheduler` instance; no global namespaces or macros.
- **Arduino / ESP-IDF friendly**: C++17, metadata for PlatformIO/Arduino CLI, and examples/tests ready for CI.

## Getting Started
Install one of two ways:
- Download the repository zip from GitHub, extract it, and drop the folder into your PlatformIO `lib/` directory, Arduino IDE `libraries/` directory, or add it as an ESP-IDF component.
- Add the public GitHub URL to `lib_deps` in `platformio.ini` so PlatformIO fetches it for you:
  ```
  lib_deps = https://github.com/ESPToolKit/esp-scheduler.git
  ```

Then include the scheduler together with its dependencies:

```cpp
#include <Arduino.h>
#include <ESPDate.h>
#include <ESPWorker.h>
#include <ESPScheduler.h>

static ESPDate date;
static ESPWorker worker;
static ESPScheduler scheduler(date, &worker);

void morningBackup(void* userData) {
    Serial.println("Running morning backup...");
}

void setup() {
    Serial.begin(115200);
    // Configure SNTP/time zone before scheduling so ESPDate reports valid local time.
    worker.init({ .maxWorkers = 4 });

    // Run every weekday at 07:30 (local time) on a dedicated worker task
    uint8_t weekdaysMask = 0b0111110; // Mon..Fri
    scheduler.addJob(
        Schedule::weeklyAtLocal(weekdaysMask, 7, 30),
        SchedulerJobMode::WorkerTask,
        &morningBackup);
}

void loop() {
    scheduler.tick(); // still safe to call; worker jobs self-drive
    delay(5000);
}
```

## Core Types
- `SchedulerJobMode`: `Inline` (callback runs inside `tick()`) or `WorkerTask` (dedicated FreeRTOS task via ESPWorker).
- `SchedulerTaskConfig`: optional per-job task config (name, stack size, priority, core, PSRAM stack flag) used only in worker mode.
- `SchedulerCallback`: `using SchedulerCallback = void (*)(void* userData);`
- `ScheduleField`: bitmask-backed allowed values for one cron field. Builders: `any()`, `only()`, `range()`, `every()`, `rangeEvery()`, `list()`.
- `Schedule`: either a one-shot (`onceUtc`) or a cron-like pattern with helpers: `dailyAtLocal`, `weeklyAtLocal`, `monthlyOnDayLocal`, `custom`.

## API Overview
```cpp
class ESPScheduler {
public:
    ESPScheduler(ESPDate& date, ESPWorker* worker = nullptr);

    uint32_t addJobOnceUtc(const DateTime& whenUtc,
                           SchedulerJobMode mode,
                           SchedulerCallback cb,
                           void* userData = nullptr,
                           const SchedulerTaskConfig* taskCfg = nullptr);

    uint32_t addJob(const Schedule& schedule,
                    SchedulerJobMode mode,
                    SchedulerCallback cb,
                    void* userData = nullptr,
                    const SchedulerTaskConfig* taskCfg = nullptr);

    bool cancelJob(uint32_t jobId);
    bool pauseJob(uint32_t jobId);
    bool resumeJob(uint32_t jobId);
    void cancelAll();

    void tick(const DateTime& nowUtc); // drive inline jobs with a known timestamp
    void tick();                       // convenience: uses ESPDate::now()
};
```

### Cron semantics
- Resolution: minutes (seconds are always treated as zero).
- Local time: matching uses local calendar components from ESPDate (TZ/DST aware).
- `dayOfMonth` vs `dayOfWeek`: classic cron rule — if both are restricted, a match on either passes; if either is `any`, that field becomes non-blocking.
- One-shots store absolute UTC times and run once.

### Inline vs worker jobs
- Inline jobs live in a lightweight vector and are dispatched when `tick()` sees they are due.
- Worker jobs spawn a dedicated FreeRTOS task through ESPWorker. Each task sleeps until the next execution, honours pause/cancel flags, and supports PSRAM stacks via `SchedulerTaskConfig::usePsramStack`.

## Examples
- `examples/inline_daily/inline_daily.ino` — simple inline scheduler tick loop.
- `examples/worker_jobs/worker_jobs.ino` — worker-backed jobs with custom stack size and name.

## Links
- [ESPToolKit website](https://esptoolkitfrontend.onrender.com/)
- [ESPToolKit on GitHub](https://github.com/ESPToolKit)
- [Support ESPToolKit on Ko-Fi](https://ko-fi.com/esptoolkit)
