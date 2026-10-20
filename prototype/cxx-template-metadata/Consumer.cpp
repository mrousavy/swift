#include "Types.h"
#include "Probe.h"

#define JOIN_IMPL(A, B) A##B
#define JOIN(A, B) JOIN_IMPL(A, B)
#define EXPORT extern "C" __attribute__((visibility("default")))

EXPORT const void *JOIN(consumerMetadata, CONSUMER_SUFFIX)() {
  return cxx_metadata_prototype::getCxxTypeMetadata<Tracked>();
}

EXPORT const void *JOIN(consumerCandidate, CONSUMER_SUFFIX)() {
  return cxx_metadata_prototype::getCxxMetadataCandidate<Tracked>();
}

EXPORT void JOIN(consumerExercise, CONSUMER_SUFFIX)(Counters *counters) {
  Tracked value(counters, 17);
  auto values = swift::Array<Tracked>::init();
  values.append(value);
  assert(Probe::sum(values) == 17);
  auto optional = swift::Optional<Tracked>::init(value);
  assert(Probe::optionalValue(optional) == 17);
}
