#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief VM panic diagnostic information
   *
   * Structure holding diagnostic information when VM enters an error state.
   * Collected by vm_panic() function and used for diagnostic output.
   * Unavailable stack values are zero. PC is currently a zero placeholder.
   */
  typedef struct V4PanicInfo
  {
    int32_t error_code;  /**< Error code (Err enumeration value) */
    uint32_t pc;         /**< Program Counter */
    int32_t tos;         /**< Top of Stack (when valid) */
    int32_t nos;         /**< Next on Stack (when valid) */
    uint8_t ds_depth;    /**< Data Stack depth */
    uint8_t rs_depth;    /**< Return Stack depth */
    bool has_stack_data; /**< Whether stack data is valid */
    int32_t stack[4];    /**< Top 4 stack values (when available) */
  } V4PanicInfo;

  // Forward declaration
  struct Vm;

  /**
   * @brief Panic handler callback type
   *
   * Called when VM encounters a fatal error.
   *
   * @param user_data  User data pointer passed to vm_set_panic_handler
   * @param info       Panic diagnostic information
   */
  typedef void (*V4PanicHandler)(void *user_data, const V4PanicInfo *info);

  /**
   * @brief Set custom panic handler
   *
   * Registers a callback to be invoked when vm_panic() is called.
   * This allows embedders to implement custom error handling/logging.
   * Standard diagnostics are printed before the callback unless the engine was
   * built with V4_PANIC_DIAGNOSTICS=0 (CMake: -DV4_PANIC_DIAGNOSTICS=OFF).
   * Disabling diagnostics does not disable this callback or error returns.
   *
   * @param vm         VM instance
   * @param handler    Panic handler callback (NULL to disable)
   * @param user_data  User data passed to handler
   */
  void vm_set_panic_handler(struct Vm *vm, V4PanicHandler handler, void *user_data);

#ifdef __cplusplus
}
#endif
