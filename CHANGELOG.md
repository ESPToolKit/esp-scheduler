# Changelog

All notable changes to this project are documented in this file.

The format follows Keep a Changelog and the project adheres to Semantic Versioning.

## [Unreleased]
### Added
- Compile-only `examples/v2_api_compile` sketch to exercise the native v2 API surface in CI.
- Lifecycle and overlap coverage in the Unity test sketch for skip, queue-one, allow-parallel, background no-`tick()`, explicit shutdown, and v1 compatibility cleanup behavior.
- Optional built-in `ESPWorker` async backend selection through `SchedulerConfig`.

### Changed
- CI now runs on every branch push and is split into PlatformIO v2 API builds, PlatformIO example builds, PlatformIO device test sketch builds, Arduino CLI v2 API builds, and Arduino CLI example builds.
- Background command submission now treats a full control queue as `QueueFull` immediately instead of waiting for the control timeout window.
- `SchedulerCore` now uses a scheduler-owned job id index, explicit pending-schedule bookkeeping, direct-indexed completion events, and heap-top validation instead of recovering hot-path state through whole-container scans.

### Fixed
- Scheduler reschedule paths now clear `hasNext` if a due-heap insertion fails, so jobs are retried on the next dispatch pass instead of getting stuck in a primed-but-undispatchable state.
- PSRAM task-stack flags are now honored by the scheduler service task, worker-pool executor, and dedicated-task executor when the platform supports external task stacks.
- Background next-deadline lookup now purges stale heap entries lazily and returns the actual earliest valid due job without scanning the heap.
- Completion events now validate slot index, job id, and generation directly, which keeps stale completions from touching reused slots.

## [2.0.0] - 2026-03-27
### Added
- New `ESPScheduler` v2 API with explicit `begin()` / `end()` lifecycle and `SchedulerResult<T>` return types.
- `ScheduleSpec`/`ScheduleCalculator` split so schedule validation and next-occurrence math are independent from runtime state.
- `SchedulerCore` with min-heap due tracking, centralized job ownership, generation counters, and explicit overlap handling.
- `SchedulerMode::Manual` and `SchedulerMode::Background`.
- `DispatchPolicy` and `OverlapPolicy`.
- Built-in worker-pool executor, dedicated-task executor, and `ESPWorkerExecutorAdapter`.
- Background scheduler service with one scheduler task, command queue, and event queue.
- Job-id-based `getJobInfo()` with runtime state fields for debugging.
- New v2-native examples for manual mode, background worker pool, `ESPWorker` adapter, shutdown, and v1 compatibility.
- `ESPSchedulerV1Compat` wrapper for legacy code paths.

### Changed
- Async scheduling no longer defaults to one FreeRTOS task per scheduled job.
- Background mode no longer requires `tick()`.
- Public docs now describe v2 as the primary API surface.

### Fixed
- Scheduler ownership and shutdown paths are centralized instead of being spread across per-job worker tasks.
- Async completion now flows back through the scheduler core, keeping reschedule decisions in one place.

## [1.0.2] - 2025-12-07
### Added
- Clock validity guard: inline and worker jobs stay idle until the wall clock reaches a configurable minimum (default 2020-01-01 UTC) to prevent catch-up storms when SNTP sets time after boot.
- `std::function` callback overloads for `addJob`/`addJobOnceUtc` to allow capturing lambdas.
- `std::function<void()>` overloads for `addJob`/`addJobOnceUtc` to allow no-arg lambdas.
- Added focused recurring-pattern examples for every minute, every day, every hour, selected weekdays, and work-hour interval schedules.
- `deinit()` plus destructor cleanup for deterministic job teardown, including worker task destruction.
- `ESPSchedulerConfig` with `usePSRAMBuffers` toggle to route scheduler-owned dynamic buffers through ESPBufferManager (safe fallback to default heap when PSRAM is unavailable).
- Additive constructor overloads that accept `ESPSchedulerConfig` while preserving existing constructor signatures.
- `isInitialized()` lifecycle state on `ESPScheduler`, including explicit teardown/re-init behavior after `deinit()`.
- Lifecycle Unity tests for teardown safety (`deinit` before use, repeated `deinit`, and re-init by scheduling again).

[Unreleased]: https://github.com/ESPToolKit/esp-scheduler/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/ESPToolKit/esp-scheduler/compare/v1.0.2...v2.0.0
