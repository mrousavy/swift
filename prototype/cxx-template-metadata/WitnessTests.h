#ifndef CXX_METADATA_PROTOTYPE_WITNESS_TESTS_H
#define CXX_METADATA_PROTOTYPE_WITNESS_TESTS_H

#include "CxxValueWitnesses.h"
#include "Types.h"
#include <array>
#include <limits>

#ifdef NDEBUG
#error "Run this prototype's witness tests with assertions enabled."
#endif

namespace cxx_metadata_prototype {

namespace witness_tests {

struct BoxPair {
  void *object;
  void *buffer;
};
extern "C" BoxPair swift_allocBox(const void *metadata) noexcept SWIFT_CALL;

inline void checkBufferCopy(void *metadata) {
  Counters counters;
  auto box = swift_allocBox(metadata);
  ::new (box.buffer) Tracked(&counters, 42);
  void *source[3] = {box.object, nullptr, nullptr};
  void *destination[3] = {};
  auto *projected =
      CxxValueWitnesses<Tracked>::table.initializeBufferWithCopyOfBuffer(
          destination, source, metadata);
  assert(projected == box.buffer);
  assert(destination[0] == source[0]);
  assert(counters.live == 1 && counters.copies == 0);
  assert(static_cast<Tracked *>(projected)->value == 42);
  swift::_impl::swift_release(source[0]);
  assert(counters.live == 1 && counters.destroyed == 0);
  swift::_impl::swift_release(destination[0]);
  assert(counters.live == 0 && counters.destroyed == 1);
}

// Exercise tag layouts where the payload cannot hold every unsigned case
// number. The public integration fixture's records are all at least 4 bytes.
struct Small {
  unsigned char value;
  Small(const Small &other) noexcept : value(other.value) {}
  ~Small() noexcept {}
};

template <class T> void checkLifetimes() {
  const auto &v = CxxValueWitnesses<T>::table;
  assert(v.size == sizeof(T));
  assert(v.stride == sizeof(T));
  assert(v.getAlignment() == alignof(T));
  assert(v.flags == (0x02130000U | (alignof(T) - 1)));
  assert(v.extraInhabitantCount == 0);

  Counters counters;
  alignas(T) unsigned char source[sizeof(T)];
  alignas(T) unsigned char copy[sizeof(T)];
  alignas(T) unsigned char destination[sizeof(T)];

  ::new (source) T(&counters, 42);
  assert(counters.live == 1);
  assert(v.initializeWithCopy(copy, source, source) == copy);
  assert(counters.live == 2 && counters.copies == 1);
  assert(counters.destroyed == 0);

  assert(v.initializeWithTake(destination, copy, source) == destination);
  assert(counters.live == 2 && counters.destroyed == 1);
  assert(reinterpret_cast<T *>(destination)->value == 42);

  assert(v.assignWithCopy(destination, source, source) == destination);
  assert(counters.live == 2 && counters.destroyed == 2);
  assert(reinterpret_cast<T *>(destination)->value == 42);

  assert(v.assignWithTake(destination, source, source) == destination);
  assert(counters.live == 1 && counters.destroyed == 4);
  assert(reinterpret_cast<T *>(destination)->value == 42);

  v.destroy(destination, destination);
  assert(counters.live == 0 && counters.destroyed == 5);
  if constexpr (std::is_move_constructible_v<T>) {
    assert(counters.copies == 2 && counters.moves == 2);
  } else {
    assert(counters.copies == 4 && counters.moves == 0);
  }
}

template <class T>
void checkTags(const swift::_impl::ValueWitnessTable *reference = nullptr,
               void *metadata = nullptr) {
  const auto &v = CxxValueWitnesses<T>::table;
  std::array<unsigned char, sizeof(T) + 4> bytes;
  std::array<unsigned char, sizeof(T) + 4> referenceBytes;
  const unsigned counts[] = {
      0, 1, 255, 256, 65535, 65536, 16777215,
      std::numeric_limits<unsigned>::max()};
  for (unsigned emptyCases : counts) {
    const unsigned tags[] = {0, emptyCases / 2, emptyCases};
    for (unsigned tag : tags) {
      bytes.fill(0xaa);
      v.storeEnumTagSinglePayload(bytes.data(), tag, emptyCases, bytes.data());
      assert(v.getEnumTagSinglePayload(bytes.data(), emptyCases, bytes.data()) ==
             tag);
      // Storing the payload tag must leave the existing payload untouched.
      if (!tag)
        for (std::size_t index = 0; index != sizeof(T); ++index)
          assert(bytes[index] == 0xaa);

      if (reference) {
        assert(metadata);
        // Compare both physical representation and consumption in both
        // directions against the independently Swift-emitted witnesses.
        assert(reference->getEnumTagSinglePayload(bytes.data(), emptyCases,
                                                  metadata) == tag);
        referenceBytes.fill(0xaa);
        reference->storeEnumTagSinglePayload(referenceBytes.data(), tag,
                                             emptyCases, metadata);
        assert(referenceBytes == bytes);
        assert(v.getEnumTagSinglePayload(referenceBytes.data(), emptyCases,
                                         metadata) == tag);
      }
    }
  }
}

} // namespace witness_tests

inline void runWitnessTests(
    const swift::_impl::ValueWitnessTable *swiftTrackedVWT,
    void *swiftTrackedMetadata, bool canonicalIsSwift) {
  witness_tests::checkLifetimes<Tracked>();
  witness_tests::checkLifetimes<CopyOnly>();
  witness_tests::checkLifetimes<OverAligned>();
  witness_tests::checkBufferCopy(swiftTrackedMetadata);
  witness_tests::checkTags<witness_tests::Small>();
  const auto &cpp = CxxValueWitnesses<Tracked>::table;
  assert(swiftTrackedVWT);
  assert(swiftTrackedVWT->size == cpp.size);
  assert(swiftTrackedVWT->stride == cpp.stride);
  assert(swiftTrackedVWT->flags == cpp.flags);
  assert(swiftTrackedVWT->extraInhabitantCount == cpp.extraInhabitantCount);
  witness_tests::checkTags<Tracked>(
      canonicalIsSwift ? swiftTrackedVWT : nullptr, swiftTrackedMetadata);
}

} // namespace cxx_metadata_prototype

#endif
