# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.18.0] - 2026-09-10

### Added
- Opt-in `V4_ENABLE_TASKS=OFF` builds omit task execution, scheduler, messaging and task-platform code while preserving VM layout and public task API symbols. APIs return `UnknownOp` (`InvalidArg` for a null VM); disabled opcodes use the existing unknown-opcode panic path without consuming arguments. Default task-enabled and runtime configurations are unchanged.
- Regression coverage for arithmetic operand order and failure snapshots, dictionary ownership/lifecycle, and every disabled task opcode and public task API.
- Size recorder `--tasks on|off` with configuration validation, CI measurements for the task-disabled build, and CI tests with tasks and/or panic diagnostics disabled.

### Changed
- Share sequential operand pops and the result push across 15 arithmetic/comparison instructions, preserving existing partial-stack effects and panic timing.
- Apply standalone FreeRTOS include paths after creating the engine target and only when tasks are enabled.

## [0.17.0] - 2026-09-10

### Added
- Opt-in removal of standard panic printing with `V4_PANIC_DIAGNOSTICS=OFF`, preserving callbacks, snapshots and error returns. Diagnostics remain enabled by default, including direct-source builds.
- Output/callback-order checks and a diagnostics-disabled CI build; size reports record the panic diagnostics setting.

### Changed
- Separate panic formatting from snapshot collection within the existing source file, preserving downstream direct-source build lists.
- Read each of the top four data-stack values once; initialize unavailable snapshot fields to zero.

## [0.16.0] - 2026-09-10

### Added
- Reference Linux/GCC size probe with three fixed profiles: -Os, -Os with section GC, and -Os with LTO and section GC. VM behavior is unchanged.
- JSON reports and Markdown comparisons for reference executables and existing firmware ELF files, with configuration mismatch checks and an optional text+data growth budget.
- Binary Size CI compares base and current revisions using the same harness/toolchain, retains ELF files and link maps, and reports changes without enforcing a growth budget yet.
- Reporting-tool regression tests and documented host/ESP32-C6 measurement procedures.

### Changed
- Advance beyond the existing v0.15.0 tag instead of reusing the stale 0.14.0 CMake version.
- Replace the Makefile's archive/doctest size measurement with the reference probe.
- Exclude ignored build artifacts from CMake formatting checks, and include the new probe in C++ formatting checks.

## [0.13.0] - 2025-11-05

### Added
- **V4-std integration** for dynamic SYS instruction handling
  - Platform-specific SYS handlers can be registered at runtime
  - V4-std provides system call routing infrastructure
  - See [V4-std](https://github.com/V4-project/V4-std) for implementation
- **Word accessor APIs** for safe access to Word structure
  - `vm_word_get_name()` - Get word name string
  - `vm_word_get_code()` - Get bytecode pointer
  - `vm_word_get_code_len()` - Get bytecode length
  - C++ wrapper class `v4::Word` in `vm_api.hpp`
- **Custom panic handler support** for platform-specific error handling
  - `vm_set_panic_handler()` - Register custom panic callback
  - `V4PanicHandler` function pointer type
  - Enhanced `V4PanicInfo` with `stack[4]` array for top stack values
  - Test coverage for custom panic handlers
  - Documentation in README with usage examples
- **Type definitions and constants**
  - `v4_u16` typedef for 16-bit unsigned integers
  - `V4_OK` constant for success return value

### Changed
- **SYS instruction format upgraded to IMM16**
  - SYS now uses 16-bit immediate operand (was 8-bit)
  - Supports 65536 system calls (was 256)
  - SYSX opcode removed (no longer needed)
  - All tests updated to use `emit16()` for SYS operands
- **Stricter compiler warnings**
  - Added `-Werror` flag to treat warnings as errors
  - Initialize all local variables to avoid `-Wmaybe-uninitialized` warnings
  - Fixed 5 uninitialized variable cases in `core.cpp`

### Removed
- **SYSX opcode** - Consolidated into SYS with IMM16

## [0.12.0] - 2025-01-05

### Added
- **VM panic handler system** for debugging and diagnostics
  - `vm_panic()` API to collect and display error information
  - Panic information structures (`V4PanicInfo`, `v4::PanicInfo`)
  - Diagnostic output including:
    - Error code with human-readable message
    - Program Counter (PC) value
    - Data Stack depth and top 2 elements (TOS, NOS)
    - Return Stack depth with call trace (up to 16 entries)
  - All error paths in VM core now call `vm_panic()` for automatic diagnostic logging
  - Stack helper functions (ds_push, ds_pop, rs_push, rs_pop) integrated with panic handler
  - Opcode execution errors (division by zero, stack overflow/underflow, invalid jumps, etc.) logged automatically
  - Test suite with 5 panic handler test cases covering:
    - Empty stack panic
    - Stack data display (TOS/NOS)
    - Multiple stack items
    - Stack overflow detection
    - Stack underflow detection

### Changed
- Library size increase: ~2KB for panic handler implementation (182KB stripped with LTO)
  - Minimal impact in final applications due to LTO optimization
  - Unused panic output code eliminated at link time

## [0.11.1] - 2025-11-04

### Fixed
- Incorrect error code name in FreeRTOS backend
  - Changed `V4_ERR(OutOfMemory)` to `V4_ERR(NoMemory)` in `task_backend_freertos.cpp`
  - This error prevented compilation of the FreeRTOS backend

## [0.11.0] - 2025-11-04

### Added
- **Pluggable task backend system** for flexible scheduler integration
  - Backend abstraction layer (`v4/internal/task_backend.h`)
  - Custom backend: V4's own priority + round-robin scheduler (default)
  - FreeRTOS backend: Native FreeRTOS task integration for RTOS platforms
  - CMake option `V4_TASK_BACKEND={CUSTOM|FREERTOS}` for compile-time selection
  - ESP-IDF support with automatic FreeRTOS detection (`ESP_PLATFORM` macro)
  - Priority mapping: V4 (0-255) → FreeRTOS (0-configMAX_PRIORITIES)
- Documentation for task backend architecture in README

### Changed
- Task and message implementation refactored as thin wrappers
  - `task.cpp` and `message.cpp` now delegate to backend implementations
  - Original scheduler logic moved to `task_backend_custom.cpp`

## [0.10.1] - 2025-11-04

### Changed
- Repository renamed from `V4` to `V4-engine`
- Updated all references: `v4vm` → `v4engine`, library names, GitHub URLs
- Applied code formatting to CMakeLists.txt

## [0.10.0] - 2025-11-03

### Added
- C-compatible `errors.h` header for platform integration
- RTOS error codes: `V4_ERR_TASK_*` for task management errors
- Support for both C and C++ error handling APIs

## [0.9.1] - 2025-11-03

### Added
- `vm_task_cleanup()`: New API to cleanup task system and free allocated task stacks
  - Should be called before `vm_destroy()` to prevent memory leaks
  - Frees all task stacks (ds_base, rs_base) allocated by `vm_task_spawn()`
  - Resets scheduler and message queue state

### Fixed
- Memory leak in task system detected by AddressSanitizer
  - Task stacks allocated by `vm_task_spawn()` were not being freed
  - All test cases now properly call `vm_task_cleanup()` before `vm_destroy()`

## [0.9.0] - 2025-11-03

### Added
- **Preemptive multitasking system** for embedded microcontrollers
  - Priority-based scheduler supporting up to 8 concurrent tasks
  - Task management API: `vm_task_init()`, `vm_task_spawn()`, `vm_task_exit()`
  - Task control: `vm_task_sleep()`, `vm_task_yield()`, `vm_task_critical_enter/exit()`
  - Inter-task messaging with 16-message ring buffer queue
    - `vm_task_send()`: Non-blocking message send
    - `vm_task_receive()`: Non-blocking receive with type filtering
    - `vm_task_receive_blocking()`: Blocking receive with timeout
  - Task information API: `vm_task_self()`, `vm_task_get_info()`
  - Platform abstraction layer for timer interrupts
    - `v4_task_platform.h`: Interface for platform-specific timer setup
    - Mock platform implementation for testing
  - 11 new VM opcodes (0x90-0x9A):
    - `TASK_SPAWN`, `TASK_EXIT`, `TASK_SLEEP`, `TASK_YIELD`
    - `CRITICAL_ENTER`, `CRITICAL_EXIT`
    - `TASK_SEND`, `TASK_RECEIVE`, `TASK_RECEIVE_BLOCKING`
    - `TASK_SELF`, `TASK_COUNT`
  - Comprehensive test suite with 119 assertions covering:
    - Task initialization and lifecycle
    - Message passing and queue limits
    - Sleep/wake functionality
    - Critical sections and nesting
    - Opcode integration

### Changed
- LTO (Link Time Optimization) now enabled by default (`V4_ENABLE_LTO=ON`)
  - Note: Incompatible with sanitizers (ASan/TSan) - disable LTO when using sanitizers
- Enhanced size optimization with function/data section splitting (`-ffunction-sections`, `-fdata-sections`)
- Linker garbage collection enabled (`-Wl,--gc-sections`)
- Binary size increase: +28KB for task system (14KB → 42KB static library)
  - Task scheduler: ~400 bytes
  - Per-task overhead: 32 bytes (TCB) + independent stacks
  - Message queue: 132 bytes (16 × 8-byte messages)
  - Unused features removed via LTO in final applications

### Performance
- Context switch overhead: <100μs
- Message passing: <50μs
- Default time slice: 10ms (configurable)

## [0.8.0] - 2025-11-02

### Added
- REPL/debugger support API: `vm_find_word()` for searching words by name
  - Case-sensitive linear search through word dictionary
  - Returns word index (>=0) on success, negative value on failure
  - Enables external tools (V4-front, V4-cli) to lookup words without maintaining separate mappings
  - Comprehensive test coverage in `test_api_extensions.cpp`
- REPL/debugger support API: `vm_ds_copy_to_array()` for bulk data stack retrieval
  - Efficient one-shot copy of entire stack to array (bottom to top)
  - Supports partial copy when max_count < stack depth
  - NULL-safe parameter validation
  - Enables efficient stack display and V4-link protocol stack transfers
- REPL/debugger support API: Return stack inspection APIs
  - `vm_rs_depth_public()`: Get return stack depth
  - `vm_rs_copy_to_array()`: Bulk copy return stack to array
  - Same behavior as data stack APIs but for return stack
  - Enables debugger to inspect call frames and local variables

### Changed
- Binary size increase: +1KB (21KB → 22KB) for new REPL/debugger APIs

## [0.7.0] - 2025-11-01

### Changed
- **BREAKING**: Migrated HAL API from `v4/v4_hal.h` to `v4/hal.h`
  - Complete migration to V4-hal library API
  - Replaced `v4_hal_*` functions with `hal_*` equivalents
  - Updated all SYS instruction implementations in VM core
  - GPIO: `v4_hal_gpio_*` → `hal_gpio_*` with proper type casting
  - UART: Character-based API → Buffer-based API with handle management
  - Timer: `v4_hal_millis/micros/delay_*` → `hal_*`
  - Console: `v4_hal_putc/getc` → `hal_console_write/read`

### Added
- UART handle management (static array for up to 4 ports)
- Proper type casting for GPIO values (`hal_gpio_value_t`)
- Comprehensive UART configuration with default 8N1 settings

### Removed
- Dependency on old `v4_hal.h` API

### Note
- System operations (`v4_hal_system_reset`, `v4_hal_system_info`) temporarily return `HAL_ERR_NOTSUP`
- These will be implemented in V4-hal library in a future release

### Tested
- ESP32-C6 M5NanoC6 with v4-blink and v4-repl-demo examples

## [0.6.0] - 2025-10-31

### Added
- V4-hal C++17 CRTP HAL implementation integration
  - Optional V4-hal library support via `V4_USE_V4HAL` option
  - Local path reference (`V4HAL_LOCAL_PATH`) or FetchContent from GitHub
  - Bridge layer (`src/hal_wrapper.cpp`) mapping `v4_hal_*` to V4-hal's `hal_*` API
  - Zero-cost abstraction benefits from C++17 CRTP architecture
  - Backward compatible with existing `v4_hal_*` API
  - All existing tests pass with V4-hal enabled

## [0.4.1] - 2025-10-29

### Changed
- Refactored error handling to use V4_ERR() macro for improved code readability
  - Replaced 68 instances of `static_cast<v4_err>(Err::XXX)` with shorter `V4_ERR(XXX)` macro
  - No runtime cost - compiler generates identical machine code
  - Reduced source code verbosity by ~1200 characters

## [0.4.0] - 2025-10-29

### Added
- Extended arithmetic operations:
  - DIVU (0x15), MODU (0x16): Unsigned division and modulo
  - INC (0x17), DEC (0x18): Increment and decrement TOS
- Extended comparison operations:
  - LTU (0x26), LEU (0x27): Unsigned less-than comparisons
- Bitwise shift operations:
  - SHL (0x2C), SHR (0x2D), SAR (0x2E): Left shift, logical right shift, arithmetic right shift
- Extended memory access operations:
  - LOAD8U (0x32), LOAD16U (0x33): Unsigned 8/16-bit loads
  - STORE8 (0x34), STORE16 (0x35): 8/16-bit stores
  - LOAD8S (0x36), LOAD16S (0x37): Signed 8/16-bit loads with sign extension
- Control flow and compact literal operations:
  - SELECT (0x43): Ternary operator (flag b a SELECT → flag ? a : b)
  - LIT0 (0x73), LIT1 (0x74), LITN1 (0x75): Common constant literals
  - LIT_U8 (0x76), LIT_I8 (0x77), LIT_I16 (0x78): Compact immediate values
- Local variable support:
  - Frame pointer (fp) added to VM structure
  - LGET (0x79), LSET (0x7A), LTEE (0x7B): General local variable access
  - LGET0 (0x7C), LGET1 (0x7D), LSET0 (0x7E), LSET1 (0x7F): Optimized local access
  - LINC (0x80), LDEC (0x81): Local variable increment/decrement
- Comprehensive test coverage for all 31 new opcodes

### Changed
- Local variables stored in return stack with frame pointer for efficient access

### Added (Build System)
- V4_ENABLE_LTO option to enable Link Time Optimization (default: OFF)
  - 35% size reduction in core VM code (23KB → 15KB stripped) when enabled
  - IPO (Interprocedural Optimization) support detection via CheckIPOSupported
  - Note: LTO is not compatible with sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer)

## [0.3.0] - 2025-10-28

### Added
- REPL support APIs for interactive programming:
  - vm_reset_dictionary: Reset word dictionary while preserving stacks
  - vm_reset_stacks: Reset stacks while preserving dictionary
  - vm_ds_push, vm_ds_pop, vm_ds_clear: Direct stack manipulation APIs
  - vm_ds_snapshot, vm_ds_restore, vm_ds_snapshot_free: Stack snapshot/restore for preserving state across VM operations
- Comprehensive test coverage for all new REPL APIs
- Arena memory allocator for embedded systems:
  - Linear bump allocator without individual free
  - Alignment support for various data types
  - v4_arena_init, v4_arena_alloc, v4_arena_reset APIs
  - Eliminates need for malloc/free in constrained environments
- Hardware Abstraction Layer (HAL) for embedded platforms:
  - GPIO API: pin initialization, read/write
  - UART API: serial communication (init, putc, getc)
  - Timer API: milliseconds/microseconds, delays
  - System API: reset and platform info
- SYS instruction (0x60) for HAL access from bytecode:
  - 10 system call IDs for GPIO, UART, and Timer operations
  - Stack-based parameter passing
  - Error code returns for all operations
- Mock HAL implementation for unit testing
- Comprehensive documentation (hal-api.md, sys-opcodes.md)

### Changed
- vm_reset now implemented as vm_reset_dictionary + vm_reset_stacks for better modularity
- All test targets now link with mock_hal for HAL function resolution
- VmConfig now includes optional arena field for memory management
- vm_register_word uses arena allocator when available (falls back to malloc)
- vm_destroy and vm_reset_dictionary skip free() when using arena
- Word name memory is now managed by arena or malloc depending on configuration
- CMakeLists.txt reorganized with clear sections and better structure
- Build system now uses helper function for test targets to reduce repetition
- Added project version to CMake configuration (0.2.1)
- Improved include directory handling with generator expressions

### Removed
- v4repl tool (moved to separate repository)

### Fixed
- Arena alignment test failure on macOS by ensuring test buffer is 16-byte aligned
- Memory leak in test_vm when using vm_register_word without cleanup (detected by AddressSanitizer)

### Added (Build System)
- V4_BUILD_TOOLS option to control tool building (default: ON)
- V4_ENABLE_MOCK_HAL option to control mock HAL inclusion (default: ON)
- V4_OPTIMIZE_SIZE option to control size optimization (default: ON)
- CMake configuration summary displayed during configuration
- Installation targets for library and headers
- V4::mock_hal alias target for easier linking

### Added (CI/CD)
- Build options matrix CI job to test all CMake option combinations
- Tests for minimal builds (library-only configurations)
- Tests for size-optimized vs non-optimized builds
- Verification of all new CMake options (V4_BUILD_TOOLS, V4_ENABLE_MOCK_HAL, V4_OPTIMIZE_SIZE)

## [0.2.1] - 2025-10-27

### Fixed
- Memory leaks in test cases where word names were not properly freed
- Critical: vm_destroy now properly frees word names allocated by vm_register_word

## [0.2.0] - 2025-10-27

### Added
- Return stack operations (TOR, FROMR, RFETCH)
- Word dictionary and CALL opcode for function calls
- New error types: InvalidArg, DictionaryFull, InvalidWordIdx
- vm_register_word and vm_get_word API functions
- vm_exec function for executing word entries
- Word name field for debugging and REPL support
- Stack inspection APIs: vm_ds_depth_public and vm_ds_peek_public

### Changed
- **BREAKING**: vm_register_word now accepts optional name parameter
- **BREAKING**: Word structure now includes name field
- Fixed stack helpers to return errors correctly

## [0.1.3] - 2025-10-26
- Fix bit length of literal number

## [0.1.2] - 2025-10-25
- Describe PrimKind in opcodes.def
- Buld in Windows

## [0.1.1] - 2025-10-25
- Add lables to opcodes

## [0.1.0] - 2025-10-12

### Added
- Initial release of V4 VM
- Stack-based bytecode interpreter with 32-bit cells
- Basic stack operations (LIT, DUP, DROP, SWAP, OVER)
- Arithmetic operations (ADD, SUB, MUL, DIV, MOD)
- Comparison operations (EQ, NE, LT, LE, GT, GE)
- Bitwise operations (AND, OR, XOR, INVERT)
- Control flow instructions (JMP, JZ, JNZ, RET)
- Memory operations (LOAD, STORE) with alignment checking
- Memory-mapped I/O (MMIO) support
- C and C++ public API
- Comprehensive test suite with doctest

[unreleased]: https://github.com/V4-project/V4-engine/compare/v0.12.0...HEAD
[0.12.0]: https://github.com/V4-project/V4-engine/compare/v0.11.1...v0.12.0
[0.11.1]: https://github.com/V4-project/V4-engine/compare/v0.11.0...v0.11.1
[0.9.1]: https://github.com/V4-project/V4-engine/compare/v0.9.0...v0.9.1
[0.9.0]: https://github.com/V4-project/V4-engine/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/V4-project/V4-engine/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/V4-project/V4-engine/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/V4-project/V4-engine/compare/v0.4.1...v0.6.0
[0.4.1]: https://github.com/V4-project/V4-engine/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/V4-project/V4-engine/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/V4-project/V4-engine/compare/v0.2.1...v0.3.0
[0.2.1]: https://github.com/V4-project/V4-engine/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/V4-project/V4-engine/compare/v0.1.3...v0.2.0
[0.1.3]: https://github.com/V4-project/V4-engine/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/V4-project/V4-engine/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/V4-project/V4-engine/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/V4-project/V4-engine/releases/tag/v0.1.0
