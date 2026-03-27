#pragma once

#include <cstddef>
#include <cstdint>

class ScheduleField {
  public:
	static ScheduleField any();
	static ScheduleField only(int value);
	static ScheduleField range(int from, int to);
	static ScheduleField every(int step);
	static ScheduleField rangeEvery(int from, int to, int step);
	static ScheduleField list(const int *values, size_t count);

	bool matches(int value) const;
	bool isAny() const {
		return isAny_;
	}
	bool empty() const {
		return !isAny_ && mask_ == 0;
	}
	uint64_t rawMask() const {
		return mask_;
	}

  private:
	uint64_t mask_ = 0;
	bool isAny_ = false;
};
