#!/usr/bin/env python3
"""Build reference probes or compare existing ELF files; Python standard library only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
PROFILES = {"plain": (False, False), "gc": (False, True), "lto-gc": (True, True)}
METRICS = ("text", "data", "bss", "text_data")


def run(args, **kwargs):
    return subprocess.check_output([str(a) for a in args], text=True, **kwargs).strip()


def tool_version(tool):
    return run([tool, "--version"]).splitlines()[0]


def parse_size(output):
    rows = [line.split() for line in output.splitlines() if line.strip()]
    if len(rows) != 2 or rows[0][:3] != ["text", "data", "bss"]:
        raise ValueError("Expected one ELF in Berkeley-format size output")
    text, data, bss = map(int, rows[1][:3])
    if min(text, data, bss) < 0:
        raise ValueError("Negative section size")
    return {"text": text, "data": data, "bss": bss, "text_data": text + data}


def measure(elf, size_tool, configuration):
    elf = Path(elf)
    content = elf.read_bytes()
    if len(content) < 20 or content[:4] != b"\x7fELF":
        raise ValueError("Input must be an ELF executable, not an archive or raw .bin")
    if content[4] not in (1, 2) or content[5] not in (1, 2):
        raise ValueError("Unsupported ELF encoding")
    endian = "little" if content[5] == 1 else "big"
    elf_type = int.from_bytes(content[16:18], endian)
    if elf_type not in (2, 3):
        raise ValueError("Relocatable objects cannot be compared as final binaries")
    identity = dict(configuration)
    identity.update(elf_class=content[4], endian=endian,
                    machine=int.from_bytes(content[18:20], endian),
                    size_tool=tool_version(size_tool))
    return {"schema": 1, "configuration": identity, "elf": str(elf.resolve()),
            "sha256": hashlib.sha256(content).hexdigest(),
            "file_bytes": len(content),
            "sizes": parse_size(run([size_tool, "--format=berkeley", elf],
                                    env={**os.environ, "LC_ALL": "C"}))}


def write_report(path, report):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def compare(before, after, max_growth=None):
    if before.get("schema") != 1 or after.get("schema") != 1:
        raise ValueError("Unsupported report schema")
    if before["configuration"] != after["configuration"]:
        raise ValueError("Incomparable configurations/toolchains; rebuild both together")
    lines = ["| Metric | Before (B) | After (B) | Delta (B) |",
             "|---|---:|---:|---:|"]
    for key in METRICS:
        old, new = before["sizes"][key], after["sizes"][key]
        lines.append(f"| {key} | {old} | {new} | {new - old:+d} |")
    growth = after["sizes"]["text_data"] - before["sizes"]["text_data"]
    return "\n".join(lines), max_growth is not None and growth > max_growth


def build(args):
    source, output = args.source.resolve(), args.output.resolve()
    if not (source / "CMakeLists.txt").is_file():
        raise ValueError("Engine source directory has no CMakeLists.txt")
    # Refuse to silently reuse a CMake cache, whose options might differ.
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError("Build output must be an empty directory")
    harness_hash = hashlib.sha256(
        (HERE / "probe.cpp").read_bytes() + (HERE / "CMakeLists.txt").read_bytes()
        + Path(__file__).read_bytes()
    ).hexdigest()
    for profile, (lto, gc) in PROFILES.items():
        directory = output / profile
        subprocess.run(["cmake", "-S", str(HERE), "-B", str(directory),
                        "-G", "Unix Makefiles", f"-DV4_SOURCE_DIR={source}",
                        f"-DSIZE_LTO={'ON' if lto else 'OFF'}",
                        f"-DSIZE_GC={'ON' if gc else 'OFF'}"], check=True)
        subprocess.run(["cmake", "--build", str(directory), "--target", "v4_size_probe",
                        "--parallel", str(args.jobs)], check=True)
        elf = directory / "v4_size_probe"
        subprocess.run([str(elf)], check=True)
        cache = {}
        for line in (directory / "CMakeCache.txt").read_text().splitlines():
            if "=" in line and not line.startswith(("//", "#")):
                key, value = line.split("=", 1)
                cache[key.split(":", 1)[0]] = value
        compiler = cache["CMAKE_CXX_COMPILER"]
        flags = {key: value for key, value in cache.items()
                 if key.startswith(("CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS",
                                    "CMAKE_EXE_LINKER_FLAGS")) and "-ADVANCED" not in key}
        config = {"profile": profile, "compiler": tool_version(compiler),
                  "target": run([compiler, "-dumpmachine"]),
                  "linker": tool_version(cache["CMAKE_LINKER"]),
                  "cmake": tool_version("cmake"), "harness": harness_hash,
                  "flags": flags, "backend": "CUSTOM", "optimization": "Os"}
        report = measure(elf, args.size_tool, config)
        report["source_revision"] = run(["git", "-C", source, "rev-parse", "HEAD"])
        report["source_dirty"] = bool(run(["git", "-C", source, "status", "--porcelain"]))
        write_report(output / f"{profile}.json", report)
        print(f"{profile}: {report['sizes']}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    builder = commands.add_parser("build", help="Build three Linux/GCC reference profiles")
    builder.add_argument("--source", type=Path, default=HERE.parent.parent)
    builder.add_argument("--output", type=Path, required=True)
    builder.add_argument("--jobs", type=int, default=2)
    builder.add_argument("--size-tool", default="size")
    recorder = commands.add_parser("measure", help="Record an existing host or firmware ELF")
    recorder.add_argument("elf", type=Path)
    recorder.add_argument("--output", type=Path, required=True)
    recorder.add_argument("--configuration", required=True,
                          help="Stable identity: board, toolchain/IDF, flags and sdkconfig hash")
    recorder.add_argument("--size-tool", default="size")
    comparer = commands.add_parser("compare", help="Compare reports, refusing unlike configurations")
    comparer.add_argument("before", type=Path)
    comparer.add_argument("after", type=Path)
    comparer.add_argument("--max-growth", type=int, help="Fail if text+data grows by more than N bytes")
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            if args.jobs < 1:
                raise ValueError("--jobs must be positive")
            build(args)
        elif args.command == "measure":
            write_report(args.output, measure(args.elf, args.size_tool,
                                             {"profile": "external", "identity": args.configuration}))
        else:
            table, exceeded = compare(json.loads(args.before.read_text()),
                                      json.loads(args.after.read_text()), args.max_growth)
            print(table)
            return 1 if exceeded else 0
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        print(f"size-report: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
