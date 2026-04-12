#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "../scheduler_allocator.h"

template <typename T> class SchedulerArray {
  public:
	explicit SchedulerArray(bool usePSRAM = false) : usePSRAM_(usePSRAM) {
	}

	~SchedulerArray() {
		clear();
		schedulerDeallocate(data_);
	}

	SchedulerArray(const SchedulerArray &) = delete;
	SchedulerArray &operator=(const SchedulerArray &) = delete;

	SchedulerArray(SchedulerArray &&other) noexcept
	    : data_(other.data_), size_(other.size_), capacity_(other.capacity_),
	      usePSRAM_(other.usePSRAM_) {
		other.data_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
	}

	SchedulerArray &operator=(SchedulerArray &&other) noexcept {
		if (this == &other) {
			return *this;
		}
		clear();
		schedulerDeallocate(data_);
		data_ = other.data_;
		size_ = other.size_;
		capacity_ = other.capacity_;
		usePSRAM_ = other.usePSRAM_;
		other.data_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
		return *this;
	}

	bool swapRemove(std::size_t index) {
		if (index >= size_) {
			return false;
		}
		if (index + 1 != size_) {
			data_[index].~T();
			new (&data_[index]) T(std::move(data_[size_ - 1]));
		}
		popBack();
		return true;
	}

	bool reserve(std::size_t requested) {
		if (requested <= capacity_) {
			return true;
		}
		T *next = schedulerAllocate<T>(requested, usePSRAM_);
		if (!next) {
			return false;
		}
		for (std::size_t index = 0; index < size_; ++index) {
			new (&next[index]) T(std::move(data_[index]));
			data_[index].~T();
		}
		schedulerDeallocate(data_);
		data_ = next;
		capacity_ = requested;
		return true;
	}

	template <typename... Args> bool emplaceBack(Args &&...args) {
		if (size_ == capacity_) {
			const std::size_t nextCapacity = capacity_ == 0 ? 4 : capacity_ * 2;
			if (!reserve(nextCapacity)) {
				return false;
			}
		}
		new (&data_[size_]) T(std::forward<Args>(args)...);
		++size_;
		return true;
	}

	bool pushBack(const T &value) {
		return emplaceBack(value);
	}

	bool pushBack(T &&value) {
		return emplaceBack(std::move(value));
	}

	void popBack() {
		if (size_ == 0) {
			return;
		}
		--size_;
		data_[size_].~T();
	}

	void clear() {
		for (std::size_t index = 0; index < size_; ++index) {
			data_[index].~T();
		}
		size_ = 0;
	}

	T &operator[](std::size_t index) {
		return data_[index];
	}

	const T &operator[](std::size_t index) const {
		return data_[index];
	}

	T *begin() {
		return data_;
	}

	const T *begin() const {
		return data_;
	}

	T *end() {
		return data_ + size_;
	}

	const T *end() const {
		return data_ + size_;
	}

	std::size_t size() const {
		return size_;
	}

	bool empty() const {
		return size_ == 0;
	}

	void erase(std::size_t index) {
		if (index >= size_) {
			return;
		}
		data_[index].~T();
		for (std::size_t cursor = index; cursor + 1 < size_; ++cursor) {
			new (&data_[cursor]) T(std::move(data_[cursor + 1]));
			data_[cursor + 1].~T();
		}
		--size_;
	}

	bool usePSRAM() const {
		return usePSRAM_;
	}

  private:
	T *data_ = nullptr;
	std::size_t size_ = 0;
	std::size_t capacity_ = 0;
	bool usePSRAM_ = false;
};

class SchedulerOwnedString {
  public:
	explicit SchedulerOwnedString(bool usePSRAM = false) : usePSRAM_(usePSRAM) {
	}

	~SchedulerOwnedString() {
		schedulerDeallocate(data_);
	}

	SchedulerOwnedString(const SchedulerOwnedString &) = delete;
	SchedulerOwnedString &operator=(const SchedulerOwnedString &) = delete;

	SchedulerOwnedString(SchedulerOwnedString &&other) noexcept
	    : data_(other.data_), length_(other.length_), usePSRAM_(other.usePSRAM_) {
		other.data_ = nullptr;
		other.length_ = 0;
	}

	SchedulerOwnedString &operator=(SchedulerOwnedString &&other) noexcept {
		if (this == &other) {
			return *this;
		}
		schedulerDeallocate(data_);
		data_ = other.data_;
		length_ = other.length_;
		usePSRAM_ = other.usePSRAM_;
		other.data_ = nullptr;
		other.length_ = 0;
		return *this;
	}

	bool assign(const char *text) {
		schedulerDeallocate(data_);
		data_ = nullptr;
		length_ = 0;
		if (!text || text[0] == '\0') {
			return true;
		}
		while (text[length_] != '\0') {
			++length_;
		}
		data_ = schedulerAllocate<char>(length_ + 1, usePSRAM_);
		if (!data_) {
			length_ = 0;
			return false;
		}
		for (std::size_t index = 0; index < length_; ++index) {
			data_[index] = text[index];
		}
		data_[length_] = '\0';
		return true;
	}

	void clear() {
		schedulerDeallocate(data_);
		data_ = nullptr;
		length_ = 0;
	}

	const char *c_str() const {
		return data_;
	}

	bool empty() const {
		return data_ == nullptr || length_ == 0;
	}

  private:
	char *data_ = nullptr;
	std::size_t length_ = 0;
	bool usePSRAM_ = false;
};

class SchedulerIdIndex {
  public:
	explicit SchedulerIdIndex(bool usePSRAM = false) : usePSRAM_(usePSRAM) {
	}

	bool set(uint32_t jobId, std::size_t slotIndex) {
		if (jobId == 0) {
			return false;
		}
		if (!ensureCapacityForInsert()) {
			return false;
		}
		return insertOrAssign(jobId, slotIndex);
	}

	bool get(uint32_t jobId, std::size_t &outSlotIndex) const {
		if (!entries_ || capacity_ == 0 || jobId == 0) {
			return false;
		}
		const std::size_t mask = capacity_ - 1;
		std::size_t index = hash(jobId) & mask;
		for (std::size_t probe = 0; probe < capacity_; ++probe) {
			const Entry &entry = entries_[index];
			if (entry.state == EntryState::Empty) {
				return false;
			}
			if (entry.state == EntryState::Occupied && entry.jobId == jobId) {
				outSlotIndex = entry.slotIndex;
				return true;
			}
			index = (index + 1) & mask;
		}
		return false;
	}

	bool remove(uint32_t jobId) {
		if (!entries_ || capacity_ == 0 || jobId == 0) {
			return false;
		}
		const std::size_t mask = capacity_ - 1;
		std::size_t index = hash(jobId) & mask;
		for (std::size_t probe = 0; probe < capacity_; ++probe) {
			Entry &entry = entries_[index];
			if (entry.state == EntryState::Empty) {
				return false;
			}
			if (entry.state == EntryState::Occupied && entry.jobId == jobId) {
				entry.state = EntryState::Deleted;
				entry.jobId = 0;
				entry.slotIndex = 0;
				--size_;
				++deletedCount_;
				return true;
			}
			index = (index + 1) & mask;
		}
		return false;
	}

	void clear() {
		if (!entries_) {
			size_ = 0;
			deletedCount_ = 0;
			return;
		}
		for (std::size_t index = 0; index < capacity_; ++index) {
			entries_[index].state = EntryState::Empty;
			entries_[index].jobId = 0;
			entries_[index].slotIndex = 0;
		}
		size_ = 0;
		deletedCount_ = 0;
	}

	~SchedulerIdIndex() {
		schedulerDeallocate(entries_);
	}

	SchedulerIdIndex(const SchedulerIdIndex &) = delete;
	SchedulerIdIndex &operator=(const SchedulerIdIndex &) = delete;

	SchedulerIdIndex(SchedulerIdIndex &&other) noexcept
	    : entries_(other.entries_), capacity_(other.capacity_), size_(other.size_),
	      deletedCount_(other.deletedCount_), usePSRAM_(other.usePSRAM_) {
		other.entries_ = nullptr;
		other.capacity_ = 0;
		other.size_ = 0;
		other.deletedCount_ = 0;
	}

	SchedulerIdIndex &operator=(SchedulerIdIndex &&other) noexcept {
		if (this == &other) {
			return *this;
		}
		schedulerDeallocate(entries_);
		entries_ = other.entries_;
		capacity_ = other.capacity_;
		size_ = other.size_;
		deletedCount_ = other.deletedCount_;
		usePSRAM_ = other.usePSRAM_;
		other.entries_ = nullptr;
		other.capacity_ = 0;
		other.size_ = 0;
		other.deletedCount_ = 0;
		return *this;
	}

  private:
	enum class EntryState : uint8_t {
		Empty = 0,
		Occupied,
		Deleted,
	};

	struct Entry {
		uint32_t jobId = 0;
		std::size_t slotIndex = 0;
		EntryState state = EntryState::Empty;
	};

	static std::size_t nextCapacity(std::size_t current) {
		return current == 0 ? 8 : current * 2;
	}

	static std::size_t hash(uint32_t jobId) {
		return static_cast<std::size_t>(jobId * 2654435761u);
	}

	bool ensureCapacityForInsert() {
		if (capacity_ == 0) {
			return rehash(nextCapacity(capacity_));
		}
		if ((size_ + deletedCount_ + 1) * 10 >= capacity_ * 7) {
			return rehash(nextCapacity(capacity_));
		}
		if (deletedCount_ > size_) {
			return rehash(capacity_);
		}
		return true;
	}

	bool insertOrAssign(uint32_t jobId, std::size_t slotIndex) {
		const std::size_t mask = capacity_ - 1;
		std::size_t index = hash(jobId) & mask;
		std::size_t firstDeleted = capacity_;
		for (std::size_t probe = 0; probe < capacity_; ++probe) {
			Entry &entry = entries_[index];
			if (entry.state == EntryState::Empty) {
				if (firstDeleted != capacity_) {
					index = firstDeleted;
				}
				Entry &target = entries_[index];
				target.jobId = jobId;
				target.slotIndex = slotIndex;
				if (target.state == EntryState::Deleted) {
					--deletedCount_;
				}
				target.state = EntryState::Occupied;
				++size_;
				return true;
			}
			if (entry.state == EntryState::Deleted) {
				if (firstDeleted == capacity_) {
					firstDeleted = index;
				}
			} else if (entry.jobId == jobId) {
				entry.slotIndex = slotIndex;
				return true;
			}
			index = (index + 1) & mask;
		}
		if (firstDeleted != capacity_) {
			Entry &target = entries_[firstDeleted];
			target.jobId = jobId;
			target.slotIndex = slotIndex;
			target.state = EntryState::Occupied;
			--deletedCount_;
			++size_;
			return true;
		}
		return false;
	}

	bool rehash(std::size_t newCapacity) {
		Entry *next = schedulerAllocate<Entry>(newCapacity, usePSRAM_);
		if (!next) {
			return false;
		}
		for (std::size_t index = 0; index < newCapacity; ++index) {
			next[index] = Entry{};
		}

		Entry *previous = entries_;
		const std::size_t previousCapacity = capacity_;
		entries_ = next;
		capacity_ = newCapacity;
		const std::size_t previousSize = size_;
		size_ = 0;
		deletedCount_ = 0;

		for (std::size_t index = 0; index < previousCapacity; ++index) {
			const Entry &entry = previous[index];
			if (entry.state == EntryState::Occupied &&
			    !insertOrAssign(entry.jobId, entry.slotIndex)) {
				schedulerDeallocate(next);
				entries_ = previous;
				capacity_ = previousCapacity;
				size_ = previousSize;
				deletedCount_ = 0;
				for (std::size_t restore = 0; restore < previousCapacity; ++restore) {
					if (previous[restore].state == EntryState::Deleted) {
						++deletedCount_;
					}
				}
				return false;
			}
		}
		schedulerDeallocate(previous);
		return true;
	}

	Entry *entries_ = nullptr;
	std::size_t capacity_ = 0;
	std::size_t size_ = 0;
	std::size_t deletedCount_ = 0;
	bool usePSRAM_ = false;
};
