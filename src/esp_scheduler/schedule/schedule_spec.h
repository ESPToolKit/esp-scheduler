#pragma once

#include <ESPDate.h>

#include "schedule_field.h"

enum class ScheduleKind : uint8_t {
	Cron = 0,
	OneShotUtc,
	Sunrise,
	Sunset,
	MoonPhaseAngle,
	MoonIlluminationPercent,
};

enum class MoonPhaseName : uint8_t {
	NewMoon = 0,
	WaxingCrescent,
	FirstQuarter,
	WaxingGibbous,
	FullMoon,
	WaningGibbous,
	LastQuarter,
	WaningCrescent,
};

struct ScheduleSpec {
	ScheduleKind kind = ScheduleKind::Cron;
	bool isOneShot = false;
	DateTime onceAtUtc{};

	ScheduleField minute = ScheduleField::any();
	ScheduleField hour = ScheduleField::any();
	ScheduleField dayOfMonth = ScheduleField::any();
	ScheduleField month = ScheduleField::any();
	ScheduleField dayOfWeek = ScheduleField::any();

	int sunOffsetMinutes = 0;
	int moonPhaseAngleDegrees = 0;
	int moonPhaseToleranceDegrees = 1;
	double moonIlluminationTargetPercent = 0.0;
	double moonIlluminationTolerancePercent = 0.5;

	static ScheduleSpec onceUtc(const DateTime &whenUtc);
	static ScheduleSpec dailyAtLocal(int hour, int minute);
	static ScheduleSpec weeklyAtLocal(uint8_t dowMask, int hour, int minute);
	static ScheduleSpec monthlyOnDayLocal(int dayOfMonth, int hour, int minute);
	static ScheduleSpec sunrise(int offsetMinutes = 0);
	static ScheduleSpec sunset(int offsetMinutes = 0);
	static ScheduleSpec moonPhaseAngle(int angleDegrees, int toleranceDegrees = 1);
	static ScheduleSpec moonPhase(MoonPhaseName name, int toleranceDegrees = 1);
	static ScheduleSpec moonIlluminationPercent(double percent, double tolerancePercent = 0.5);
	static ScheduleSpec custom(
	    const ScheduleField &minute,
	    const ScheduleField &hour,
	    const ScheduleField &dom,
	    const ScheduleField &month,
	    const ScheduleField &dow
	);
};

using Schedule = ScheduleSpec;
