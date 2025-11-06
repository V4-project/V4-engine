#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "v4/arena.h"
#include "v4/errors.hpp"
#include "v4/hal.h"
#include "v4/internal/memory.hpp"
#include "v4/internal/vm.h"
#include "v4/opcodes.hpp"
#include "v4/sys_ids.h"
#include "v4/task.h"
#include "v4/vm_api.h"

/* ========================================================================= */
/* SYS Handler Registration                                                  */
/* ========================================================================= */

// Global SYS handler function pointer
static v4_sys_handler_fn g_sys_handler = nullptr;

extern "C" void v4_register_sys_handler(v4_sys_handler_fn handler)
{
  g_sys_handler = handler;
}

// Internal function to invoke registered SYS handler
static int32_t v4_invoke_sys_handler(Vm* vm, int32_t sys_id, int32_t arg0, int32_t arg1,
                                     int32_t arg2)
{
  if (g_sys_handler)
  {
    return g_sys_handler(vm, sys_id, arg0, arg1, arg2);
  }
  else
  {
    // No handler registered - return error
    return -1;  // HAL_ERR_NOTSUP or similar
  }
}

extern "C" int v4_vm_version(void)
{
  return 0;
}

extern "C" void vm_reset_dictionary(Vm* vm)
{
  if (!vm)
    return;

  // Free word names only if using malloc (not arena)
  if (!vm->arena)
  {
    for (int i = 0; i < vm->word_count; i++)
    {
      if (vm->words[i].name)
      {
        free(vm->words[i].name);
        vm->words[i].name = nullptr;
      }
    }
  }
  // If using arena, names are managed by arena owner

  vm->word_count = 0;
}

extern "C" void vm_reset_stacks(Vm* vm)
{
  if (!vm)
    return;

  // Reset data/return stacks to initial positions
  vm->sp = vm->DS;
  vm->rp = vm->RS;
  vm->fp = nullptr;  // No local frame initially
}

extern "C" void vm_reset(Vm* vm)
{
  if (!vm)
    return;

  // Reset dictionary and stacks
  vm_reset_dictionary(vm);
  vm_reset_stacks(vm);
}

/* ========================== Data stack helpers =========================== */

static inline v4_err ds_push(Vm* vm, v4_i32 v)
{
  if (vm->sp >= vm->DS + 256)
    return vm_panic(vm, V4_ERR(StackOverflow));
  *vm->sp++ = v;
  return V4_ERR(OK);
}

static inline v4_err ds_pop(Vm* vm, v4_i32* out)
{
  if (vm->sp <= vm->DS)
    return vm_panic(vm, V4_ERR(StackUnderflow));
  *out = *--vm->sp;
  return V4_ERR(OK);
}

static inline v4_err ds_peek(const Vm* vm, int i, v4_i32* out)
{
  if (vm->sp - 1 - i < vm->DS)
    return vm_panic(const_cast<Vm*>(vm), V4_ERR(StackUnderflow));
  *out = *(vm->sp - 1 - i);
  return V4_ERR(OK);
}

/* ========================== Return stack helpers ========================= */

static inline v4_err rs_push(Vm* vm, v4_i32 v)
{
  if (vm->rp >= vm->RS + 64)
    return vm_panic(vm, V4_ERR(StackOverflow));
  *vm->rp++ = v;
  return V4_ERR(OK);
}

static inline v4_err rs_pop(Vm* vm, v4_i32* out)
{
  if (vm->rp <= vm->RS)
    return vm_panic(vm, V4_ERR(StackUnderflow));
  *out = *--vm->rp;
  return V4_ERR(OK);
}

static inline v4_err rs_peek(const Vm* vm, int i, v4_i32* out)
{
  if (vm->rp - 1 - i < vm->RS)
    return vm_panic(const_cast<Vm*>(vm), V4_ERR(StackUnderflow));
  *out = *(vm->rp - 1 - i);
  return V4_ERR(OK);
}

/* ===================== Little-endian readers/writers ===================== */

static inline v4_i32 read_i32_le(const v4_u8* p)
{
  return (v4_i32)((v4_u32)p[0] | ((uint32_t)p[1] << 8) | ((v4_u32)p[2] << 16) |
                  ((v4_i32)p[3] << 24));
}

static inline int16_t read_i16_le(const v4_u8* p)
{
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* =================== Internal raw bytecode interpreter =================== */

extern "C" v4_err vm_exec_raw(Vm* vm, const v4_u8* bc, int len)
{
  assert(vm && bc && len > 0);
  const v4_u8* ip = bc;
  const v4_u8* ip_end = bc + len;

  while (ip < ip_end)
  {
    v4::Op op = static_cast<v4::Op>(*ip++);
    switch (op)
    {
      /* -------- Literal -------- */
      case v4::Op::LIT:
      {
        if (ip + 4 > ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        v4_i32 k = read_i32_le(ip);
        ip += 4;
        if (v4_err e = ds_push(vm, k))
          return e;
        break;
      }

      /* -------- Stack manipulation -------- */
      case v4::Op::DUP:
      {
        v4_i32 a = 0;
        if (v4_err e = ds_peek(vm, 0, &a))
          return e;
        if (v4_err e = ds_push(vm, a))
          return e;
        break;
      }

      case v4::Op::DROP:
      {
        v4_i32 dummy;
        if (v4_err e = ds_pop(vm, &dummy))
          return e;
        break;
      }

      case v4::Op::SWAP:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_push(vm, a))
          return e;
        if (v4_err e = ds_push(vm, b))
          return e;
        break;
      }

      case v4::Op::OVER:
      {
        v4_i32 v = 0;
        if (v4_err e = ds_peek(vm, 1, &v))
          return e;
        if (v4_err e = ds_push(vm, v))
          return e;
        break;
      }

      /* -------- Arithmetic -------- */
      case v4::Op::ADD:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_push(vm, b + a))
          return e;
        break;
      }

      case v4::Op::SUB:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_push(vm, b - a))
          return e;
        break;
      }

      case v4::Op::MUL:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a * b))
          return e;
        break;
      }

      case v4::Op::DIV:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (b == 0)
          return vm_panic(vm, V4_ERR(DivByZero));
        if (v4_err e = ds_push(vm, a / b))
          return e;
        break;
      }

      case v4::Op::MOD:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (b == 0)
          return vm_panic(vm, V4_ERR(DivByZero));
        if (v4_err e = ds_push(vm, a % b))
          return e;
        break;
      }

      case v4::Op::DIVU:
      {
        v4_u32 a, b;
        v4_i32 a_i32, b_i32;
        if (v4_err e = ds_pop(vm, &b_i32))
          return e;
        if (v4_err e = ds_pop(vm, &a_i32))
          return e;
        b = (v4_u32)b_i32;
        a = (v4_u32)a_i32;
        if (b == 0)
          return vm_panic(vm, V4_ERR(DivByZero));
        if (v4_err e = ds_push(vm, (v4_i32)(a / b)))
          return e;
        break;
      }

      case v4::Op::MODU:
      {
        v4_u32 a, b;
        v4_i32 a_i32, b_i32;
        if (v4_err e = ds_pop(vm, &b_i32))
          return e;
        if (v4_err e = ds_pop(vm, &a_i32))
          return e;
        b = (v4_u32)b_i32;
        a = (v4_u32)a_i32;
        if (b == 0)
          return vm_panic(vm, V4_ERR(DivByZero));
        if (v4_err e = ds_push(vm, (v4_i32)(a % b)))
          return e;
        break;
      }

      case v4::Op::INC:
      {
        v4_i32 a;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a + 1))
          return e;
        break;
      }

      case v4::Op::DEC:
      {
        v4_i32 a;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a - 1))
          return e;
        break;
      }

      /* -------- Comparison -------- */
      case v4::Op::EQ:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a == b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      case v4::Op::NE:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a != b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      case v4::Op::LT:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a < b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      case v4::Op::LE:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a <= b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      case v4::Op::GT:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a > b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      case v4::Op::GE:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a >= b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      case v4::Op::LTU:
      {
        v4_u32 a, b;
        v4_i32 a_i32, b_i32;
        if (v4_err e = ds_pop(vm, &b_i32))
          return e;
        if (v4_err e = ds_pop(vm, &a_i32))
          return e;
        b = (v4_u32)b_i32;
        a = (v4_u32)a_i32;
        if (v4_err e = ds_push(vm, a < b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      case v4::Op::LEU:
      {
        v4_u32 a, b;
        v4_i32 a_i32, b_i32;
        if (v4_err e = ds_pop(vm, &b_i32))
          return e;
        if (v4_err e = ds_pop(vm, &a_i32))
          return e;
        b = (v4_u32)b_i32;
        a = (v4_u32)a_i32;
        if (v4_err e = ds_push(vm, a <= b ? V4_TRUE : V4_FALSE))
          return e;
        break;
      }

      /* -------- Bitwise -------- */
      case v4::Op::AND:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a & b))
          return e;
        break;
      }

      case v4::Op::OR:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a | b))
          return e;
        break;
      }

      case v4::Op::XOR:
      {
        v4_i32 a, b;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, a ^ b))
          return e;
        break;
      }

      case v4::Op::INVERT:
      {
        v4_i32 a;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_push(vm, ~a))
          return e;
        break;
      }

      case v4::Op::SHL:
      {
        v4_i32 shift, val;
        if (v4_err e = ds_pop(vm, &shift))
          return e;
        if (v4_err e = ds_pop(vm, &val))
          return e;
        if (v4_err e = ds_push(vm, val << (shift & 0x1F)))
          return e;
        break;
      }

      case v4::Op::SHR:
      {
        v4_i32 shift, val_i32;
        v4_u32 val;
        if (v4_err e = ds_pop(vm, &shift))
          return e;
        if (v4_err e = ds_pop(vm, &val_i32))
          return e;
        val = (v4_u32)val_i32;
        if (v4_err e = ds_push(vm, (v4_i32)(val >> (shift & 0x1F))))
          return e;
        break;
      }

      case v4::Op::SAR:
      {
        v4_i32 shift, val;
        if (v4_err e = ds_pop(vm, &shift))
          return e;
        if (v4_err e = ds_pop(vm, &val))
          return e;
        if (v4_err e = ds_push(vm, val >> (shift & 0x1F)))
          return e;
        break;
      }

      /* -------- Control flow -------- */
      case v4::Op::JMP:
      {
        if (ip + 2 > ip_end)
          return vm_panic(vm, V4_ERR(TruncatedJump));
        int16_t off = read_i16_le(ip);
        ip += 2;
        const v4_u8* tgt = ip + off;
        if (tgt < bc || tgt > ip_end)
          return vm_panic(vm, V4_ERR(JumpOutOfRange));
        ip = tgt;
        break;
      }

      case v4::Op::JZ:
      {
        if (ip + 2 > ip_end)
          return vm_panic(vm, V4_ERR(TruncatedJump));
        int16_t off = read_i16_le(ip);
        ip += 2;
        v4_i32 cond;
        if (v4_err e = ds_pop(vm, &cond))
          return e;
        if (cond == 0)
        {
          const v4_u8* tgt = ip + off;
          if (tgt < bc || tgt > ip_end)
            return vm_panic(vm, V4_ERR(JumpOutOfRange));
          ip = tgt;
        }
        break;
      }

      case v4::Op::JNZ:
      {
        if (ip + 2 > ip_end)
          return vm_panic(vm, V4_ERR(TruncatedJump));
        int16_t off = read_i16_le(ip);
        ip += 2;
        v4_i32 cond;
        if (v4_err e = ds_pop(vm, &cond))
          return e;
        if (cond != 0)
        {
          const v4_u8* tgt = ip + off;
          if (tgt < bc || tgt > ip_end)
            return vm_panic(vm, V4_ERR(JumpOutOfRange));
          ip = tgt;
        }
        break;
      }

      case v4::Op::SELECT:
      {
        v4_i32 a, b, flag;
        if (v4_err e = ds_pop(vm, &a))
          return e;
        if (v4_err e = ds_pop(vm, &b))
          return e;
        if (v4_err e = ds_pop(vm, &flag))
          return e;
        if (v4_err e = ds_push(vm, flag ? a : b))
          return e;
        break;
      }

      /* -------- Memory -------- */
      case v4::Op::LOAD:
      {
        v4_i32 addr_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = 0;
        if (v4_err e = v4_mem_read32_core(vm, addr, &val))
          return e;
        if (v4_err e = ds_push(vm, (v4_i32)val))
          return e;
        break;
      }

      case v4::Op::STORE:
      {
        v4_i32 addr_i32, val_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        if (v4_err e = ds_pop(vm, &val_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = (v4_u32)val_i32;
        if (v4_err e = v4_mem_write32_core(vm, addr, val))
          return e;
        break;
      }

      case v4::Op::LOAD8U:
      {
        v4_i32 addr_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = 0;
        if (v4_err e = v4_mem_read8_core(vm, addr, &val))
          return e;
        if (v4_err e = ds_push(vm, (v4_i32)val))
          return e;
        break;
      }

      case v4::Op::LOAD16U:
      {
        v4_i32 addr_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = 0;
        if (v4_err e = v4_mem_read16_core(vm, addr, &val))
          return e;
        if (v4_err e = ds_push(vm, (v4_i32)val))
          return e;
        break;
      }

      case v4::Op::STORE8:
      {
        v4_i32 addr_i32, val_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        if (v4_err e = ds_pop(vm, &val_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = (v4_u32)val_i32;
        if (v4_err e = v4_mem_write8_core(vm, addr, val))
          return e;
        break;
      }

      case v4::Op::STORE16:
      {
        v4_i32 addr_i32, val_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        if (v4_err e = ds_pop(vm, &val_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = (v4_u32)val_i32;
        if (v4_err e = v4_mem_write16_core(vm, addr, val))
          return e;
        break;
      }

      case v4::Op::LOAD8S:
      {
        v4_i32 addr_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = 0;
        if (v4_err e = v4_mem_read8_core(vm, addr, &val))
          return e;
        // Sign-extend 8-bit to 32-bit
        int8_t val_i8 = (int8_t)(val & 0xFF);
        if (v4_err e = ds_push(vm, (v4_i32)val_i8))
          return e;
        break;
      }

      case v4::Op::LOAD16S:
      {
        v4_i32 addr_i32;
        if (v4_err e = ds_pop(vm, &addr_i32))
          return e;
        v4_u32 addr = (v4_u32)addr_i32;
        v4_u32 val = 0;
        if (v4_err e = v4_mem_read16_core(vm, addr, &val))
          return e;
        // Sign-extend 16-bit to 32-bit
        int16_t val_i16 = (int16_t)(val & 0xFFFF);
        if (v4_err e = ds_push(vm, (v4_i32)val_i16))
          return e;
        break;
      }

      /* -------- Return stack operations -------- */
      case v4::Op::TOR:
      {
        v4_i32 val;
        if (v4_err e = ds_pop(vm, &val))
          return e;
        if (v4_err e = rs_push(vm, val))
          return e;
        break;
      }

      case v4::Op::FROMR:
      {
        v4_i32 val = 0;
        if (v4_err e = rs_pop(vm, &val))
          return e;
        if (v4_err e = ds_push(vm, val))
          return e;
        break;
      }

      case v4::Op::RFETCH:
      {
        v4_i32 val = 0;
        if (v4_err e = rs_peek(vm, 0, &val))
          return e;
        if (v4_err e = ds_push(vm, val))
          return e;
        break;
      }

      /* -------- Compact literals -------- */
      case v4::Op::LIT0:
      {
        if (v4_err e = ds_push(vm, 0))
          return e;
        break;
      }

      case v4::Op::LIT1:
      {
        if (v4_err e = ds_push(vm, 1))
          return e;
        break;
      }

      case v4::Op::LITN1:
      {
        if (v4_err e = ds_push(vm, -1))
          return e;
        break;
      }

      case v4::Op::LIT_U8:
      {
        if (ip >= ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        v4_u32 val = (v4_u32)*ip++;
        if (v4_err e = ds_push(vm, (v4_i32)val))
          return e;
        break;
      }

      case v4::Op::LIT_I8:
      {
        if (ip >= ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        int8_t val = (int8_t)*ip++;
        if (v4_err e = ds_push(vm, (v4_i32)val))
          return e;
        break;
      }

      case v4::Op::LIT_I16:
      {
        if (ip + 2 > ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        int16_t val = read_i16_le(ip);
        ip += 2;
        if (v4_err e = ds_push(vm, (v4_i32)val))
          return e;
        break;
      }

      /* -------- Local variables -------- */
      case v4::Op::LGET:
      {
        if (ip >= ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        uint8_t idx = *ip++;
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));  // No local frame
        if (vm->fp + idx >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));  // Out of bounds
        if (v4_err e = ds_push(vm, vm->fp[idx]))
          return e;
        break;
      }

      case v4::Op::LSET:
      {
        if (ip >= ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        uint8_t idx = *ip++;
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));  // No local frame
        if (vm->fp + idx >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));  // Out of bounds
        v4_i32 val;
        if (v4_err e = ds_pop(vm, &val))
          return e;
        vm->fp[idx] = val;
        break;
      }

      case v4::Op::LTEE:
      {
        if (ip >= ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        uint8_t idx = *ip++;
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));  // No local frame
        if (vm->fp + idx >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));  // Out of bounds
        v4_i32 val = 0;
        if (v4_err e = ds_peek(vm, 0, &val))
          return e;
        vm->fp[idx] = val;
        break;
      }

      case v4::Op::LGET0:
      {
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));
        if (vm->fp >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));
        if (v4_err e = ds_push(vm, vm->fp[0]))
          return e;
        break;
      }

      case v4::Op::LGET1:
      {
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));
        if (vm->fp + 1 >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));
        if (v4_err e = ds_push(vm, vm->fp[1]))
          return e;
        break;
      }

      case v4::Op::LSET0:
      {
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));
        if (vm->fp >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));
        v4_i32 val;
        if (v4_err e = ds_pop(vm, &val))
          return e;
        vm->fp[0] = val;
        break;
      }

      case v4::Op::LSET1:
      {
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));
        if (vm->fp + 1 >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));
        v4_i32 val;
        if (v4_err e = ds_pop(vm, &val))
          return e;
        vm->fp[1] = val;
        break;
      }

      case v4::Op::LINC:
      {
        if (ip >= ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        uint8_t idx = *ip++;
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));
        if (vm->fp + idx >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));
        vm->fp[idx]++;
        break;
      }

      case v4::Op::LDEC:
      {
        if (ip >= ip_end)
          return vm_panic(vm, V4_ERR(TruncatedLiteral));
        uint8_t idx = *ip++;
        if (!vm->fp)
          return vm_panic(vm, V4_ERR(InvalidArg));
        if (vm->fp + idx >= vm->rp)
          return vm_panic(vm, V4_ERR(StackUnderflow));
        vm->fp[idx]--;
        break;
      }

      /* -------- Call / Return -------- */
      case v4::Op::CALL:
      {
        if (ip + 2 > ip_end)
          return vm_panic(vm, V4_ERR(TruncatedJump));
        uint16_t word_idx = (uint16_t)ip[0] | ((uint16_t)ip[1] << 8);
        ip += 2;

        if (word_idx >= (uint16_t)vm->word_count)
          return vm_panic(vm, V4_ERR(InvalidWordIdx));

        Word* word = &vm->words[word_idx];
        if (!word->code || word->code_len <= 0)
          return vm_panic(vm, V4_ERR(InvalidArg));

        // Save current frame pointer (for nested calls)
        v4_i32* old_fp = vm->fp;

        // Set new frame pointer to current return stack position
        // This allows local variables to be accessed relative to the frame base
        vm->fp = vm->rp;

        // Execute the called word
        v4_err e = vm_exec_raw(vm, word->code, word->code_len);

        // Restore frame pointer after call returns
        vm->fp = old_fp;

        if (e)
          return e;
        break;
      }

      case v4::Op::SYS:
      {
        // Stack layout: ( arg0 arg1 arg2 sys_id -- result )
        // sys_id is now 32-bit (no longer limited to 8-bit or 16-bit)
        v4_i32 sys_id, arg2, arg1, arg0;
        v4_err err;

        if ((err = ds_pop(vm, &sys_id)))
          return err;
        if ((err = ds_pop(vm, &arg2)))
          return err;
        if ((err = ds_pop(vm, &arg1)))
          return err;
        if ((err = ds_pop(vm, &arg0)))
          return err;

        // Invoke registered SYS handler
        int32_t result = v4_invoke_sys_handler(vm, sys_id, arg0, arg1, arg2);

        if ((err = ds_push(vm, result)))
          return err;
        break;
      }
      case v4::Op::RET:
        return V4_ERR(OK);

      /* === Task Management (0x90-0x9A) === */
      case v4::Op::TASK_SPAWN:
      {
        v4_i32 rs_size, ds_size, priority, word_idx;
        v4_err err;

        if ((err = ds_pop(vm, &rs_size)))
          return err;
        if ((err = ds_pop(vm, &ds_size)))
          return err;
        if ((err = ds_pop(vm, &priority)))
          return err;
        if ((err = ds_pop(vm, &word_idx)))
          return err;

        int task_id = vm_task_spawn(
            vm, static_cast<uint16_t>(word_idx), static_cast<uint8_t>(priority),
            static_cast<uint16_t>(ds_size), static_cast<uint16_t>(rs_size));

        if ((err = ds_push(vm, task_id)))
          return err;
        break;
      }

      case v4::Op::TASK_EXIT:
      {
        vm_task_exit(vm);
        // Never returns - switches to another task
        return V4_ERR(OK);
      }

      case v4::Op::TASK_SLEEP:
      {
        v4_i32 ms_delay;
        v4_err err;

        if ((err = ds_pop(vm, &ms_delay)))
          return err;

        vm_task_sleep(vm, static_cast<uint32_t>(ms_delay));
        break;
      }

      case v4::Op::TASK_YIELD:
      {
        vm_task_yield(vm);
        break;
      }

      case v4::Op::CRITICAL_ENTER:
      {
        vm_task_critical_enter(vm);
        break;
      }

      case v4::Op::CRITICAL_EXIT:
      {
        vm_task_critical_exit(vm);
        break;
      }

      case v4::Op::TASK_SEND:
      {
        v4_i32 data, msg_type, target_task;
        v4_err err;

        if ((err = ds_pop(vm, &data)))
          return err;
        if ((err = ds_pop(vm, &msg_type)))
          return err;
        if ((err = ds_pop(vm, &target_task)))
          return err;

        int result = vm_task_send(vm, static_cast<uint8_t>(target_task),
                                  static_cast<uint8_t>(msg_type), data);

        if ((err = ds_push(vm, result)))
          return err;
        break;
      }

      case v4::Op::TASK_RECEIVE:
      {
        v4_i32 msg_type;
        int32_t data;
        uint8_t src_task;
        v4_err err;

        if ((err = ds_pop(vm, &msg_type)))
          return err;

        int result =
            vm_task_receive(vm, static_cast<uint8_t>(msg_type), &data, &src_task);

        if ((err = ds_push(vm, data)))
          return err;
        if ((err = ds_push(vm, src_task)))
          return err;
        if ((err = ds_push(vm, result)))
          return err;
        break;
      }

      case v4::Op::TASK_RECEIVE_BLOCKING:
      {
        v4_i32 timeout_ms, msg_type;
        int32_t data;
        uint8_t src_task;
        v4_err err;

        if ((err = ds_pop(vm, &timeout_ms)))
          return err;
        if ((err = ds_pop(vm, &msg_type)))
          return err;

        int result =
            vm_task_receive_blocking(vm, static_cast<uint8_t>(msg_type), &data, &src_task,
                                     static_cast<uint32_t>(timeout_ms));

        if ((err = ds_push(vm, data)))
          return err;
        if ((err = ds_push(vm, src_task)))
          return err;
        if ((err = ds_push(vm, result)))
          return err;
        break;
      }

      case v4::Op::TASK_SELF:
      {
        v4_err err;
        int task_id = vm_task_self(vm);
        if ((err = ds_push(vm, task_id)))
          return err;
        break;
      }

      case v4::Op::TASK_COUNT:
      {
        v4_err err;
        int count = vm->scheduler.task_count;
        if ((err = ds_push(vm, count)))
          return err;
        break;
      }

      default:
        return vm_panic(vm, V4_ERR(UnknownOp));
    }
  }

  return vm_panic(vm, V4_ERR(FellOffEnd));
}

/* ======================= Word management API ============================= */

extern "C" int vm_register_word(Vm* vm, const char* name, const uint8_t* code,
                                int code_len)
{
  if (!vm || !code || code_len <= 0)
    return V4_ERR(InvalidArg);

  if (vm->word_count >= Vm::V4_MAX_WORDS)
    return vm_panic(vm, V4_ERR(DictionaryFull));

  int idx = vm->word_count;

  // Copy name if provided (NULL is allowed for anonymous words)
  if (name)
  {
    size_t len = strlen(name) + 1;  // +1 for null terminator

    // Use arena if available, otherwise malloc
    if (vm->arena)
    {
      char* name_copy = static_cast<char*>(v4_arena_alloc(vm->arena, len, 1));
      if (!name_copy)
        return vm_panic(vm, V4_ERR(InvalidArg));  // Arena allocation failed
      memcpy(name_copy, name, len);
      vm->words[idx].name = name_copy;
    }
    else
    {
      vm->words[idx].name = strdup(name);
      if (!vm->words[idx].name)
        return vm_panic(vm, V4_ERR(InvalidArg));  // strdup failed (out of memory)
    }
  }
  else
  {
    vm->words[idx].name = nullptr;
  }

  vm->words[idx].code = code;
  vm->words[idx].code_len = code_len;
  vm->word_count++;

  return idx;
}

extern "C" Word* vm_get_word(Vm* vm, int idx)
{
  if (!vm || idx < 0 || idx >= vm->word_count)
    return nullptr;
  return &vm->words[idx];
}

extern "C" const char* vm_word_get_name(const Word* word)
{
  if (!word)
    return nullptr;
  return word->name;
}

extern "C" const v4_u8* vm_word_get_code(const Word* word)
{
  if (!word)
    return nullptr;
  return word->code;
}

extern "C" v4_u16 vm_word_get_code_len(const Word* word)
{
  if (!word)
    return 0;
  return word->code_len;
}

extern "C" int vm_find_word(Vm* vm, const char* name)
{
  if (!vm || !name)
    return -1;

  // Search backward to find newest definition (Forth shadowing)
  for (int i = vm->word_count - 1; i >= 0; i--)
  {
    if (vm->words[i].name && strcmp(vm->words[i].name, name) == 0)
    {
      return i;
    }
  }

  return -1;  // Not found
}

/* =========================== Public API entry ============================ */

extern "C" v4_err vm_exec(Vm* vm, Word* entry)
{
  if (!vm || !entry || !entry->code)
    return V4_ERR(InvalidArg);

  return vm_exec_raw(vm, entry->code, entry->code_len);
}

/* ======================= Stack inspection API ============================ */

extern "C" int vm_ds_depth_public(struct Vm* vm)
{
  if (!vm)
    return 0;
  return (int)(vm->sp - vm->DS);
}

extern "C" v4_i32 vm_ds_peek_public(struct Vm* vm, int index_from_top)
{
  if (!vm)
    return 0;

  int depth = (int)(vm->sp - vm->DS);
  if (index_from_top < 0 || index_from_top >= depth)
    return 0;  // Out of range

  return vm->sp[-1 - index_from_top];
}

extern "C" int vm_ds_copy_to_array(struct Vm* vm, v4_i32* out_array, int max_count)
{
  if (!vm || !out_array || max_count <= 0)
    return 0;

  int depth = static_cast<int>(vm->sp - vm->DS);
  int copy_count = (depth < max_count) ? depth : max_count;

  // Copy stack from bottom to top
  std::memcpy(out_array, vm->DS, copy_count * sizeof(v4_i32));

  return copy_count;
}
/* ===================== Stack manipulation API ============================ */

extern "C" v4_err vm_ds_push(struct Vm* vm, v4_i32 value)
{
  if (!vm)
    return V4_ERR(InvalidArg);
  return ds_push(vm, value);
}

extern "C" v4_err vm_ds_pop(struct Vm* vm, v4_i32* out_value)
{
  if (!vm)
    return V4_ERR(InvalidArg);

  v4_i32 val;
  v4_err err = ds_pop(vm, &val);
  if (err == 0 && out_value)
    *out_value = val;
  return err;
}

extern "C" void vm_ds_clear(struct Vm* vm)
{
  if (!vm)
    return;
  vm->sp = vm->DS;
}

/* ==================== Return stack inspection API ======================== */

extern "C" int vm_rs_depth_public(struct Vm* vm)
{
  if (!vm)
    return 0;
  return static_cast<int>(vm->rp - vm->RS);
}

extern "C" int vm_rs_copy_to_array(struct Vm* vm, v4_i32* out_array, int max_count)
{
  if (!vm || !out_array || max_count <= 0)
    return 0;

  int depth = static_cast<int>(vm->rp - vm->RS);
  int copy_count = (depth < max_count) ? depth : max_count;

  // Copy stack from bottom to top
  std::memcpy(out_array, vm->RS, copy_count * sizeof(v4_i32));

  return copy_count;
}

/* ==================== Stack snapshot API ================================= */

extern "C" struct VmStackSnapshot* vm_ds_snapshot(struct Vm* vm)
{
  if (!vm)
    return nullptr;

  int depth = (int)(vm->sp - vm->DS);
  if (depth == 0)
  {
    // Empty stack - return snapshot with NULL data
    VmStackSnapshot* snap = (VmStackSnapshot*)malloc(sizeof(VmStackSnapshot));
    if (!snap)
      return nullptr;
    snap->data = nullptr;
    snap->depth = 0;
    return snap;
  }

  VmStackSnapshot* snap = (VmStackSnapshot*)malloc(sizeof(VmStackSnapshot));
  if (!snap)
    return nullptr;

  snap->data = (v4_i32*)malloc(depth * sizeof(v4_i32));
  if (!snap->data)
  {
    free(snap);
    return nullptr;
  }

  // Copy stack contents (bottom to top)
  memcpy(snap->data, vm->DS, depth * sizeof(v4_i32));
  snap->depth = depth;

  return snap;
}

extern "C" v4_err vm_ds_restore(struct Vm* vm, const struct VmStackSnapshot* snapshot)
{
  if (!vm || !snapshot)
    return V4_ERR(InvalidArg);

  // Check if restored stack would fit
  if (snapshot->depth > 256)
    return vm_panic(vm, V4_ERR(StackOverflow));

  // Clear current stack
  vm->sp = vm->DS;

  // Restore from snapshot (bottom to top)
  if (snapshot->depth > 0 && snapshot->data)
  {
    memcpy(vm->DS, snapshot->data, snapshot->depth * sizeof(v4_i32));
    vm->sp = vm->DS + snapshot->depth;
  }

  return V4_ERR(OK);
}

extern "C" void vm_ds_snapshot_free(struct VmStackSnapshot* snapshot)
{
  if (!snapshot)
    return;

  if (snapshot->data)
    free(snapshot->data);
  free(snapshot);
}
