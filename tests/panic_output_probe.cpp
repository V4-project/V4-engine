#include <cstdio>

#include "v4/errors.hpp"
#include "v4/internal/vm.h"
#include "v4/panic.h"
#include "v4/vm_api.h"

static void handler(void*, const V4PanicInfo*)
{
  std::puts("CUSTOM PANIC");
}

int main(int argc, char**)
{
  Vm vm{};
  vm_reset(&vm);
  if (argc > 1)
  {
    vm_ds_push(&vm, 42);
    vm_ds_push(&vm, 100);
    for (int i = 0; i < 20; ++i)
      *vm.rp++ = i;
  }
  vm_set_panic_handler(&vm, handler, nullptr);
  return vm_panic(&vm, static_cast<v4_err>(Err::InvalidArg)) ==
                 static_cast<v4_err>(Err::InvalidArg)
             ? 0
             : 1;
}
