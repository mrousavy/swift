public import ProbeTypes

public func identity<T>(_ value: T) -> T { value }

public func optionalValue(_ value: Tracked?) -> Int32 {
  value?.value ?? -1
}

public func makeOptional(_ value: Tracked) -> Tracked? { value }

public func sum(_ values: [Tracked]) -> Int32 {
  values.reduce(0) { $0 + $1.value }
}

public func optionalCopyOnly(_ value: CopyOnly?) -> Int32 {
  value?.value ?? -1
}

public func sumOverAligned(_ values: [OverAligned]) -> Int32 {
  values.reduce(0) { $0 + $1.value }
}

@inline(never)
private func copyAny(_ value: Any) -> Any { value }

public func boxedValue(_ value: Tracked) -> Int32 {
  let boxed: Any = value
  let copied = copyAny(boxed)
  return (copied as! Tracked).value
}

public func reflectedFieldCount(_ value: Tracked) -> Int {
  Mirror(reflecting: value).children.count
}

@_cdecl("swiftTrackedMetadata")
public func swiftTrackedMetadata() -> UnsafeRawPointer {
  unsafeBitCast(Tracked.self, to: UnsafeRawPointer.self)
}

@_cdecl("swiftCopyOnlyMetadata")
public func swiftCopyOnlyMetadata() -> UnsafeRawPointer {
  unsafeBitCast(CopyOnly.self, to: UnsafeRawPointer.self)
}

@_cdecl("swiftOverAlignedMetadata")
public func swiftOverAlignedMetadata() -> UnsafeRawPointer {
  unsafeBitCast(OverAligned.self, to: UnsafeRawPointer.self)
}

@_cdecl("swiftTrackedOptionalMetadata")
public func swiftTrackedOptionalMetadata() -> UnsafeRawPointer {
  unsafeBitCast(Optional<Tracked>.self, to: UnsafeRawPointer.self)
}

@_cdecl("swiftTrackedArrayMetadata")
public func swiftTrackedArrayMetadata() -> UnsafeRawPointer {
  unsafeBitCast(Array<Tracked>.self, to: UnsafeRawPointer.self)
}
