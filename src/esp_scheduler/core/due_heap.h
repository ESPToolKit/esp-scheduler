#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime_containers.h"

struct DueHeapEntry {
	int64_t nextEpoch = 0;
	size_t slotIndex = 0;
	uint32_t generation = 0;
};

class DueHeap {
  public:
	explicit DueHeap(bool usePSRAM = false) : entries_(usePSRAM) {
	}

	bool push(const DueHeapEntry &entry) {
		if (!entries_.pushBack(entry)) {
			return false;
		}
		std::size_t index = entries_.size() - 1;
		while (index > 0) {
			const std::size_t parent = (index - 1) / 2;
			if (!Compare{}(entries_[parent], entries_[index])) {
				break;
			}
			DueHeapEntry tmp = entries_[parent];
			entries_[parent] = entries_[index];
			entries_[index] = tmp;
			index = parent;
		}
		return true;
	}

	bool empty() const {
		return entries_.empty();
	}

	const DueHeapEntry &top() const {
		return entries_[0];
	}

	DueHeapEntry pop() {
		DueHeapEntry entry = entries_[0];
		if (entries_.size() == 1) {
			entries_.popBack();
			return entry;
		}
		entries_[0] = entries_[entries_.size() - 1];
		entries_.popBack();
		std::size_t index = 0;
		while (true) {
			const std::size_t left = index * 2 + 1;
			const std::size_t right = left + 1;
			std::size_t candidate = index;
			if (left < entries_.size() && Compare{}(entries_[candidate], entries_[left])) {
				candidate = left;
			}
			if (right < entries_.size() && Compare{}(entries_[candidate], entries_[right])) {
				candidate = right;
			}
			if (candidate == index) {
				break;
			}
			DueHeapEntry tmp = entries_[index];
			entries_[index] = entries_[candidate];
			entries_[candidate] = tmp;
			index = candidate;
		}
		return entry;
	}

	void clear() {
		entries_.clear();
	}

	std::size_t size() const {
		return entries_.size();
	}

	const DueHeapEntry &at(std::size_t index) const {
		return entries_[index];
	}

  private:
	struct Compare {
		bool operator()(const DueHeapEntry &lhs, const DueHeapEntry &rhs) const {
			return lhs.nextEpoch > rhs.nextEpoch;
		}
	};

	SchedulerArray<DueHeapEntry> entries_{};
};
