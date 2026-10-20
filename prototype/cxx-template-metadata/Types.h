#pragma once

#include <cassert>
#include <cstdint>

struct Counters {
  int live = 0;
  int copies = 0;
  int moves = 0;
  int destroyed = 0;
};

struct Tracked {
  Counters *counters;
  int32_t value;
  uint32_t magic = 0xfeed1234;

  Tracked(Counters *counters, int32_t value) noexcept
      : counters(counters), value(value) {
    ++counters->live;
  }
  Tracked(const Tracked &other) noexcept
      : counters(other.counters), value(other.value) {
    assert(other.magic == 0xfeed1234);
    ++counters->live;
    ++counters->copies;
  }
  Tracked(Tracked &&other) noexcept
      : counters(other.counters), value(other.value) {
    assert(other.magic == 0xfeed1234);
    other.value = -12345;
    ++counters->live;
    ++counters->moves;
  }
  Tracked &operator=(const Tracked &) = delete;
  Tracked &operator=(Tracked &&) = delete;
  ~Tracked() noexcept {
    assert(magic == 0xfeed1234);
    magic = 0;
    --counters->live;
    ++counters->destroyed;
    assert(counters->live >= 0);
  }
};

struct CopyOnly {
  Counters *counters;
  int32_t value;

  CopyOnly(Counters *counters, int32_t value) noexcept
      : counters(counters), value(value) {
    ++counters->live;
  }
  CopyOnly(const CopyOnly &other) noexcept
      : counters(other.counters), value(other.value) {
    ++counters->live;
    ++counters->copies;
  }
  CopyOnly(CopyOnly &&) = delete;
  CopyOnly &operator=(const CopyOnly &) = delete;
  ~CopyOnly() noexcept {
    --counters->live;
    ++counters->destroyed;
    assert(counters->live >= 0);
  }
};

struct alignas(16) OverAligned {
  Counters *counters;
  int32_t value;

  OverAligned(Counters *counters, int32_t value) noexcept
      : counters(counters), value(value) {
    ++counters->live;
  }
  OverAligned(const OverAligned &other) noexcept
      : counters(other.counters), value(other.value) {
    ++counters->live;
    ++counters->copies;
  }
  OverAligned(OverAligned &&other) noexcept
      : counters(other.counters), value(other.value) {
    ++counters->live;
    ++counters->moves;
  }
  OverAligned &operator=(const OverAligned &) = delete;
  ~OverAligned() noexcept {
    --counters->live;
    ++counters->destroyed;
    assert(counters->live >= 0);
  }
};
