cmake_minimum_required(VERSION 3.16)
project(V4SizeProbe LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "The reference size probe requires Linux and GCC")
endif()

set(V4_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Engine source to measure")
option(SIZE_LTO "Measure with LTO" OFF)
option(SIZE_GC "Measure with section garbage collection" OFF)

set(CMAKE_BUILD_TYPE
    Release
    CACHE STRING "Build type" FORCE)
set(V4_BUILD_TESTS
    OFF
    CACHE BOOL "" FORCE)
set(V4_BUILD_TOOLS
    OFF
    CACHE BOOL "" FORCE)
set(V4_ENABLE_MOCK_HAL
    ON
    CACHE BOOL "" FORCE)
set(V4_USE_V4HAL
    OFF
    CACHE BOOL "" FORCE)
set(V4_USE_V4STD
    OFF
    CACHE BOOL "" FORCE)
set(V4_OPTIMIZE_SIZE
    ON
    CACHE BOOL "" FORCE)
set(V4_ENABLE_LTO
    ${SIZE_LTO}
    CACHE BOOL "" FORCE)
set(V4_TASK_BACKEND
    CUSTOM
    CACHE STRING "" FORCE)
add_subdirectory("${V4_SOURCE_DIR}" engine)

add_executable(v4_size_probe probe.cpp)
target_compile_features(v4_size_probe PRIVATE cxx_std_17)
target_link_libraries(v4_size_probe PRIVATE v4engine mock_hal)
target_compile_options(v4_size_probe PRIVATE -Os -fno-exceptions -fno-rtti
                                             -ffunction-sections -fdata-sections)
target_link_options(v4_size_probe PRIVATE "-Wl,-Map=${CMAKE_BINARY_DIR}/probe.map")
if(SIZE_GC)
  target_link_options(v4_size_probe PRIVATE -Wl,--gc-sections)
else()
  target_link_options(v4_size_probe PRIVATE -Wl,--no-gc-sections)
endif()
if(SIZE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(
    RESULT supported
    OUTPUT detail
    LANGUAGES C CXX)
  if(NOT supported)
    message(FATAL_ERROR "LTO profile unavailable: ${detail}")
  endif()
  set_target_properties(v4_size_probe v4engine mock_hal
                        PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
