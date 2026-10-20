#include "Types.h"
#include "Probe-original.h"

int main() {
  auto values = swift::Array<Tracked>::init();
  return static_cast<int>(Probe::sum(values));
}
