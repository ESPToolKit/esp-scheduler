#pragma once

#include <Arduino.h>
#include <ESPDate.h>
#include <ESPWorker.h>

#include <atomic>
#include <functional>
#include <memory>

#include "scheduler_allocator.h"

enum class SchedulerJobMode : uint8_t {
    Inline,
    WorkerTask
};

struct SchedulerTaskConfig {
    const char* name = "sched-job";
    uint32_t stackSize = 4096;         // bytes
    UBaseType_t priority = 1;
    BaseType_t coreId = tskNO_AFFINITY;
    bool usePsramStack = false;
};

struct ESPSchedulerConfig {
    // Prefer PSRAM-backed buffers for scheduler-owned dynamic containers.
    // Falls back to default heap automatically when unavailable.
    bool usePSRAMBuffers = false;
};

using SchedulerCallback = void (*)(void* userData);
using SchedulerFunction = std::function<void(void* userData)>;
using SchedulerFunctionNoData = std::function<void()>;

class ScheduleField {
public:
    static ScheduleField any();
    static ScheduleField only(int value);
    static ScheduleField range(int from, int to);
    static ScheduleField every(int step);
    static ScheduleField rangeEvery(int from, int to, int step);
    // If any value is out of range, the field is cleared and will fail validation.
    static ScheduleField list(const int* values, size_t count);

    bool matches(int value) const;
    bool isAny() const { return m_isAny; }
    bool empty() const { return !m_isAny && m_mask == 0; }
    uint64_t rawMask() const { return m_mask; }

private:
    uint64_t m_mask = 0;
    bool m_isAny = false;
};

struct Schedule {
    bool isOneShot = false;
    DateTime onceAtUtc{};

    ScheduleField minute = ScheduleField::any();
    ScheduleField hour = ScheduleField::any();
    ScheduleField dayOfMonth = ScheduleField::any();
    ScheduleField month = ScheduleField::any();
    ScheduleField dayOfWeek = ScheduleField::any();

    static Schedule onceUtc(const DateTime& whenUtc);
    static Schedule dailyAtLocal(int hour, int minute);
    // dowMask bits: 0=Sun..6=Sat; empty mask falls back to any day of week.
    static Schedule weeklyAtLocal(uint8_t dowMask, int hour, int minute);
    static Schedule monthlyOnDayLocal(int dayOfMonth, int hour, int minute);
    static Schedule custom(const ScheduleField& minute,
                           const ScheduleField& hour,
                           const ScheduleField& dom,
                           const ScheduleField& month,
                           const ScheduleField& dow);
};

struct JobInfo {
    uint32_t id = 0;
    bool enabled = false;
    SchedulerJobMode mode = SchedulerJobMode::Inline;
    Schedule schedule{};
    DateTime nextRunUtc{};
};

class ESPScheduler {
public:
    // Default guard: block scheduling until at least 2020-01-01T00:00:00Z.
    static constexpr int64_t kDefaultMinValidEpochSeconds = 1577836800;

    ESPScheduler(ESPDate& date, ESPWorker* worker = nullptr);
    ESPScheduler(ESPDate& date, const ESPSchedulerConfig& config);
    ESPScheduler(ESPDate& date, ESPWorker* worker, const ESPSchedulerConfig& config);
    ~ESPScheduler();
    void deinit();

    // Configure / inspect the minimum valid wall-clock time; scheduler idles until time >= min.
    void setMinValidUnixSeconds(int64_t minEpochSeconds);
    void setMinValidUtc(const DateTime& minUtc);
    int64_t minValidUnixSeconds() const;

    uint32_t addJobOnceUtc(const DateTime& whenUtc,
                           SchedulerJobMode mode,
                           SchedulerCallback cb,
                           void* userData = nullptr,
                           const SchedulerTaskConfig* taskCfg = nullptr);
    uint32_t addJobOnceUtc(const DateTime& whenUtc,
                           SchedulerJobMode mode,
                           SchedulerFunction cb,
                           void* userData = nullptr,
                           const SchedulerTaskConfig* taskCfg = nullptr);
    uint32_t addJobOnceUtc(const DateTime& whenUtc,
                           SchedulerJobMode mode,
                           SchedulerFunctionNoData cb,
                           const SchedulerTaskConfig* taskCfg = nullptr);

    uint32_t addJob(const Schedule& schedule,
                    SchedulerJobMode mode,
                    SchedulerCallback cb,
                    void* userData = nullptr,
                    const SchedulerTaskConfig* taskCfg = nullptr);
    uint32_t addJob(const Schedule& schedule,
                    SchedulerJobMode mode,
                    SchedulerFunction cb,
                    void* userData = nullptr,
                    const SchedulerTaskConfig* taskCfg = nullptr);
    uint32_t addJob(const Schedule& schedule,
                    SchedulerJobMode mode,
                    SchedulerFunctionNoData cb,
                    const SchedulerTaskConfig* taskCfg = nullptr);

    bool cancelJob(uint32_t jobId);
    bool pauseJob(uint32_t jobId);
    bool resumeJob(uint32_t jobId);
    void cancelAll();

    void tick(const DateTime& nowUtc);
    void tick();
    void cleanup();

    bool computeNextOccurrence(const Schedule& schedule,
                               const DateTime& fromUtc,
                               DateTime& outNextUtc) const;

    bool getJobInfo(size_t index, JobInfo& out) const;

private:
    struct InlineJob {
        uint32_t id = 0;
        Schedule schedule{};
        SchedulerFunction callback{};
        void* userData = nullptr;
        DateTime nextRunUtc{};
        bool hasNext = false;
        bool paused = false;
        bool finished = false;
    };

    struct WorkerJobContext {
        Schedule schedule{};
        SchedulerFunction callback{};
        void* userData = nullptr;
        ESPDate* date = nullptr;
        std::shared_ptr<std::atomic<int64_t>> minValidEpochSeconds{};
        std::atomic<bool> paused{false};
        std::atomic<bool> cancelRequested{false};
        std::atomic<bool> finished{false};
        DateTime nextRunUtc{};
        bool hasNext = false;
    };

    struct WorkerJob {
        uint32_t id = 0;
        std::shared_ptr<WorkerJobContext> context{};
        WorkerResult workerResult{};
    };

    uint32_t nextId();
    bool validateSchedule(const Schedule& schedule) const;
    bool fieldWithinRange(const ScheduleField& field, int min, int max) const;
    uint64_t allowedMask(int min, int max) const;
    static void runWorkerJob(const std::shared_ptr<WorkerJobContext>& ctx);
    WorkerConfig makeWorkerConfig(const SchedulerTaskConfig* taskCfg) const;
    void cleanupInline();
    void cleanupWorkers();
    bool clockValid(const DateTime& nowUtc) const;

    ESPDate& m_date;
    ESPWorker* m_worker;
    uint32_t m_nextId = 1;
    int64_t m_minValidEpochSeconds = kDefaultMinValidEpochSeconds;
    std::shared_ptr<std::atomic<int64_t>> m_minValidEpochSecondsRef;
    bool usePSRAMBuffers_ = false;
    SchedulerVector<InlineJob> m_inlineJobs;
    SchedulerVector<WorkerJob> m_workerJobs;
};
