# C++-generated Swift metadata prototype

This experiment tests the approach proposed in
[swiftlang/swift#93293](https://github.com/swiftlang/swift/pull/93293#issuecomment-6293136354):
instantiate a Swift-compatible value witness table in C++, with witnesses that
call the C++ type's constructors and destructor.

It also constructs the surrounding foreign struct metadata and type
descriptors in C++, then calls `swift_getForeignTypeMetadata` to obtain the
canonical metadata pointer. Swift's existing `Array`, `Optional`, generic
functions and runtime use that metadata without modification.

This is a standalone feasibility experiment. The Swift compiler is unchanged.
`run.py` compiles a Swift library, then patches three `TypeMetadataTrait`
accessors in its generated header to call the prototype. The per-type
descriptor names and field offsets are supplied manually for the three test
types. A compiler implementation would need to emit that information.

## Run

On an arm64 Mac with the tested Swift toolchain available through `xcrun`:

```sh
python3 run.py --optimization debug
python3 run.py --optimization release
```

Each run saves the generated header, Swift IR, binaries, every command and its
output, and a result summary under `build/debug` or `build/release`. Assertions
remain enabled in both configurations. C++ compilation uses
`-Wall -Wextra -Werror`.

## Results

Tested on October 20, 2026:

```text
Apple Swift version 6.4 (swiftlang-6.4.0.34.1 clang-2100.3.34.1)
Target: arm64-apple-macosx27.0.0
```

Both debug (`-Onone`, `-O0`) and optimized (`-O`, `-O2`) runs passed:

- The original generated header fails to link against the Swift dylib and after
  Darwin `ld -r`, with an undefined foreign type metadata accessor.
- The patched header links and runs in both cases. The Swift dylib does not
  export the foreign accessors. The C++ objects do not reference Swift-emitted
  foreign accessor, value witness table or value witness function symbols.
- Swift and C++ receive identical canonical metadata pointers, including the
  instantiated `Array<Tracked>` and `Optional<Tracked>` metadata.
- Separate processes test Swift initializing first, C++ in the executable
  initializing first, and C++ in a consumer dylib initializing first. Two C++
  dylibs and the executable create distinct candidates and agree on the
  canonical result. The consumer-first case applies to the dylib test.
- Generic identity, array append and copy-on-write, optional construction and
  assignment, and `Any` boxing/casting preserve values and balance construction
  and destruction.
- Direct calls exercise all eight value witness functions. Checks include
  destroying the source after a take, reconstructing a destination whose C++
  assignment operators are deleted, retaining a real Swift existential box,
  and single-payload enum tag encoding. Tag representations are compared with
  Swift-emitted witnesses when Swift's metadata wins.
- Copy-only and `alignas(16)` records work.
- Including the header without using the prototype emits no prototype metadata
  or witness symbols in the C++ object file.

The main `Tracked` integration scope finishes with 26 copies, 13 moves,
40 destructions and zero live values in debug; optimized builds finish with
12 copies, 6 moves, 19 destructions and zero live values. These are correctness
observations, not a performance comparison.

Canonicalization chooses the first initialized candidate and its VWT. When
Swift initializes first, the C++ accessor returns Swift's metadata and witnesses.
When either C++ image initializes first, Swift accepts that image's metadata
and witnesses. Generating a C++ VWT therefore does not establish that Swift
generic calls will always use it or inline its operations. No performance
benchmark was performed.

## Scope and remaining work

- arm64 Darwin without pointer authentication, using the observed Swift 6.4
  ABI. Other targets and ABI versions are untested.
- Three ordinary, nonempty C++ value records with nontrivial special members,
  nothrow copying/destruction, zero extra inhabitants, and no special Swift
  ownership behavior. The prototype rejects alignment above 16 because Swift
  currently caps imported-record alignment at 16.
- Type names, the `__C` module context and field offsets are explicit traits.
  Namespaces, renaming, C++ templates, enums, imported reference types,
  noncopyable values and special ownership annotations are not implemented.
- Reflection field records and registration for lookup by mangled name are
  omitted. The integration test demonstrates the consequence: Swift's `Mirror`
  reports three fields when Swift's candidate wins, and zero when a C++
  candidate wins. This metadata is not a complete replacement for compiler
  emission.
- Production code must use the importer's eligibility and selected special
  members. Standard C++ type traits alone do not establish every Swift
  ownership or layout property.
- The candidate's descriptors use 32-bit relative pointers. The experiment
  checks their range at initialization; production emission must respect the
  target's relocation, placement and pointer authentication requirements.
- It still links the Swift runtime and uses Swift's generic ABI. Swift also
  still emits its own metadata for these types when compiling `Probe.swift`.

## Files

- `CxxValueWitnesses.h`: C++ template implementing the VWT.
- `CxxMetadata.h`: metadata candidates, per-type descriptors and canonicalization.
- `Types.h`: instrumented C++ records.
- `Probe.swift`: Swift generic and concrete APIs used by the test.
- `main.cpp`, `Consumer.cpp`, `WitnessTests.h`: integration and witness checks.
- `baseline.cpp`: failing link control using the original generated header.
- `run.py`: generation, header patching, compilation and execution.

Implemented and tested with Codex assistance.
