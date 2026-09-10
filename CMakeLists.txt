cmake_minimum_required(VERSION 3.16)
project(
  V4Engine
  VERSION 0.18.1
  LANGUAGES C CXX)

# ============================================================================
# Project Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(V4_BUILD_TESTS "Build unit tests" ON)
option(V4_BUILD_TOOLS "Build command-line tools" ON)
option(V4_ENABLE_MOCK_HAL "Build with mock HAL (required for tests)" ON)
option(V4_USE_V4HAL "Use V4-hal C++17 CRTP implementation" OFF)
option(V4_USE_V4STD "Use V4-std device-independent standard library" OFF)
option(V4_OPTIMIZE_SIZE "Optimize for size (-Os)" ON)
option(V4_PANIC_DIAGNOSTICS "Include standard panic diagnostic output" ON)
option(V4_ENABLE_TASKS "Include task execution, scheduling and messaging" ON)
option(
  V4_ENABLE_LTO
  "Enable Link Time Optimization (incompatible with sanitizers - disable for ASan/TSan)"
  ON)

# Task backend selection
set(V4_TASK_BACKEND
    "CUSTOM"
    CACHE STRING "Task backend implementation (CUSTOM|FREERTOS)")
set_property(CACHE V4_TASK_BACKEND PROPERTY STRINGS "CUSTOM" "FREERTOS")

# V4-hal path configuration
set(V4HAL_LOCAL_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/../V4-hal"
    CACHE PATH "Path to local V4-hal source")

# V4-std path configuration
set(V4STD_LOCAL_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/../V4-std"
    CACHE PATH "Path to local V4-std source")

# FreeRTOS path configuration (only needed when V4_TASK_BACKEND=FREERTOS)
set(FREERTOS_PATH
    ""
    CACHE PATH "Path to FreeRTOS source directory")

# ============================================================================
# Core VM Library
# ============================================================================

set(V4ENGINE_SOURCES src/core.cpp src/memory.cpp src/arena.cpp)
if(V4_ENABLE_TASKS)
  list(APPEND V4ENGINE_SOURCES src/task.cpp src/scheduler.cpp src/message.cpp)
endif()
list(APPEND V4ENGINE_SOURCES src/panic.cpp)

# Add task backend implementation based on selection
if(NOT V4_ENABLE_TASKS)
  list(APPEND V4ENGINE_SOURCES src/task_disabled.cpp)
  message(STATUS "Task support disabled (public API stubs only)")
elseif(V4_TASK_BACKEND STREQUAL "CUSTOM")
  list(APPEND V4ENGINE_SOURCES src/task_backend_custom.cpp)
  message(STATUS "Using CUSTOM task backend")
elseif(V4_TASK_BACKEND STREQUAL "FREERTOS")
  list(APPEND V4ENGINE_SOURCES src/task_backend_freertos.cpp)
  message(STATUS "Using FreeRTOS task backend")

  # FreeRTOS integration (only needed for standalone FreeRTOS, not ESP-IDF)
  if(FREERTOS_PATH AND EXISTS "${FREERTOS_PATH}")
    message(STATUS "Using FreeRTOS from: ${FREERTOS_PATH}")
  else()
    message(STATUS "FreeRTOS provided by platform (e.g., ESP-IDF)")
  endif()
else()
  message(
    FATAL_ERROR "Invalid V4_TASK_BACKEND: ${V4_TASK_BACKEND}. Must be CUSTOM or FREERTOS")
endif()

add_library(v4engine STATIC ${V4ENGINE_SOURCES})
target_compile_definitions(v4engine PUBLIC V4_ENABLE_TASKS=$<BOOL:${V4_ENABLE_TASKS}>)
if(V4_ENABLE_TASKS
   AND V4_TASK_BACKEND STREQUAL "FREERTOS"
   AND FREERTOS_PATH
   AND EXISTS "${FREERTOS_PATH}")
  target_include_directories(
    v4engine PRIVATE "${FREERTOS_PATH}/include"
                     "${FREERTOS_PATH}/portable/${FREERTOS_PORT}")
endif()
target_compile_definitions(v4engine
                           PRIVATE V4_PANIC_DIAGNOSTICS=$<BOOL:${V4_PANIC_DIAGNOSTICS}>)

target_include_directories(
  v4engine
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
  PRIVATE ${PROJECT_SOURCE_DIR}/include/v4/internal)

# Compiler flags
if(MSVC)
  target_compile_options(
    v4engine
    PRIVATE /W4 # Warning level 4
            /GR- # Disable RTTI
            /EHs-c- # Disable exceptions
  )
  # Configuration-specific optimizations
  target_compile_options(v4engine PRIVATE $<$<CONFIG:Debug>:/Od> $<$<CONFIG:Release>:/O1>)
else()
  target_compile_options(
    v4engine
    PRIVATE -Wall # Enable all warnings
            -Wextra # Enable extra warnings
            -Werror # Treat warnings as errors
            -fno-exceptions
            -fno-rtti
            -ffunction-sections # Function sections for gc-sections
            -fdata-sections # Data sections for gc-sections
  )
  if(V4_OPTIMIZE_SIZE)
    target_compile_options(v4engine PRIVATE -Os)
  endif()
  # Linker flags for size optimization
  target_link_options(v4engine PRIVATE -Wl,--gc-sections)
endif()

# Enable LTO if supported and requested
if(V4_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(
    RESULT ipo_supported
    OUTPUT ipo_output
    LANGUAGES C CXX)
  if(ipo_supported)
    set_target_properties(v4engine PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "LTO enabled for v4engine")
  else()
    message(WARNING "LTO not supported: ${ipo_output}")
  endif()
endif()

# ============================================================================
# Mock HAL Library (for testing and development)
# ============================================================================

if(V4_ENABLE_MOCK_HAL)
  add_library(mock_hal STATIC tests/mock_hal.cpp)
  if(V4_ENABLE_TASKS)
    target_sources(mock_hal PRIVATE tests/mock_task_platform.cpp)
  endif()

  target_include_directories(mock_hal PUBLIC ${PROJECT_SOURCE_DIR}/include
                                             ${PROJECT_SOURCE_DIR}/tests)

  # Enable LTO for mock_hal if requested
  if(V4_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(
      RESULT ipo_supported
      OUTPUT ipo_output
      LANGUAGES C CXX)
    if(ipo_supported)
      set_target_properties(mock_hal PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
  endif()

  # Export mock_hal as an alias for easier linking
  add_library(V4::mock_hal ALIAS mock_hal)
endif()

# ============================================================================
# V4-hal Integration (C++17 CRTP HAL implementation)
# ============================================================================

if(V4_USE_V4HAL)
  include(FetchContent)

  if(EXISTS "${V4HAL_LOCAL_PATH}/CMakeLists.txt")
    message(STATUS "Using local V4-hal from: ${V4HAL_LOCAL_PATH}")
    add_subdirectory(${V4HAL_LOCAL_PATH} ${CMAKE_BINARY_DIR}/v4hal)
  else()
    message(STATUS "Fetching V4-hal from repository")
    fetchcontent_declare(
      v4hal
      GIT_REPOSITORY https://github.com/V4-project/V4-hal.git
      GIT_TAG v0.1.0)
    fetchcontent_makeavailable(v4hal)
  endif()

  # Create v4_hal_* wrapper for V4-hal's hal_* API
  add_library(v4_hal_wrapper STATIC src/hal_wrapper.cpp)
  target_link_libraries(v4_hal_wrapper PUBLIC v4-hal-lib)
  target_include_directories(v4_hal_wrapper PUBLIC ${PROJECT_SOURCE_DIR}/include)

  message(STATUS "V4-hal C++17 CRTP implementation enabled")
endif()

# ============================================================================
# V4-std Integration (Device-Independent Standard Library)
# ============================================================================

if(V4_USE_V4STD)
  include(FetchContent)

  if(EXISTS "${V4STD_LOCAL_PATH}/CMakeLists.txt")
    message(STATUS "Using local V4-std from: ${V4STD_LOCAL_PATH}")
    add_subdirectory(${V4STD_LOCAL_PATH} ${CMAKE_BINARY_DIR}/v4std)
  else()
    message(STATUS "Fetching V4-std from repository")
    fetchcontent_declare(
      v4std
      GIT_REPOSITORY https://github.com/V4-project/V4-std.git
      GIT_TAG main)
    fetchcontent_makeavailable(v4std)
  endif()

  # Link V4-std to v4engine
  target_link_libraries(v4engine PUBLIC v4std)
  target_compile_definitions(v4engine PUBLIC V4_USE_V4STD)

  message(STATUS "V4-std device-independent standard library enabled")
endif()

# ============================================================================
# Tools
# ============================================================================

# NOTE: v4repl tool has been moved to a separate repository V4_BUILD_TOOLS option is kept
# for future tools

# ============================================================================
# Tests
# ============================================================================

if(V4_BUILD_TESTS)
  enable_testing()

  if(NOT V4_ENABLE_MOCK_HAL)
    message(WARNING "Tests require V4_ENABLE_MOCK_HAL=ON. Skipping test targets.")
  else()
    # Common test settings
    set(DOCTEST_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/tests/vendor/doctest)

    # Helper function to add test executables
    function(add_v4_test TEST_NAME TEST_SOURCE)
      add_executable(${TEST_NAME} ${TEST_SOURCE})
      target_include_directories(${TEST_NAME} PRIVATE ${DOCTEST_INCLUDE_DIR})
      target_link_libraries(${TEST_NAME} PRIVATE v4engine mock_hal)
      add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endfunction()

    # VM Core Tests
    add_v4_test(test_vm tests/test_vm.cpp)
    add_v4_test(test_binary_ops tests/test_binary_ops.cpp)
    add_v4_test(test_return_stack tests/test_return_stack.cpp)
    add_v4_test(test_call tests/test_call.cpp)
    add_v4_test(test_word_shadowing tests/test_word_shadowing.cpp)

    # Memory Tests
    add_v4_test(test_memory tests/test_memory.cpp)
    add_v4_test(test_arena tests/test_arena.cpp)
    add_v4_test(test_vm_arena tests/test_vm_arena.cpp)

    # HAL/System Tests
    add_v4_test(test_sys tests/test_sys.cpp)

    # API Extension Tests
    add_v4_test(test_api_extensions tests/test_api_extensions.cpp)

    # Task System Tests
    if(V4_ENABLE_TASKS)
      add_v4_test(test_task tests/test_task.cpp)
    else()
      add_v4_test(test_task_disabled tests/test_task_disabled.cpp)
    endif()

    # Panic Handler Tests
    add_v4_test(test_panic tests/test_panic.cpp)
    add_executable(panic_output_probe tests/panic_output_probe.cpp)
    target_link_libraries(panic_output_probe PRIVATE v4engine mock_hal)
    add_test(
      NAME test_panic_output
      COMMAND
        ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:panic_output_probe>
        -DDIAGNOSTICS=${V4_PANIC_DIAGNOSTICS} -P
        ${PROJECT_SOURCE_DIR}/tests/check_panic_output.cmake)

    # C API Tests
    add_executable(test_vm_c tests/test_vm_c.c)
    target_include_directories(test_vm_c PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(test_vm_c PRIVATE v4engine mock_hal)
    set_target_properties(test_vm_c PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME test_vm_c COMMAND test_vm_c)
  endif()
endif()

# ============================================================================
# Installation (optional)
# ============================================================================

# Install headers
install(
  DIRECTORY include/v4
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "*.hpp")

# Install library
install(
  TARGETS v4engine
  EXPORT V4EngineTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "V4 Engine Configuration Summary:")
message(STATUS "  Version:              ${PROJECT_VERSION}")
message(STATUS "  Build type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:         ${CMAKE_CXX_STANDARD}")
message(STATUS "  Task backend:         ${V4_TASK_BACKEND}")
message(STATUS "  Enable tasks:         ${V4_ENABLE_TASKS}")
message(STATUS "  Build tests:          ${V4_BUILD_TESTS}")
message(STATUS "  Build tools:          ${V4_BUILD_TOOLS}")
message(STATUS "  Enable mock HAL:      ${V4_ENABLE_MOCK_HAL}")
message(STATUS "  Use V4-hal (C++17):   ${V4_USE_V4HAL}")
message(STATUS "  Use V4-std:           ${V4_USE_V4STD}")
message(STATUS "  Optimize for size:    ${V4_OPTIMIZE_SIZE}")
message(STATUS "")
