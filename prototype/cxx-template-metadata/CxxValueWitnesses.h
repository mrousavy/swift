//===--- CxxValueWitnesses.h - C++ value witness prototype --------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2026 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef CXX_METADATA_PROTOTYPE_VALUE_WITNESSES_H
#define CXX_METADATA_PROTOTYPE_VALUE_WITNESSES_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

// Include at global scope after the generated header's
// swift::_impl::ValueWitnessTable definition.
namespace cxx_metadata_prototype {

// This deliberately handles a narrower set than all imported C++ types:
// nonempty, copyable value records with nontrivial special members, no extra
// inhabitants, and no Swift ownership annotations or reference-type fields.
// The caller must verify those last properties against Swift's imported type.
// Its fixed flags match AddressOnlyCXXClangRecordTypeInfo for that subset.
template <class T> struct CxxValueWitnesses {
  static_assert(std::is_class_v<T> && !std::is_empty_v<T>);
  static_assert(std::is_nothrow_copy_constructible_v<T>);
  static_assert(std::is_nothrow_destructible_v<T>);
  static_assert(!std::is_trivially_destructible_v<T>);
  static_assert(!std::is_move_constructible_v<T> ||
                std::is_nothrow_move_constructible_v<T>);
  static_assert(alignof(T) <= 16,
                "This prototype requires C++ alignment to match Swift's "
                "current imported-record alignment, capped at 16 bytes");
  static_assert(sizeof(void *) == 8,
                "This prototype uses the 64-bit Swift heap object header");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "This prototype has only been implemented for little-endian targets"
#endif

  // These functions use the calling convention of the generated VWT typedefs.
  // They intentionally do not use SWIFT_CALL. Initializing fields of the
  // generated table also preserves their pointer-authentication qualifiers.
  static void *initializeBufferWithCopyOfBuffer(void *dest, void *src,
                                                void *) noexcept {
    // Non-bitwise-takable values always use Swift's boxed existential buffer,
    // even if their size would fit in its three-word inline storage.
    // See stdlib/public/runtime/MetadataImpl.h, BufferValueWitnesses<Allocate>.
    void *box;
    std::memcpy(&box, src, sizeof(box));
    std::memcpy(dest, &box, sizeof(box));
    swift::_impl::swift_retain(box);
    constexpr auto heapHeaderSize = 2 * sizeof(void *);
    constexpr auto alignmentMask = alignof(T) - 1;
    constexpr auto payloadOffset =
        (heapHeaderSize + alignmentMask) & ~alignmentMask;
    return static_cast<char *>(box) + payloadOffset;
  }

  static void destroy(void *value, void *) noexcept {
    static_cast<T *>(value)->~T();
  }

  static void *initializeWithCopy(void *dest, void *src, void *) noexcept {
    return ::new (dest) T(*static_cast<const T *>(src));
  }

  static void *assignWithCopy(void *dest, void *src, void *metadata) noexcept {
    // Swift assignment uses destruction and reconstruction, even for a C++
    // type whose copy-assignment operator is deleted.
    destroy(dest, metadata);
    return initializeWithCopy(dest, src, metadata);
  }

  static void *initializeWithTake(void *dest, void *src, void *metadata) noexcept {
    T *result;
    if constexpr (std::is_move_constructible_v<T>)
      result = ::new (dest) T(std::move(*static_cast<T *>(src)));
    else
      result = ::new (dest) T(*static_cast<const T *>(src));
    // A Swift take invalidates the source. A C++ move alone does not.
    destroy(src, metadata);
    return result;
  }

  static void *assignWithTake(void *dest, void *src, void *metadata) noexcept {
    destroy(dest, metadata);
    return initializeWithTake(dest, src, metadata);
  }

private:
  // Zero-extra-inhabitant specialization of the single-payload enum helpers
  // in include/swift/ABI/Enum.h and stdlib/public/runtime/EnumImpl.h.
  static constexpr unsigned tagBytes(unsigned emptyCases) noexcept {
    std::uint64_t tags = 1;
    if (emptyCases) {
      if constexpr (sizeof(T) >= 4) {
        ++tags;
      } else {
        constexpr auto casesPerTag = std::uint64_t{1} << (sizeof(T) * 8);
        tags += (emptyCases + casesPerTag - 1) / casesPerTag;
      }
    }
    return tags <= 1 ? 0 : tags < 256 ? 1 : tags < 65536 ? 2 : 4;
  }

  static std::uint32_t loadElement(const unsigned char *src,
                                   std::size_t size) noexcept {
    std::uint32_t result = 0;
    std::memcpy(&result, src, size < 4 ? size : 4);
    return result;
  }

  static void storeElement(unsigned char *dest, std::uint32_t value,
                           std::size_t size) noexcept {
    const auto storedBytes = size < 4 ? size : 4;
    std::memcpy(dest, &value, storedBytes);
    if (size > storedBytes)
      std::memset(dest + storedBytes, 0, size - storedBytes);
  }

public:
  static unsigned getEnumTagSinglePayload(const void *value,
                                          unsigned emptyCases,
                                          void *) noexcept {
    if (!emptyCases)
      return 0;
    const auto *bytes = static_cast<const unsigned char *>(value);
    const auto tag = loadElement(bytes + sizeof(T), tagBytes(emptyCases));
    if (!tag)
      return 0;
    const auto payloadIndex = loadElement(bytes, sizeof(T));
    if constexpr (sizeof(T) >= 4)
      return payloadIndex + 1;
    else
      return (((tag - 1U) << (sizeof(T) * 8)) | payloadIndex) + 1;
  }

  static void storeEnumTagSinglePayload(void *value, unsigned whichCase,
                                        unsigned emptyCases, void *) noexcept {
    assert(whichCase <= emptyCases);
    auto *bytes = static_cast<unsigned char *>(value);
    const auto count = tagBytes(emptyCases);
    if (!whichCase) {
      // The payload has already been initialized. Only clear tag storage.
      storeElement(bytes + sizeof(T), 0, count);
      return;
    }
    const auto caseIndex = whichCase - 1;
    unsigned payloadIndex;
    unsigned tagIndex;
    if constexpr (sizeof(T) >= 4) {
      payloadIndex = caseIndex;
      tagIndex = 1;
    } else {
      constexpr auto bits = sizeof(T) * 8;
      payloadIndex = caseIndex & ((1U << bits) - 1);
      tagIndex = 1 + (caseIndex >> bits);
    }
    storeElement(bytes, payloadIndex, sizeof(T));
    storeElement(bytes + sizeof(T), tagIndex, count);
  }

  static constexpr unsigned flags =
      (alignof(T) - 1) | 0x00010000U /* IsNonPOD */ |
      0x00020000U /* IsNonInline */ | 0x00100000U /* IsNonBitwiseTakable */ |
      0x02000000U /* IsAddressableForDependencies */;

  static inline const swift::_impl::ValueWitnessTable table = {
      initializeBufferWithCopyOfBuffer,
      destroy,
      initializeWithCopy,
      assignWithCopy,
      initializeWithTake,
      assignWithTake,
      getEnumTagSinglePayload,
      storeEnumTagSinglePayload,
      sizeof(T),
      sizeof(T),
      flags,
      0 /* extraInhabitantCount */};
};

} // namespace cxx_metadata_prototype

#endif
