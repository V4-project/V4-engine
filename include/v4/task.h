#pragma once
#include <stdint.h>

#include "v4/vm_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* ========================================================================= */
  /* Task Management API (Public)                                             */
  /* ========================================================================= */

  /**
   * Builds with V4_ENABLE_TASKS=OFF retain all public task/message API symbols.
   * Calls return V4_ERR_UnknownOp (-99), or V4_ERR_InvalidArg (-16) for a NULL VM,
   * without modifying state or output arguments. No task IDs or messages are
   * produced. Task opcodes retain their values but panic with UnknownOp before
   * consuming stack arguments. The default build enables task support.
   */

  /**
   * @brief Task state enumeration (public view)
   */
  typedef enum
  {
    V4_TASK_STATE_DEAD = 0,
    V4_TASK_STATE_READY,
    V4_TASK_STATE_RUNNING,
    V4_TASK_STATE_BLOCKED,
  } v4_task_state_t;

  /**
   * @brief Initialize task system
   *
   * Initializes the task system in the VM instance.
   * Must be called after vm_create() and before spawning any tasks.
   *
   * @param vm VM instance
   * @param time_slice_ms Time slice in milliseconds (0 = default 10ms)
   * @return 0 on success, negative on error
   */
  v4_err vm_task_init(struct Vm *vm, uint32_t time_slice_ms);

  /**
   * @brief Cleanup task system
   *
   * Cleans up the task system in the VM instance.
   * Frees all task stacks and resets the scheduler.
   * Should be called before vm_destroy().
   *
   * @param vm VM instance
   * @return 0 on success, negative on error
   */
  v4_err vm_task_cleanup(struct Vm *vm);

  /**
   * @brief Spawn a new task
   *
   * Creates a new task that executes the specified word.
   * The task immediately enters READY state and becomes schedulable.
   *
   * @param vm VM instance
   * @param word_idx Word index to execute
   * @param priority Priority (0=lowest, 255=highest)
   * @param ds_size Data stack size in elements (0 = default 256)
   * @param rs_size Return stack size in elements (0 = default 64)
   * @return Task ID (0-7) on success, negative on error
   *         -1: Task table full
   *         -2: Out of memory
   *         -3: Invalid word_idx
   */
  int vm_task_spawn(struct Vm *vm, uint16_t word_idx, uint8_t priority, uint16_t ds_size,
                    uint16_t rs_size);

  /**
   * @brief Exit current task
   *
   * Terminates the calling task and sets it to DEAD state.
   * This function does not return (switches to another task).
   *
   * @param vm VM instance
   * @return 0 on success (actually never returns)
   */
  v4_err vm_task_exit(struct Vm *vm);

  /**
   * @brief Sleep current task
   *
   * Puts the current task to sleep for the specified duration.
   * The task enters BLOCKED state and yields CPU to other tasks.
   *
   * @param vm VM instance
   * @param ms_delay Sleep duration in milliseconds
   * @return 0 on success
   */
  v4_err vm_task_sleep(struct Vm *vm, uint32_t ms_delay);

  /**
   * @brief Yield CPU to next task
   *
   * Explicitly yields CPU to other tasks.
   * Useful in long-running loops for efficiency.
   *
   * @param vm VM instance
   * @return 0 on success
   */
  v4_err vm_task_yield(struct Vm *vm);

  /**
   * @brief Enter critical section (disable preemption)
   *
   * Disables preemption to prevent task switching.
   * Use when accessing shared resources.
   * Supports nesting (must match enter/exit pairs).
   *
   * @param vm VM instance
   * @return 0 on success
   */
  v4_err vm_task_critical_enter(struct Vm *vm);

  /**
   * @brief Exit critical section (enable preemption)
   *
   * Re-enables preemption.
   * Must be paired with vm_task_critical_enter().
   *
   * @param vm VM instance
   * @return 0 on success
   */
  v4_err vm_task_critical_exit(struct Vm *vm);

  /**
   * @brief Get current task ID
   *
   * @param vm VM instance
   * @return Current task ID (0-7)
   */
  int vm_task_self(struct Vm *vm);

  /**
   * @brief Get task information
   *
   * @param vm VM instance
   * @param task_id Task ID
   * @param[out] state Task state (can be NULL)
   * @param[out] priority Priority (can be NULL)
   * @return 0 on success, negative on error
   */
  v4_err vm_task_get_info(struct Vm *vm, uint8_t task_id, v4_task_state_t *state,
                          uint8_t *priority);

  /* ========================================================================= */
  /* Message Passing API                                                      */
  /* ========================================================================= */

  /**
   * @brief Send inter-task message
   *
   * Sends a message to another task (non-blocking).
   * Returns error if queue is full.
   *
   * @param vm VM instance
   * @param target_task Target task ID (0xFF=broadcast)
   * @param msg_type Message type (user-defined, 0-254)
   * @param data Data payload (32bit)
   * @return 0 on success, negative on error
   *         -1: Queue full
   *         -2: Invalid target_task
   */
  v4_err vm_task_send(struct Vm *vm, uint8_t target_task, uint8_t msg_type, int32_t data);

  /**
   * @brief Receive inter-task message (non-blocking)
   *
   * Receives a message addressed to current task.
   * Returns immediately if no message available.
   *
   * @param vm VM instance
   * @param msg_type Message type to receive (0=any)
   * @param[out] data Data storage (can be NULL)
   * @param[out] src_task Source task ID storage (can be NULL)
   * @return 1 if message received, 0 if no message, negative on error
   */
  int vm_task_receive(struct Vm *vm, uint8_t msg_type, int32_t *data, uint8_t *src_task);

  /**
   * @brief Receive inter-task message (blocking)
   *
   * Receives a message addressed to current task.
   * Blocks (BLOCKED state) if no message available.
   *
   * @param vm VM instance
   * @param msg_type Message type to receive (0=any)
   * @param[out] data Data storage
   * @param[out] src_task Source task ID storage (can be NULL)
   * @param timeout_ms Timeout in milliseconds (0=infinite)
   * @return 1 if received, 0 if timeout, negative on error
   */
  int vm_task_receive_blocking(struct Vm *vm, uint8_t msg_type, int32_t *data,
                               uint8_t *src_task, uint32_t timeout_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif
