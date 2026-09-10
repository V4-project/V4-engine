// Default direct-source builds may collect every .cpp file; emit no stubs there.
#if defined(V4_ENABLE_TASKS) && !V4_ENABLE_TASKS

#include "v4/errors.h"
#include "v4/internal/scheduler.hpp"
#include "v4/task.h"

// Keep the public API linkable without pulling in a scheduler or platform hooks.
static v4_err unavailable(Vm* vm)
{
  return vm ? V4_ERR_UnknownOp : V4_ERR_InvalidArg;
}

extern "C" v4_err vm_task_init(Vm* vm, uint32_t)
{
  return unavailable(vm);
}

extern "C" v4_err vm_schedule(Vm* vm)
{
  return unavailable(vm);
}

extern "C" v4_err vm_schedule_from_isr(Vm* vm)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_cleanup(Vm* vm)
{
  return unavailable(vm);
}

extern "C" int vm_task_spawn(Vm* vm, uint16_t, uint8_t, uint16_t, uint16_t)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_exit(Vm* vm)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_sleep(Vm* vm, uint32_t)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_yield(Vm* vm)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_critical_enter(Vm* vm)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_critical_exit(Vm* vm)
{
  return unavailable(vm);
}

extern "C" int vm_task_self(Vm* vm)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_get_info(Vm* vm, uint8_t, v4_task_state_t*, uint8_t*)
{
  return unavailable(vm);
}

extern "C" v4_err vm_task_send(Vm* vm, uint8_t, uint8_t, int32_t)
{
  return unavailable(vm);
}

extern "C" int vm_task_receive(Vm* vm, uint8_t, int32_t*, uint8_t*)
{
  return unavailable(vm);
}

extern "C" int vm_task_receive_blocking(Vm* vm, uint8_t, int32_t*, uint8_t*, uint32_t)
{
  return unavailable(vm);
}

#endif
