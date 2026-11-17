#pragma once

#include <cstddef>

namespace stomp {

// Single-context FIFO. Platform callbacks need a short critical section or
// task-safe mailbox around this queue; they must not call push concurrently.
template <typename T, size_t Capacity> class BoundedQueue {
  static_assert(Capacity > 0, "queue needs storage");

public:
  bool push(const T &value) {
    if (count_ == Capacity) return false;
    values_[write_] = value;
    write_ = (write_ + 1) % Capacity;
    ++count_;
    return true;
  }

  bool pop(T &value) {
    if (count_ == 0) return false;
    value = values_[read_];
    read_ = (read_ + 1) % Capacity;
    --count_;
    return true;
  }

  void clear() { read_ = write_ = count_ = 0; }
  size_t size() const { return count_; }
  constexpr size_t capacity() const { return Capacity; }

private:
  T values_[Capacity] = {};
  size_t read_ = 0;
  size_t write_ = 0;
  size_t count_ = 0;
};

} // namespace stomp
