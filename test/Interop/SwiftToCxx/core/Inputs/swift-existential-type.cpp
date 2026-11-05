#include "core.h"

struct ProtocolTag {
  using WitnessTable = void;
};

struct MarkerTag {
  static constexpr bool IsMarker = true;
};

template class swift::_impl::SwiftExistentialType<>;
template class swift::_impl::SwiftExistentialType<MarkerTag>;
template class swift::_impl::SwiftExistentialType<ProtocolTag>;
template class swift::_impl::SwiftClassExistentialType<>;
template class swift::_impl::SwiftClassExistentialType<MarkerTag>;
template class swift::_impl::SwiftClassExistentialType<ProtocolTag>;

swift::Any eraseMarker(
    const swift::_impl::SwiftExistentialType<MarkerTag> &value) {
  return value;
}

swift::Any eraseProtocol(
    const swift::_impl::SwiftExistentialType<ProtocolTag> &value) {
  return value;
}
