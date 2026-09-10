#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "v4/errors.hpp"
#include "v4/internal/vm.h"
#include "v4/opcodes.hpp"
#include "v4/vm_api.h"

extern "C" v4_err vm_exec_raw(Vm* vm, const v4_u8* code, int code_len);

namespace
{
struct Invocation
{
  Vm* vm = nullptr;
  v4_i32 id = 0;
  v4_i32 args[3] = {};
  int count = 0;
};
Invocation invocation;

v4_i32 record_sys(Vm* vm, v4_i32 id, v4_i32 arg0, v4_i32 arg1, v4_i32 arg2)
{
  invocation.vm = vm;
  invocation.id = id;
  invocation.args[0] = arg0;
  invocation.args[1] = arg1;
  invocation.args[2] = arg2;
  ++invocation.count;
  return -42;
}

v4_i32 replacement_sys(Vm*, v4_i32, v4_i32, v4_i32, v4_i32)
{
  return 99;
}

struct SysFixture
{
  Vm vm{};

  SysFixture()
  {
    v4_register_sys_handler(nullptr);
    invocation = {};
    vm_reset(&vm);
  }

  ~SysFixture()
  {
    v4_register_sys_handler(nullptr);
  }

  void arguments(v4_i32 id)
  {
    REQUIRE(vm_ds_push(&vm, 11) == 0);
    REQUIRE(vm_ds_push(&vm, -22) == 0);
    REQUIRE(vm_ds_push(&vm, 33) == 0);
    REQUIRE(vm_ds_push(&vm, id) == 0);
  }

  v4_err execute()
  {
    const v4_u8 code[] = {static_cast<v4_u8>(v4::Op::SYS),
                          static_cast<v4_u8>(v4::Op::RET)};
    return vm_exec_raw(&vm, code, sizeof(code));
  }
};
}  // namespace

TEST_CASE_FIXTURE(SysFixture, "SYS forwards the VM, full ID and arguments in stack order")
{
  v4_register_sys_handler(record_sys);
  const v4_i32 ids[] = {0, 0x100, 0x12345678, (-2147483647 - 1), -1};
  for (v4_i32 id : ids)
  {
    vm_reset(&vm);
    invocation = {};
    REQUIRE(vm_ds_push(&vm, 77) == 0);  // Unrelated caller stack value
    arguments(id);
    REQUIRE(execute() == 0);
    CHECK(invocation.count == 1);
    CHECK(invocation.vm == &vm);
    CHECK(invocation.id == id);
    CHECK(invocation.args[0] == 11);
    CHECK(invocation.args[1] == -22);
    CHECK(invocation.args[2] == 33);
    REQUIRE(vm_ds_depth_public(&vm) == 2);
    CHECK(vm_ds_peek_public(&vm, 0) == -42);  // Negative result is not a VM error
    CHECK(vm_ds_peek_public(&vm, 1) == 77);
  }
}

TEST_CASE_FIXTURE(SysFixture, "SYS without a handler returns unsupported")
{
  arguments(0x12345678);
  REQUIRE(execute() == 0);
  REQUIRE(vm_ds_depth_public(&vm) == 1);
  CHECK(vm_ds_peek_public(&vm, 0) == -1);
  CHECK(invocation.count == 0);
}

TEST_CASE_FIXTURE(SysFixture, "SYS handler can be replaced and unregistered")
{
  v4_register_sys_handler(record_sys);
  v4_register_sys_handler(replacement_sys);
  arguments(1);
  REQUIRE(execute() == 0);
  CHECK(vm_ds_peek_public(&vm, 0) == 99);
  CHECK(invocation.count == 0);

  vm_reset(&vm);
  v4_register_sys_handler(nullptr);
  arguments(1);
  REQUIRE(execute() == 0);
  REQUIRE(vm_ds_depth_public(&vm) == 1);
  CHECK(vm_ds_peek_public(&vm, 0) == -1);
}

TEST_CASE_FIXTURE(SysFixture, "SYS rejects every incomplete argument stack")
{
  v4_register_sys_handler(record_sys);
  for (int depth = 0; depth < 4; ++depth)
  {
    vm_reset(&vm);
    for (int i = 0; i < depth; ++i)
      REQUIRE(vm_ds_push(&vm, i) == 0);
    CHECK(execute() == V4_ERR(StackUnderflow));
    CHECK(invocation.count == 0);
  }
}

TEST_CASE_FIXTURE(SysFixture, "SYS has no immediate operand")
{
  v4_register_sys_handler(record_sys);
  arguments(0x10000);
  const v4_u8 code[] = {
      static_cast<v4_u8>(v4::Op::SYS), static_cast<v4_u8>(v4::Op::LIT), 7, 0, 0, 0,
      static_cast<v4_u8>(v4::Op::RET)};
  REQUIRE(vm_exec_raw(&vm, code, sizeof(code)) == 0);
  REQUIRE(vm_ds_depth_public(&vm) == 2);
  CHECK(vm_ds_peek_public(&vm, 0) == 7);
  CHECK(vm_ds_peek_public(&vm, 1) == -42);
}

TEST_CASE_FIXTURE(SysFixture, "SYS registration is shared by VM instances")
{
  v4_register_sys_handler(record_sys);
  Vm other{};
  vm_reset(&other);
  REQUIRE(vm_ds_push(&other, 1) == 0);
  REQUIRE(vm_ds_push(&other, 2) == 0);
  REQUIRE(vm_ds_push(&other, 3) == 0);
  REQUIRE(vm_ds_push(&other, 4) == 0);
  const v4_u8 code[] = {static_cast<v4_u8>(v4::Op::SYS), static_cast<v4_u8>(v4::Op::RET)};
  REQUIRE(vm_exec_raw(&other, code, sizeof(code)) == 0);
  CHECK(invocation.vm == &other);
  CHECK(invocation.id == 4);
  REQUIRE(vm_ds_depth_public(&other) == 1);
  CHECK(vm_ds_peek_public(&other, 0) == -42);
}
