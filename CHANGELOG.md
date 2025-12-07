# Changelog

All notable changes to this project are documented in this file.

The format follows Keep a Changelog and the project adheres to Semantic Versioning.

## [Unreleased]

## [0.1.0] - 2025-12-07
### Added
- Initial ESPScheduler cron-style engine built atop ESPDate with inline and ESPWorker-backed task modes.
- Schedule helpers for daily, weekly (bitmask DOW), monthly, one-shot UTC triggers, and custom cron-like fields.
- Inline scheduler loop with `tick()` plus per-job pause/resume/cancel controls.
- Worker task mode that spawns a dedicated FreeRTOS task per job via ESPWorker with optional PSRAM stacks.
- Examples for inline and worker-driven jobs, README and metadata for Arduino/PlatformIO/ESP-IDF.
- CI + release workflows, issue/PR templates, and Unity smoke tests for cron matching.

[Unreleased]: https://github.com/ESPToolKit/esp-scheduler/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/ESPToolKit/esp-scheduler/releases/tag/v0.1.0
