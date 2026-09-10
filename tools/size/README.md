# Binary size comparisons

Measure a **final linked ELF**, not `libv4engine.a` (which can contain LTO intermediate
representation), and not a test runner containing doctest. No VM optimizations are
introduced by this harness.

## Reference host build

Requirements: Linux, GCC, GNU binutils (`size`), CMake, Make and Python 3.8+.
No Python packages or downloaded C++ test framework are needed.

```sh
make size SIZE_OUTPUT=build-size-before
# After making an engine change:
make size SIZE_OUTPUT=build-size-after
python3 tools/size/size_report.py compare build-size-before/lto-gc.json build-size-after/lto-gc.json
```

The output directory must be empty: the tool never deletes previous reports or reuses
a potentially stale CMake cache. Keep build outputs outside the source tree or in a
git-ignored `build-*` directory. Use `--source /path/to/other/revision` to build another
checkout **with the current harness**, including revisions predating this tool.

The three profiles use Release, `-Os`, CUSTOM tasks, mock platform support, and no
V4-hal/V4-std or test executables:

| Profile | LTO | Link-time section GC |
|---|---|---|
| plain | off | off |
| gc | off | on |
| lto-gc | on | on |

The probe creates a VM, registers and executes a word, and destroys the VM. It accepts
runtime bytecode input so the compiler cannot replace the interpreter with a known
RET-only result. The smoke test runs its safe RET default. It is not a performance
benchmark or a validation of task scheduling. The mock platform and host libc affect
the result; these numbers are **not ESP32 firmware sizes**. Do not compare different
profiles against each other as if they were code-change deltas.

Standard panic diagnostics are enabled by default. With engine 0.17.0+, use
`build --panic-diagnostics off --output build-size-no-panic-output` to measure
an opt-in build without the standard panic formatter. Reports record this setting
and reject comparisons between on/off configurations. Compare matching settings
for refactoring deltas; an on/off comparison represents a feature tradeoff instead.

Engine 0.18.0+ also supports `build --tasks off --output build-size-no-tasks`.
This retains the VM layout but removes task instruction implementations and scheduler/
platform code. Task and message APIs remain linkable and report unsupported operations.
The recorder verifies that the requested OFF setting reached the compiler, records
`tasks` and the effective backend, and rejects on/off comparisons. Older engine
revisions cannot be measured with tasks disabled. CI additionally retains the current
no-task measurements as a separate configuration, not as a compatible base/current delta.

Each profile retains `v4_size_probe`, `probe.map`, and a JSON report with source revision,
dirty status, ELF architecture, compiler/linker/size/CMake versions, CMake flags and a
harness hash. Comparisons reject differing configurations. Rebuild both revisions when
the harness or toolchain changes; do not edit metadata just to permit a comparison.

## Metrics and regression limits

Reports use Berkeley-format `size`:

- `text`: code plus read-only sections and other content classified as text by the tool.
- `data`: initialized writable data.
- `bss`: zero-initialized data; normally consumes RAM, not payload bytes in flash.
- `text_data`: text + data, a useful linked-image comparison metric.
- `file_bytes`: ELF file length, recorded only; debug sections and file padding make it
  unsuitable as the optimization budget.

`text_data` is **not an exact flash image size**, especially with IDF's multiple memory
regions and segment alignment. Dynamic heap usage and stack consumption are not measured.
For target decisions also inspect the linker map and the platform's memory report.

By default increases are reported without failing. To enforce a budget explicitly:

```sh
python3 tools/size/size_report.py compare before.json after.json --max-growth 0
```

Exit codes: 0 = valid comparison within budget (or no budget); 1 = text+data budget
exceeded; 2 = invalid input, differing configurations or a failed build/tool invocation.
Budgets apply independently per profile; BSS deltas remain visible but are not gated.

## Existing ESP32-C6 firmware ELF

Build both firmware revisions using identical ESP-IDF, compiler, board, sdkconfig,
optimization options, and all other repository revisions. Record that identity explicitly:

```sh
python3 tools/size/size_report.py measure /path/to/before/v4-runtime.elf \
  --size-tool riscv32-esp-elf-size \
  --configuration 'nanoc6|IDF=<version>|compiler=<version>|sdkconfig=<sha256>|deps=<revisions>' \
  --output before.json
python3 tools/size/size_report.py measure /path/to/after/v4-runtime.elf \
  --size-tool riscv32-esp-elf-size \
  --configuration 'nanoc6|IDF=<version>|compiler=<version>|sdkconfig=<sha256>|deps=<revisions>' \
  --output after.json
python3 tools/size/size_report.py compare before.json after.json
```

Replace placeholders with real values (the engine revision being compared is excluded
from the invariant dependency identity). The recorder verifies ELF encoding, architecture
and the size-tool version; it cannot infer sdkconfig/build flags from an arbitrary ELF.
The caller is responsible for accurate `--configuration`. Keep each platform linker map
and `idf.py size` result alongside these reports. This does not build, flash, or execute
firmware. RISC-V measurements cannot be compared with host measurements.

## CI

Binary Size builds base and current revisions on the same Ubuntu 24.04 runner with
GCC 13. PRs compare against the PR base; pushes compare against the previous branch SHA.
Manual runs accept a baseline ref. Initial pushes without a baseline only record current
sizes. A changed harness is used for **both** builds, avoiding harness drift. Results
appear in the job summary; JSON, ELF and map artifacts are kept for 30 days. No historical
machine-independent baseline or default growth budget is imposed.

Run reporter tests with:

```sh
python3 -m unittest discover -s tools/size -v
```
