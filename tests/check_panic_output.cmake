execute_process(
  COMMAND "${PROBE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Panic output probe failed: ${result}")
endif()
set(empty_output "${output}")
execute_process(
  COMMAND "${PROBE}" trace
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Panic trace probe failed: ${result}")
endif()
string(REPLACE "\r\n" "\n" output "${output}")
if(DIAGNOSTICS)
  foreach(expected "Data Stack: [2] TOS=100, NOS=42" "Return Stack: [20]" "Call trace:"
                   "... (4 more entries)" "CUSTOM PANIC")
    string(FIND "${output}" "${expected}" position)
    if(position LESS 0)
      message(FATAL_ERROR "Missing panic trace field: ${expected}")
    endif()
  endforeach()
elseif(NOT output STREQUAL "CUSTOM PANIC\n")
  message(FATAL_ERROR "Unexpected trace output with diagnostics disabled: ${output}")
endif()
set(output "${empty_output}")
string(REPLACE "\r\n" "\n" output "${output}")
if(DIAGNOSTICS)
  if(NOT
     output
     MATCHES
     "========== V4 PANIC ==========\nError: .*\nPC: 0x00000000\nData Stack: \\[0\\]\nReturn Stack: \\[0\\]\n==============================\n\nCUSTOM PANIC\n$"
  )
    message(FATAL_ERROR "Missing diagnostics or incorrect callback order: ${output}")
  endif()
elseif(NOT output STREQUAL "CUSTOM PANIC\n")
  message(
    FATAL_ERROR "Diagnostics-disabled build must only call the custom handler: ${output}")
endif()
