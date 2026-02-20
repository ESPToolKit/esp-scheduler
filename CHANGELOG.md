# Changelog

All notable changes to this project are documented in this file.

The format follows Keep a Changelog and the project adheres to Semantic Versioning.

## [Unreleased]
### Added
- Clock validity guard: inline and worker jobs stay idle until the wall clock reaches a configurable minimum (default 2020-01-01 UTC) to prevent catch-up storms when SNTP sets time after boot.
- `std::function` callback overloads for `addJob`/`addJobOnceUtc` to allow capturing lambdas.
- `std::function<void()>` overloads for `addJob`/`addJobOnceUtc` to allow no-arg lambdas.
- Added focused recurring-pattern examples for every minute, every day, every hour, selected weekdays, and work-hour interval schedules.
- `deinit()` plus destructor cleanup for deterministic job teardown, including worker task destruction.
- `ESPSchedulerConfig` with `usePSRAMBuffers` toggle to route scheduler-owned dynamic buffers through ESPBufferManager (safe fallback to default heap when PSRAM is unavailable).
- Additive constructor overloads that accept `ESPSchedulerConfig` while preserving existing constructor signatures.

### Fixed
- Worker job tasks no longer capture the scheduler instance pointer, avoiding use-after-free risks during scheduler teardown.
- Worker jobs now spawn directly via FreeRTOS (`xTaskCreatePinnedToCore`) using `SchedulerTaskConfig` values.
- Scheduler-owned inline/worker job container allocations and worker context allocations now follow the scheduler PSRAM buffer policy while keeping task-stack PSRAM handling (`usePsramStack`) separate.

## [1.0.1] - 2025-12-07
### Added
- `JobInfo` inspector and `getJobInfo()` helper to enumerate active inline/worker jobs with their schedules and next run time.
- `cleanup()` helper to purge finished inline/worker jobs when not driving the scheduler via `tick()`.

### Changed
- `Schedule::weeklyAtLocal()` now treats an empty weekday mask as “any day” instead of producing an empty field, and `ScheduleField::list()` documents that out-of-range values clear the field.
- Worker jobs now track `nextRunUtc` inside their context to align with the inspector API.

## [1.0.0] - 2025-12-07
### Added
- Initial ESPScheduler cron-style engine built atop ESPDate with inline and ESPWorker-backed task modes.
- Schedule helpers for daily, weekly (bitmask DOW), monthly, one-shot UTC triggers, and custom cron-like fields.
- Inline scheduler loop with `tick()` plus per-job pause/resume/cancel controls.
- Worker task mode that spawns a dedicated FreeRTOS task per job via ESPWorker with optional PSRAM stacks.
- Examples for inline and worker-driven jobs, README and metadata for Arduino/PlatformIO/ESP-IDF.
- CI + release workflows, issue/PR templates, and Unity smoke tests for cron matching.
- Expanded README with API map, cron recipes, and execution-mode guidance.
- Added focused examples covering inline one-shot, pause/resume, custom cron fields, monthly triggers, and worker-based weekly/one-shot jobs.

[Unreleased]: https://github.com/ESPToolKit/esp-scheduler/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/ESPToolKit/esp-scheduler/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/ESPToolKit/esp-scheduler/releases/tag/v1.0.0
