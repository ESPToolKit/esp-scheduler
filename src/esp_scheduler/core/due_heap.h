#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

struct DueHeapEntry {
	int64_t nextEpoch = 0;
	size_t slotIndex = 0;
	uint32_t generation = 0;
};

class DueHeap {
  public:
	void push(const DueHeapEntry &entry) {
		entries_.push_back(entry);
		std::push_heap(entries_.begin(), entries_.end(), Compare{});
	}

	bool empty() const {
		return entries_.empty();
	}

	const DueHeapEntry &top() const {
		return entries_.front();
	}

	DueHeapEntry pop() {
		std::pop_heap(entries_.begin(), entries_.end(), Compare{});
		DueHeapEntry entry = entries_.back();
		entries_.pop_back();
		return entry;
	}

	void clear() {
		entries_.clear();
	}

  private:
	struct Compare {
		bool operator()(const DueHeapEntry &lhs, const DueHeapEntry &rhs) const {
			return lhs.nextEpoch > rhs.nextEpoch;
		}
	};

	std::vector<DueHeapEntry> entries_{};
};
