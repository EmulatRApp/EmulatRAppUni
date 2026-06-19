# ============================================================================
# CMakeLists.txt -- EmulatR V4 SMP harness scaffold.
# ============================================================================
# Header-only core (matches the chipsetLib style). One test executable.
# C++20 required for <barrier>/<jthread>. No Qt anywhere in this target.
#
# Integrate into the V4 tree by adding this subdirectory, or lift the
# include/ tree into systemLib and the test into your existing DOCTest target.
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(emulatr_smp_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Header-only interface library -----------------------------------------
add_library(emulatr_smp_harness INTERFACE)
target_include_directories(emulatr_smp_harness INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Threads (std::thread/std::jthread/std::barrier).
find_package(Threads REQUIRED)
target_link_libraries(emulatr_smp_harness INTERFACE Threads::Threads)

# Some libstdc++ versions gate std::jthread/std::barrier behind these; harmless
# elsewhere. MSVC needs none of this.
if (NOT MSVC)
    target_compile_options(emulatr_smp_harness INTERFACE -pthread)
endif()

# ---- Tests -----------------------------------------------------------------
# Expects a vendored doctest (header at <doctest/doctest.h>). If your tree
# vendors it elsewhere, point DOCTEST_INCLUDE_DIR at it.
option(SMP_HARNESS_BUILD_TESTS "Build SMP harness DOCTest target" ON)

if (SMP_HARNESS_BUILD_TESTS)
    set(DOCTEST_INCLUDE_DIR "" CACHE PATH "Directory containing doctest/doctest.h")

    add_executable(smp_harness_tests tests/smp_harness_tests.cpp)
    target_link_libraries(smp_harness_tests PRIVATE emulatr_smp_harness)

    if (DOCTEST_INCLUDE_DIR)
        target_include_directories(smp_harness_tests PRIVATE ${DOCTEST_INCLUDE_DIR})
    endif()

    enable_testing()
    add_test(NAME smp_harness COMMAND smp_harness_tests)
endif()
