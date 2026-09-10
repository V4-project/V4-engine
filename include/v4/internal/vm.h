#pragma once
#include <stdint.h>

#include "v4/internal/task.hpp"
#include "v4/vm_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Word structure representing a compiled Forth word.
   */
  typedef struct Word
  {
    char *name;          /**< Word name (dynamically allocated, can be NULL) */
    const uint8_t *code; /**< Bytecode pointer */
    int code_len;        /**< Length of bytecode in bytes */
  } Word;

  /**
   * @brief Internal VM structure (not part of the public API).
   *        Visible only for unit tests or tightly coupled components.
   */
  typedef struct Vm
  {
    v4_i32 DS[256]; /**< Data stack (top at sp-1) */
    v4_i32 RS[64];  /**< Return stack (top at rp-1) */
    v4_i32 *sp;     /**< Next push position on data stack */
    v4_i32 *rp;     /**< Next push position on return stack */
    v4_i32 *fp; /**< Local frame pointer (points to base of current local frame in RS) */

    /* Memory configuration */
    uint8_t *mem;
    v4_u32 mem_size;

    /* MMIO window table (fixed capacity, linear search) */
    enum
    {
      V4_MAX_MMIO = 16
    };
    V4_Mmio mmio[V4_MAX_MMIO];
    int mmio_count;

    /* Execution state */
    int last_err; /**< Last error code (0 = OK) */

    /* Word dictionary (for CALL opcode) */
    enum
    {
      V4_MAX_WORDS = 256
    };
    Word words[V4_MAX_WORDS]; /**< Word dictionary */
    int word_count;           /**< Number of registered words */

    /* Memory management */
    V4Arena *arena; /**< Optional arena allocator (NULL = use malloc) */

    /* Reserved for future JIT / IC linkage */
    const VmConfig *boot_cfg_snapshot;

    /* Task storage retained even when disabled, preserving the VM layout. */
    v4_scheduler_t scheduler; /**< Task scheduler */
    v4_msg_queue_t msg_queue; /**< Message queue */

    /* Panic handler */
    void (*panic_handler)(void *,
                          const struct V4PanicInfo *); /**< Custom panic handler */
    void *panic_user_data;                             /**< User data for panic handler */
  } Vm;

  /* Internal-only helper to execute raw bytecode for early bring-up/tests. */
  /* Not part of the public C API. */
  v4_err vm_exec_raw(Vm *vm, const uint8_t *bc, int len);

#ifdef __cplusplus
} /* extern "C" */
#endif
