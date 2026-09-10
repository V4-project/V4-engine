#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>

#include "doctest.h"
#include "v4/errors.hpp"
#include "v4/internal/vm.h"
#include "v4/opcodes.hpp"
#include "v4/panic.h"
#include "v4/vm_api.h"

namespace
{
constexpr v4::Op binary_ops[] = {v4::Op::ADD, v4::Op::SUB,  v4::Op::MUL,  v4::Op::DIV,
                                 v4::Op::MOD, v4::Op::DIVU, v4::Op::MODU, v4::Op::EQ,
                                 v4::Op::NE,  v4::Op::LT,   v4::Op::LE,   v4::Op::GT,
                                 v4::Op::GE,  v4::Op::LTU,  v4::Op::LEU};

struct PanicCapture
{
  int calls = 0;
  V4PanicInfo info{};
};

void capture_panic(void *user, const V4PanicInfo *info)
{
  auto *capture = static_cast<PanicCapture *>(user);
  ++capture->calls;
  capture->info = *info;
}

v4_err execute(Vm *vm, v4::Op op)
{
  const v4_u8 code[] = {static_cast<v4_u8>(op), static_cast<v4_u8>(v4::Op::RET)};
  return vm_exec_raw(vm, code, sizeof(code));
}
}  // namespace

TEST_CASE("binary operations preserve operand order and signedness")
{
  struct Example
  {
    v4::Op op;
    v4_i32 a, b, expected;
  };
  const Example examples[] = {
      {v4::Op::ADD, -9, 4, -5},       {v4::Op::SUB, -9, 4, -13},
      {v4::Op::MUL, -9, 4, -36},      {v4::Op::DIV, -9, 4, -2},
      {v4::Op::MOD, -9, 4, -1},       {v4::Op::DIVU, -1, 2, INT32_MAX},
      {v4::Op::MODU, -1, 2, 1},       {v4::Op::DIVU, -1, -1, 1},
      {v4::Op::MODU, -1, -1, 0},      {v4::Op::EQ, -9, 4, V4_FALSE},
      {v4::Op::EQ, -9, -9, V4_TRUE},  {v4::Op::NE, -9, 4, V4_TRUE},
      {v4::Op::NE, -9, -9, V4_FALSE}, {v4::Op::LT, -9, 4, V4_TRUE},
      {v4::Op::LT, 4, -9, V4_FALSE},  {v4::Op::LE, -9, -9, V4_TRUE},
      {v4::Op::LE, 4, -9, V4_FALSE},  {v4::Op::GT, -9, 4, V4_FALSE},
      {v4::Op::GT, 4, -9, V4_TRUE},   {v4::Op::GE, -9, -9, V4_TRUE},
      {v4::Op::GE, -9, 4, V4_FALSE},  {v4::Op::LTU, -9, 4, V4_FALSE},
      {v4::Op::LTU, 4, -9, V4_TRUE},  {v4::Op::LEU, -9, 4, V4_FALSE},
      {v4::Op::LEU, -9, -9, V4_TRUE}};
  for (const auto &example : examples)
  {
    INFO("opcode=", static_cast<int>(example.op));
    Vm vm{};
    vm_reset(&vm);
    REQUIRE(vm_ds_push(&vm, 1234) == 0);
    REQUIRE(vm_ds_push(&vm, example.a) == 0);
    REQUIRE(vm_ds_push(&vm, example.b) == 0);
    CHECK(execute(&vm, example.op) == 0);
    REQUIRE(vm.sp == vm.DS + 2);
    CHECK(vm.DS[0] == 1234);
    CHECK(vm.DS[1] == example.expected);
  }
}

TEST_CASE("binary underflow consumes a lone operand before exactly one panic")
{
  for (v4::Op op : binary_ops)
  {
    for (int initial_depth = 0; initial_depth <= 1; ++initial_depth)
    {
      INFO("opcode=", static_cast<int>(op), ", initial depth=", initial_depth);
      Vm vm{};
      vm_reset(&vm);
      PanicCapture capture;
      vm_set_panic_handler(&vm, capture_panic, &capture);
      if (initial_depth)
        REQUIRE(vm_ds_push(&vm, 0) == 0);
      CHECK(execute(&vm, op) == V4_ERR(StackUnderflow));
      CHECK(vm.sp == vm.DS);
      CHECK(capture.calls == 1);
      CHECK(capture.info.error_code == V4_ERR(StackUnderflow));
      CHECK(capture.info.ds_depth == 0);
      CHECK_FALSE(capture.info.has_stack_data);
      CHECK(capture.info.tos == 0);
      CHECK(capture.info.nos == 0);
    }
  }
}

TEST_CASE("division by zero panics after consuming both operands")
{
  const v4::Op division_ops[] = {v4::Op::DIV, v4::Op::MOD, v4::Op::DIVU, v4::Op::MODU};
  for (v4::Op op : division_ops)
  {
    Vm vm{};
    vm_reset(&vm);
    PanicCapture capture;
    vm_set_panic_handler(&vm, capture_panic, &capture);
    REQUIRE(vm_ds_push(&vm, 71) == 0);
    REQUIRE(vm_ds_push(&vm, 81) == 0);
    REQUIRE(vm_ds_push(&vm, -9) == 0);
    REQUIRE(vm_ds_push(&vm, 0) == 0);
    CHECK(execute(&vm, op) == V4_ERR(DivByZero));
    CHECK(vm.sp == vm.DS + 2);
    CHECK(capture.calls == 1);
    CHECK(capture.info.error_code == V4_ERR(DivByZero));
    CHECK(capture.info.ds_depth == 2);
    CHECK(capture.info.has_stack_data);
    CHECK(capture.info.tos == 81);
    CHECK(capture.info.nos == 71);
    CHECK(capture.info.stack[0] == 81);
    CHECK(capture.info.stack[1] == 71);
  }
}

TEST_CASE("binary operations can reduce a full data stack")
{
  for (v4::Op op : binary_ops)
  {
    Vm vm{};
    vm_reset(&vm);
    PanicCapture capture;
    vm_set_panic_handler(&vm, capture_panic, &capture);
    for (int i = 0; i < 256; ++i)
      REQUIRE(vm_ds_push(&vm, 1) == 0);
    CHECK(execute(&vm, op) == 0);
    CHECK(vm.sp == vm.DS + 255);
    CHECK(capture.calls == 0);
  }
}
