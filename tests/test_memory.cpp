#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string.h>

#include "doctest.h"
#include "v4/internal/vm.h"
#include "v4/vm_api.h"

/* ------------------------------------------------------------------------- */
/* Dummy MMIO handlers                                                       */
/* ------------------------------------------------------------------------- */
/**
 * A minimal MMIO device for testing.
 * - read32 returns a fixed pattern (0xAABBCCDD + addr low nibble)
 * - write32 stores the last written address and value
 */
struct Dummy
{
  v4_u32 last_write_addr = 0;
  v4_u32 last_write_val = 0;
};

static v4_err d_read32(void *user, v4_u32 addr, v4_u32 *out)
{
  (void)user;
  *out = 0xAABBCCDDu + (addr & 0xFu);
  return 0;
}

static v4_err d_write32(void *user, v4_u32 addr, v4_u32 val)
{
  Dummy *d = (Dummy *)user;
  d->last_write_addr = addr;
  d->last_write_val = val;
  return 0;
}

/* ------------------------------------------------------------------------- */
/* Test cases                                                                */
/* ------------------------------------------------------------------------- */

TEST_CASE("Dictionary lifecycle preserves borrowed storage")
{
  uint8_t ram[16] = {0x42};
  uint8_t code[] = {0x51};  // RET; caller-owned bytecode must not be freed.
  VmConfig cfg{ram, (v4_u32)sizeof(ram), nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  CHECK(vm_register_word(vm, "first", code, sizeof(code)) == 0);
  CHECK(vm_register_word(vm, nullptr, code, sizeof(code)) == 1);
  CHECK(vm_register_word(vm, "last", code, sizeof(code)) == 2);

  SUBCASE("Destroy populated dictionary") {}
  SUBCASE("Repeated reset followed by destroy")
  {
    vm_reset_dictionary(vm);
    CHECK(vm->word_count == 0);
    CHECK(vm->words[0].name == nullptr);
    CHECK(vm->words[2].name == nullptr);
    vm_reset_dictionary(vm);
  }
  SUBCASE("Reset then reuse dictionary")
  {
    vm_reset_dictionary(vm);
    CHECK(vm_register_word(vm, "replacement", code, sizeof(code)) == 0);
    CHECK(vm->word_count == 1);
  }

  vm_destroy(vm);
  CHECK(ram[0] == 0x42);
  CHECK(code[0] == 0x51);
}

TEST_CASE("Dictionary destruction leaves arena storage and allocations intact")
{
  uint8_t ram[16] = {0x42};
  uint8_t code[] = {0x51};
  uint8_t storage[128] = {};
  V4Arena arena;
  v4_arena_init(&arena, storage, sizeof(storage));
  VmConfig cfg{ram, (v4_u32)sizeof(ram), nullptr, 0, &arena};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  CHECK(vm_register_word(vm, "first", code, sizeof(code)) == 0);
  CHECK(vm_register_word(vm, nullptr, code, sizeof(code)) == 1);
  const char *first_name = vm->words[0].name;

  SUBCASE("Destroy populated dictionary") {}
  SUBCASE("Reset then reuse arena dictionary")
  {
    const size_t used = arena.used;
    vm_reset_dictionary(vm);
    CHECK(vm->word_count == 0);
    CHECK(arena.used == used);
    CHECK(vm_register_word(vm, "replacement", code, sizeof(code)) == 0);
  }

  const size_t used = arena.used;
  vm_destroy(vm);
  CHECK(arena.buffer == storage);
  CHECK(arena.size == sizeof(storage));
  CHECK(arena.used == used);
  CHECK(strcmp(first_name, "first") == 0);
  CHECK(ram[0] == 0x42);
  CHECK(code[0] == 0x51);
  CHECK(v4_arena_alloc(&arena, 1, 1) == storage + used);
}

TEST_CASE("Dictionary lifecycle accepts null and empty VMs")
{
  vm_destroy(nullptr);
  vm_reset_dictionary(nullptr);
  VmConfig cfg{};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);
  vm_destroy(vm);
}

/**
 * @test RAM normal path: STORE → LOAD should return the same value.
 */
TEST_CASE("RAM basic read/write round-trip")
{
  uint8_t ram[64] = {};
  VmConfig cfg{ram, (v4_u32)sizeof(ram), nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  v4_err e;
  e = vm_mem_write32(vm, 0, 0x12345678u);
  CHECK(e == 0);

  v4_u32 out = 0;
  e = vm_mem_read32(vm, 0, &out);
  CHECK(e == 0);
  CHECK(out == 0x12345678u);

  // Verify little-endian layout in memory
  CHECK(ram[0] == 0x78);
  CHECK(ram[1] == 0x56);
  CHECK(ram[2] == 0x34);
  CHECK(ram[3] == 0x12);

  vm_destroy(vm);
}

/**
 * @test Out-of-bounds check:
 *       addr = mem_size - 4 → OK
 *       addr = mem_size - 3 → should fail with -13 (OobMemory)
 */
TEST_CASE("Out-of-bounds access handling")
{
  uint8_t ram[16] = {};
  VmConfig cfg{ram, 16u, nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  // Last aligned word should be accessible
  v4_err e = vm_mem_write32(vm, 12u, 0xDEADBEEFu);
  CHECK(e == 0);

  // Crossing the end of memory should fail
  v4_u32 out = 0;
  e = vm_mem_read32(vm, 13u, &out);
  CHECK(e == -13);  // OobMemory

  vm_destroy(vm);
}

/**
 * @test Unaligned address should raise -12 (Unaligned).
 */
TEST_CASE("Unaligned access detection")
{
  uint8_t ram[16] = {};
  VmConfig cfg{ram, 16u, nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  v4_err e = vm_mem_write32(vm, 2u, 0);
  CHECK(e == -12);  // Unaligned

  vm_destroy(vm);
}

/**
 * @test Verify that MMIO read/write callbacks are invoked correctly.
 */
TEST_CASE("MMIO callback dispatch")
{
  uint8_t ram[64] = {};
  Dummy dummy{};
  V4_Mmio m{/* base */ 0x20u,
            /* size */ 0x10u,
            /* rd/wr */ d_read32, d_write32,
            /* user */ &dummy};
  VmConfig cfg{ram, (v4_u32)sizeof(ram), &m, 1};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  // Write should go to MMIO
  v4_err e = vm_mem_write32(vm, 0x24u, 0xCAFEBABEu);
  CHECK(e == 0);
  CHECK(dummy.last_write_addr == 0x24u);
  CHECK(dummy.last_write_val == 0xCAFEBABEu);

  // Read should also go to MMIO
  v4_u32 out = 0;
  e = vm_mem_read32(vm, 0x28u, &out);
  CHECK(e == 0);
  CHECK(out == 0xAABBCCDDu + (0x28u & 0xFu));

  vm_destroy(vm);
}

/**
 * @test Combined scenario: alternating between RAM and MMIO regions.
 */
TEST_CASE("Mixed RAM/MMIO access")
{
  uint8_t ram[64] = {};
  Dummy dummy{};
  V4_Mmio m{0x10u, 0x10u, d_read32, d_write32, &dummy};
  VmConfig cfg{ram, (v4_u32)sizeof(ram), &m, 1};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  // 1. RAM region
  CHECK(vm_mem_write32(vm, 0x00u, 0x01020304u) == 0);

  // 2. MMIO region
  CHECK(vm_mem_write32(vm, 0x14u, 0x11111111u) == 0);
  v4_u32 v = 0;
  CHECK(vm_mem_read32(vm, 0x18u, &v) == 0);

  // 3. Back to RAM region
  CHECK(vm_mem_write32(vm, 0x2Cu, 0xFFEEDDCCu) == 0);
  CHECK(vm_mem_read32(vm, 0x2Cu, &v) == 0);
  CHECK(v == 0xFFEEDDCCu);

  vm_destroy(vm);
}

/* ------------------------------------------------------------------------- */
/* Extended memory access operations (Commit 2)                              */
/* ------------------------------------------------------------------------- */
TEST_CASE("8-bit memory read/write (LOAD8U/STORE8)")
{
  uint8_t ram[64] = {};
  VmConfig cfg{ram, (v4_u32)sizeof(ram), nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  // Write pattern to memory
  vm_ds_push(vm, 0x42);               // value
  vm_ds_push(vm, 0);                  // address
  v4_u8 store_code[] = {0x34, 0x51};  // STORE8, RET
  CHECK(vm_exec_raw(vm, store_code, 2) == 0);

  // Read back
  vm_ds_push(vm, 0);                 // address
  v4_u8 load_code[] = {0x32, 0x51};  // LOAD8U, RET
  CHECK(vm_exec_raw(vm, load_code, 2) == 0);

  v4_i32 val;
  vm_ds_pop(vm, &val);
  CHECK(val == 0x42);

  vm_destroy(vm);
}

TEST_CASE("8-bit signed load (LOAD8S)")
{
  uint8_t ram[64] = {};
  VmConfig cfg{ram, (v4_u32)sizeof(ram), nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  // Write 0xFF (should be sign-extended to -1)
  vm_ds_push(vm, 0xFF);
  vm_ds_push(vm, 0);
  v4_u8 store_code[] = {0x34, 0x51};  // STORE8, RET
  CHECK(vm_exec_raw(vm, store_code, 2) == 0);

  // Read as signed
  vm_ds_push(vm, 0);
  v4_u8 load_code[] = {0x36, 0x51};  // LOAD8S, RET
  CHECK(vm_exec_raw(vm, load_code, 2) == 0);

  v4_i32 val;
  vm_ds_pop(vm, &val);
  CHECK(val == -1);

  vm_destroy(vm);
}

TEST_CASE("16-bit memory read/write (LOAD16U/STORE16)")
{
  uint8_t ram[64] = {};
  VmConfig cfg{ram, (v4_u32)sizeof(ram), nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  // Write pattern to memory (little-endian)
  vm_ds_push(vm, 0x1234);
  vm_ds_push(vm, 0);
  v4_u8 store_code[] = {0x35, 0x51};  // STORE16, RET
  CHECK(vm_exec_raw(vm, store_code, 2) == 0);

  // Verify little-endian byte order
  CHECK(ram[0] == 0x34);
  CHECK(ram[1] == 0x12);

  // Read back
  vm_ds_push(vm, 0);
  v4_u8 load_code[] = {0x33, 0x51};  // LOAD16U, RET
  CHECK(vm_exec_raw(vm, load_code, 2) == 0);

  v4_i32 val;
  vm_ds_pop(vm, &val);
  CHECK(val == 0x1234);

  vm_destroy(vm);
}

TEST_CASE("16-bit signed load (LOAD16S)")
{
  uint8_t ram[64] = {};
  VmConfig cfg{ram, (v4_u32)sizeof(ram), nullptr, 0};
  Vm *vm = vm_create(&cfg);
  REQUIRE(vm);

  // Write 0xFFFF (should be sign-extended to -1)
  vm_ds_push(vm, 0xFFFF);
  vm_ds_push(vm, 0);
  v4_u8 store_code[] = {0x35, 0x51};  // STORE16, RET
  CHECK(vm_exec_raw(vm, store_code, 2) == 0);

  // Read as signed
  vm_ds_push(vm, 0);
  v4_u8 load_code[] = {0x37, 0x51};  // LOAD16S, RET
  CHECK(vm_exec_raw(vm, load_code, 2) == 0);

  v4_i32 val;
  vm_ds_pop(vm, &val);
  CHECK(val == -1);

  vm_destroy(vm);
}
