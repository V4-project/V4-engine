#pragma once
#include <stddef.h>
#include <stdint.h>

#include "v4/arena.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* ------------------------------------------------------------------------- */
  /* Basic typedefs                                                            */
  /* ------------------------------------------------------------------------- */

  /** 32-bit signed integer used internally by the VM. */
  typedef int32_t v4_i32;
  /** 32-bit unsigned integer used internally by the VM. */
  typedef uint32_t v4_u32;
  /** 16-bit unsigned integer. */
  typedef uint16_t v4_u16;
  /** 8-bit unsigned integer used for bytecode and memory. */
  typedef uint8_t v4_u8;
  /** Error code type. 0 = OK, negative = error. */
  typedef int v4_err;

  /** Success return value. */
#define V4_OK 0

  /* ------------------------------------------------------------------------- */
  /* MMIO (Memory-Mapped I/O)                                                  */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Function pointer type for 32-bit MMIO read callback.
   * @param user  User-defined context pointer.
   * @param addr  Absolute address within the MMIO window.
   * @param out   Output value pointer.
   * @return 0 on success, negative error code otherwise.
   */
  typedef v4_err (*v4_mmio_read32_fn)(void *user, v4_u32 addr, v4_u32 *out);

  /**
   * @brief Function pointer type for 32-bit MMIO write callback.
   * @param user  User-defined context pointer.
   * @param addr  Absolute address within the MMIO window.
   * @param val   Value to write.
   * @return 0 on success, negative error code otherwise.
   */
  typedef v4_err (*v4_mmio_write32_fn)(void *user, v4_u32 addr, v4_u32 val);

  /**
   * @brief Descriptor for a single MMIO window.
   *
   * Address range is [base, base + size).
   * Each window may define separate callbacks for read and write.
   * A NULL callback means that operation is prohibited and will return
   * an "out of bounds" error (-13) when accessed.
   */
  typedef struct V4_Mmio
  {
    v4_u32 base;                /**< Base address (absolute) */
    v4_u32 size;                /**< Window size in bytes */
    v4_mmio_read32_fn read32;   /**< Optional read callback (NULL = forbidden) */
    v4_mmio_write32_fn write32; /**< Optional write callback (NULL = forbidden) */
    void *user;                 /**< User data passed to callbacks */
  } V4_Mmio;

  /* ------------------------------------------------------------------------- */
  /* VM configuration                                                          */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Configuration structure used when creating a VM instance.
   *
   * The VM operates on a flat 32-bit little-endian address space.
   * Memory and MMIO regions are defined here and remain constant
   * throughout the lifetime of the VM.
   */
  typedef struct VmConfig
  {
    uint8_t *mem;        /**< Base pointer of VM RAM (can be NULL) */
    v4_u32 mem_size;     /**< RAM size in bytes */
    const V4_Mmio *mmio; /**< Optional static MMIO table (can be NULL) */
    int mmio_count;      /**< Number of MMIO entries in the table */
    V4Arena *arena; /**< Optional arena allocator for word names (can be NULL, uses malloc
                       if NULL) */
  } VmConfig;

  /* Forward declarations for opaque VM and Word structures. */
  struct Vm;
  struct Word;
  struct VmStackSnapshot;

/* ------------------------------------------------------------------------- */
/* Boolean constants (Forth-style truth values)                              */
/* ------------------------------------------------------------------------- */

/**
 * @brief Boolean constants used by the VM and Forth-level code.
 *
 *  Forth traditionally defines "true" as all bits set (-1) and
 *  "false" as zero. These match the semantics of signed 32-bit words.
 */
#define V4_TRUE ((v4_i32)(-1))
#define V4_FALSE ((v4_i32)(0))

  /* ------------------------------------------------------------------------- */
  /* Lifecycle and execution                                                   */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Create a new VM instance.
   * @param cfg  Pointer to a valid VmConfig.
   * @return Pointer to the new VM, or NULL on allocation failure.
   */
  struct Vm *vm_create(const VmConfig *cfg);

  /**
   * @brief Reset VM stacks and state to initial positions.
   * @param vm     VM instance.
   */
  void vm_reset(struct Vm *vm);

  /**
   * @brief Reset only the word dictionary, preserving stacks and memory.
   * @param vm     VM instance.
   */
  void vm_reset_dictionary(struct Vm *vm);

  /**
   * @brief Reset only the data and return stacks, preserving dictionary and
   * memory.
   * @param vm     VM instance.
   */
  void vm_reset_stacks(struct Vm *vm);

  /**
   * @brief Destroy a VM instance and free its resources.
   * @param vm  VM instance to destroy (NULL-safe).
   */
  void vm_destroy(struct Vm *vm);

  /**
   * @brief Execute the given word entry in the specified VM.
   * @param vm     VM instance.
   * @param entry  Entry word to execute.
   * @return 0 on success, negative error code on failure.
   */
  v4_err vm_exec(struct Vm *vm, struct Word *entry);

  /**
   * @brief Register a new word in the VM's dictionary.
   * @param vm        VM instance.
   * @param name      Word name (can be NULL for anonymous words).
   * @param code      Pointer to bytecode.
   * @param code_len  Length of bytecode in bytes.
   * @return Word index on success (>= 0), negative error code on failure.
   */
  int vm_register_word(struct Vm *vm, const char *name, const uint8_t *code,
                       int code_len);

  /**
   * @brief Get a word by index from the VM's dictionary.
   * @param vm    VM instance.
   * @param idx   Word index.
   * @return Pointer to Word structure, or NULL if index is invalid.
   */
  struct Word *vm_get_word(struct Vm *vm, int idx);

  /**
   * @brief Get word name.
   * @param word  Word pointer (from vm_get_word).
   * @return Word name string, or NULL if word is NULL.
   */
  const char *vm_word_get_name(const struct Word *word);

  /**
   * @brief Get word bytecode pointer.
   * @param word  Word pointer (from vm_get_word).
   * @return Pointer to bytecode, or NULL if word is NULL.
   */
  const v4_u8 *vm_word_get_code(const struct Word *word);

  /**
   * @brief Get word bytecode length.
   * @param word  Word pointer (from vm_get_word).
   * @return Bytecode length in bytes, or 0 if word is NULL.
   */
  v4_u16 vm_word_get_code_len(const struct Word *word);

  /**
   * @brief Find a word by name in the VM's dictionary.
   *
   * Searches the registered words for one matching the specified name.
   * The search is case-sensitive and uses linear search O(n).
   * For typical word counts (tens to hundreds), this is sufficiently fast.
   *
   * @param vm    VM instance (must not be NULL).
   * @param name  Word name to search for (must not be NULL).
   * @return Word index (>= 0) if found, negative value if not found or on error.
   *
   * @example
   * int idx = vm_find_word(vm, "SQUARE");
   * if (idx >= 0) {
   *     Word* word = vm_get_word(vm, idx);
   *     vm_exec(vm, word);
   * }
   */
  int vm_find_word(struct Vm *vm, const char *name);

  /* ------------------------------------------------------------------------- */
  /* MMIO registration and direct memory access                                */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Dynamically register additional MMIO windows.
   *        (May be used after creation.)
   * @param vm     VM instance.
   * @param list   Pointer to an array of MMIO descriptors.
   * @param count  Number of elements in the array.
   * @return 0 on success, negative value on failure.
   */
  v4_err vm_register_mmio(struct Vm *vm, const V4_Mmio *list, int count);

  /**
   * @brief Read a 32-bit little-endian value from the VM memory space.
   *
   * Performs alignment, bounds, and MMIO checks.
   * Intended primarily for testing and embedding, not for fast I/O.
   *
   * @param vm    VM instance.
   * @param addr  Absolute address.
   * @param out   Output value pointer.
   * @return 0 on success, negative error code otherwise.
   */
  v4_err vm_mem_read32(struct Vm *vm, v4_u32 addr, v4_u32 *out);

  /**
   * @brief Write a 32-bit little-endian value to the VM memory space.
   *
   * Performs alignment, bounds, and MMIO checks.
   * Intended primarily for testing and embedding, not for fast I/O.
   *
   * @param vm    VM instance.
   * @param addr  Absolute address.
   * @param val   Value to store.
   * @return 0 on success, negative error code otherwise.
   */
  v4_err vm_mem_write32(struct Vm *vm, v4_u32 addr, v4_u32 val);

  /* ------------------------------------------------------------------------- */
  /* Minimal stack inspector (for testing)                                     */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Get the current data stack depth.
   * @param vm  VM instance.
   * @return Number of elements currently on the data stack.
   */
  int vm_ds_depth_public(struct Vm *vm);

  /**
   * @brief Peek a value from the data stack by index from the top.
   * @param vm              VM instance.
   * @param index_from_top  0 = top, 1 = next, ...
   * @return Value at the given stack position, or 0 if out of range.
   */
  v4_i32 vm_ds_peek_public(struct Vm *vm, int index_from_top);

  /**
   * @brief Copy the entire data stack to an array.
   *
   * Copies stack contents from bottom to top into the output array.
   * out_array[0] is the oldest value (stack bottom), out_array[depth-1] is TOS.
   *
   * @param vm         VM instance (must not be NULL).
   * @param out_array  Output array (must not be NULL).
   * @param max_count  Maximum number of elements to copy.
   * @return Number of elements actually copied (0 to max_count).
   *
   * @note If stack depth exceeds max_count, only max_count elements are copied.
   * @note Caller must allocate the output array.
   *
   * @example
   * v4_i32 stack[256];
   * int count = vm_ds_copy_to_array(vm, stack, 256);
   * printf("Stack depth: %d\n", count);
   * for (int i = 0; i < count; i++) {
   *     printf("  [%d]: %d\n", i, stack[i]);
   * }
   */
  int vm_ds_copy_to_array(struct Vm *vm, v4_i32 *out_array, int max_count);

  /* ------------------------------------------------------------------------- */
  /* Stack manipulation (for REPL and advanced use cases)                      */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Push a value onto the data stack.
   * @param vm     VM instance.
   * @param value  Value to push.
   * @return 0 on success, negative error code on stack overflow.
   */
  v4_err vm_ds_push(struct Vm *vm, v4_i32 value);

  /**
   * @brief Pop a value from the data stack.
   * @param vm         VM instance.
   * @param out_value  Output pointer for popped value (can be NULL).
   * @return 0 on success, negative error code on stack underflow.
   */
  v4_err vm_ds_pop(struct Vm *vm, v4_i32 *out_value);

  /**
   * @brief Clear the data stack.
   * @param vm  VM instance.
   */
  void vm_ds_clear(struct Vm *vm);

  /* ------------------------------------------------------------------------- */
  /* Return stack inspection (for debugging and REPL)                          */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Get the current return stack depth.
   * @param vm  VM instance (must not be NULL).
   * @return Number of elements currently on the return stack.
   *
   * @example
   * int rs_depth = vm_rs_depth_public(vm);
   * printf("Return stack depth: %d\n", rs_depth);
   */
  int vm_rs_depth_public(struct Vm *vm);

  /**
   * @brief Copy the entire return stack to an array.
   *
   * Copies return stack contents from bottom to top into the output array.
   * Same behavior as vm_ds_copy_to_array() but for the return stack.
   *
   * @param vm         VM instance (must not be NULL).
   * @param out_array  Output array (must not be NULL).
   * @param max_count  Maximum number of elements to copy.
   * @return Number of elements actually copied (0 to max_count).
   *
   * @note Implementation mirrors vm_ds_copy_to_array() but uses vm->RS.
   * @note Caller must allocate the output array.
   */
  int vm_rs_copy_to_array(struct Vm *vm, v4_i32 *out_array, int max_count);

  /* ------------------------------------------------------------------------- */
  /* Stack snapshot (for preserving stack across VM resets)                    */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Stack snapshot structure.
   *
   * Contains a copy of the data stack contents at a point in time.
   * Created by vm_ds_snapshot() and freed by vm_ds_snapshot_free().
   */
  typedef struct VmStackSnapshot
  {
    v4_i32 *data; /**< Stack data (dynamically allocated) */
    int depth;    /**< Stack depth */
  } VmStackSnapshot;

  /**
   * @brief Create a snapshot of the current data stack.
   *
   * Allocates memory to store a copy of the stack contents.
   * Must be freed with vm_ds_snapshot_free().
   *
   * @param vm  VM instance.
   * @return Pointer to snapshot, or NULL on allocation failure.
   */
  struct VmStackSnapshot *vm_ds_snapshot(struct Vm *vm);

  /**
   * @brief Restore the data stack from a snapshot.
   *
   * Clears the current stack and replaces it with the snapshot contents.
   *
   * @param vm        VM instance.
   * @param snapshot  Snapshot to restore from.
   * @return 0 on success, negative error code on failure.
   */
  v4_err vm_ds_restore(struct Vm *vm, const struct VmStackSnapshot *snapshot);

  /**
   * @brief Free a stack snapshot.
   * @param snapshot  Snapshot to free (NULL-safe).
   */
  void vm_ds_snapshot_free(struct VmStackSnapshot *snapshot);

  /* ------------------------------------------------------------------------- */
  /* Version and error handling                                                */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief Get the current version of the VM.
   * @return Version number as an integer.
   */
  int v4_vm_version(void);

  /**
   * @brief VM panic handler
   *
   * Collects error information and outputs diagnostic details to UART/console.
   * This API should be called from all error paths.
   *
   * Output information:
   * - Error code and message
   * - Program Counter (PC)
   * - Data Stack depth and top 2 elements
   * - Return Stack depth and call trace
   *
   * @param vm         VM instance
   * @param error_code Error code (Err enumeration value)
   * @return Always returns error_code (allows usage as: return vm_panic(...))
   *
   * @note This API only performs diagnostic output and does not modify VM state.
   * @note Output destination is stdout (can be redirected to UART via HAL).
   */
  v4_err vm_panic(struct Vm *vm, v4_err error_code);

  /* ------------------------------------------------------------------------- */
  /* SYS instruction handler                                                   */
  /* ------------------------------------------------------------------------- */

  /**
   * @brief SYS handler function type
   *
   * Stack layout: ( arg0 arg1 arg2 sys_id -- result )
   *
   * @param vm     VM instance
   * @param sys_id 32-bit system call ID
   * @param arg0   First argument
   * @param arg1   Second argument
   * @param arg2   Third argument
   * @return Result value to push on stack (typically 0 for success, negative for error)
   */
  typedef v4_i32 (*v4_sys_handler_fn)(struct Vm *vm, v4_i32 sys_id, v4_i32 arg0, v4_i32 arg1,
                                      v4_i32 arg2);

  /**
   * @brief Register global SYS handler
   *
   * The registered handler will be invoked for all SYS instructions.
   * If no handler is registered, SYS instructions return -1 (not supported).
   *
   * @param handler Function pointer to SYS handler (or NULL to unregister)
   */
  void v4_register_sys_handler(v4_sys_handler_fn handler);

  /* ------------------------------------------------------------------------- */
  /* Error handling notes                                                      */
  /* ------------------------------------------------------------------------- */
  /**
   * All public APIs return 0 on success and a negative v4_err on failure.
   * Errors are defined in `errors.def` and generated into `errors.h/c`.
   *
   * Examples:
   *  - -12 = Unaligned access
   *  - -13 = Out of bounds memory access
   *  - -11 = Division by zero
   *
   * Exceptions are never thrown (compiled with -fno-exceptions).
   * Error propagation is done purely via return values.
   */

  /* ------------------------------------------------------------------------- */

#ifdef __cplusplus
} /* extern "C" */
#endif
