#pragma once
#include <stddef.h>
#include <string.h>

// Retain the newest bytes without allocating. The caller provides locking.
template <size_t Capacity>
class BoundedTextLog {
  static_assert(Capacity > 0, "Log capacity must be positive");
 public:
  void append(const char* text, size_t length) {
    if (!length) return;
    if (length >= Capacity) {
      memcpy(bytes_, text + length - Capacity, Capacity);
      size_ = Capacity;
    } else {
      if (size_ + length > Capacity) {
        const size_t discarded = size_ + length - Capacity;
        memmove(bytes_, bytes_ + discarded, size_ - discarded);
        size_ -= discarded;
      }
      memcpy(bytes_ + size_, text, length);
      size_ += length;
    }
    bytes_[size_] = '\0';
  }
  const char* data() const { return bytes_; }
  size_t size() const { return size_; }
 private:
  char bytes_[Capacity + 1] = {};
  size_t size_ = 0;
};
