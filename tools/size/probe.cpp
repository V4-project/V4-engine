#include <cstring>

#include "v4/vm_api.h"

// Input remains runtime-dependent so LTO cannot specialize the interpreter for RET.
// This measures the linked interpreter, not a doctest executable or archive file.
int main(int argc, char** argv)
{
  uint8_t memory[1024] = {};
  VmConfig config = {memory, sizeof(memory), nullptr, 0, nullptr};
  Vm* vm = vm_create(&config);
  if (!vm)
    return 1;

  const uint8_t ret[] = {0x51};
  const uint8_t* code = argc > 1 ? reinterpret_cast<const uint8_t*>(argv[1]) : ret;
  const int length = argc > 1 ? static_cast<int>(std::strlen(argv[1])) : 1;
  const int id = vm_register_word(vm, nullptr, code, length);
  const int result = id < 0 ? id : vm_exec(vm, vm_get_word(vm, id));
  vm_destroy(vm);
  return result != 0;
}
