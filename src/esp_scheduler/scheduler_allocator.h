#pragma once

#if __has_include(<ESPBufferManager.h>)
#include <ESPBufferManager.h>
#define ESP_SCHEDULER_HAS_BUFFER_MANAGER 1
#elif __has_include(<esp_buffer_manager/buffer_manager.h>)
#include <esp_buffer_manager/buffer_manager.h>
#define ESP_SCHEDULER_HAS_BUFFER_MANAGER 1
#else
#define ESP_SCHEDULER_HAS_BUFFER_MANAGER 0
#endif

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace scheduler_allocator_detail {
inline void *allocate(std::size_t bytes, bool usePSRAMBuffers) noexcept {
#if ESP_SCHEDULER_HAS_BUFFER_MANAGER
	return ESPBufferManager::allocate(bytes, usePSRAMBuffers);
#else
	(void)usePSRAMBuffers;
	return std::malloc(bytes);
#endif
}

inline void deallocate(void *ptr) noexcept {
#if ESP_SCHEDULER_HAS_BUFFER_MANAGER
	ESPBufferManager::deallocate(ptr);
#else
	std::free(ptr);
#endif
}

inline void *reallocate(void *ptr, std::size_t bytes, bool usePSRAMBuffers) noexcept {
#if ESP_SCHEDULER_HAS_BUFFER_MANAGER
	void *next = ESPBufferManager::allocate(bytes, usePSRAMBuffers);
	if (!next) {
		return nullptr;
	}
	if (ptr && next != ptr) {
		std::memcpy(next, ptr, bytes);
		ESPBufferManager::deallocate(ptr);
	}
	return next;
#else
	(void)usePSRAMBuffers;
	return std::realloc(ptr, bytes);
#endif
}
} // namespace scheduler_allocator_detail

template <typename T> T *schedulerAllocate(std::size_t count, bool usePSRAMBuffers) noexcept {
	if (count == 0) {
		return nullptr;
	}
	if (count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
		return nullptr;
	}
	return static_cast<T *>(
	    scheduler_allocator_detail::allocate(count * sizeof(T), usePSRAMBuffers)
	);
}

template <typename T> void schedulerDeallocate(T *ptr) noexcept {
	scheduler_allocator_detail::deallocate(ptr);
}

template <typename T>
T *schedulerReallocate(
    T *ptr, std::size_t oldCount, std::size_t newCount, bool usePSRAMBuffers
) noexcept {
	(void)oldCount;
	if (newCount == 0) {
		scheduler_allocator_detail::deallocate(ptr);
		return nullptr;
	}
	if (newCount > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
		return nullptr;
	}
#if ESP_SCHEDULER_HAS_BUFFER_MANAGER
	T *next = schedulerAllocate<T>(newCount, usePSRAMBuffers);
	if (!next) {
		return nullptr;
	}
	if (ptr) {
		const std::size_t toCopy = oldCount < newCount ? oldCount : newCount;
		for (std::size_t index = 0; index < toCopy; ++index) {
			new (&next[index]) T(std::move(ptr[index]));
			ptr[index].~T();
		}
		schedulerDeallocate(ptr);
	}
	return next;
#else
	return static_cast<T *>(
	    scheduler_allocator_detail::reallocate(ptr, newCount * sizeof(T), usePSRAMBuffers)
	);
#endif
}

template <typename T> class SchedulerAllocator {
  public:
	using value_type = T;

	SchedulerAllocator() noexcept = default;
	explicit SchedulerAllocator(bool usePSRAMBuffers) noexcept : usePSRAMBuffers_(usePSRAMBuffers) {
	}

	template <typename U>
	SchedulerAllocator(const SchedulerAllocator<U> &other) noexcept
	    : usePSRAMBuffers_(other.usePSRAMBuffers()) {
	}

	T *allocate(std::size_t n) {
		if (n == 0) {
			return nullptr;
		}
		if (n > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
			return nullptr;
		}

		void *memory = scheduler_allocator_detail::allocate(n * sizeof(T), usePSRAMBuffers_);
		if (!memory) {
			return nullptr;
		}
		return static_cast<T *>(memory);
	}

	void deallocate(T *ptr, std::size_t) noexcept {
		scheduler_allocator_detail::deallocate(ptr);
	}

	bool usePSRAMBuffers() const noexcept {
		return usePSRAMBuffers_;
	}

	template <typename U> bool operator==(const SchedulerAllocator<U> &other) const noexcept {
		return usePSRAMBuffers_ == other.usePSRAMBuffers();
	}

	template <typename U> bool operator!=(const SchedulerAllocator<U> &other) const noexcept {
		return !(*this == other);
	}

  private:
	template <typename> friend class SchedulerAllocator;

	bool usePSRAMBuffers_ = false;
};

template <typename T> using SchedulerVector = std::vector<T, SchedulerAllocator<T>>;
