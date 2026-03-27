#include "schedule_spec.h"

namespace {
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
} // namespace

ScheduleSpec ScheduleSpec::onceUtc(const DateTime &whenUtc) {
	ScheduleSpec spec;
	spec.kind = ScheduleKind::OneShotUtc;
	spec.isOneShot = true;
	spec.onceAtUtc = whenUtc;
	return spec;
}

ScheduleSpec ScheduleSpec::dailyAtLocal(int hour, int minute) {
	ScheduleSpec spec;
	spec.hour = ScheduleField::only(hour);
	spec.minute = ScheduleField::only(minute);
	return spec;
}

ScheduleSpec ScheduleSpec::weeklyAtLocal(uint8_t dowMask, int hour, int minute) {
	int days[7];
	size_t count = 0;
	for (int bit = 0; bit < 7; ++bit) {
		if ((dowMask & (1 << bit)) != 0) {
			days[count++] = bit;
		}
	}

	ScheduleSpec spec;
	spec.hour = ScheduleField::only(hour);
	spec.minute = ScheduleField::only(minute);
	spec.dayOfWeek = count == 0 ? ScheduleField::any() : ScheduleField::list(days, count);
	return spec;
}

ScheduleSpec ScheduleSpec::monthlyOnDayLocal(int dayOfMonth, int hour, int minute) {
	ScheduleSpec spec;
	if (dayOfMonth < 1) {
		dayOfMonth = 1;
	} else if (dayOfMonth > 31) {
		dayOfMonth = 31;
	}
	spec.dayOfMonth = ScheduleField::only(dayOfMonth);
	spec.hour = ScheduleField::only(hour);
	spec.minute = ScheduleField::only(minute);
	return spec;
}

ScheduleSpec ScheduleSpec::sunrise(int offsetMinutes) {
	ScheduleSpec spec;
	spec.kind = ScheduleKind::Sunrise;
	spec.sunOffsetMinutes = offsetMinutes;
	return spec;
}

ScheduleSpec ScheduleSpec::sunset(int offsetMinutes) {
	ScheduleSpec spec;
	spec.kind = ScheduleKind::Sunset;
	spec.sunOffsetMinutes = offsetMinutes;
	return spec;
}

ScheduleSpec ScheduleSpec::moonPhaseAngle(int angleDegrees, int toleranceDegrees) {
	ScheduleSpec spec;
	spec.kind = ScheduleKind::MoonPhaseAngle;
	spec.moonPhaseAngleDegrees = angleDegrees;
	spec.moonPhaseToleranceDegrees = toleranceDegrees;
	return spec;
}

ScheduleSpec ScheduleSpec::moonPhase(MoonPhaseName name, int toleranceDegrees) {
	return moonPhaseAngle(moonPhaseAngleForName(name), toleranceDegrees);
}

ScheduleSpec ScheduleSpec::moonIlluminationPercent(double percent, double tolerancePercent) {
	ScheduleSpec spec;
	spec.kind = ScheduleKind::MoonIlluminationPercent;
	spec.moonIlluminationTargetPercent = percent;
	spec.moonIlluminationTolerancePercent = tolerancePercent;
	return spec;
}

ScheduleSpec ScheduleSpec::custom(
    const ScheduleField &minute,
    const ScheduleField &hour,
    const ScheduleField &dom,
    const ScheduleField &month,
    const ScheduleField &dow
) {
	ScheduleSpec spec;
	spec.minute = minute;
	spec.hour = hour;
	spec.dayOfMonth = dom;
	spec.month = month;
	spec.dayOfWeek = dow;
	return spec;
}
