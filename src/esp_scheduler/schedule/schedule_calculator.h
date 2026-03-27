#pragma once

#include <ESPDate.h>

#include "schedule_spec.h"

class ScheduleCalculator {
  public:
	static bool validate(const ScheduleSpec &spec);
	static bool computeNext(
	    ESPDate &date, const ScheduleSpec &spec, const DateTime &fromUtc, DateTime &outNextUtc
	);
};
