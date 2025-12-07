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

## API quick map
- `SchedulerJobMode`: `Inline` (runs inside `tick()`) or `WorkerTask` (dedicated FreeRTOS task via ESPWorker).
- `SchedulerTaskConfig`: optional worker task config (name, stack size, priority, core, PSRAM stack flag).
- `SchedulerCallback`: `using SchedulerCallback = void (*)(void* userData);`
- `ScheduleField`: bitmask-backed allowed values for one cron field. Builders: `any()`, `only()`, `range()`, `every()`, `rangeEvery()`, `list()`.
- `Schedule`: one-shot (`onceUtc`) or cron-like via helpers: `dailyAtLocal`, `weeklyAtLocal`, `monthlyOnDayLocal`, `custom`.

```cpp
ESPScheduler scheduler(date, &worker);              // worker optional; required for WorkerTask mode
uint32_t id = scheduler.addJob(
    Schedule::dailyAtLocal(7, 30),
    SchedulerJobMode::Inline,
    &myCallback,
    nullptr);

scheduler.pauseJob(id);
scheduler.resumeJob(id);
scheduler.cancelJob(id);
```

## Schedule recipes
```cpp
// One-shot absolute UTC
Schedule once = Schedule::onceUtc(date.fromUtc(2025, 1, 1, 12, 0));

// Daily 08:15 (local)
Schedule daily = Schedule::dailyAtLocal(8, 15);

// Weekdays at 18:30 (bitmask: 0=Sun, 1=Mon...)
uint8_t weekdays = 0b0111110; // Mon..Fri
Schedule weekly = Schedule::weeklyAtLocal(weekdays, 18, 30);

// Monthly on the 1st at 09:00 (clamps 29/30/31 to valid)
Schedule monthly = Schedule::monthlyOnDayLocal(1, 9, 0);

// Custom cron-like: every 5 minutes between 9-17 on Mon/Wed/Fri
int days[] = {1, 3, 5};
Schedule custom = Schedule::custom(
    ScheduleField::every(5),              // minute
    ScheduleField::range(9, 17),          // hour
    ScheduleField::any(),                 // day of month
    ScheduleField::any(),                 // month
    ScheduleField::list(days, 3));        // day of week
```

### Execution modes
- **Inline**: call `tick()` periodically; callbacks run in the caller’s context. Works without ESPWorker.
- **WorkerTask**: requires ESPWorker; each job gets its own FreeRTOS task that sleeps until due. Configure stacks/priority/affinity via `SchedulerTaskConfig`.

### Cron semantics
- Resolution: minutes (seconds always treated as zero).
- Local time matching via ESPDate; honour your TZ/DST setup before scheduling.
- `dayOfMonth` vs `dayOfWeek`: classic cron OR rule when both are restricted; either can satisfy the day check.

## Examples (one focus per sketch)
- `examples/inline_daily/inline_daily.ino` — inline daily tick loop.
- `examples/inline_one_shot/inline_one_shot.ino` — single UTC trigger inline.
- `examples/inline_pause_resume/inline_pause_resume.ino` — pausing/resuming a repeating inline job.
- `examples/worker_weekly/worker_weekly.ino` — weekly heavy job on its own task with custom stack/priority.
- `examples/worker_one_shot/worker_one_shot.ino` — one-shot worker task using PSRAM stack.
- `examples/custom_fields/custom_fields.ino` — custom cron fields (every N minutes, selected weekdays/hours).
- `examples/monthly_on_day/monthly_on_day.ino` — monthly day-of-month trigger with clamping.

## Links
- [ESPToolKit website](https://esptoolkitfrontend.onrender.com/)
- [ESPToolKit on GitHub](https://github.com/ESPToolKit)
- [Support ESPToolKit on Ko-Fi](https://ko-fi.com/esptoolkit)
