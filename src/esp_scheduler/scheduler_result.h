#pragma once

#include <cstdint>
#include <cstddef>

enum class SchedulerError : uint8_t {
	Ok = 0,
	NotInitialized,
	AlreadyInitialized,
	InvalidSchedule,
	NoMemory,
	QueueFull,
	NotFound,
	Busy,
	ExecutorUnavailable,
	Timeout,
};

template <typename T> struct SchedulerResult {
	SchedulerError error{SchedulerError::Ok};
	T value{};

	bool ok() const {
		return error == SchedulerError::Ok;
	}

	explicit operator bool() const {
		return ok();
	}

	static SchedulerResult<T> success(const T &value) {
		return {SchedulerError::Ok, value};
	}

	static SchedulerResult<T> failure(SchedulerError error) {
		return {error, T{}};
	}
};

template <> struct SchedulerResult<void> {
	SchedulerError error{SchedulerError::Ok};

	bool ok() const {
		return error == SchedulerError::Ok;
	}

	explicit operator bool() const {
		return ok();
	}

	static SchedulerResult<void> success() {
		return {SchedulerError::Ok};
	}

	static SchedulerResult<void> failure(SchedulerError error) {
		return {error};
	}
};
