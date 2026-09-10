#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "v4/errors.h"
#include "v4/internal/scheduler.hpp"
#include "v4/internal/vm.h"
#include "v4/opcodes.hpp"
#include "v4/panic.h"
#include "v4/task.h"

static int panic_count;
static V4PanicInfo last_panic;

static void capture_panic(void*, const V4PanicInfo* info)
{
  ++panic_count;
  last_panic = *info;
}

TEST_CASE("Disabled task APIs remain linkable and reject without changing outputs")
{
  VmConfig cfg{};
  Vm* allocated = vm_create(&cfg);
  REQUIRE(allocated != nullptr);
  for (Vm* vm : {allocated, static_cast<Vm*>(nullptr)})
  {
    const auto expected = vm ? V4_ERR_UnknownOp : V4_ERR_InvalidArg;
    v4_task_state_t state = V4_TASK_STATE_READY;
    uint8_t priority = 123;
    int32_t data = 456;
    uint8_t source = 7;
    CHECK(vm_task_init(vm, 10) == expected);
    CHECK(vm_schedule(vm) == expected);
    CHECK(vm_schedule_from_isr(vm) == expected);
    CHECK(vm_task_cleanup(vm) == expected);
    CHECK(vm_task_spawn(vm, 0, 128, 256, 64) == expected);
    CHECK(vm_task_exit(vm) == expected);
    CHECK(vm_task_sleep(vm, 1) == expected);
    CHECK(vm_task_yield(vm) == expected);
    CHECK(vm_task_critical_enter(vm) == expected);
    CHECK(vm_task_critical_exit(vm) == expected);
    CHECK(vm_task_self(vm) == expected);
    CHECK(vm_task_get_info(vm, 0, &state, &priority) == expected);
    CHECK(vm_task_get_info(vm, 255, nullptr, nullptr) == expected);
    CHECK(vm_task_send(vm, 0, 1, 42) == expected);
    CHECK(vm_task_receive(vm, 0, &data, &source) == expected);
    CHECK(vm_task_receive_blocking(vm, 0, &data, &source, 0) == expected);
    CHECK(vm_task_receive(vm, 0, nullptr, nullptr) == expected);
    CHECK(vm_task_receive_blocking(vm, 0, nullptr, nullptr, 0) == expected);
    CHECK(state == V4_TASK_STATE_READY);
    CHECK(priority == 123);
    CHECK(data == 456);
    CHECK(source == 7);
  }
  CHECK(allocated->scheduler.task_count == 0);
  CHECK(allocated->scheduler.critical_nesting == 0);
  vm_destroy(allocated);
}

TEST_CASE("Every disabled task opcode panics before consuming stack arguments")
{
  const v4::Op ops[] = {
      v4::Op::TASK_SPAWN, v4::Op::TASK_EXIT,      v4::Op::TASK_SLEEP,
      v4::Op::TASK_YIELD, v4::Op::CRITICAL_ENTER, v4::Op::CRITICAL_EXIT,
      v4::Op::TASK_SEND,  v4::Op::TASK_RECEIVE,   v4::Op::TASK_RECEIVE_BLOCKING,
      v4::Op::TASK_SELF,  v4::Op::TASK_COUNT};
  for (auto op : ops)
  {
    for (int depth : {0, 4})
    {
      CAPTURE(static_cast<int>(op));
      CAPTURE(depth);
      VmConfig cfg{};
      Vm* vm = vm_create(&cfg);
      REQUIRE(vm != nullptr);
      for (int i = 0; i < depth; ++i)
        REQUIRE(vm_ds_push(vm, 40 + i) == 0);
      const uint8_t code[] = {static_cast<uint8_t>(op), 0x51};
      int idx = vm_register_word(vm, nullptr, code, sizeof(code));
      REQUIRE(idx >= 0);
      panic_count = 0;
      vm_set_panic_handler(vm, capture_panic, nullptr);
      CHECK(vm_exec(vm, vm_get_word(vm, idx)) == V4_ERR_UnknownOp);
      CHECK(panic_count == 1);
      CHECK(last_panic.error_code == V4_ERR_UnknownOp);
      CHECK(vm_ds_depth_public(vm) == depth);
      for (int i = 0; i < depth; ++i)
        CHECK(vm_ds_peek_public(vm, i) == 40 + depth - 1 - i);
      CHECK(vm->scheduler.task_count == 0);
      vm_destroy(vm);
    }
  }
}
