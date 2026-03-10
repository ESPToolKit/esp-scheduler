#include "esp_scheduler/scheduler.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace {
constexpr int64_t kMaxSearchMinutes = 366 * 24 * 60;
constexpr int64_t kMaxSunSearchDays = 732;
constexpr int64_t kMaxMoonSearchMinutes = 62 * 24 * 60;
constexpr int64_t kWorkerSleepChunkSeconds = 60;
constexpr int kMinSunOffsetMinutes = -1440;
constexpr int kMaxSunOffsetMinutes = 1440;
constexpr int kMinMoonPhaseAngle = 0;
constexpr int kMaxMoonPhaseAngle = 359;
constexpr int kMaxMoonPhaseTolerance = 30;
constexpr double kMinIlluminationPercent = 0.0;
constexpr double kMaxIlluminationPercent = 100.0;
constexpr double kMaxIlluminationTolerancePercent = 50.0;
constexpr double kFullCircleDegrees = 360.0;
constexpr double kComparisonEpsilon = 1e-9;

bool clockValidForMin(const DateTime& nowUtc, int64_t minValidEpochSeconds) {
    return nowUtc.epochSeconds >= minValidEpochSeconds;
}

ScheduleKind resolvedScheduleKind(const Schedule& schedule) {
    if (schedule.kind == ScheduleKind::Cron && schedule.isOneShot) {
        return ScheduleKind::OneShotUtc;
    }
    return schedule.kind;
}

bool isOneShotSchedule(const Schedule& schedule) {
    return resolvedScheduleKind(schedule) == ScheduleKind::OneShotUtc;
}

DateTime roundToNextMinute(const ESPDate& date, const DateTime& fromUtc) {
    DateTime rounded = fromUtc;
    if (fromUtc.secondUtc() > 0) {
        rounded = date.addMinutes(rounded, 1);
    }
    return date.setTimeOfDayUtc(rounded, rounded.hourUtc(), rounded.minuteUtc(), 0);
}

double normalizeAngle360(double angle) {
    double normalized = std::fmod(angle, kFullCircleDegrees);
    if (normalized < 0.0) {
        normalized += kFullCircleDegrees;
    }
    return normalized;
}

double unwrapAngle(double previousUnwrapped, double currentWrapped) {
    const double previousWrapped = normalizeAngle360(previousUnwrapped);
    double delta = currentWrapped - previousWrapped;
    if (delta > 180.0) {
        delta -= kFullCircleDegrees;
    } else if (delta < -180.0) {
        delta += kFullCircleDegrees;
    }
    return previousUnwrapped + delta;
}

bool valueWithinPeriodicWindow(double value, double center, double tolerance) {
    const double distance = std::fabs(value - center);
    double wrapped = std::fmod(distance, kFullCircleDegrees);
    if (wrapped < 0.0) {
        wrapped += kFullCircleDegrees;
    }
    const double minimumDistance = std::min(wrapped, kFullCircleDegrees - wrapped);
    return minimumDistance <= tolerance + kComparisonEpsilon;
}

bool segmentIntersectsRange(double a, double b, double minValue, double maxValue) {
    const double lo = std::min(a, b);
    const double hi = std::max(a, b);
    return hi >= minValue - kComparisonEpsilon && lo <= maxValue + kComparisonEpsilon;
}

bool segmentIntersectsPeriodicWindow(double a, double b, double center, double tolerance) {
    const double lo = std::min(a, b);
    const double hi = std::max(a, b);
    const int64_t firstPeriod =
        static_cast<int64_t>(std::floor((lo - (center + tolerance)) / kFullCircleDegrees)) - 1;
    const int64_t lastPeriod =
        static_cast<int64_t>(std::ceil((hi - (center - tolerance)) / kFullCircleDegrees)) + 1;
    for (int64_t period = firstPeriod; period <= lastPeriod; ++period) {
        const double periodShift = static_cast<double>(period) * kFullCircleDegrees;
        const double windowMin = center - tolerance + periodShift;
        const double windowMax = center + tolerance + periodShift;
        if (segmentIntersectsRange(lo, hi, windowMin, windowMax)) {
            return true;
        }
    }
    return false;
}

bool computeNextCronOccurrenceForDate(const ESPDate& date,
                                      const Schedule& schedule,
                                      const DateTime& fromUtc,
                                      DateTime& outNextUtc) {
    const DateTime rounded = roundToNextMinute(date, fromUtc);
    DateTime cursor = rounded;
    for (int64_t i = 0; i < kMaxSearchMinutes; ++i) {
        const int month = date.getMonthLocal(cursor);
        const int day = date.getDayLocal(cursor);
        const int dow = date.getWeekdayLocal(cursor);

        const DateTime startOfDay = date.startOfDayLocal(cursor);
        const int64_t minutesIntoDay = date.differenceInMinutes(cursor, startOfDay);
        if (minutesIntoDay < 0) {
            cursor = date.addMinutes(cursor, 1);
            continue;
        }
        const int hour = static_cast<int>(minutesIntoDay / 60);
        const int minute = static_cast<int>(minutesIntoDay % 60);

        const bool monthOk = schedule.month.matches(month);
        const bool hourOk = schedule.hour.matches(hour);
        const bool minuteOk = schedule.minute.matches(minute);

        const bool domAny = schedule.dayOfMonth.isAny();
        const bool dowAny = schedule.dayOfWeek.isAny();
        const bool domOk = schedule.dayOfMonth.matches(day);
        const bool dowOk = schedule.dayOfWeek.matches(dow);

        bool dayOk = false;
        if (domAny && dowAny) {
            dayOk = true;
        } else if (domAny && !dowAny) {
            dayOk = dowOk;
        } else if (!domAny && dowAny) {
            dayOk = domOk;
        } else {
            dayOk = domOk || dowOk;
        }

        if (monthOk && hourOk && minuteOk && dayOk) {
            outNextUtc = date.setTimeOfDayLocal(cursor, hour, minute, 0);
            return true;
        }
        cursor = date.addMinutes(cursor, 1);
    }
    return false;
}

bool computeNextSunOccurrenceForDate(const ESPDate& date,
                                     const Schedule& schedule,
                                     const DateTime& fromUtc,
                                     DateTime& outNextUtc,
                                     bool sunrise) {
    const DateTime rounded = roundToNextMinute(date, fromUtc);
    const DateTime startDay = date.startOfDayLocal(rounded);
    for (int64_t dayOffset = 0; dayOffset < kMaxSunSearchDays; ++dayOffset) {
        const DateTime dayCursor = date.addDays(startDay, static_cast<int32_t>(dayOffset));
        const SunCycleResult cycle = sunrise ? date.sunrise(dayCursor) : date.sunset(dayCursor);
        if (!cycle.ok) {
            continue;
        }
        const DateTime candidate = date.addMinutes(cycle.value, schedule.sunOffsetMinutes);
        if (date.isBefore(candidate, rounded)) {
            continue;
        }
        outNextUtc = candidate;
        return true;
    }
    return false;
}

bool moonPhaseCrossed(double previousUnwrapped,
                      double currentUnwrapped,
                      double targetAngleDegrees,
                      double toleranceDegrees) {
    if (valueWithinPeriodicWindow(previousUnwrapped, targetAngleDegrees, toleranceDegrees)) {
        return false;
    }
    return segmentIntersectsPeriodicWindow(previousUnwrapped, currentUnwrapped, targetAngleDegrees, toleranceDegrees);
}

bool moonIlluminationCrossed(double previousValue, double currentValue, double targetPercent, double tolerancePercent) {
    const double minWindow = targetPercent - tolerancePercent;
    const double maxWindow = targetPercent + tolerancePercent;
    const bool wasInside = previousValue >= minWindow - kComparisonEpsilon && previousValue <= maxWindow + kComparisonEpsilon;
    if (wasInside) {
        return false;
    }
    return segmentIntersectsRange(previousValue, currentValue, minWindow, maxWindow);
}

bool computeNextMoonPhaseOccurrenceForDate(const ESPDate& date,
                                           const Schedule& schedule,
                                           const DateTime& fromUtc,
                                           DateTime& outNextUtc) {
    const DateTime rounded = roundToNextMinute(date, fromUtc);
    DateTime previous = date.addMinutes(rounded, -1);
    DateTime current = rounded;

    MoonPhaseResult previousPhase = date.moonPhase(previous);
    if (!previousPhase.ok) {
        return false;
    }
    double previousUnwrapped = static_cast<double>(previousPhase.angleDegrees);

    for (int64_t i = 0; i < kMaxMoonSearchMinutes; ++i) {
        MoonPhaseResult currentPhase = date.moonPhase(current);
        if (!currentPhase.ok) {
            return false;
        }
        const double currentUnwrapped = unwrapAngle(previousUnwrapped, static_cast<double>(currentPhase.angleDegrees));
        if (moonPhaseCrossed(previousUnwrapped,
                             currentUnwrapped,
                             static_cast<double>(schedule.moonPhaseAngleDegrees),
                             static_cast<double>(schedule.moonPhaseToleranceDegrees))) {
            outNextUtc = current;
            return true;
        }
        previousUnwrapped = currentUnwrapped;
        current = date.addMinutes(current, 1);
    }
    return false;
}

bool computeNextMoonIlluminationOccurrenceForDate(const ESPDate& date,
                                                  const Schedule& schedule,
                                                  const DateTime& fromUtc,
                                                  DateTime& outNextUtc) {
    const DateTime rounded = roundToNextMinute(date, fromUtc);
    DateTime previous = date.addMinutes(rounded, -1);
    DateTime current = rounded;

    MoonPhaseResult previousPhase = date.moonPhase(previous);
    if (!previousPhase.ok) {
        return false;
    }
    double previousIllumination = previousPhase.illumination * kMaxIlluminationPercent;

    for (int64_t i = 0; i < kMaxMoonSearchMinutes; ++i) {
        MoonPhaseResult currentPhase = date.moonPhase(current);
        if (!currentPhase.ok) {
            return false;
        }
        const double currentIllumination = currentPhase.illumination * kMaxIlluminationPercent;
        if (moonIlluminationCrossed(previousIllumination,
                                    currentIllumination,
                                    schedule.moonIlluminationTargetPercent,
                                    schedule.moonIlluminationTolerancePercent)) {
            outNextUtc = current;
            return true;
        }
        previousIllumination = currentIllumination;
        current = date.addMinutes(current, 1);
    }
    return false;
}

int moonPhaseAngleForName(MoonPhaseName name) {
    switch (name) {
        case MoonPhaseName::NewMoon:
            return 0;
        case MoonPhaseName::WaxingCrescent:
            return 45;
        case MoonPhaseName::FirstQuarter:
            return 90;
        case MoonPhaseName::WaxingGibbous:
            return 135;
        case MoonPhaseName::FullMoon:
            return 180;
        case MoonPhaseName::WaningGibbous:
            return 225;
        case MoonPhaseName::LastQuarter:
            return 270;
        case MoonPhaseName::WaningCrescent:
            return 315;
        default:
            return 0;
    }
}

bool computeNextOccurrenceForDate(const ESPDate& date,
                                  const Schedule& schedule,
                                  const DateTime& fromUtc,
                                  DateTime& outNextUtc) {
    switch (resolvedScheduleKind(schedule)) {
        case ScheduleKind::OneShotUtc:
            outNextUtc = schedule.onceAtUtc;
            return true;
        case ScheduleKind::Cron:
            return computeNextCronOccurrenceForDate(date, schedule, fromUtc, outNextUtc);
        case ScheduleKind::Sunrise:
            return computeNextSunOccurrenceForDate(date, schedule, fromUtc, outNextUtc, true);
        case ScheduleKind::Sunset:
            return computeNextSunOccurrenceForDate(date, schedule, fromUtc, outNextUtc, false);
        case ScheduleKind::MoonPhaseAngle:
            return computeNextMoonPhaseOccurrenceForDate(date, schedule, fromUtc, outNextUtc);
        case ScheduleKind::MoonIlluminationPercent:
            return computeNextMoonIlluminationOccurrenceForDate(date, schedule, fromUtc, outNextUtc);
        default:
            return false;
    }
}
}  // namespace

ScheduleField ScheduleField::any() {
    ScheduleField f;
    f.m_isAny = true;
    f.m_mask = 0;
    return f;
}

ScheduleField ScheduleField::only(int value) {
    ScheduleField f;
    if (value < 0 || value > 63) {
        return f;
    }
    f.m_mask = 1ULL << value;
    return f;
}

ScheduleField ScheduleField::range(int from, int to) {
    ScheduleField f;
    if (from < 0 || to < 0 || from > to || to > 63) {
        return f;
    }
    for (int i = from; i <= to; ++i) {
        f.m_mask |= 1ULL << i;
    }
    return f;
}

ScheduleField ScheduleField::every(int step) {
    ScheduleField f;
    if (step <= 0) {
        return f;
    }
    for (int i = 0; i <= 63; i += step) {
        f.m_mask |= 1ULL << i;
    }
    return f;
}

ScheduleField ScheduleField::rangeEvery(int from, int to, int step) {
    ScheduleField f;
    if (step <= 0 || from < 0 || to < 0 || from > to || to > 63) {
        return f;
    }
    for (int i = from; i <= to; i += step) {
        f.m_mask |= 1ULL << i;
    }
    return f;
}

ScheduleField ScheduleField::list(const int* values, size_t count) {
    ScheduleField f;
    if (!values || count == 0) {
        return f;
    }
    for (size_t i = 0; i < count; ++i) {
        const int v = values[i];
        if (v < 0 || v > 63) {
            f.m_mask = 0;
            return f;
        }
        f.m_mask |= 1ULL << v;
    }
    return f;
}

bool ScheduleField::matches(int value) const {
    if (m_isAny) {
        return true;
    }
    if (value < 0 || value > 63) {
        return false;
    }
    return (m_mask & (1ULL << value)) != 0;
}

Schedule Schedule::onceUtc(const DateTime& whenUtc) {
    Schedule s;
    s.kind = ScheduleKind::OneShotUtc;
    s.isOneShot = true;
    s.onceAtUtc = whenUtc;
    return s;
}

Schedule Schedule::dailyAtLocal(int hour, int minute) {
    Schedule s;
    s.kind = ScheduleKind::Cron;
    s.isOneShot = false;
    s.hour = ScheduleField::only(hour);
    s.minute = ScheduleField::only(minute);
    s.dayOfMonth = ScheduleField::any();
    s.month = ScheduleField::any();
    s.dayOfWeek = ScheduleField::any();
    return s;
}

Schedule Schedule::weeklyAtLocal(uint8_t dowMask, int hour, int minute) {
    int days[7];
    size_t count = 0;
    for (int i = 0; i < 7; ++i) {
        if (dowMask & (1 << i)) {
            days[count++] = i;
        }
    }
    Schedule s;
    s.kind = ScheduleKind::Cron;
    s.isOneShot = false;
    s.hour = ScheduleField::only(hour);
    s.minute = ScheduleField::only(minute);
    s.dayOfMonth = ScheduleField::any();
    s.month = ScheduleField::any();
    if (count == 0) {
        s.dayOfWeek = ScheduleField::any();
    } else {
        s.dayOfWeek = ScheduleField::list(days, count);
    }
    return s;
}

Schedule Schedule::monthlyOnDayLocal(int dayOfMonth, int hour, int minute) {
    Schedule s;
    s.kind = ScheduleKind::Cron;
    s.isOneShot = false;
    int clamped = dayOfMonth;
    if (clamped < 1) {
        clamped = 1;
    } else if (clamped > 31) {
        clamped = 31;
    }
    s.dayOfMonth = ScheduleField::only(clamped);
    s.hour = ScheduleField::only(hour);
    s.minute = ScheduleField::only(minute);
    s.month = ScheduleField::any();
    s.dayOfWeek = ScheduleField::any();
    return s;
}

Schedule Schedule::sunrise(int offsetMinutes) {
    Schedule s;
    s.kind = ScheduleKind::Sunrise;
    s.isOneShot = false;
    s.sunOffsetMinutes = offsetMinutes;
    return s;
}

Schedule Schedule::sunset(int offsetMinutes) {
    Schedule s;
    s.kind = ScheduleKind::Sunset;
    s.isOneShot = false;
    s.sunOffsetMinutes = offsetMinutes;
    return s;
}

Schedule Schedule::moonPhaseAngle(int angleDegrees, int toleranceDegrees) {
    Schedule s;
    s.kind = ScheduleKind::MoonPhaseAngle;
    s.isOneShot = false;
    s.moonPhaseAngleDegrees = angleDegrees;
    s.moonPhaseToleranceDegrees = toleranceDegrees;
    return s;
}

Schedule Schedule::moonPhase(MoonPhaseName name, int toleranceDegrees) {
    return moonPhaseAngle(moonPhaseAngleForName(name), toleranceDegrees);
}

Schedule Schedule::moonIlluminationPercent(double percent, double tolerancePercent) {
    Schedule s;
    s.kind = ScheduleKind::MoonIlluminationPercent;
    s.isOneShot = false;
    s.moonIlluminationTargetPercent = percent;
    s.moonIlluminationTolerancePercent = tolerancePercent;
    return s;
}

Schedule Schedule::custom(const ScheduleField& minute,
                          const ScheduleField& hour,
                          const ScheduleField& dom,
                          const ScheduleField& month,
                          const ScheduleField& dow) {
    Schedule s;
    s.kind = ScheduleKind::Cron;
    s.isOneShot = false;
    s.minute = minute;
    s.hour = hour;
    s.dayOfMonth = dom;
    s.month = month;
    s.dayOfWeek = dow;
    return s;
}

ESPScheduler::ESPScheduler(ESPDate& date, ESPWorker* worker)
    : ESPScheduler(date, worker, ESPSchedulerConfig{}) {}

ESPScheduler::ESPScheduler(ESPDate& date, const ESPSchedulerConfig& config)
    : ESPScheduler(date, nullptr, config) {}

ESPScheduler::ESPScheduler(ESPDate& date, ESPWorker* worker, const ESPSchedulerConfig& config)
    : m_date(date),
      m_minValidEpochSecondsRef(std::make_shared<std::atomic<int64_t>>(kDefaultMinValidEpochSeconds)),
      usePSRAMBuffers_(config.usePSRAMBuffers),
      m_inlineJobs(SchedulerAllocator<InlineJob>(usePSRAMBuffers_)),
      m_workerJobs(SchedulerAllocator<WorkerJob>(usePSRAMBuffers_)) {
    (void)worker;
}

ESPScheduler::~ESPScheduler() {
    deinit();
}

void ESPScheduler::deinit() {
    if (!m_initialized.exchange(false, std::memory_order_relaxed)) {
        return;
    }

    for (auto& job : m_inlineJobs) {
        job.finished = true;
    }
    for (auto& job : m_workerJobs) {
        if (job.context) {
            job.context->cancelRequested.store(true);
        }
    }
    cleanupInline();
    m_workerJobs.clear();

    SchedulerVector<InlineJob>(SchedulerAllocator<InlineJob>(usePSRAMBuffers_)).swap(m_inlineJobs);
    SchedulerVector<WorkerJob>(SchedulerAllocator<WorkerJob>(usePSRAMBuffers_)).swap(m_workerJobs);
    m_nextId = 1;
}

bool ESPScheduler::isInitialized() const {
    return m_initialized.load(std::memory_order_relaxed);
}

void ESPScheduler::ensureInitialized() {
    if (!isInitialized()) {
        m_initialized.store(true, std::memory_order_relaxed);
    }
}

void ESPScheduler::setMinValidUnixSeconds(int64_t minEpochSeconds) {
    m_minValidEpochSeconds = minEpochSeconds;
    if (m_minValidEpochSecondsRef) {
        m_minValidEpochSecondsRef->store(minEpochSeconds);
    }
}

void ESPScheduler::setMinValidUtc(const DateTime& minUtc) {
    setMinValidUnixSeconds(minUtc.epochSeconds);
}

int64_t ESPScheduler::minValidUnixSeconds() const {
    return m_minValidEpochSeconds;
}

uint32_t ESPScheduler::nextId() {
    if (m_nextId == 0) {
        m_nextId = 1;
    }
    return m_nextId++;
}

bool ESPScheduler::fieldWithinRange(const ScheduleField& field, int min, int max) const {
    if (field.isAny()) {
        return true;
    }
    const uint64_t mask = field.rawMask();
    const uint64_t allowed = allowedMask(min, max);
    return mask != 0 && (mask & allowed) != 0;
}

uint64_t ESPScheduler::allowedMask(int min, int max) const {
    if (min < 0) {
        min = 0;
    }
    if (max > 63) {
        max = 63;
    }
    if (max >= 63) {
        return ~static_cast<uint64_t>(0);
    }
    const uint64_t upper = (1ULL << (max + 1)) - 1;
    const uint64_t lower = (min == 0) ? 0 : ((1ULL << min) - 1);
    return upper & ~lower;
}

bool ESPScheduler::validateSchedule(const Schedule& schedule) const {
    const ScheduleKind kind = resolvedScheduleKind(schedule);
    switch (kind) {
        case ScheduleKind::OneShotUtc:
            return true;
        case ScheduleKind::Cron: {
            const bool minuteOk = fieldWithinRange(schedule.minute, 0, 59);
            const bool hourOk = fieldWithinRange(schedule.hour, 0, 23);
            const bool domOk = fieldWithinRange(schedule.dayOfMonth, 1, 31);
            const bool monthOk = fieldWithinRange(schedule.month, 1, 12);
            const bool dowOk = fieldWithinRange(schedule.dayOfWeek, 0, 6);
            return minuteOk && hourOk && domOk && monthOk && dowOk;
        }
        case ScheduleKind::Sunrise:
        case ScheduleKind::Sunset:
            return schedule.sunOffsetMinutes >= kMinSunOffsetMinutes && schedule.sunOffsetMinutes <= kMaxSunOffsetMinutes;
        case ScheduleKind::MoonPhaseAngle:
            return schedule.moonPhaseAngleDegrees >= kMinMoonPhaseAngle &&
                   schedule.moonPhaseAngleDegrees <= kMaxMoonPhaseAngle &&
                   schedule.moonPhaseToleranceDegrees >= 0 &&
                   schedule.moonPhaseToleranceDegrees <= kMaxMoonPhaseTolerance;
        case ScheduleKind::MoonIlluminationPercent:
            return std::isfinite(schedule.moonIlluminationTargetPercent) &&
                   std::isfinite(schedule.moonIlluminationTolerancePercent) &&
                   schedule.moonIlluminationTargetPercent >= kMinIlluminationPercent &&
                   schedule.moonIlluminationTargetPercent <= kMaxIlluminationPercent &&
                   schedule.moonIlluminationTolerancePercent > 0.0 &&
                   schedule.moonIlluminationTolerancePercent <= kMaxIlluminationTolerancePercent;
        default:
            return false;
    }
}

uint32_t ESPScheduler::addJobOnceUtc(const DateTime& whenUtc,
                                     SchedulerJobMode mode,
                                     SchedulerCallback cb,
                                     void* userData,
                                     const SchedulerTaskConfig* taskCfg) {
    return addJobOnceUtc(whenUtc, mode, SchedulerFunction(cb), userData, taskCfg);
}

uint32_t ESPScheduler::addJobOnceUtc(const DateTime& whenUtc,
                                     SchedulerJobMode mode,
                                     SchedulerFunction cb,
                                     void* userData,
                                     const SchedulerTaskConfig* taskCfg) {
    Schedule s = Schedule::onceUtc(whenUtc);
    return addJob(s, mode, std::move(cb), userData, taskCfg);
}

uint32_t ESPScheduler::addJobOnceUtc(const DateTime& whenUtc,
                                     SchedulerJobMode mode,
                                     SchedulerFunctionNoData cb,
                                     const SchedulerTaskConfig* taskCfg) {
    if (!cb) {
        return 0;
    }
    SchedulerFunction wrapped = [fn = std::move(cb)](void*) { fn(); };
    return addJobOnceUtc(whenUtc, mode, std::move(wrapped), nullptr, taskCfg);
}

uint32_t ESPScheduler::addJob(const Schedule& schedule,
                              SchedulerJobMode mode,
                              SchedulerCallback cb,
                              void* userData,
                              const SchedulerTaskConfig* taskCfg) {
    return addJob(schedule, mode, SchedulerFunction(cb), userData, taskCfg);
}

uint32_t ESPScheduler::addJob(const Schedule& schedule,
                              SchedulerJobMode mode,
                              SchedulerFunction cb,
                              void* userData,
                              const SchedulerTaskConfig* taskCfg) {
    if (!cb) {
        return 0;
    }
    if (!validateSchedule(schedule)) {
        return 0;
    }
    ensureInitialized();
    const uint32_t id = nextId();

    if (mode == SchedulerJobMode::Inline) {
        InlineJob job{};
        job.id = id;
        job.schedule = schedule;
        job.callback = std::move(cb);
        job.userData = userData;
        m_inlineJobs.push_back(job);
        return id;
    }

    auto ctx = std::allocate_shared<WorkerJobContext>(SchedulerAllocator<WorkerJobContext>(usePSRAMBuffers_));
    ctx->schedule = schedule;
    ctx->callback = std::move(cb);
    ctx->userData = userData;
    ctx->date = &m_date;
    ctx->minValidEpochSeconds = m_minValidEpochSecondsRef;

    const SchedulerTaskConfig runtimeCfg = makeTaskConfig(taskCfg);
    auto* taskCtx = new (std::nothrow) std::shared_ptr<WorkerJobContext>(ctx);
    if (!taskCtx) {
        return 0;
    }
    TaskHandle_t taskHandle = nullptr;
    const BaseType_t created = xTaskCreatePinnedToCore(
        &ESPScheduler::workerTaskEntry,
        runtimeCfg.name ? runtimeCfg.name : "sched-job",
        runtimeCfg.stackSize,
        taskCtx,
        runtimeCfg.priority,
        &taskHandle,
        runtimeCfg.coreId);
    if (created != pdPASS || taskHandle == nullptr) {
        delete taskCtx;
        return 0;
    }

    WorkerJob job{};
    job.id = id;
    job.context = ctx;
    job.task = taskHandle;
    m_workerJobs.push_back(job);
    return id;
}

uint32_t ESPScheduler::addJob(const Schedule& schedule,
                              SchedulerJobMode mode,
                              SchedulerFunctionNoData cb,
                              const SchedulerTaskConfig* taskCfg) {
    if (!cb) {
        return 0;
    }
    SchedulerFunction wrapped = [fn = std::move(cb)](void*) { fn(); };
    return addJob(schedule, mode, std::move(wrapped), nullptr, taskCfg);
}

bool ESPScheduler::cancelJob(uint32_t jobId) {
    if (!isInitialized()) {
        return false;
    }

    bool canceled = false;
    for (auto& job : m_inlineJobs) {
        if (job.id == jobId && !job.finished) {
            job.finished = true;
            canceled = true;
        }
    }
    for (auto& job : m_workerJobs) {
        if (job.id == jobId && job.context) {
            job.context->cancelRequested.store(true);
            canceled = true;
        }
    }
    if (canceled) {
        cleanupInline();
        cleanupWorkers();
    }
    return canceled;
}

bool ESPScheduler::pauseJob(uint32_t jobId) {
    if (!isInitialized()) {
        return false;
    }

    for (auto& job : m_inlineJobs) {
        if (job.id == jobId && !job.finished) {
            job.paused = true;
            return true;
        }
    }
    for (auto& job : m_workerJobs) {
        if (job.id == jobId && job.context) {
            job.context->paused.store(true);
            return true;
        }
    }
    return false;
}

bool ESPScheduler::resumeJob(uint32_t jobId) {
    if (!isInitialized()) {
        return false;
    }

    for (auto& job : m_inlineJobs) {
        if (job.id == jobId && !job.finished) {
            job.paused = false;
            return true;
        }
    }
    for (auto& job : m_workerJobs) {
        if (job.id == jobId && job.context) {
            job.context->paused.store(false);
            return true;
        }
    }
    return false;
}

void ESPScheduler::cancelAll() {
    if (!isInitialized()) {
        return;
    }

    for (auto& job : m_inlineJobs) {
        job.finished = true;
    }
    for (auto& job : m_workerJobs) {
        if (job.context) {
            job.context->cancelRequested.store(true);
        }
    }
    cleanupInline();
    m_workerJobs.clear();
}

void ESPScheduler::tick() { tick(m_date.now()); }

void ESPScheduler::tick(const DateTime& nowUtc) {
    if (!isInitialized()) {
        return;
    }

    if (!clockValid(nowUtc)) {
        return;
    }

    for (auto& job : m_inlineJobs) {
        if (job.finished || job.paused) {
            continue;
        }
        if (!job.hasNext) {
            if (isOneShotSchedule(job.schedule)) {
                job.nextRunUtc = job.schedule.onceAtUtc;
                job.hasNext = true;
            } else {
                job.hasNext = computeNextOccurrence(job.schedule, nowUtc, job.nextRunUtc);
                if (!job.hasNext) {
                    job.finished = true;
                    continue;
                }
            }
        }

        if (m_date.isAfter(job.nextRunUtc, nowUtc)) {
            continue;
        }
        job.callback(job.userData);
        if (isOneShotSchedule(job.schedule)) {
            job.finished = true;
            continue;
        }
        DateTime from = m_date.addMinutes(job.nextRunUtc, 1);
        job.hasNext = computeNextOccurrence(job.schedule, from, job.nextRunUtc);
        if (!job.hasNext) {
            job.finished = true;
        }
    }

    cleanupInline();
    cleanupWorkers();
}

void ESPScheduler::cleanup() {
    if (!isInitialized()) {
        return;
    }

    cleanupInline();
    cleanupWorkers();
}

bool ESPScheduler::getJobInfo(size_t index, JobInfo& out) const {
    if (!isInitialized()) {
        out = JobInfo{};
        return false;
    }

    out = JobInfo{};
    size_t current = 0;
    auto fillNext = [this](const Schedule& schedule, bool hasNext, const DateTime& storedNext, DateTime& outNext) {
        if (hasNext) {
            outNext = storedNext;
            return;
        }
        if (isOneShotSchedule(schedule)) {
            outNext = schedule.onceAtUtc;
            return;
        }
        DateTime computed{};
        if (computeNextOccurrence(schedule, m_date.now(), computed)) {
            outNext = computed;
        } else {
            outNext = {};
        }
    };

    for (const auto& job : m_inlineJobs) {
        if (job.finished) {
            continue;
        }
        if (current == index) {
            out.id = job.id;
            out.enabled = !job.paused;
            out.mode = SchedulerJobMode::Inline;
            out.schedule = job.schedule;
            fillNext(job.schedule, job.hasNext, job.nextRunUtc, out.nextRunUtc);
            return true;
        }
        ++current;
    }

    for (const auto& job : m_workerJobs) {
        if (!job.context) {
            continue;
        }
        if (job.context->cancelRequested.load() || job.context->finished.load()) {
            continue;
        }
        if (current == index) {
            out.id = job.id;
            out.enabled = !job.context->paused.load();
            out.mode = SchedulerJobMode::WorkerTask;
            out.schedule = job.context->schedule;
            fillNext(job.context->schedule, job.context->hasNext, job.context->nextRunUtc, out.nextRunUtc);
            return true;
        }
        ++current;
    }

    return false;
}

bool ESPScheduler::computeNextOccurrence(const Schedule& schedule,
                                         const DateTime& fromUtc,
                                         DateTime& outNextUtc) const {
    return computeNextOccurrenceForDate(m_date, schedule, fromUtc, outNextUtc);
}

void ESPScheduler::runWorkerJob(const std::shared_ptr<WorkerJobContext>& ctx) {
    if (!ctx || !ctx->date) {
        return;
    }

    ESPDate& date = *ctx->date;
    while (!ctx->cancelRequested.load()) {
        DateTime now = date.now();
        const int64_t minValidEpochSeconds =
            ctx->minValidEpochSeconds ? ctx->minValidEpochSeconds->load() : kDefaultMinValidEpochSeconds;
        if (!clockValidForMin(now, minValidEpochSeconds)) {
            vTaskDelay(pdMS_TO_TICKS(kWorkerSleepChunkSeconds * 1000));
            continue;
        }
        if (!ctx->hasNext) {
            if (isOneShotSchedule(ctx->schedule)) {
                ctx->nextRunUtc = ctx->schedule.onceAtUtc;
                ctx->hasNext = true;
            } else {
                ctx->hasNext = computeNextOccurrenceForDate(date, ctx->schedule, now, ctx->nextRunUtc);
                if (!ctx->hasNext) {
                    break;
                }
            }
        }

        if (ctx->paused.load()) {
            vTaskDelay(pdMS_TO_TICKS(kWorkerSleepChunkSeconds * 1000));
            continue;
        }

        const int64_t diffSec = date.differenceInSeconds(ctx->nextRunUtc, now);
        if (diffSec > 0) {
            const int64_t chunk = (diffSec > kWorkerSleepChunkSeconds) ? kWorkerSleepChunkSeconds : diffSec;
            vTaskDelay(pdMS_TO_TICKS(static_cast<TickType_t>(chunk * 1000)));
            continue;
        }

        ctx->callback(ctx->userData);

        if (isOneShotSchedule(ctx->schedule)) {
            break;
        }
        DateTime from = date.addMinutes(ctx->nextRunUtc, 1);
        ctx->hasNext = computeNextOccurrenceForDate(date, ctx->schedule, from, ctx->nextRunUtc);
        if (!ctx->hasNext) {
            break;
        }
    }
    ctx->finished.store(true);
}

bool ESPScheduler::clockValid(const DateTime& nowUtc) const {
    return clockValidForMin(nowUtc, m_minValidEpochSeconds);
}

SchedulerTaskConfig ESPScheduler::makeTaskConfig(const SchedulerTaskConfig* taskCfg) const {
    SchedulerTaskConfig cfg{};
    cfg.stackSize = taskCfg ? taskCfg->stackSize : SchedulerTaskConfig{}.stackSize;
    cfg.priority = taskCfg ? taskCfg->priority : SchedulerTaskConfig{}.priority;
    cfg.coreId = taskCfg ? taskCfg->coreId : SchedulerTaskConfig{}.coreId;
    cfg.usePsramStack = taskCfg ? taskCfg->usePsramStack : SchedulerTaskConfig{}.usePsramStack;
    cfg.name = taskCfg && taskCfg->name ? taskCfg->name : "sched-job";
    return cfg;
}

void ESPScheduler::workerTaskEntry(void* arg) {
    auto* ctxPtr = static_cast<std::shared_ptr<WorkerJobContext>*>(arg);
    if (!ctxPtr) {
        vTaskDelete(nullptr);
        return;
    }
    std::shared_ptr<WorkerJobContext> ctx = *ctxPtr;
    delete ctxPtr;
    runWorkerJob(ctx);
    vTaskDelete(nullptr);
}

void ESPScheduler::cleanupInline() {
    m_inlineJobs.erase(std::remove_if(m_inlineJobs.begin(),
                                      m_inlineJobs.end(),
                                      [](const InlineJob& job) { return job.finished; }),
                       m_inlineJobs.end());
}

void ESPScheduler::cleanupWorkers() {
    m_workerJobs.erase(std::remove_if(m_workerJobs.begin(),
                                      m_workerJobs.end(),
                                      [](const WorkerJob& job) {
                                          return !job.context || job.context->finished.load() ||
                                                 job.context->cancelRequested.load();
                                      }),
                       m_workerJobs.end());
}
