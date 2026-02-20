# ESPScheduler

ESPScheduler is a C++17, class-based scheduler for ESP32 firmware that brings cron-like calendar patterns without parsing cron strings. It builds on [ESPDate](https://github.com/ESPToolKit/esp-date) for all wall-clock math and can run jobs either inline (driven by `tick()`) or on dedicated FreeRTOS tasks via [ESPWorker](https://github.com/ESPToolKit/esp-worker).

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-scheduler/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-scheduler/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-scheduler?sort=semver)](https://github.com/ESPToolKit/esp-scheduler/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- **Cron-style patterns, no strings**: express minute/hour/day/month/weekday filters with `ScheduleField` objects and helpers for daily/weekly/monthly runs.
- **Inline or worker execution**: run callbacks inside `tick()` or on their own FreeRTOS task via ESPWorker (with separate PSRAM policies for buffers and task stacks).
- **One-shot UTC triggers**: schedule absolute UTC times alongside recurring patterns.
- **Calendar-aware**: respects classic cron `dayOfMonth` vs `dayOfWeek` logic and always operates in local time.
- **Clock guard for unset RTC**: defaults to idling until the wall clock reaches 2020-01-01 UTC (configurable) so jobs do not replay from the 1970 epoch when SNTP syncs later.
- **Optional PSRAM buffer policy**: `ESPSchedulerConfig::usePSRAMBuffers` routes scheduler-owned job/context storage through ESPBufferManager with automatic fallback to default heap.
- **Class-based API**: everything hangs off an `ESPScheduler` instance; no global namespaces or macros.
- **Arduino / ESP-IDF friendly**: C++17, metadata for PlatformIO/Arduino CLI, and examples/tests ready for CI.

## Getting Started
Install one of two ways:
- Download the repository zip from GitHub, extract it, and drop the folder into your PlatformIO `lib/` directory, Arduino IDE `libraries/` directory, or add it as an ESP-IDF component.
- Add the public GitHub URL to `lib_deps` in `platformio.ini` so PlatformIO fetches it for you:
  ```
  lib_deps = https://github.com/ESPToolKit/esp-scheduler.git
  ```
- Arduino CLI: install the library and its deps, then compile any example sketch:
  ```bash
  arduino-cli lib install --git-url https://github.com/ESPToolKit/esp-date.git
  arduino-cli lib install --git-url https://github.com/ESPToolKit/esp-worker.git
  arduino-cli lib install --git-url https://github.com/ESPToolKit/esp-scheduler.git
  arduino-cli compile --fqbn esp32:esp32:esp32 examples/inline_daily
  ```

Then include the scheduler together with its dependencies:

```cpp
#include <Arduino.h>
#include <ESPDate.h>
#include <ESPWorker.h>
#include <ESPScheduler.h>

ESPDate date;
ESPWorker worker;
ESPScheduler scheduler(date, &worker);

void morningBackup(void* userData) {
    Serial.println("Running morning backup...");
}

void setup() {
    Serial.begin(115200);
    // Configure SNTP/time zone before scheduling so ESPDate reports valid local time.
    worker.init({ .maxWorkers = 4 });
    // Optional: raise the minimum valid clock to block jobs until SNTP sets time.
    scheduler.setMinValidUtc(date.fromUtc(2020, 1, 1, 0, 0, 0));

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
- `ESPSchedulerConfig`: scheduler-level memory policy (`usePSRAMBuffers`) for scheduler-owned dynamic buffers.
- `SchedulerTaskConfig`: optional worker task config (name, stack size, priority, core, PSRAM stack flag).
- `SchedulerCallback`: `using SchedulerCallback = void (*)(void* userData);`
- `SchedulerFunction`: `using SchedulerFunction = std::function<void(void* userData)>;` (capturing lambdas supported).
- `SchedulerFunctionNoData`: `using SchedulerFunctionNoData = std::function<void()>;` (no-arg lambdas supported).
- `setMinValidUnixSeconds` / `setMinValidUtc`: block all inline/worker jobs until the wall clock reaches this point (default: 2020-01-01 UTC).
- `ScheduleField`: bitmask-backed allowed values for one cron field. Builders: `any()`, `only()`, `range()`, `every()`, `rangeEvery()`, `list()`.
- `Schedule`: one-shot (`onceUtc`) or cron-like via helpers: `dailyAtLocal`, `weeklyAtLocal`, `monthlyOnDayLocal`, `custom`.
- `JobInfo` / `getJobInfo(index, info)`: inspect active jobs (inline first, then worker), including enabled state, schedule copy, and next run (if known).
- `cleanup()`: manually purge finished inline/worker jobs when you are not calling `tick()`.
- `deinit()`: cancels and destroys all active jobs; destructor calls it automatically.

```cpp
ESPSchedulerConfig schedCfg;
schedCfg.usePSRAMBuffers = true;                    // falls back safely when PSRAM is unavailable
ESPScheduler scheduler(date, &worker, schedCfg);    // worker optional; required for WorkerTask mode
uint32_t id = scheduler.addJob(
    Schedule::dailyAtLocal(7, 30),
    SchedulerJobMode::Inline,
    &myCallback,
    nullptr);

scheduler.pauseJob(id);
scheduler.resumeJob(id);
scheduler.cancelJob(id);
```

Capturing lambda callbacks are supported via the `std::function` overload:

```cpp
DateTime bootTargetUtc = date.addMinutes(date.now(), 2);
scheduler.addJobOnceUtc(
    bootTargetUtc,
    SchedulerJobMode::Inline,
    [this](void* /*userData*/) {
        doSomething();
    });
```

No-arg lambdas are also supported:

```cpp
scheduler.addJobOnceUtc(
    bootTargetUtc,
    SchedulerJobMode::Inline,
    [this]() {
        doSomething();
    });
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
- **Memory policy split**: `ESPSchedulerConfig::usePSRAMBuffers` controls scheduler-owned dynamic buffer placement; `SchedulerTaskConfig::usePsramStack` controls worker task stack placement.
- Even if you only schedule `WorkerTask` jobs, call `tick()` or `cleanup()` occasionally so the scheduler can drop finished worker job metadata.

### Cron semantics
- Resolution: minutes (seconds always treated as zero).
- Local time matching via ESPDate; honour your TZ/DST setup before scheduling.
- `dayOfMonth` vs `dayOfWeek`: classic cron OR rule when both are restricted; either can satisfy the day check.
- Clock validity guard: inline and worker paths stay idle while `now()` is before `setMinValidUnixSeconds()` (default 2020-01-01 UTC). Set it to `0` if you explicitly want to allow pre-2000 times.

## Examples

### Inline daily tick (no worker needed)
```cpp
#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);  // no ESPWorker -> Inline jobs only

static void waterPlants(void* /*userData*/) {
    Serial.println("Watering plants...");
}

void setup() {
    Serial.begin(115200);
    // Set TZ + SNTP before scheduling so local time is valid
    scheduler.setMinValidUtc(date.fromUtc(2020, 1, 1, 0, 0, 0));

    // 07:00 every day, inline
    scheduler.addJob(
        Schedule::dailyAtLocal(7, 0),
        SchedulerJobMode::Inline,
        &waterPlants);
}

void loop() {
    scheduler.tick();  // computes next runs using date.now()
    delay(1000);
}
```

### Worker task with custom stack/priority
```cpp
#include <Arduino.h>
#include <ESPDate.h>
#include <ESPWorker.h>
#include <ESPScheduler.h>

ESPDate date;
ESPWorker worker;
ESPScheduler scheduler(date, &worker);

static void backupJob(void* /*userData*/) {
    Serial.println("Backing up to cloud...");
    // heavy work is safe here; job owns its own FreeRTOS task
}

void setup() {
    Serial.begin(115200);
    worker.init({ .maxWorkers = 2 });
    scheduler.setMinValidUtc(date.fromUtc(2020, 1, 1, 0, 0, 0));

    SchedulerTaskConfig cfg;
    cfg.name = "backup";
    cfg.stackSize = 8192;
    cfg.priority = 3;
    cfg.coreId = 1;
    cfg.usePsramStack = true;

    scheduler.addJob(
        Schedule::weeklyAtLocal(0b0000010, 2, 30), // Mondays 02:30 local
        SchedulerJobMode::WorkerTask,
        &backupJob,
        nullptr,
        &cfg);
}

void loop() {
    scheduler.tick();   // still safe to call; frees finished worker metadata
    delay(1000);
}
```

### One-shot + inspecting/pause/resume
```cpp
#include <Arduino.h>
#include <ESPDate.h>
#include <ESPScheduler.h>

ESPDate date;
ESPScheduler scheduler(date);

static void firmwareSwap(void* /*userData*/) {
    Serial.println("Swapping firmware banks now");
}

void setup() {
    Serial.begin(115200);
    DateTime when = date.fromUtc(2025, 1, 15, 12, 0, 0);
    uint32_t id = scheduler.addJobOnceUtc(
        when,
        SchedulerJobMode::Inline,
        &firmwareSwap);

    JobInfo info{};
    if (scheduler.getJobInfo(0, info)) {
        Serial.printf("Job %u next run: %lld\n", info.id, info.nextRunUtc.epochSeconds);
        scheduler.pauseJob(info.id);   // stop until resumeJob is called
        scheduler.resumeJob(info.id);
    }
}

void loop() {
    scheduler.tick();
    delay(500);
}
```

Example sketches in this repo:
- `examples/inline_daily/inline_daily.ino` — inline daily tick loop.
- `examples/inline_one_shot/inline_one_shot.ino` — single UTC trigger inline.
- `examples/inline_pause_resume/inline_pause_resume.ino` — pausing/resuming a repeating inline job.
- `examples/inline_every_day_midnight/inline_every_day_midnight.ino` — every day at local midnight.
- `examples/inline_every_hour/inline_every_hour.ino` — every hour (top of hour) every day.
- `examples/inline_every_minute/inline_every_minute.ino` — every minute all day.
- `examples/inline_every_minute_selected_days/inline_every_minute_selected_days.ino` — every minute on selected weekdays.
- `examples/inline_every_hour_selected_days/inline_every_hour_selected_days.ino` — every hour on selected weekdays.
- `examples/inline_every_15_minutes_work_hours/inline_every_15_minutes_work_hours.ino` — every 15 minutes during business hours.
- `examples/worker_weekly/worker_weekly.ino` — weekly heavy job on its own task with custom stack/priority.
- `examples/worker_one_shot/worker_one_shot.ino` — one-shot worker task using PSRAM stack.
- `examples/custom_fields/custom_fields.ino` — custom cron fields (every N minutes, selected weekdays/hours).
- `examples/monthly_on_day/monthly_on_day.ino` — monthly day-of-month trigger with clamping.

## Gotchas
- Always set time zone and SNTP before scheduling; pair that with `setMinValidUtc` so jobs do not all replay at boot from the 1970 epoch.
- `SchedulerJobMode::WorkerTask` requires an `ESPWorker` pointer in the constructor; without it, addJob in worker mode will fail.
- Even when you only run worker tasks, call `tick()` or `cleanup()` periodically so finished worker metadata is freed.
- `ScheduleField::list` drops out-of-range values; if every entry is invalid, `addJob` returns `0` because the schedule fails validation.
- Matching happens at minute resolution; if you need per-second triggers, pair ESPScheduler with ESPTimer counters instead.

## Restrictions
- Designed for ESP32 boards (Arduino-ESP32 or ESP-IDF) with FreeRTOS and C++17 enabled.
- Depends on ESPDate for wall-clock math; ESPWorker is optional but required for `WorkerTask` jobs.
- Each worker job spawns its own task with its own stack; size those stacks (or enable PSRAM stacks) according to your workload.
- Schedules operate in local time and clamp invalid calendar combinations (e.g., 31st on shorter months).

## Examples (one focus per sketch)
- `examples/inline_daily/inline_daily.ino` — inline daily tick loop.
- `examples/inline_one_shot/inline_one_shot.ino` — single UTC trigger inline.
- `examples/inline_pause_resume/inline_pause_resume.ino` — pausing/resuming a repeating inline job.
- `examples/inline_every_day_midnight/inline_every_day_midnight.ino` — every day at local midnight.
- `examples/inline_every_hour/inline_every_hour.ino` — every hour (top of hour) every day.
- `examples/inline_every_minute/inline_every_minute.ino` — every minute all day.
- `examples/inline_every_minute_selected_days/inline_every_minute_selected_days.ino` — every minute on selected weekdays.
- `examples/inline_every_hour_selected_days/inline_every_hour_selected_days.ino` — every hour on selected weekdays.
- `examples/inline_every_15_minutes_work_hours/inline_every_15_minutes_work_hours.ino` — every 15 minutes during business hours.
- `examples/worker_weekly/worker_weekly.ino` — weekly heavy job on its own task with custom stack/priority.
- `examples/worker_one_shot/worker_one_shot.ino` — one-shot worker task using PSRAM stack.
- `examples/custom_fields/custom_fields.ino` — custom cron fields (every N minutes, selected weekdays/hours).
- `examples/monthly_on_day/monthly_on_day.ino` — monthly day-of-month trigger with clamping.

## Tests
- Unity-based device tests live in `test/test_esp_scheduler`; drop the folder into a PlatformIO workspace and run `pio test -e esp32dev` against real hardware.
- Host-side CTest is intentionally skipped because the scheduler relies on ESP32 FreeRTOS and ESPDate wall-clock helpers.
- CI also compiles all examples through PlatformIO and Arduino CLI across ESP32, S3, C3, and P4 boards.

## License
MIT — see `LICENSE.md`.

## ESPToolKit
- Check out other libraries: https://github.com/orgs/ESPToolKit/repositories
- Hang out on Discord: https://discord.gg/WG8sSqAy
- Support the project: https://ko-fi.com/esptoolkit
- Visit the website: https://www.esptoolkit.hu/
