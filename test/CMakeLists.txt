# ESPScheduler depends on ESP-IDF/Arduino FreeRTOS primitives and ESPDate for
# calendar math, so host-side builds are not supported. Run Unity tests on
# device via PlatformIO/Arduino instead.
message(STATUS "ESPScheduler: Unity tests are Arduino/PlatformIO only; skipping host build.")
