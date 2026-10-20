#pragma once

// Standalone proof of concept for arm64 Darwin and the Swift 6.4 ABI.
// Include this after the generated header's core metadata/VWT declarations.
// The descriptors below model the ordinary imported structs in Types.h.
// Reflection field records and registration for lookup by mangled name are
// deliberately outside this prototype's scope.

#include "CxxValueWitnesses.h"
#include "Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

#if !defined(__APPLE__) || !defined(__aarch64__) || defined(__arm64e__)
#error "This metadata prototype currently supports arm64 Darwin without ptrauth."
#endif

namespace cxx_metadata_prototype {

extern "C" swift::_impl::MetadataResponseTy swift_getForeignTypeMetadata(
    swift::_impl::MetadataRequestTy request,
    void *candidate) noexcept __attribute__((swiftcall));

// These are the 32-bit relative-pointer structures produced for these imported
// records by the installed Swift compiler. Names are supplied per type instead
// of relying on C++ RTTI names, which are not Swift's canonical type identity.
struct ModuleDescriptor {
  uint32_t flags;
  int32_t parent;
  int32_t name;
};

struct StructDescriptor {
  uint32_t flags;
  int32_t parent;
  int32_t name;
  int32_t accessor;
  int32_t fields;
  uint32_t numberOfFields;
  uint32_t fieldOffsetVectorOffset;
  int32_t foreignMetadataCompletion;
};

static_assert(sizeof(ModuleDescriptor) == 12);
static_assert(sizeof(StructDescriptor) == 32);

inline void setRelativePointer(int32_t &field, uintptr_t target) noexcept {
  const auto delta = static_cast<intptr_t>(target) -
                     reinterpret_cast<intptr_t>(&field);
  if (delta < std::numeric_limits<int32_t>::min() ||
      delta > std::numeric_limits<int32_t>::max())
    std::abort();
  field = static_cast<int32_t>(delta);
}

template <class T> struct CxxMetadataTraits;

template <> struct CxxMetadataTraits<Tracked> {
  static constexpr char name[] = "Tracked";
  static constexpr std::array<uint32_t, 3> fieldOffsets = {
      offsetof(Tracked, counters), offsetof(Tracked, value),
      offsetof(Tracked, magic)};
};

template <> struct CxxMetadataTraits<CopyOnly> {
  static constexpr char name[] = "CopyOnly";
  static constexpr std::array<uint32_t, 2> fieldOffsets = {
      offsetof(CopyOnly, counters), offsetof(CopyOnly, value)};
};

template <> struct CxxMetadataTraits<OverAligned> {
  static constexpr char name[] = "OverAligned";
  static constexpr std::array<uint32_t, 2> fieldOffsets = {
      offsetof(OverAligned, counters), offsetof(OverAligned, value)};
};

template <class T> void *getCxxMetadataCandidate() noexcept;

template <class T>
swift::_impl::MetadataResponseTy accessCxxTypeMetadata(
    swift::_impl::MetadataRequestTy request) noexcept
    __attribute__((swiftcall));

template <class T>
struct ForeignMetadataCandidate {
  // FullMetadata<StructMetadata> has two prefix words with the installed
  // toolchain: layout string and value witness table. The metadata pointer
  // handed to Swift points at kind, not at the start of this allocation.
  const void *layoutString = nullptr;
  const swift::_impl::ValueWitnessTable *witnesses =
      &CxxValueWitnesses<T>::table;
  uintptr_t kind = 0x200; // MetadataKind::Struct.
  const StructDescriptor *description = nullptr;
  decltype(CxxMetadataTraits<T>::fieldOffsets) fieldOffsets =
      CxxMetadataTraits<T>::fieldOffsets;
};

template <class T> struct MetadataStorage {
  ModuleDescriptor module{0, 0, 0};
  char moduleName[4] = "__C";
  // Struct kind (0x11), nonunique, non-generic, foreign initialization (2).
  StructDescriptor descriptor{
      0x00020011, 0, 0, 0, 0,
      static_cast<uint32_t>(CxxMetadataTraits<T>::fieldOffsets.size()), 2, 0};
  ForeignMetadataCandidate<T> candidate;

  MetadataStorage() noexcept {
    static_assert(std::is_standard_layout_v<T>);
    static_assert(offsetof(ForeignMetadataCandidate<T>, kind) ==
                  2 * sizeof(void *));
    static_assert(offsetof(ForeignMetadataCandidate<T>, fieldOffsets) ==
                  4 * sizeof(void *));

    setRelativePointer(module.name, reinterpret_cast<uintptr_t>(moduleName));
    setRelativePointer(descriptor.parent, reinterpret_cast<uintptr_t>(&module));
    setRelativePointer(descriptor.name,
                       reinterpret_cast<uintptr_t>(CxxMetadataTraits<T>::name));
    setRelativePointer(
        descriptor.accessor,
        reinterpret_cast<uintptr_t>(&accessCxxTypeMetadata<T>));
    candidate.description = &descriptor;
  }
};

template <class T> void *getCxxMetadataCandidate() noexcept {
  static MetadataStorage<T> storage;
  return &storage.candidate.kind;
}

template <class T>
swift::_impl::MetadataResponseTy accessCxxTypeMetadata(
    swift::_impl::MetadataRequestTy request) noexcept {
  return swift_getForeignTypeMetadata(request,
                                     getCxxMetadataCandidate<T>());
}

template <class T> void *getCxxTypeMetadata() noexcept {
  // Swift's metadata accessors cache the canonical result as well. In separate
  // processes, either the Swift-emitted or C++-emitted candidate can win.
  static void *metadata = accessCxxTypeMetadata<T>(0)._0;
  return metadata;
}

} // namespace cxx_metadata_prototype
