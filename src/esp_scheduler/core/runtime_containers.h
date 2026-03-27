#pragma once

#include <cstddef>
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
	    : data_(other.data_),
	      size_(other.size_),
	      capacity_(other.capacity_),
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
