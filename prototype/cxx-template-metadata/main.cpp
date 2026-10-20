#include "Types.h"
#include "Probe.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "WitnessTests.h"

extern "C" const void *swiftTrackedMetadata() noexcept;
extern "C" const void *swiftCopyOnlyMetadata() noexcept;
extern "C" const void *swiftOverAlignedMetadata() noexcept;
extern "C" const void *swiftTrackedOptionalMetadata() noexcept;
extern "C" const void *swiftTrackedArrayMetadata() noexcept;

#if TEST_SHARED_CONSUMERS
extern "C" const void *consumerMetadataA();
extern "C" const void *consumerMetadataB();
extern "C" const void *consumerCandidateA();
extern "C" const void *consumerCandidateB();
extern "C" void consumerExerciseA(Counters *);
extern "C" void consumerExerciseB(Counters *);
#endif

static const swift::_impl::ValueWitnessTable *witnesses(const void *metadata) {
  return static_cast<const swift::_impl::ValueWitnessTable *const *>(metadata)[-1];
}

template <class T>
static void checkMetadata(const void *(*swiftMetadata)(), bool swiftFirst,
                          bool localCandidateWins) {
  using namespace cxx_metadata_prototype;
  const void *swift = nullptr;
  if (swiftFirst)
    swift = swiftMetadata();
  void *cxx = getCxxTypeMetadata<T>();
  if (!swiftFirst)
    swift = swiftMetadata();
  assert(cxx == swift);
  assert((cxx == getCxxMetadataCandidate<T>()) == localCandidateWins);
  const auto *table = witnesses(swift);
  assert(table->size == sizeof(T));
  assert(table->stride == sizeof(T));
  assert(table->flags == CxxValueWitnesses<T>::flags);
  assert(table->extraInhabitantCount == 0);
  assert((table == &CxxValueWitnesses<T>::table) == localCandidateWins);
}

static void exerciseGenerics() {
  Counters counters;
  {
    Tracked original(&counters, 42);
    auto returned = Probe::identity(original);
    assert(returned.value == 42);
    assert(original.value == 42);

    auto some = swift::Optional<Tracked>::init(original);
    auto copied = some;
    assert(some.isSome());
    assert(Probe::optionalValue(copied) == 42);
    auto none = swift::Optional<Tracked>::none();
    assert(none.isNone());
    assert(Probe::optionalValue(none) == -1);
    copied = none;
    assert(copied.isNone());
    copied = some;
    assert(Probe::optionalValue(copied) == 42);
    auto fromSwift = Probe::makeOptional(original);
    assert(Probe::optionalValue(fromSwift) == 42);

    auto values = swift::Array<Tracked>::init();
    values.append(original);
    values.append(returned);
    assert(Probe::sum(values) == 84);
    auto otherValues = values;
    otherValues.append(original);
    assert(Probe::sum(values) == 84);
    assert(Probe::sum(otherValues) == 126);
    otherValues = swift::Array<Tracked>::init();
    assert(Probe::sum(otherValues) == 0);
    assert(Probe::boxedValue(original) == 42);
  }
  assert(counters.live == 0);
  assert(counters.copies > 0);
  assert(counters.moves > 0);
  assert(counters.destroyed == counters.copies + counters.moves + 1);
  std::printf("PASS generic/array/optional/Any lifetime: copies=%d moves=%d "
              "destroyed=%d live=%d\n",
              counters.copies, counters.moves, counters.destroyed, counters.live);

  Counters copyCounters;
  {
    CopyOnly original(&copyCounters, 19);
    auto optional = swift::Optional<CopyOnly>::init(original);
    auto copied = optional;
    assert(Probe::optionalCopyOnly(copied) == 19);
  }
  assert(copyCounters.live == 0);
  assert(copyCounters.copies > 0);
  assert(copyCounters.moves == 0);
  assert(copyCounters.destroyed == copyCounters.copies + 1);

  Counters alignedCounters;
  {
    OverAligned original(&alignedCounters, 13);
    auto values = swift::Array<OverAligned>::init();
    values.append(original);
    auto copied = values;
    copied.append(original);
    assert(Probe::sumOverAligned(copied) == 26);
  }
  assert(alignedCounters.live == 0);
  assert(alignedCounters.destroyed ==
         alignedCounters.copies + alignedCounters.moves + 1);
  std::puts("PASS copy-only and alignas(16)");
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const bool swiftFirst = std::strcmp(argv[1], "swift-first") == 0;
  const bool consumerFirst = std::strcmp(argv[1], "consumer-first") == 0;
  assert(swiftFirst || consumerFirst || std::strcmp(argv[1], "cxx-first") == 0);
#if TEST_SHARED_CONSUMERS
  if (consumerFirst)
    assert(consumerMetadataA() == consumerCandidateA());
#else
  assert(!consumerFirst);
#endif
  checkMetadata<Tracked>(swiftTrackedMetadata, swiftFirst,
                         !swiftFirst && !consumerFirst);
  checkMetadata<CopyOnly>(swiftCopyOnlyMetadata, swiftFirst, !swiftFirst);
  checkMetadata<OverAligned>(swiftOverAlignedMetadata, swiftFirst, !swiftFirst);

  cxx_metadata_prototype::runWitnessTests(
      witnesses(swiftTrackedMetadata()),
      const_cast<void *>(swiftTrackedMetadata()), swiftFirst);
  std::puts("PASS direct witnesses: lifetime, boxed-buffer copy, enum tags");

  assert(swiftTrackedOptionalMetadata() ==
         swift::TypeMetadataTrait<swift::Optional<Tracked>>::getTypeMetadata());
  assert(swiftTrackedArrayMetadata() ==
         swift::TypeMetadataTrait<swift::Array<Tracked>>::getTypeMetadata());
  std::printf("PASS canonical metadata: %s\n", argv[1]);

  exerciseGenerics();

  // Record an intentional prototype gap instead of claiming full metadata
  // equivalence: our candidate does not carry reflection field records.
  Counters reflectionCounters;
  {
    Tracked value(&reflectionCounters, 42);
    auto fieldCount = Probe::reflectedFieldCount(value);
    assert(fieldCount == (swiftFirst ? 3 : 0));
    std::printf("OBSERVED reflection limitation: %lld fields (%s)\n",
                static_cast<long long>(fieldCount), argv[1]);
  }
  assert(reflectionCounters.live == 0);

#if TEST_SHARED_CONSUMERS
  const void *canonical = cxx_metadata_prototype::getCxxTypeMetadata<Tracked>();
  assert(consumerCandidateA() != consumerCandidateB());
  assert(consumerCandidateA() !=
         cxx_metadata_prototype::getCxxMetadataCandidate<Tracked>());
  assert(consumerMetadataA() == canonical);
  assert(consumerMetadataB() == canonical);
  Counters counters;
  consumerExerciseA(&counters);
  consumerExerciseB(&counters);
  assert(counters.live == 0);
  std::puts("PASS two C++ dylibs: distinct candidates, same canonical metadata");
#endif

  return 0;
}
