#!/usr/bin/env python3
"""Run the standalone C++ metadata experiment using an installed Swift toolchain."""

import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--optimization", choices=["debug", "release"], default="debug")
    args = parser.parse_args()
    build = ROOT / "build" / args.optimization
    build.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(command, *, expected_failure=False):
        command = [str(part) for part in command]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        commands.append(
            {"command": command, "exit": result.returncode,
             "stdout": result.stdout, "stderr": result.stderr}
        )
        (build / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if expected_failure:
            if result.returncode == 0:
                raise RuntimeError("Expected link failure: " + shlex.join(command))
        elif result.returncode:
            print(result.stdout, end="")
            print(result.stderr, end="", file=sys.stderr)
            raise RuntimeError("Failed: " + shlex.join(command))
        return result

    swiftc = run(["xcrun", "--find", "swiftc"]).stdout.strip()
    clangxx = run(["xcrun", "--find", "clang++"]).stdout.strip()
    runtime_headers = Path(swiftc).parent.parent / "lib" / "swift"
    version = run([swiftc, "--version"]).stdout.strip()
    print(version)
    swift = [
        swiftc, "-parse-as-library", "-cxx-interoperability-mode=default",
        "-module-name", "Probe", "-I", ROOT,
        "-O" if args.optimization == "release" else "-Onone",
    ]
    run(swift + ["-emit-ir", ROOT / "Probe.swift", "-o", build / "Probe.ll"])
    run(swift + [
        "-emit-library", "-emit-module",
        "-emit-module-path", build / "Probe.swiftmodule",
        "-emit-clang-header-path", build / "Probe-original.h",
        ROOT / "Probe.swift", "-o", build / "libProbe.dylib",
    ])
    original = (build / "Probe-original.h").read_text()
    header = original
    for name in ["Tracked", "CopyOnly", "OverAligned"]:
        old = f"return Probe::_impl::$sSo{len(name)}{name}VMa(0)._0;"
        assert header.count(old) == 1, name
        header = header.replace(
            old, f"return ::cxx_metadata_prototype::getCxxTypeMetadata<{name}>();"
        )
    marker = 'namespace Probe SWIFT_PRIVATE_ATTR SWIFT_SYMBOL_MODULE("Probe") {'
    assert marker in header
    header = header.replace(marker, '#include "CxxMetadata.h"\n\n' + marker, 1)
    (build / "Probe.h").write_text(header)

    # Check our chosen ABI flags against independent Swift-emitted IR.
    ir = (build / "Probe.ll").read_text()
    for name, size, flags in [
        ("Tracked", 16, 0x02130007),
        ("CopyOnly", 16, 0x02130007),
        ("OverAligned", 16, 0x0213000F),
    ]:
        match = re.search(
            rf'@"\$sSo{len(name)}{name}VWV" = .*?i64 (\d+), i64 (\d+), '
            r'i32 (\d+), i32 (\d+) \}', ir
        )
        assert match, name
        assert tuple(map(int, match.groups())) == (size, size, flags, 0), name

    cpp = [
        clangxx, "-std=c++20", "-I", ROOT, "-I", build, "-I", runtime_headers,
        "-Wall", "-Wextra", "-Werror",
        "-fvisibility=hidden", "-fvisibility-inlines-hidden",
        "-O2" if args.optimization == "release" else "-O0",
    ]
    runtime_link = [
        "-L", build, "-lswiftCore", "-lc++",
        "-Xlinker", "-rpath", "-Xlinker", build,
    ]
    run(cpp + ["-c", ROOT / "baseline.cpp", "-o", build / "baseline.o"])
    failed = run(
        [swiftc, build / "baseline.o", "-lProbe", *runtime_link,
         "-o", build / "baseline"],
        expected_failure=True,
    )
    assert "type metadata accessor for __C.Tracked" in failed.stderr
    print("PASS original header fails to link against dylib as expected")

    exports = run(["nm", "-gU", build / "libProbe.dylib"]).stdout
    for name in ["Tracked", "CopyOnly", "OverAligned"]:
        assert f"$sSo{len(name)}{name}VMa" not in exports

    for suffix in ["A", "B"]:
        obj = build / f"Consumer{suffix}.o"
        run(cpp + [
            f"-DCONSUMER_SUFFIX={suffix}", "-c", ROOT / "Consumer.cpp", "-o", obj,
        ])
        run([
            swiftc, "-emit-library", obj, "-lProbe", *runtime_link,
            "-o", build / f"libConsumer{suffix}.dylib",
        ])

    run(cpp + [
        "-DTEST_SHARED_CONSUMERS=1", "-c", ROOT / "main.cpp",
        "-o", build / "main.o",
    ])
    run([
        swiftc, build / "main.o", "-lProbe", "-lConsumerA", "-lConsumerB",
        *runtime_link, "-o", build / "dylib-client",
    ])
    for obj in ["main.o", "ConsumerA.o", "ConsumerB.o"]:
        undefined = run(["nm", "-u", build / obj]).stdout
        for name in ["Tracked", "CopyOnly", "OverAligned"]:
            assert f"$sSo{len(name)}{name}VMa" not in undefined
            assert f"$sSo{len(name)}{name}VWV" not in undefined
            assert f"$sSo{len(name)}{name}Vw" not in undefined
    print("PASS C++ objects have no foreign accessor or VWT symbol dependency")
    for order in ["cxx-first", "swift-first", "consumer-first"]:
        print(run([build / "dylib-client", order]).stdout, end="")

    # Reproduce the Darwin prelink boundary with the unmodified Swift object.
    run(swift + ["-emit-object", ROOT / "Probe.swift", "-o", build / "Probe.o"])
    run(["xcrun", "ld", "-r", build / "Probe.o", "-o", build / "Probe-prelinked.o"])
    failed = run(
        [swiftc, build / "baseline.o", build / "Probe-prelinked.o",
         *runtime_link, "-o", build / "baseline-prelinked"],
        expected_failure=True,
    )
    assert "type metadata accessor for __C.Tracked" in failed.stderr
    print("PASS original header fails after ld -r as expected")
    run(cpp + ["-c", ROOT / "main.cpp", "-o", build / "main-prelinked.o"])
    run([
        swiftc, build / "main-prelinked.o", build / "Probe-prelinked.o",
        *runtime_link, "-o", build / "prelinked-client",
    ])
    for order in ["cxx-first", "swift-first"]:
        print(run([build / "prelinked-client", order]).stdout, end="")

    unused = build / "unused.cpp"
    unused.write_text('#include "Types.h"\n#include "Probe.h"\nint main() {}\n')
    run(cpp + ["-c", unused, "-o", build / "unused.o"])
    symbols = run(["nm", build / "unused.o"]).stdout
    assert "MetadataStorage" not in symbols
    assert "CxxValueWitnesses" not in symbols
    print("PASS unused header emits no prototype metadata or witnesses")

    result = {"optimization": args.optimization, "toolchain": version, "passed": True}
    (build / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS {args.optimization}: results in {build}")


if __name__ == "__main__":
    main()
