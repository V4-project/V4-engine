# V4 Engine

A lightweight Forth-compatible bytecode VM written in C++17.

## Features

- Stack-based architecture with 32-bit cells
- **Preemptive multitasking system** (v0.9.0+)
  - Priority-based scheduler with up to 8 concurrent tasks
  - Inter-task message passing with 16-message queue
  - Task sleep, yield, and critical sections
  - Platform-abstracted timer interrupts
- **Panic Handler** (v0.12.0+)
  - Comprehensive error diagnostics with `vm_panic()`
  - Stack trace and register dump
  - Custom panic handler support for embedded systems
- Memory-mapped I/O support
- Little-endian byte ordering
- No exceptions, no RTTI
- Zero dependencies (except tests)

## Building

### Current SYS contract (local source, 2026-09-10)

`SYS` has no immediate operand and uses `( arg0 arg1 arg2 sys_id -- result )`.
The ID is a 32-bit stack value. Register one global callback with `v4_register_sys_handler()`;
it receives the VM, ID and three arguments. With no handler, SYS consumes its arguments and pushes -1.
Linking V4-hal does not automatically dispatch GPIO/UART/timer IDs.

The `test_sys` cases cover callback registration, replacement/removal, full-width IDs,
argument order, stack underflow and the absence of an immediate operand.
See [the public API](include/v4/vm_api.h) and [execution code](src/core.cpp).

### CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Binary size comparisons

`make size SIZE_OUTPUT=build-size-before` measures a minimal linked VM executable in
three fixed GC/LTO configurations. JSON reports can be compared locally or against the
base revision in the Binary Size CI workflow. Existing firmware ELF files can also be
recorded and compared; host measurements are not firmware flash sizes.
See [measurement commands, constraints and optional budgets](tools/size/README.md).

### Building with V4-hal (C++17 CRTP HAL)

V4 can optionally use the [V4-hal](https://github.com/kirisaki/V4-hal) C++17 CRTP implementation for zero-cost hardware abstraction:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DV4_USE_V4HAL=ON
cmake --build build -j
```

This provides:
- Zero-cost abstraction via compile-time polymorphism
- Platform support: POSIX, ESP32, CH32V203
- Minimal runtime footprint (~5.7KB for GPIO+Timer)
- HAL API linkage; SYS dispatch still requires the callback described above

## Task System

V4 includes a preemptive multitasking system with **pluggable task backends**:

### Task Backends

**Custom Backend** (default):
- V4's own priority-based + round-robin scheduler
- Suitable for bare-metal and testing
- Minimal dependencies

**FreeRTOS Backend**:
- Native FreeRTOS task integration
- For ESP32, ESP-IDF, and other FreeRTOS platforms
- Full FreeRTOS scheduler features

Configure backend via CMake:
```bash
# Custom backend (default)
cmake -B build -DV4_TASK_BACKEND=CUSTOM

# FreeRTOS backend
cmake -B build -DV4_TASK_BACKEND=FREERTOS -DFREERTOS_PATH=/path/to/freertos
```

For ESP-IDF, FreeRTOS is provided automatically (no `FREERTOS_PATH` needed).

### Memory Footprint
- Task scheduler: ~400 bytes
- Per-task overhead: 32 bytes + stack allocation
- Total VM with tasks: ~42KB (Release build with LTO)

### API Example
```c
#include "v4/task.h"

// Initialize task system (10ms time slice)
vm_task_init(vm, 10);

// Spawn a task
int task_id = vm_task_spawn(vm, word_idx, priority, ds_size, rs_size);

// Task control
vm_task_sleep(vm, 100);  // Sleep 100ms
vm_task_yield(vm);       // Yield to other tasks

// Inter-task messaging
vm_task_send(vm, target_task, msg_type, data);
vm_task_receive(vm, msg_type, &data, &src_task);
```

### VM Opcodes (0x90-0x9A)
- `TASK_SPAWN`, `TASK_EXIT`, `TASK_SLEEP`, `TASK_YIELD`
- `CRITICAL_ENTER`, `CRITICAL_EXIT`
- `TASK_SEND`, `TASK_RECEIVE`, `TASK_RECEIVE_BLOCKING`
- `TASK_SELF`, `TASK_COUNT`

See [v4/task.h](include/v4/task.h) for full API documentation.

## Panic Handler

Standard diagnostics are enabled by default and precede any custom callback.
For size-sensitive embedders with their own logging, configure
`-DV4_PANIC_DIAGNOSTICS=OFF` to remove the standard formatter and its strings.
Callbacks, stack snapshots and error returns remain enabled. Without a callback,
this configuration returns the error silently. Direct-source builds must define
`V4_PANIC_DIAGNOSTICS=0` when compiling `src/panic.cpp`; defining it only in the
application does not change an already-built engine. The public ABI is unchanged.

V4 includes a comprehensive panic handler for debugging errors in production:

### Features
- **Automatic diagnostics**: Collects PC, stack depths, top stack values
- **Stack trace**: Full return stack dump for call trace analysis
- **Custom handlers**: Register application-specific panic handlers
- **Embedded-friendly**: No dynamic allocation, minimal overhead

### API Example
```c
#include "v4/panic.h"

// Optional: Register custom panic handler
void my_panic_handler(void* user_data, const V4PanicInfo* info) {
    // Log to external storage, blink LED, etc.
    printf("Custom handler: Error %d at PC=0x%08X\n",
           info->error_code, info->pc);
}

vm_set_panic_handler(vm, my_panic_handler, NULL);

// Panic is called automatically on errors
// Or call manually for custom error conditions
vm_panic(vm, V4_ERR_CUSTOM);
```

### Output Example
```
========== V4 PANIC ==========
Error: StackOverflow (code=-3)
PC: 0x00001234
Data Stack: [256] TOS=42, NOS=100
Return Stack: [3]
Call trace:
  [0] 0x00001200
  [1] 0x00001180
  [2] 0x00001100
==============================
```

See [include/v4/panic.h](include/v4/panic.h) and [tests/test_panic.cpp](tests/test_panic.cpp) for details.

## Testing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DV4_BUILD_TESTS=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

### Testing with V4-hal

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DV4_BUILD_TESTS=ON -DV4_USE_V4HAL=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

## License

Licensed under either of:

- MIT License ([LICENSE-MIT](LICENSE-MIT))
- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))

at your option.
