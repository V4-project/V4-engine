#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>

#include "doctest.h"
#include "v4/errors.hpp"
#include "v4/internal/vm.h"
#include "v4/opcodes.hpp"
#include "v4/panic.h"
#include "v4/vm_api.h"

// Test helper: custom panic handler
static bool g_panic_called = false;
static V4PanicInfo g_panic_info = {};
static void *g_panic_user_data = nullptr;

static void test_panic_handler(void *user_data, const V4PanicInfo *info)
{
  g_panic_called = true;
  g_panic_user_data = user_data;
  if (info)
  {
    g_panic_info = *info;
  }
}

TEST_CASE("Panic handler: Empty stack")
{
  Vm vm{};
  vm_reset(&vm);

  // Trigger panic with empty stack
  v4_err err = vm_panic(&vm, static_cast<v4_err>(Err::StackUnderflow));

  CHECK(err == static_cast<v4_err>(Err::StackUnderflow));
  // Expected output:
  // Data Stack: [0]
}

TEST_CASE("Panic handler: With stack data")
{
  Vm vm{};
  vm_reset(&vm);

  // Push data to stack
  vm_ds_push(&vm, 42);
  vm_ds_push(&vm, 100);

  // Trigger panic
  v4_err err = vm_panic(&vm, static_cast<v4_err>(Err::InvalidArg));

  CHECK(err == static_cast<v4_err>(Err::InvalidArg));
  CHECK(vm_ds_depth_public(&vm) == 2);
  // Expected output:
  // TOS=100, NOS=42
}

TEST_CASE("Panic handler: Multiple stack items")
{
  Vm vm{};
  vm_reset(&vm);

  // Push multiple items
  for (int i = 1; i <= 5; i++)
  {
    vm_ds_push(&vm, i * 10);
  }

  // Trigger panic
  v4_err err = vm_panic(&vm, static_cast<v4_err>(Err::OobMemory));

  CHECK(err == static_cast<v4_err>(Err::OobMemory));
  CHECK(vm_ds_depth_public(&vm) == 5);
  // Expected output:
  // Data Stack: [5] TOS=50, NOS=40
}

TEST_CASE("Panic handler: Stack overflow detection")
{
  Vm vm{};
  vm_reset(&vm);

  // Intentionally overflow the stack
  v4_err err = 0;
  for (int i = 0; i < 1000; i++)
  {
    err = vm_ds_push(&vm, i);
    if (err < 0)
    {
      break;
    }
  }

  CHECK(err == static_cast<v4_err>(Err::StackOverflow));
  // vm_ds_push should have called vm_panic internally
}

TEST_CASE("Panic handler: Stack underflow detection")
{
  Vm vm{};
  vm_reset(&vm);

  // Pop from empty stack
  v4_i32 value;
  v4_err err = vm_ds_pop(&vm, &value);

  CHECK(err == static_cast<v4_err>(Err::StackUnderflow));
  // vm_ds_pop should have called vm_panic internally
}

TEST_CASE("Custom panic handler: Basic callback")
{
  Vm vm{};
  vm_reset(&vm);

  // Reset test state
  g_panic_called = false;
  g_panic_user_data = nullptr;

  // Register custom handler
  int user_value = 42;
  vm_set_panic_handler(&vm, test_panic_handler, &user_value);

  // Push some data and trigger panic
  vm_ds_push(&vm, 100);
  vm_ds_push(&vm, 200);

  v4_err err = vm_panic(&vm, static_cast<v4_err>(Err::InvalidArg));

  // Verify callback was invoked
  CHECK(g_panic_called == true);
  CHECK(g_panic_user_data == &user_value);
  CHECK(g_panic_info.error_code == static_cast<int32_t>(Err::InvalidArg));
  CHECK(g_panic_info.ds_depth == 2);
  CHECK(g_panic_info.has_stack_data == true);
  CHECK(g_panic_info.tos == 200);
  CHECK(g_panic_info.nos == 100);
}

TEST_CASE("Custom panic handler: Stack array population")
{
  Vm vm{};
  vm_reset(&vm);

  // Reset test state
  g_panic_called = false;

  // Register custom handler
  vm_set_panic_handler(&vm, test_panic_handler, nullptr);

  // Push 4 values
  vm_ds_push(&vm, 10);
  vm_ds_push(&vm, 20);
  vm_ds_push(&vm, 30);
  vm_ds_push(&vm, 40);

  v4_err err = vm_panic(&vm, static_cast<v4_err>(Err::OobMemory));

  // Verify stack array contains top 4 values
  CHECK(g_panic_called == true);
  CHECK(g_panic_info.ds_depth == 4);
  CHECK(g_panic_info.stack[0] == 40);  // TOS
  CHECK(g_panic_info.stack[1] == 30);
  CHECK(g_panic_info.stack[2] == 20);
  CHECK(g_panic_info.stack[3] == 10);
}

TEST_CASE("Custom panic handler: NULL handler")
{
  Vm vm{};
  vm_reset(&vm);

  // Set and then clear handler
  vm_set_panic_handler(&vm, test_panic_handler, nullptr);
  vm_set_panic_handler(&vm, nullptr, nullptr);

  g_panic_called = false;

  // Trigger panic - should not crash
  v4_err err = vm_panic(&vm, static_cast<v4_err>(Err::InvalidArg));

  CHECK(err == static_cast<v4_err>(Err::InvalidArg));
  CHECK(g_panic_called == false);  // Handler was cleared
}

TEST_CASE("Custom panic handler: Deterministic snapshot without stack mutation")
{
  for (int depth = 0; depth <= 6; ++depth)
  {
    Vm vm{};
    vm_reset(&vm);
    vm_set_panic_handler(&vm, test_panic_handler, nullptr);
    for (int i = 0; i < depth; ++i)
      REQUIRE(vm_ds_push(&vm, 10 + i) == 0);
    *vm.rp++ = 123;

    g_panic_called = false;
    REQUIRE(vm_panic(&vm, static_cast<v4_err>(Err::InvalidArg)) ==
            static_cast<v4_err>(Err::InvalidArg));
    CHECK(g_panic_called);
    CHECK(g_panic_info.pc == 0);
    CHECK(g_panic_info.ds_depth == depth);
    CHECK(g_panic_info.rs_depth == 1);
    CHECK(vm.rp == vm.RS + 1);
    CHECK(vm.RS[0] == 123);
    CHECK(g_panic_info.has_stack_data == (depth > 0));
    CHECK(g_panic_info.tos == (depth > 0 ? 9 + depth : 0));
    CHECK(g_panic_info.nos == (depth > 1 ? 8 + depth : 0));
    for (int i = 0; i < 4; ++i)
      CHECK(g_panic_info.stack[i] == (i < depth ? 9 + depth - i : 0));
    CHECK(vm_ds_depth_public(&vm) == depth);
    for (int i = 0; i < depth; ++i)
      CHECK(vm_ds_peek_public(&vm, i) == 9 + depth - i);
  }
}
