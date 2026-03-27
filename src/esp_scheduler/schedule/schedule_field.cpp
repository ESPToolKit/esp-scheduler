#include "schedule_field.h"

ScheduleField ScheduleField::any() {
	ScheduleField field;
	field.isAny_ = true;
	return field;
}

ScheduleField ScheduleField::only(int value) {
	ScheduleField field;
	if (value < 0 || value > 63) {
		return field;
	}
	field.mask_ = 1ULL << value;
	return field;
}

ScheduleField ScheduleField::range(int from, int to) {
	ScheduleField field;
	if (from < 0 || to < 0 || from > to || to > 63) {
		return field;
	}
	for (int value = from; value <= to; ++value) {
		field.mask_ |= 1ULL << value;
	}
	return field;
}

ScheduleField ScheduleField::every(int step) {
	ScheduleField field;
	if (step <= 0) {
		return field;
	}
	for (int value = 0; value <= 63; value += step) {
		field.mask_ |= 1ULL << value;
	}
	return field;
}

ScheduleField ScheduleField::rangeEvery(int from, int to, int step) {
	ScheduleField field;
	if (step <= 0 || from < 0 || to < 0 || from > to || to > 63) {
		return field;
	}
	for (int value = from; value <= to; value += step) {
		field.mask_ |= 1ULL << value;
	}
	return field;
}

ScheduleField ScheduleField::list(const int *values, size_t count) {
	ScheduleField field;
	if (!values || count == 0) {
		return field;
	}
	for (size_t index = 0; index < count; ++index) {
		const int value = values[index];
		if (value < 0 || value > 63) {
			field.mask_ = 0;
			return field;
		}
		field.mask_ |= 1ULL << value;
	}
	return field;
}

bool ScheduleField::matches(int value) const {
	if (isAny_) {
		return true;
	}
	if (value < 0 || value > 63) {
		return false;
	}
	return (mask_ & (1ULL << value)) != 0;
}
